using System;
using System.Diagnostics;
using System.ServiceProcess;
using System.Threading;

namespace SysBar.Sensors
{
    /// <summary>
    /// Publishes readings while a client asks for them (sys-bar signals the "wanted" event on every
    /// sample) and idles otherwise: nothing is read while the display is off or sys-bar is closed.
    /// </summary>
    internal sealed class SensorService : ServiceBase
    {
        public const string Name = "SysBarSensors";
        private static readonly TimeSpan ActiveWindow = TimeSpan.FromSeconds(10);
        private static readonly TimeSpan ReleaseAfter = TimeSpan.FromMinutes(5);
        private static readonly TimeSpan MinInterval = TimeSpan.FromMilliseconds(750);

        private readonly ManualResetEvent _stop = new ManualResetEvent(false);
        private Thread? _worker;
        private Timer? _consoleTimer;  // field: a local timer could be collected before it fires

        public SensorService()
        {
            ServiceName = Name;
            CanStop = true;
            CanShutdown = true;
        }

        protected override void OnStart(string[] args)
        {
            _stop.Reset();
            _worker = new Thread(Run) { IsBackground = true, Name = "sensors" };
            _worker.Start();
        }

        protected override void OnStop() => Halt();

        protected override void OnShutdown() => Halt();

        private void Halt()
        {
            _stop.Set();
            _worker?.Join(TimeSpan.FromSeconds(10));
        }

        /// <summary>Runs in the foreground (diagnostics): publishes like the service and prints readings.</summary>
        public void RunInConsole(TimeSpan? duration)
        {
            Log.ToConsole = true;
            if (duration.HasValue)
            {
                _consoleTimer = new Timer(_ => _stop.Set(), null, duration.Value, Timeout.InfiniteTimeSpan);
            }
            Console.CancelKeyPress += (_, e) =>
            {
                e.Cancel = true;
                _stop.Set();
            };
            Run();
        }

        private void Run()
        {
            Log.Write($"service started (pid {Process.GetCurrentProcess().Id})");
            SharedBlock? block = null;
            EventWaitHandle? wanted = null;
            SensorReader? reader = null;
            try
            {
                block = new SharedBlock();
                wanted = SharedBlock.CreateWantedEvent();
                DateTime lastWanted = Log.ToConsole ? DateTime.UtcNow : DateTime.MinValue;
                DateTime lastRead = DateTime.MinValue;
                var handles = new WaitHandle[] { _stop, wanted };

                while (true)
                {
                    int signaled = WaitHandle.WaitAny(handles, TimeSpan.FromSeconds(1));
                    if (signaled == 0) break;
                    DateTime now = DateTime.UtcNow;
                    if (signaled == 1 || Log.ToConsole) lastWanted = now;

                    if (now - lastWanted < ActiveWindow)
                    {
                        if (now - lastRead < MinInterval) continue;
                        lastRead = now;
                        try
                        {
                            reader ??= Open();
                            var (cpu, readings) = reader.Read();
                            block.Publish(cpu, readings);
                            if (Log.ToConsole)
                            {
                                Log.Write($"cpu {cpu:0.0} C, {readings.Count} readings");
                                foreach (Reading r in readings)
                                    Log.Write($"  {r.Group,-5} {r.Kind,-11} {r.Value,8:0.0}  {r.Name}");
                            }
                        }
                        catch (Exception e)
                        {
                            Log.Write($"read failed: {e.GetType().Name}: {e.Message}");
                            reader?.Dispose();
                            reader = null;
                            _stop.WaitOne(TimeSpan.FromSeconds(5));
                        }
                    }
                    else if (reader != null && now - lastWanted > ReleaseAfter)
                    {
                        // Nobody has asked for a while: release the driver handles until needed again.
                        reader.Dispose();
                        reader = null;
                        Log.Write("idle: sensors released");
                    }
                }
            }
            catch (Exception e)
            {
                Log.Write($"fatal: {e}");
                throw;
            }
            finally
            {
                reader?.Dispose();
                wanted?.Dispose();
                block?.Dispose();
                Log.Write("service stopped");
            }
        }

        private static SensorReader Open()
        {
            var watch = Stopwatch.StartNew();
            var reader = new SensorReader();
            Log.Write($"sensors opened in {watch.ElapsedMilliseconds} ms");
            return reader;
        }
    }
}

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
        private static readonly TimeSpan UpdateCooldown = TimeSpan.FromMinutes(10);  // at most one GitHub check per 10 min
        private static readonly TimeSpan EventRetry = TimeSpan.FromSeconds(30);
        private static readonly TimeSpan DetailWindow = TimeSpan.FromSeconds(3);  // sys-bar signals every second
        private static readonly TimeSpan PawnIoCheck = TimeSpan.FromSeconds(30);   // opened without PawnIO: is it back?

        private readonly ManualResetEvent _stop = new ManualResetEvent(false);
        private readonly ManualResetEvent _never = new ManualResetEvent(false);  // stands in for a missing event
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
            if (!Log.ToConsole) SelfUpdate.CleanupStaging();  // leftovers of a finished update
            SharedBlock? block = null;
            EventWaitHandle? wanted = null;
            EventWaitHandle? updateRequest = null;
            EventWaitHandle? detail = null;
            EventWaitHandle? onDemand = null;
            // Monotonic clock: a clock change (NTP, a user) must not freeze sampling or the update cooldown.
            var clock = Stopwatch.StartNew();
            TimeSpan never = TimeSpan.FromDays(-1);
            TimeSpan lastUpdateCheck = never, lastEventAttempt = never;
            int updating = 0;
            SensorReader? reader = null;
            try
            {
                block = new SharedBlock();
                TimeSpan lastWanted = Log.ToConsole ? clock.Elapsed : never;
                TimeSpan lastRead = never, lastDetail = never, lastFansOnly = never, lastOnDemand = never;
                TimeSpan lastPawnIoCheck = never;

                while (true)
                {
                    // The events are (re)created until they exist: a name taken by another account only delays
                    // us, it never takes the service down.
                    if ((wanted == null || updateRequest == null || detail == null || onDemand == null) &&
                        clock.Elapsed - lastEventAttempt >= EventRetry)
                    {
                        lastEventAttempt = clock.Elapsed;
                        wanted ??= SharedBlock.CreateWantedEvent();
                        updateRequest ??= SharedBlock.CreateClientEvent(SelfUpdate.UpdateEventName);
                        detail ??= SharedBlock.CreateClientEvent(SharedBlock.DetailEventName);
                        onDemand ??= SharedBlock.CreateClientEvent(SharedBlock.OnDemandEventName);
                    }
                    var handles = new WaitHandle[]
                    {
                        _stop, (WaitHandle?)wanted ?? _never, (WaitHandle?)updateRequest ?? _never, (WaitHandle?)detail ?? _never,
                        (WaitHandle?)onDemand ?? _never,
                    };
                    int signaled = WaitHandle.WaitAny(handles, TimeSpan.FromSeconds(1));
                    if (signaled == 0) break;
                    TimeSpan now = clock.Elapsed;
                    if (signaled == 2)
                    {
                        // sys-bar found this service older than itself: look for an update (rate-limited).
                        if (now - lastUpdateCheck >= UpdateCooldown && Interlocked.CompareExchange(ref updating, 1, 0) == 0)
                        {
                            lastUpdateCheck = now;
                            ThreadPool.QueueUserWorkItem(_ =>
                            {
                                try { SelfUpdate.CheckAndUpdate(); }
                                catch (Exception e) { Log.Write($"update failed: {e.GetType().Name}: {e.Message}"); }
                                finally { Interlocked.Exchange(ref updating, 0); }
                            });
                        }
                        continue;
                    }
                    if (signaled == 4)
                    {
                        lastOnDemand = now;  // the client tells us when fans are on screen
                        continue;
                    }
                    bool fansOnly = false;
                    if (signaled == 3)
                    {
                        // The card is opening: its fans are refreshed right away (at most every MinInterval), then
                        // at full rate while it stays open.
                        fansOnly = now - lastDetail >= DetailWindow && now - lastFansOnly >= MinInterval;
                        lastDetail = now;
                        lastWanted = now;
                        if (!fansOnly) continue;
                        lastFansOnly = now;
                    }
                    if (signaled == 1 || Log.ToConsole) lastWanted = now;

                    if (now - lastWanted < ActiveWindow)
                    {
                        if (!fansOnly && now - lastRead < MinInterval) continue;
                        if (!fansOnly) lastRead = now;
                        try
                        {
                            if (reader == null)
                            {
                                reader = Open();
                                fansOnly = false;  // nothing has been read yet: everything at once
                            }
                            // Fans at full rate while on screen, or for a client that never says when they are.
                            bool fansShown = now - lastOnDemand >= ActiveWindow || now - lastDetail < DetailWindow;
                            var (cpu, readings) = reader.Read(fansShown, fansOnly);
                            block.Publish(cpu, readings);
                            if (reader.OpenedWithoutPawnIo && now - lastPawnIoCheck >= PawnIoCheck)
                            {
                                lastPawnIoCheck = now;
                                if (SensorReader.PawnIoReady())
                                {
                                    Log.Write("PawnIO is available now: opening the sensors again");
                                    SafeDispose(reader);
                                    reader = null;
                                }
                            }
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
                            SafeDispose(reader);
                            reader = null;
                            _stop.WaitOne(TimeSpan.FromSeconds(5));
                        }
                    }
                    else if (reader != null && now - lastWanted > ReleaseAfter)
                    {
                        // Nobody has asked for a while: release the driver handles until needed again.
                        SafeDispose(reader);
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
                SafeDispose(reader);
                wanted?.Dispose();
                updateRequest?.Dispose();
                detail?.Dispose();
                onDemand?.Dispose();
                block?.Dispose();
                Log.Write("service stopped");
            }
        }

        private static void SafeDispose(IDisposable? disposable)
        {
            try
            {
                disposable?.Dispose();
            }
            catch (Exception e)
            {
                Log.Write($"dispose failed: {e.GetType().Name}: {e.Message}");
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

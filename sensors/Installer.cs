using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Principal;
using System.ServiceProcess;
using System.Threading;

namespace SysBar.Sensors
{
    /// <summary>
    /// Installs/removes the service. The binaries are copied to Program Files first: a LocalSystem service
    /// must never run from a folder its user can write to (sys-bar itself lives in %LOCALAPPDATA%).
    /// The elevated install starts from that user-writable folder, so nothing is taken from it on trust:
    /// every library is checked against the manifest embedded in this executable (and read through a
    /// handle that denies writers), the executable copies its own locked image, and the PawnIO installer
    /// must match a hash compiled in here before it runs, from an administrators-only folder.
    /// </summary>
    internal static class Installer
    {
        public const int ExitOk = 0;
        public const int ExitFailed = 1;
        public const int ExitNotAdmin = 5;

        private const string ExeName = "sys-bar-sensors.exe";
        private const string PawnIOSetup = "PawnIO_setup.exe";
        // PawnIO 2.2.0 (namazso/PawnIO.Setup), the same hash tools/package.ps1 pins at build time.
        private const string PawnIOSha256 = "1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032";

        public static readonly string Target = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "sys-bar", "sensors");

        public static bool IsAdministrator() =>
            new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);

        public static int Install()
        {
            if (!IsAdministrator()) return ExitNotAdmin;
            string source = AppDomain.CurrentDomain.BaseDirectory;
            bool stopped = false;
            try
            {
                // Everything is read and checked before the running service is touched: a refused install
                // leaves the installed one exactly as it was, running. Our own image is read through a
                // handle that also blocks renames, so the running file cannot be swapped under us.
                var files = new List<KeyValuePair<string, byte[]>>();
                if (!SamePath(source, Target))
                {
                    Dictionary<string, string>? manifest = Manifest.Load();
                    if (manifest == null) Log.Write("install: development build (no manifest), copying by file type");
                    IEnumerable<string> names = manifest?.Keys ??
                        Directory.GetFiles(source).Select(Path.GetFileName)
                            .Where(n => Path.GetExtension(n).ToLowerInvariant() is ".dll" or ".config");
                    foreach (string name in names)
                    {
                        if (name.Equals(ExeName, StringComparison.OrdinalIgnoreCase)) continue;
                        byte[] data = Manifest.ReadLocked(Path.Combine(source, name));
                        if (manifest != null && Manifest.Sha256(data) != manifest[name])
                        {
                            Log.Write($"install refused: {name} does not match this release");
                            return ExitFailed;
                        }
                        files.Add(new KeyValuePair<string, byte[]>(name, data));
                    }
                    files.Add(new KeyValuePair<string, byte[]>(ExeName, Manifest.ReadLocked(Path.Combine(source, ExeName))));
                }

                stopped = true;
                StopService();
                if (files.Count > 0)
                {
                    Directory.CreateDirectory(Target);
                    foreach (var file in files) Manifest.WriteAtomic(Path.Combine(Target, file.Key), file.Value);
                }

                if (!DriverInstalled("PawnIO"))
                {
                    int result = InstallPawnIO(source);
                    if (result != ExitOk)
                    {
                        TryStartService();
                        return result;
                    }
                }

                string exe = Path.Combine(Target, "sys-bar-sensors.exe");
                string binPath = $"\"{exe}\" --service";
                if (ServiceExists())
                {
                    Sc($"config {SensorService.Name} binPath= \"{binPath.Replace("\"", "\\\"")}\" start= auto");
                }
                else
                {
                    Sc($"create {SensorService.Name} binPath= \"{binPath.Replace("\"", "\\\"")}\" start= auto DisplayName= \"sys-bar sensors\"");
                }
                Sc($"description {SensorService.Name} \"Publishes CPU temperature and fan speeds for sys-bar (LibreHardwareMonitor + PawnIO).\"");
                Sc($"failure {SensorService.Name} reset= 86400 actions= restart/5000/restart/30000/restart/60000");
                using (var service = new ServiceController(SensorService.Name))
                {
                    service.Start();
                    service.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(30));
                }
                Log.Write($"install: service running from {Target}");
                return ExitOk;
            }
            catch (Exception e)
            {
                Log.Write($"install failed: {e}");
                if (stopped) TryStartService();  // never leave a working installation stopped
                return ExitFailed;
            }
        }

        private static void TryStartService()
        {
            try
            {
                if (!ServiceExists()) return;
                using var service = new ServiceController(SensorService.Name);
                if (service.Status == ServiceControllerStatus.Stopped) service.Start();
            }
            catch (Exception e)
            {
                Log.Write($"restart after a failed install failed: {e.Message}");
            }
        }

        public static int Uninstall()
        {
            if (!IsAdministrator()) return ExitNotAdmin;
            try
            {
                StopService();
                if (ServiceExists()) Sc($"delete {SensorService.Name}");
                SelfUpdate.CleanupStaging();
                if (SamePath(AppDomain.CurrentDomain.BaseDirectory, Target))
                {
                    // Running from the installed copy (the safe way: only administrators can write there). A
                    // running executable cannot delete its own folder: a detached cmd does it once we exit.
                    string parent = Path.GetDirectoryName(Target)!;
                    Process.Start(new ProcessStartInfo("cmd.exe",
                        $"/c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"{Target}\" & rmdir \"{parent}\"")
                    {
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        WorkingDirectory = Environment.SystemDirectory,
                    })?.Dispose();
                }
                else
                {
                    // The binaries may still be mapped for a moment after the service stops.
                    for (int attempt = 0; attempt < 10 && Directory.Exists(Target); attempt++)
                    {
                        try
                        {
                            Directory.Delete(Target, recursive: true);
                            break;
                        }
                        catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
                        {
                            Thread.Sleep(500);
                        }
                    }
                    string parent = Path.GetDirectoryName(Target)!;
                    if (Directory.Exists(parent) && !Directory.EnumerateFileSystemEntries(parent).Any()) Directory.Delete(parent);
                }
                // PawnIO is a shared driver (FanControl, LibreHardwareMonitor...): it is left installed.
                return ExitOk;
            }
            catch (Exception e)
            {
                Log.Write($"uninstall failed: {e}");
                return ExitFailed;
            }
        }

        private static bool ServiceExists() =>
            ServiceController.GetServices().Any(s => s.ServiceName.Equals(SensorService.Name, StringComparison.OrdinalIgnoreCase));

        private static bool DriverInstalled(string name) =>
            ServiceController.GetDevices().Any(s => s.ServiceName.Equals(name, StringComparison.OrdinalIgnoreCase));

        /// <summary>
        /// The PawnIO installer runs only after it matched the pinned hash, as a copy in an administrators-only
        /// folder (the original sits in a user-writable one).
        /// </summary>
        private static int InstallPawnIO(string source)
        {
            string original = Path.Combine(source, PawnIOSetup);
            if (!File.Exists(original))
            {
                Log.Write("install: PawnIO is missing and PawnIO_setup.exe was not found");
                return ExitFailed;
            }
            byte[] data = Manifest.ReadLocked(original);
            if (Manifest.Sha256(data) != PawnIOSha256)
            {
                Log.Write("install refused: PawnIO_setup.exe is not the expected file");
                return ExitFailed;
            }
            string folder = Path.Combine(Path.GetDirectoryName(Target)!, "pawnio-setup");
            Directory.CreateDirectory(folder);  // inherits the Program Files ACL
            string setup = Path.Combine(folder, PawnIOSetup);
            try
            {
                Manifest.WriteAtomic(setup, data);
                Log.Write("install: installing PawnIO");
                using Process? process = Process.Start(new ProcessStartInfo(setup, "-install")
                {
                    UseShellExecute = false,
                    WorkingDirectory = folder,
                });
                process?.WaitForExit();
            }
            finally
            {
                try { Directory.Delete(folder, recursive: true); } catch (Exception) { }
            }
            if (!DriverInstalled("PawnIO"))
            {
                Log.Write("install: PawnIO installation was cancelled or failed");
                return ExitFailed;
            }
            return ExitOk;
        }

        public static void StopService()
        {
            if (!ServiceExists()) return;
            using var service = new ServiceController(SensorService.Name);
            if (service.Status != ServiceControllerStatus.Stopped)
            {
                service.Stop();
                service.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(30));
            }
            WaitForServiceExit();
        }

        /// <summary>"Stopped" is reported before the process is gone: wait for it to release its files.</summary>
        public static void WaitForServiceExit()
        {
            string exe = Path.Combine(Target, ExeName);
            foreach (Process process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(ExeName)))
            {
                try
                {
                    if (process.Id != Process.GetCurrentProcess().Id && SamePath(process.MainModule!.FileName, exe))
                        process.WaitForExit(15000);
                }
                catch (Exception)
                {
                    // Already gone, or not ours to inspect.
                }
                finally
                {
                    process.Dispose();
                }
            }
        }

        public static bool ServiceRunning()
        {
            if (!ServiceExists()) return false;
            using var service = new ServiceController(SensorService.Name);
            return service.Status != ServiceControllerStatus.Stopped;
        }

        private static void Sc(string arguments)
        {
            using Process process = Process.Start(new ProcessStartInfo("sc.exe", arguments)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
            })!;
            string output = process.StandardOutput.ReadToEnd();
            process.WaitForExit();
            if (process.ExitCode != 0) throw new InvalidOperationException($"sc {arguments} -> {process.ExitCode}: {output.Trim()}");
        }

        public static bool SamePath(string a, string b) =>
            string.Equals(Path.GetFullPath(a).TrimEnd('\\'), Path.GetFullPath(b).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase);
    }
}

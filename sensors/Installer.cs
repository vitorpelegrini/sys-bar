using System;
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
    /// </summary>
    internal static class Installer
    {
        public const int ExitOk = 0;
        public const int ExitFailed = 1;
        public const int ExitNotAdmin = 5;

        private static readonly string Target = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "sys-bar", "sensors");

        public static bool IsAdministrator() =>
            new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);

        public static int Install()
        {
            if (!IsAdministrator()) return ExitNotAdmin;
            string source = AppDomain.CurrentDomain.BaseDirectory;
            try
            {
                StopService();

                if (!SamePath(source, Target))
                {
                    Directory.CreateDirectory(Target);
                    foreach (string file in Directory.GetFiles(source))
                    {
                        string name = Path.GetFileName(file);
                        string ext = Path.GetExtension(file).ToLowerInvariant();
                        if (ext is ".exe" or ".dll" or ".config" && !name.Equals("PawnIO_setup.exe", StringComparison.OrdinalIgnoreCase))
                            File.Copy(file, Path.Combine(Target, name), overwrite: true);
                    }
                }

                if (!DriverInstalled("PawnIO"))
                {
                    string setup = Path.Combine(source, "PawnIO_setup.exe");
                    if (!File.Exists(setup))
                    {
                        Log.Write("install: PawnIO is missing and PawnIO_setup.exe was not found");
                        return ExitFailed;
                    }
                    Log.Write("install: installing PawnIO");
                    using Process? process = Process.Start(new ProcessStartInfo(setup, "-install") { UseShellExecute = false });
                    process?.WaitForExit();
                    if (!DriverInstalled("PawnIO"))
                    {
                        Log.Write("install: PawnIO installation was cancelled or failed");
                        return ExitFailed;
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
                Sc($"failure {SensorService.Name} reset= 86400 actions= restart/5000/restart/30000//");
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
                return ExitFailed;
            }
        }

        public static int Uninstall()
        {
            if (!IsAdministrator()) return ExitNotAdmin;
            try
            {
                StopService();
                if (ServiceExists()) Sc($"delete {SensorService.Name}");
                // The binaries may still be mapped for a moment after the service stops.
                for (int attempt = 0; attempt < 10 && Directory.Exists(Target); attempt++)
                {
                    try
                    {
                        if (!SamePath(AppDomain.CurrentDomain.BaseDirectory, Target)) Directory.Delete(Target, recursive: true);
                        break;
                    }
                    catch (IOException)
                    {
                        Thread.Sleep(500);
                    }
                    catch (UnauthorizedAccessException)
                    {
                        Thread.Sleep(500);
                    }
                }
                SelfUpdate.CleanupStaging();
                string parent = Path.GetDirectoryName(Target)!;
                if (Directory.Exists(parent) && !Directory.EnumerateFileSystemEntries(parent).Any()) Directory.Delete(parent);
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

        private static void StopService()
        {
            if (!ServiceExists()) return;
            using var service = new ServiceController(SensorService.Name);
            if (service.Status != ServiceControllerStatus.Stopped)
            {
                service.Stop();
                service.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(30));
            }
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

        private static bool SamePath(string a, string b) =>
            string.Equals(Path.GetFullPath(a).TrimEnd('\\'), Path.GetFullPath(b).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase);
    }
}

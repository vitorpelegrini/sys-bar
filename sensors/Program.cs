using System;
using System.Runtime.InteropServices;
using System.ServiceProcess;

namespace SysBar.Sensors
{
    /// <summary>
    /// sys-bar-sensors.exe
    ///   --install     copy to Program Files, install PawnIO if needed, register and start the service (admin)
    ///   --uninstall   stop and remove the service and its files (admin); PawnIO is left in place
    ///   --console [s] run in the foreground and print the readings, optionally for s seconds (admin)
    ///   --service     entry point used by the Service Control Manager
    /// </summary>
    internal static class Program
    {
        [DllImport("kernel32.dll")]
        private static extern bool AttachConsole(int processId);

        [DllImport("kernel32.dll")]
        private static extern bool AllocConsole();

        private static int Main(string[] args)
        {
            string command = args.Length > 0 ? args[0].ToLowerInvariant() : "--service";
            switch (command)
            {
                case "--install":
                    return Installer.Install();
                case "--uninstall":
                    return Installer.Uninstall();
                case "--console":
                    if (!AttachConsole(-1)) AllocConsole();
                    if (!Installer.IsAdministrator())
                    {
                        Console.WriteLine("Run from an elevated terminal: reading sensors requires administrator rights.");
                        return Installer.ExitNotAdmin;
                    }
                    TimeSpan? duration = args.Length > 1 && int.TryParse(args[1], out int seconds) && seconds > 0
                        ? TimeSpan.FromSeconds(seconds)
                        : (TimeSpan?)null;
                    new SensorService().RunInConsole(duration);
                    return Installer.ExitOk;
                default:
                    ServiceBase.Run(new SensorService());
                    return Installer.ExitOk;
            }
        }
    }
}

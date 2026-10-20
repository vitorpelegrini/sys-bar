using System;
using System.IO;

namespace SysBar.Sensors
{
    /// <summary>
    /// Small diagnostic log in the installed folder, always (Program Files: only administrators and SYSTEM
    /// can write there, unlike ProgramData or the per-user copy the elevated installer starts from, which
    /// would expose elevated code to link attacks). The staged updater logs there too, so it survives.
    /// </summary>
    internal static class Log
    {
        private const long MaxBytes = 256 * 1024;
        private static readonly object Gate = new object();
        private static readonly string FilePath = Path.Combine(Installer.Target, "sensors.log");

        public static bool ToConsole { get; set; }

        public static void Write(string message)
        {
            string line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {message}";
            if (ToConsole) Console.WriteLine(line);
            lock (Gate)
            {
                try
                {
                    if (!Directory.Exists(Installer.Target)) return;  // nothing installed yet: nowhere safe to write
                    var info = new FileInfo(FilePath);
                    if (info.Exists && info.Length > MaxBytes) info.Delete();
                    File.AppendAllText(FilePath, line + Environment.NewLine);
                }
                catch (Exception)
                {
                    // Logging must never take the service down.
                }
            }
        }
    }
}

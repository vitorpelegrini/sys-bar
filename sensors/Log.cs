using System;
using System.IO;

namespace SysBar.Sensors
{
    /// <summary>
    /// Small diagnostic log next to the binaries (Program Files: only administrators and SYSTEM can write
    /// there, unlike ProgramData, which would expose the SYSTEM service to link attacks).
    /// </summary>
    internal static class Log
    {
        private const long MaxBytes = 256 * 1024;
        private static readonly object Gate = new object();
        private static readonly string FilePath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "sensors.log");

        public static bool ToConsole { get; set; }

        public static void Write(string message)
        {
            string line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {message}";
            if (ToConsole) Console.WriteLine(line);
            lock (Gate)
            {
                try
                {
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

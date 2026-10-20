using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;

namespace SysBar.Sensors
{
    /// <summary>
    /// SHA-256 of every file shipped next to the executable (libraries, .config), embedded in the executable
    /// by tools/package.ps1. Elevated steps copy nothing that is not listed with the right hash, so a file
    /// planted or swapped in a user-writable folder never reaches Program Files. Development builds have no
    /// manifest (<see cref="Load"/> returns null) and fall back to copying by file type.
    /// </summary>
    internal static class Manifest
    {
        private const string ResourceName = "manifest.txt";

        /// <summary>File name (case-insensitive) to lowercase hex SHA-256, or null for a development build.</summary>
        public static Dictionary<string, string>? Load() => Parse(typeof(Manifest).Assembly);

        /// <summary>The manifest embedded in another copy of the executable (a staged update), read without running it.</summary>
        public static Dictionary<string, string>? LoadFrom(string exe)
        {
            try
            {
                return Parse(Assembly.ReflectionOnlyLoad(File.ReadAllBytes(exe)));
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static Dictionary<string, string>? Parse(Assembly assembly)
        {
            using Stream? stream = assembly.GetManifestResourceStream(ResourceName);
            if (stream == null) return null;
            var files = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            using var reader = new StreamReader(stream);
            string? line;
            while ((line = reader.ReadLine()) != null)
            {
                // "<64 hex>  <file name>", as written by package.ps1.
                line = line.Trim();
                if (line.Length < 67) continue;
                string hash = line.Substring(0, 64).ToLowerInvariant();
                string name = line.Substring(64).Trim();
                if (name.Length == 0 || name.IndexOfAny(new[] { '/', '\\', ':' }) >= 0) continue;
                files[name] = hash;
            }
            return files;
        }

        public static string Sha256(byte[] data)
        {
            using var sha = SHA256.Create();
            return BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant();
        }

        /// <summary>
        /// Reads a file once, through a handle that denies writers, renames and deletes for as long as it is
        /// open: what is hashed is exactly what gets copied.
        /// </summary>
        public static byte[] ReadLocked(string path)
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            var data = new byte[stream.Length];
            int offset = 0;
            while (offset < data.Length)
            {
                int read = stream.Read(data, offset, data.Length - offset);
                if (read <= 0) throw new IOException($"short read: {path}");
                offset += read;
            }
            return data;
        }

        /// <summary>Writes through a temporary file and a rename, so a reader never sees half a file.</summary>
        public static void WriteAtomic(string path, byte[] data)
        {
            string temp = path + ".new";
            File.WriteAllBytes(temp, data);
            if (File.Exists(path)) File.Delete(path);
            File.Move(temp, path);
        }
    }
}

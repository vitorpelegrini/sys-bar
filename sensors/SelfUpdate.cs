using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.ServiceProcess;
using System.Text;
using System.Threading;

namespace SysBar.Sensors
{
    /// <summary>
    /// Keeps the service in step with sys-bar without any UAC prompt. sys-bar updates itself without
    /// administrator rights, but this service lives in Program Files; so the service updates itself:
    /// <list type="number">
    /// <item>sys-bar notices the service is older and sets the "update" event (no data is passed).</item>
    /// <item>The service downloads the sensors package of the latest stable release straight from GitHub,
    /// checks its SHA-256 (and, once releases are code-signed, the publisher) and extracts it into an
    /// administrators-only staging folder.</item>
    /// <item>The new executable, started from staging with --apply-update, stops the service, copies the
    /// files and starts it again.</item>
    /// </list>
    /// No local process can feed files to the service: the only inputs are the official release and its hash.
    /// </summary>
    internal static class SelfUpdate
    {
        public const string UpdateEventName = @"Global\SysBarSensorsUpdate";
        private const string ReleasesApi = "https://api.github.com/repos/vitorpelegrini/sys-bar/releases/latest";
        private const long MaxPackageBytes = 64L * 1024 * 1024;
        private static readonly string[] AllowedExtensions = { ".exe", ".dll", ".config" };

        private static string Root =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "sys-bar");
        public static string Target => Path.Combine(Root, "sensors");
        public static string Staging => Path.Combine(Root, "sensors-update");
        private static string StagedFiles => Path.Combine(Staging, "files");

        public static Version Current
        {
            get
            {
                Version v = typeof(SelfUpdate).Assembly.GetName().Version ?? new Version(0, 0, 0);
                return new Version(v.Major, v.Minor, Math.Max(v.Build, 0));
            }
        }

        /// <summary>Version as published in the shared block: major &lt;&lt; 20 | minor &lt;&lt; 10 | patch.</summary>
        public static uint Encoded(Version v) =>
            ((uint)Math.Min(v.Major, 1023) << 20) | ((uint)Math.Min(v.Minor, 1023) << 10) | (uint)Math.Min(Math.Max(v.Build, 0), 1023);

        [DataContract]
        private sealed class ReleaseDto
        {
            [DataMember(Name = "tag_name")] public string? Tag { get; set; }
            [DataMember(Name = "draft")] public bool Draft { get; set; }
            [DataMember(Name = "prerelease")] public bool Prerelease { get; set; }
            [DataMember(Name = "assets")] public AssetDto[]? Assets { get; set; }
        }

        [DataContract]
        private sealed class AssetDto
        {
            [DataMember(Name = "name")] public string? Name { get; set; }
            [DataMember(Name = "browser_download_url")] public string? Url { get; set; }
        }

        /// <summary>Checks GitHub and, when a newer release exists, stages and applies it. Runs as SYSTEM.</summary>
        public static void CheckAndUpdate()
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(60) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd($"sys-bar-sensors/{Current.ToString(3)}");
            http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");

            ReleaseDto? release;
            using (HttpResponseMessage response = http.GetAsync(ReleasesApi).GetAwaiter().GetResult())
            {
                if (response.StatusCode == HttpStatusCode.NotFound)
                {
                    Log.Write("update: no release published yet");
                    return;
                }
                response.EnsureSuccessStatusCode();
                using Stream json = response.Content.ReadAsStreamAsync().GetAwaiter().GetResult();
                release = (ReleaseDto?)new DataContractJsonSerializer(typeof(ReleaseDto)).ReadObject(json);
            }
            if (release == null || release.Draft || release.Prerelease) return;
            Version? latest = ParseTag(release.Tag);
            if (latest == null || latest <= Current)
            {
                Log.Write($"update: up to date ({Current.ToString(3)})");
                return;
            }

            string package = $"sys-bar-{latest.ToString(3)}-sensors-x64.zip";
            string? zipUrl = release.Assets?.FirstOrDefault(a => a.Name == package)?.Url;
            string? hashUrl = release.Assets?.FirstOrDefault(a => a.Name == package + ".sha256")?.Url;
            if (!IsHttps(zipUrl) || !IsHttps(hashUrl))
            {
                Log.Write($"update: release {latest.ToString(3)} has no sensors package");
                return;
            }

            string expected = ParseSha256(http.GetStringAsync(hashUrl).GetAwaiter().GetResult(), package)
                ?? throw new InvalidDataException("malformed checksum file");
            ResetStaging();
            string zip = Path.Combine(Staging, package);
            using (Stream source = http.GetStreamAsync(zipUrl).GetAwaiter().GetResult())
            using (FileStream target = File.Create(zip))
            {
                CopyLimited(source, target, MaxPackageBytes);
            }
            Log.Write($"update: downloaded {package}");
            InstallFrom(zip, expected, latest);
        }

        /// <summary>Verifies a package, extracts it into staging and launches the apply step.</summary>
        public static void InstallFrom(string zip, string expectedSha256, Version version, bool wait = false)
        {
            string actual = Sha256(zip);
            if (!string.Equals(actual, expectedSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"checksum mismatch: {actual}");

            if (Directory.Exists(StagedFiles)) Directory.Delete(StagedFiles, recursive: true);
            Directory.CreateDirectory(StagedFiles);
            using (ZipArchive archive = ZipFile.OpenRead(zip))
            {
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    // Flat packages only, known file types only: no path traversal, nothing unexpected.
                    string name = entry.FullName;
                    if (name.IndexOfAny(new[] { '/', '\\', ':' }) >= 0 ||
                        !AllowedExtensions.Contains(Path.GetExtension(name).ToLowerInvariant()))
                    {
                        if (name.EndsWith("PawnIO_setup.exe", StringComparison.OrdinalIgnoreCase)) continue;
                        throw new InvalidDataException($"unexpected entry {name}");
                    }
                    entry.ExtractToFile(Path.Combine(StagedFiles, name), overwrite: true);
                }
            }

            string staged = Path.Combine(StagedFiles, "sys-bar-sensors.exe");
            string fileVersion = FileVersionInfo.GetVersionInfo(staged).FileVersion ?? "";
            if (!fileVersion.StartsWith(version.ToString(3), StringComparison.Ordinal))
                throw new InvalidDataException($"package version {fileVersion} does not match {version.ToString(3)}");

            // Once releases are code-signed, an update must carry a valid signature from the same publisher.
            string current = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "sys-bar-sensors.exe");
            string? publisher = Publisher(current);
            if (publisher != null && (!TrustedSignature(staged) || Publisher(staged) != publisher))
                throw new InvalidDataException("update is not signed by the current publisher");

            Log.Write($"update: staged {version.ToString(3)}, applying");
            using Process? process = Process.Start(new ProcessStartInfo(staged, "--apply-update")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = StagedFiles,
            });
            if (wait) process?.WaitForExit(120000);
        }

        /// <summary>Runs from the staged copy: swaps the installed files and restarts the service.</summary>
        public static int Apply()
        {
            string source = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
            if (!string.Equals(Path.GetFullPath(source), Path.GetFullPath(StagedFiles), StringComparison.OrdinalIgnoreCase))
            {
                Log.Write("apply-update refused: not running from the staging folder");
                return Installer.ExitFailed;
            }
            using var service = new ServiceController(SensorService.Name);
            try
            {
                if (service.Status != ServiceControllerStatus.Stopped)
                {
                    service.Stop();
                    service.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(30));
                }
                Thread.Sleep(500);  // let the old process release its files
                foreach (string file in Directory.GetFiles(source))
                {
                    File.Copy(file, Path.Combine(Target, Path.GetFileName(file)), overwrite: true);
                }
                Log.Write($"apply-update: installed {Current.ToString(3)}");
                return Installer.ExitOk;
            }
            catch (Exception e)
            {
                Log.Write($"apply-update failed: {e.Message}");
                return Installer.ExitFailed;
            }
            finally
            {
                // Always bring the service back, updated or not.
                try
                {
                    service.Refresh();
                    if (service.Status == ServiceControllerStatus.Stopped) service.Start();
                }
                catch (Exception e)
                {
                    Log.Write($"apply-update: restart failed: {e.Message}");
                }
            }
        }

        /// <summary>Removes the staging folder (the service calls it on start, after an update).</summary>
        public static void CleanupStaging()
        {
            try
            {
                if (Directory.Exists(Staging)) Directory.Delete(Staging, recursive: true);
            }
            catch (Exception)
            {
                // Still in use right after an update: removed on a later start.
            }
        }

        private static void ResetStaging()
        {
            CleanupStaging();
            Directory.CreateDirectory(Staging);  // inherits the Program Files ACL: administrators/SYSTEM only
        }

        private static Version? ParseTag(string? tag)
        {
            if (tag == null || !tag.StartsWith("v", StringComparison.Ordinal)) return null;
            string[] parts = tag.Substring(1).Split('.');
            if (parts.Length != 3 || !parts.All(p => p.Length > 0 && p.All(char.IsDigit))) return null;
            return new Version(int.Parse(parts[0]), int.Parse(parts[1]), int.Parse(parts[2]));
        }

        private static bool IsHttps(string? url) => url != null && url.StartsWith("https://", StringComparison.Ordinal);

        public static string? ParseSha256(string text, string fileName)
        {
            string line = text.Trim();
            if (line.Length < 64) return null;
            string hex = line.Substring(0, 64);
            if (!hex.All(Uri.IsHexDigit)) return null;
            string rest = line.Substring(64).Trim().TrimStart('*');
            return rest.Length == 0 || rest.Equals(fileName, StringComparison.OrdinalIgnoreCase) ? hex.ToLowerInvariant() : null;
        }

        private static string Sha256(string file)
        {
            using var sha = SHA256.Create();
            using FileStream stream = File.OpenRead(file);
            return string.Concat(sha.ComputeHash(stream).Select(b => b.ToString("x2")));
        }

        private static void CopyLimited(Stream source, Stream target, long limit)
        {
            var buffer = new byte[81920];
            long total = 0;
            int read;
            while ((read = source.Read(buffer, 0, buffer.Length)) > 0)
            {
                total += read;
                if (total > limit) throw new InvalidDataException("package too large");
                target.Write(buffer, 0, read);
            }
        }

        // ---- Authenticode -----------------------------------------------------------------------------

        private static string? Publisher(string file)
        {
            try
            {
                return X509Certificate.CreateFromSignedFile(file).Subject;
            }
            catch (Exception)
            {
                return null;  // unsigned
            }
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustFileInfo
        {
            public uint StructSize;
            public string FilePath;
            public IntPtr File;
            public IntPtr KnownSubject;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustData
        {
            public uint StructSize;
            public IntPtr PolicyCallbackData;
            public IntPtr SipClientData;
            public uint UiChoice;              // 2 = no UI
            public uint RevocationChecks;      // 0 = none (offline-friendly), chain still validated
            public uint UnionChoice;           // 1 = file
            public IntPtr FileInfo;
            public uint StateAction;
            public IntPtr StateData;
            public string? UrlReference;
            public uint ProvFlags;
            public uint UiContext;
            public IntPtr SignatureSettings;
        }

        [DllImport("wintrust.dll", CharSet = CharSet.Unicode)]
        private static extern int WinVerifyTrust(IntPtr hwnd, ref Guid action, ref WinTrustData data);

        private static bool TrustedSignature(string file)
        {
            var action = new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");  // WINTRUST_ACTION_GENERIC_VERIFY_V2
            var info = new WinTrustFileInfo { StructSize = (uint)Marshal.SizeOf<WinTrustFileInfo>(), FilePath = file };
            IntPtr infoPtr = Marshal.AllocHGlobal(Marshal.SizeOf<WinTrustFileInfo>());
            try
            {
                Marshal.StructureToPtr(info, infoPtr, false);
                var data = new WinTrustData
                {
                    StructSize = (uint)Marshal.SizeOf<WinTrustData>(),
                    UiChoice = 2,
                    UnionChoice = 1,
                    FileInfo = infoPtr,
                };
                return WinVerifyTrust(IntPtr.Zero, ref action, ref data) == 0;
            }
            finally
            {
                Marshal.DestroyStructure<WinTrustFileInfo>(infoPtr);
                Marshal.FreeHGlobal(infoPtr);
            }
        }
    }
}

using System;
using System.Collections.Generic;
using System.IO.MemoryMappedFiles;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;

namespace SysBar.Sensors
{
    /// <summary>Kind of a reading. Must match <c>SensorKind</c> in src/metrics.h.</summary>
    internal enum Kind : byte { Temperature = 0, Fan = 1 }

    /// <summary>Hardware group of a reading. Must match <c>SensorGroup</c> in src/metrics.h.</summary>
    internal enum Group : byte { Cpu = 0, Gpu = 1, Disk = 2, Board = 3 }

    /// <summary>What a fan cools. Must match <c>FanRole</c> in src/metrics.h.</summary>
    internal enum Role : byte { Unknown = 0, Cpu = 1, Gpu = 2, Case = 3, Pump = 4, Psu = 5 }

    internal readonly struct Reading
    {
        public Reading(Kind kind, Group group, string name, float value, Role role = Role.Unknown, float duty = float.NaN)
        {
            Kind = kind;
            Group = group;
            Name = name;
            Value = value;
            Role = role;
            Duty = duty;
        }

        public Kind Kind { get; }
        public Group Group { get; }
        public string Name { get; }
        public float Value { get; }
        public Role Role { get; }

        /// <summary>Fans: how hard the fan is driven, 0..100 % (PWM duty); NaN when unknown.</summary>
        public float Duty { get; }
    }

    /// <summary>
    /// Read-only (for clients) shared memory with the latest readings, protected by a sequence lock.
    /// Layout v1, little-endian - mirrored by src/sensorfeed.cpp:
    /// <code>
    ///  0 uint32 magic 'SBS1'     4 uint32 version (1)    8 uint32 sequence (odd while writing)
    /// 12 uint32 count           16 int64 updated (FILETIME, UTC)
    /// 24 float  cpuTemperature (NaN = unknown)           28 uint32 service version (major&lt;&lt;20|minor&lt;&lt;10|patch)
    /// 32 entries[64] x 128 bytes: kind u8, group u8, role u8 (fans), duty u8 (fans: percent + 1, 0 = unknown),
    ///    value f32, name wchar[60]
    /// </code>
    /// Clients ask for fresh data by setting the "wanted" event; when nobody asks, the service idles.
    /// </summary>
    internal sealed class SharedBlock : IDisposable
    {
        public const string MappingName = @"Global\SysBarSensors";
        public const string WantedEventName = @"Global\SysBarSensorsWanted";
        public const uint Magic = 0x31534253;  // "SBS1"
        public const uint Version = 1;
        public const int MaxEntries = 64;
        public const int NameChars = 60;
        private const int HeaderSize = 32;
        private const int EntrySize = 128;
        public const int Size = HeaderSize + MaxEntries * EntrySize;

        private readonly MemoryMappedFile _mapping;
        private readonly MemoryMappedViewAccessor _view;
        private uint _sequence;

        public SharedBlock()
        {
            // SYSTEM and administrators own it; any signed-in user may only read it.
            var security = new MemoryMappedFileSecurity();
            security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null), MemoryMappedFileRights.FullControl,
                AccessControlType.Allow));
            security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null),
                MemoryMappedFileRights.FullControl, AccessControlType.Allow));
            security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid, null), MemoryMappedFileRights.Read,
                AccessControlType.Allow));
            // CreateOrOpen: after a service restart a client may still hold the previous block open.
            _mapping = MemoryMappedFile.CreateOrOpen(MappingName, Size, MemoryMappedFileAccess.ReadWrite,
                MemoryMappedFileOptions.None, security, System.IO.HandleInheritability.None);
            _view = _mapping.CreateViewAccessor(0, Size, MemoryMappedFileAccess.ReadWrite);
            // A block kept alive by a client across a service restart: continue its sequence (never reuse a
            // value a reader may have seen), starting from an even, consistent state.
            _sequence = _view.ReadUInt32(8);
            if ((_sequence & 1) != 0) _sequence++;
            Publish(float.NaN, Array.Empty<Reading>());
        }

        /// <summary>Creates the auto-reset event clients set to ask for data (they may only signal it).</summary>
        public static EventWaitHandle? CreateWantedEvent() => CreateClientEvent(WantedEventName);

        /// <summary>
        /// Auto-reset event that signed-in users may only signal (no data travels through it). Any user can
        /// create a named event, so one that already exists is only used when SYSTEM owns it (a leftover of
        /// this service); otherwise null is returned and the caller tries again later, never crashing.
        /// </summary>
        public static EventWaitHandle? CreateClientEvent(string name)
        {
            var security = new EventWaitHandleSecurity();
            security.AddAccessRule(new EventWaitHandleAccessRule(
                new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null), EventWaitHandleRights.FullControl,
                AccessControlType.Allow));
            security.AddAccessRule(new EventWaitHandleAccessRule(
                new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid, null),
                EventWaitHandleRights.Modify | EventWaitHandleRights.Synchronize, AccessControlType.Allow));
            EventWaitHandle? handle = null;
            try
            {
                handle = new EventWaitHandle(false, EventResetMode.AutoReset, name, out bool created, security);
                if (created || OwnedBySystem(handle.GetAccessControl())) return handle;
                Log.Write($"refusing {name}: created by another account");
            }
            catch (Exception e)
            {
                Log.Write($"cannot create {name}: {e.GetType().Name}: {e.Message}");
            }
            handle?.Dispose();
            return null;
        }

        private static bool OwnedBySystem(ObjectSecurity security) =>
            security.GetOwner(typeof(SecurityIdentifier)) is SecurityIdentifier owner &&
            (owner.IsWellKnown(WellKnownSidType.LocalSystemSid) || owner.IsWellKnown(WellKnownSidType.BuiltinAdministratorsSid));

        public void Publish(float cpuTemperature, IReadOnlyList<Reading> readings)
        {
            int count = Math.Min(readings.Count, MaxEntries);
            _view.Write(8, ++_sequence);  // odd: write in progress
            Thread.MemoryBarrier();
            _view.Write(0, Magic);
            _view.Write(4, Version);
            _view.Write(28, SelfUpdate.Encoded(SelfUpdate.Current));  // lets sys-bar spot an outdated service
            _view.Write(12, (uint)count);
            _view.Write(16, DateTime.UtcNow.ToFileTimeUtc());
            _view.Write(24, cpuTemperature);
            for (int i = 0; i < count; i++)
            {
                long at = HeaderSize + (long)i * EntrySize;
                Reading r = readings[i];
                _view.Write(at, (byte)r.Kind);
                _view.Write(at + 1, (byte)r.Group);
                _view.Write(at + 2, (byte)r.Role);
                _view.Write(at + 3, float.IsNaN(r.Duty) ? (byte)0 : (byte)(Math.Round(Math.Max(0f, Math.Min(100f, r.Duty))) + 1));
                _view.Write(at + 4, r.Value);
                string name = r.Name.Length > NameChars - 1 ? r.Name.Substring(0, NameChars - 1) : r.Name;
                for (int c = 0; c < NameChars; c++)
                {
                    _view.Write(at + 8 + c * 2, c < name.Length ? name[c] : '\0');
                }
            }
            Thread.MemoryBarrier();
            _view.Write(8, ++_sequence);  // even: consistent
        }

        public void Dispose()
        {
            _view.Dispose();
            _mapping.Dispose();
        }
    }
}

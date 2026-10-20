using System;
using System.Text.RegularExpressions;

namespace SysBar.Sensors
{
    /// <summary>Works out what a motherboard fan cools from its name or, failing that, from its header.</summary>
    internal static class FanRoles
    {
        /// <summary>Names LibreHardwareMonitor gives on boards it knows ("CPU Fan", "System Fan #1", "AIO Pump"...).</summary>
        public static Role FromName(string name)
        {
            string n = name.ToUpperInvariant();
            if (n.Contains("PUMP") || n.Contains("AIO") || n.Contains("WATER")) return Role.Pump;
            if (n.Contains("CPU")) return Role.Cpu;
            if (n.Contains("SYSTEM") || n.Contains("SYS ") || n.StartsWith("SYS", StringComparison.Ordinal) ||
                n.Contains("CHASSIS") || n.Contains("CASE") || n.Contains("REAR") || n.Contains("FRONT"))
            {
                return Role.Case;
            }
            if (n.Contains("PSU") || n.Contains("POWER SUPPLY")) return Role.Psu;
            return Role.Unknown;
        }

        /// <summary>
        /// Generic "Fan #N" names: the Super I/O reference designs wire the CPU header to a fixed input. Nuvoton
        /// NCT67xx: FANIN0 = SYSFAN, FANIN1 = CPUFAN ("Fan #2"); the NCT668x family (MSI boards) uses input 0
        /// ("Fan #1"). ITE IT87xx and Fintek F718xx: FAN1 = CPU_FAN ("Fan #1"). With a single fan spinning on
        /// the chip, that fan is the CPU fan. Other inputs are case fans. A guess: the menu corrects it.
        /// </summary>
        public static Role FromChip(string chip, string name, int fanCount)
        {
            Match m = Regex.Match(name, @"^Fan #(\d+)$");
            if (!m.Success) return Role.Unknown;
            int index = int.Parse(m.Groups[1].Value);
            if (fanCount == 1) return Role.Cpu;
            string c = chip.ToUpperInvariant();
            int cpuIndex = c.Contains("NCT668") ? 1
                : c.Contains("NUVOTON") || c.Contains("NCT") ? 2
                : c.Contains("ITE") || c.Contains("IT87") || c.Contains("FINTEK") || c.Contains("F718") ? 1
                : 0;
            if (cpuIndex == 0) return Role.Unknown;
            return index == cpuIndex ? Role.Cpu : Role.Case;
        }
    }
}

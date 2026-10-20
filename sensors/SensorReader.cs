using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using LibreHardwareMonitor.Hardware;

namespace SysBar.Sensors
{
    /// <summary>Reads CPU temperature, fans and drive temperatures through LibreHardwareMonitorLib.</summary>
    internal sealed class SensorReader : IDisposable
    {
        private static readonly TimeSpan StorageInterval = TimeSpan.FromSeconds(10);  // SMART queries are slow
        private static readonly TimeSpan GpuInterval = TimeSpan.FromSeconds(3);       // only its fan, which is slow
        private readonly Computer _computer;
        private readonly Stopwatch _clock = Stopwatch.StartNew();  // monotonic: immune to clock changes
        private TimeSpan _lastStorageUpdate = TimeSpan.FromDays(-1);
        private TimeSpan _lastGpuUpdate = TimeSpan.FromDays(-1);

        public SensorReader()
        {
            // GPU temperatures are read natively by sys-bar (D3DKMT); the GPU is only opened here for its fan,
            // which D3DKMT cannot tell apart from "stopped" (zero-RPM mode). Memory/network/battery are not needed.
            _computer = new Computer
            {
                IsCpuEnabled = true,
                IsMotherboardEnabled = true,
                IsControllerEnabled = true,
                IsStorageEnabled = true,
                IsPsuEnabled = true,
                IsGpuEnabled = true,
                IsMemoryEnabled = false,
                IsNetworkEnabled = false,
                IsBatteryEnabled = false,
            };
            _computer.Open();
        }

        public (float cpuTemperature, List<Reading> readings) Read()
        {
            var readings = new List<Reading>();
            var boardFans = new List<(IHardware item, string name, float rpm, float duty)>();
            float cpu = float.NaN;
            TimeSpan now = _clock.Elapsed;
            bool refreshStorage = now - _lastStorageUpdate >= StorageInterval;
            if (refreshStorage) _lastStorageUpdate = now;
            bool refreshGpu = now - _lastGpuUpdate >= GpuInterval;
            if (refreshGpu) _lastGpuUpdate = now;
            foreach (IHardware hardware in _computer.Hardware)
            {
                // Drive temperatures and GPU fans change slowly and cost most of the time: refresh them less often.
                bool due = hardware.HardwareType == HardwareType.Storage ? refreshStorage
                    : IsGpu(hardware.HardwareType) ? refreshGpu
                    : true;
                if (due) Update(hardware);
                switch (hardware.HardwareType)
                {
                    case HardwareType.Cpu:
                        float? package = CpuTemperature(hardware);
                        if (package.HasValue) cpu = float.IsNaN(cpu) ? package.Value : Math.Max(cpu, package.Value);
                        break;
                    case HardwareType.Storage:
                        foreach (ISensor sensor in hardware.Sensors)
                        {
                            if (sensor.SensorType == SensorType.Temperature && sensor.Value is float t && Plausible(t))
                            {
                                readings.Add(new Reading(Kind.Temperature, Group.Disk, hardware.Name, t));
                                break;  // the first one is the drive temperature
                            }
                        }
                        break;
                }
                if (IsGpu(hardware.HardwareType))
                {
                    // A GPU fan at 0 RPM is a stopped fan (zero-RPM mode at idle), worth showing as such.
                    ISensor? fan = hardware.Sensors.FirstOrDefault(s => s.SensorType == SensorType.Fan);
                    ISensor? control = hardware.Sensors.FirstOrDefault(s => s.SensorType == SensorType.Control);
                    if (fan?.Value is float gpuRpm && gpuRpm >= 0f && gpuRpm < 20000f)
                    {
                        readings.Add(new Reading(Kind.Fan, Group.Gpu, hardware.Name, gpuRpm, Role.Gpu, Duty(control)));
                    }
                    continue;
                }
                // Fans: motherboard Super I/O chips, embedded controllers, fan controllers, PSUs.
                foreach (IHardware item in Flatten(hardware))
                {
                    foreach (ISensor sensor in item.Sensors)
                    {
                        if (sensor.SensorType == SensorType.Fan && sensor.Value is float rpm && rpm >= 1f && rpm < 20000f)
                        {
                            // The header's PWM control has the same index as its tachometer on Super I/O chips.
                            ISensor? control = item.Sensors.FirstOrDefault(s => s.SensorType == SensorType.Control && s.Index == sensor.Index);
                            boardFans.Add((item, sensor.Name, rpm, Duty(control)));
                        }
                    }
                }
            }
            // Boards LibreHardwareMonitor does not know report "Fan #1", "Fan #2"...: fall back on the reference
            // wiring of the main Super I/O chip (the first one; secondary chips only drive extra headers), or on
            // the only fan spinning on it (a desktop always has its CPU fan connected).
            IHardware? mainChip = boardFans.Select(f => f.item).FirstOrDefault(h => h.HardwareType == HardwareType.SuperIO);
            int mainChipFans = boardFans.Count(f => f.item == mainChip);
            foreach (var (item, name, rpm, duty) in boardFans)
            {
                Role role = item.HardwareType == HardwareType.Psu ? Role.Psu : FanRoles.FromName(name);
                if (role == Role.Unknown && item == mainChip) role = FanRoles.FromChip(item.Name, name, mainChipFans);
                readings.Add(new Reading(Kind.Fan, Group.Board, name, rpm, role, duty));
            }
            if (!float.IsNaN(cpu)) readings.Insert(0, new Reading(Kind.Temperature, Group.Cpu, "CPU", cpu));
            return (cpu, readings);
        }

        private static float Duty(ISensor? control) =>
            control?.Value is float v && v >= 0f && v <= 100f ? v : float.NaN;

        private static bool IsGpu(HardwareType type) =>
            type == HardwareType.GpuAmd || type == HardwareType.GpuNvidia || type == HardwareType.GpuIntel;

        // Prefers the package/die temperature, falling back to the hottest core.
        private static float? CpuTemperature(IHardware cpu)
        {
            string[] preferred = { "CPU Package", "Core (Tctl/Tdie)", "Core (Tdie)", "Core (Tctl)", "Core Average" };
            var temps = cpu.Sensors.Where(s => s.SensorType == SensorType.Temperature && s.Value is float v && Plausible(v))
                .ToList();
            foreach (string name in preferred)
            {
                ISensor? match = temps.FirstOrDefault(s => s.Name == name);
                if (match?.Value is float v) return v;
            }
            return temps.Count > 0 ? temps.Max(s => s.Value!.Value) : (float?)null;
        }

        private static bool Plausible(float celsius) => celsius > 0f && celsius < 150f;

        private static IEnumerable<IHardware> Flatten(IHardware hardware)
        {
            yield return hardware;
            foreach (IHardware sub in hardware.SubHardware)
            {
                foreach (IHardware nested in Flatten(sub)) yield return nested;
            }
        }

        private static void Update(IHardware hardware)
        {
            hardware.Update();
            foreach (IHardware sub in hardware.SubHardware) Update(sub);
        }

        public void Dispose() => _computer.Close();
    }
}

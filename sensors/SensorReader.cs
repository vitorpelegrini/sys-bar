using System;
using System.Collections.Generic;
using System.Linq;
using LibreHardwareMonitor.Hardware;

namespace SysBar.Sensors
{
    /// <summary>Reads CPU temperature, fans and drive temperatures through LibreHardwareMonitorLib.</summary>
    internal sealed class SensorReader : IDisposable
    {
        private static readonly TimeSpan StorageInterval = TimeSpan.FromSeconds(10);  // SMART queries are slow
        private readonly Computer _computer;
        private DateTime _lastStorageUpdate = DateTime.MinValue;

        public SensorReader()
        {
            // GPUs are read natively by sys-bar (D3DKMT); memory/network/battery are not needed.
            _computer = new Computer
            {
                IsCpuEnabled = true,
                IsMotherboardEnabled = true,
                IsControllerEnabled = true,
                IsStorageEnabled = true,
                IsPsuEnabled = true,
                IsGpuEnabled = false,
                IsMemoryEnabled = false,
                IsNetworkEnabled = false,
                IsBatteryEnabled = false,
            };
            _computer.Open();
        }

        public (float cpuTemperature, List<Reading> readings) Read()
        {
            var readings = new List<Reading>();
            float cpu = float.NaN;
            DateTime now = DateTime.UtcNow;
            bool refreshStorage = now - _lastStorageUpdate >= StorageInterval;
            if (refreshStorage) _lastStorageUpdate = now;
            foreach (IHardware hardware in _computer.Hardware)
            {
                // Drive temperatures change slowly and cost most of the time: refresh them less often.
                if (hardware.HardwareType != HardwareType.Storage || refreshStorage) Update(hardware);
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
                // Fans: motherboard Super I/O chips, embedded controllers, fan controllers, PSUs.
                foreach (IHardware item in Flatten(hardware))
                {
                    foreach (ISensor sensor in item.Sensors)
                    {
                        if (sensor.SensorType == SensorType.Fan && sensor.Value is float rpm && rpm >= 1f && rpm < 20000f)
                        {
                            readings.Add(new Reading(Kind.Fan, Group.Board, sensor.Name, rpm));
                        }
                    }
                }
            }
            if (!float.IsNaN(cpu)) readings.Insert(0, new Reading(Kind.Temperature, Group.Cpu, "CPU", cpu));
            return (cpu, readings);
        }

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

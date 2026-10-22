# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.2] - 2026-10-22

### Fixed

- The CPU temperature and board fans could go missing until the sensors service was restarted: when PawnIO
  was uninstalled (by its own uninstaller or another program's), its driver stayed registered, so the
  installer took it as present and skipped it, and a service started while PawnIO was missing never looked
  again. PawnIO now counts as installed only when its library is in place, and the service opens the sensors
  again as soon as PawnIO comes back.

## [1.0.1] - 2026-10-20

### Changed

- About half the CPU time, same pixels and same numbers. Measured side by side against 1.0.0 on a 36-thread
  desktop, card closed: sys-bar 1.07% → 0.55% of one core and its GPU use 0.105% → 0.012%; the sensors
  service 0.68% → 0.27% of one core, with 69% fewer hardware queries.
  - Animation frames redraw only the bars that moved, straight into the bitmap (about 5 µs instead of 1 ms),
    and hand Windows only the changed rectangle. The bars are drawn by a small rasterizer matched to
    Direct2D's: 99.8% of the pixels within one level, the rest on the antialiased round ends.
  - The network list is read in full every 5 s or when Windows reports an interface change; in between only
    the adapters in use are queried, instead of the whole table (27 rows here) every second.
  - The sensors service refreshes motherboard and GPU fans, which are only shown in the card, every 15 s
    while the card is closed. When the mouse reaches the strip, sys-bar tells the service, which refreshes
    them at once (26 ms here, before the card appears) and every second while the card stays open. The CPU
    temperature on the strip is still read every second. An older sys-bar keeps the previous behavior.

## [1.0.0] - 2026-10-20

First public release.

### Added

#### In the taskbar

- A strip of live metrics embedded in the Windows taskbar, next to the clock, with thin animated bars colored
  by load (amber above 80%, red above 92%). On classic taskbars the task buttons move aside to make room.
- A strip on every taskbar: other monitors too, on Windows 10 (classic) and Windows 11, each with its own
  size, DPI and hover card; monitors coming and going are followed live ("Show on all taskbars" in the menu).
  On Windows 11, where the clock has no window of its own, its position is measured through UI Automation.
- Works with vertical taskbars, small taskbar buttons, several rows, the Windows 11 classic taskbar restored
  by ExplorerPatcher or StartAllBack, per-monitor DPI, light, dark and high-contrast themes.

#### Metrics

- CPU, RAM, GPU and GPU memory (VRAM) with the same numbers as Task Manager: `% Processor Utility`, RAM in
  use, the busiest engine of any physical GPU, dedicated video memory.
- Disks: Task Manager's active time, with read/write throughput. Each drive gets its own column and card
  tile, labeled with its type and first letter (`NVMe C:`, `SSD D:`, `HDD E:`, `USB F:`, detected without
  administrator rights), or all combined; five or more drives are always combined.
- Network (opt-in, shown before the drives): download and upload of the physical adapters (no loopback,
  tunnels or virtual switches), link utilization in the strip and a self-scaling graph in the card.
- Battery on laptops: charge, charging state, power in watts and time left; amber at 20% and red at 10%
  when unplugged.

#### Temperatures and fans

- Without drivers or administrator rights: GPU temperature and fan (WDDM 2.7+) and NVMe/SSD temperatures,
  as badges in the strip and in the card. Drive temperatures are only read while a drive is busy, so
  sleeping hard drives are never woken up.
- Optional advanced sensors (x64): CPU temperature, motherboard and controller fans and SATA drive
  temperatures, through a small LocalSystem service built on LibreHardwareMonitorLib and the PawnIO driver.
  Enabled from the installer or the menu (one UAC prompt), it runs from Program Files, publishes read-only
  data and only samples while sys-bar is showing it.
- Thermometers in the card: a snowflake and a flame at the ends, marks every 10 °C with the warm and hot
  thresholds, and a liquid column that stays neutral while the device is comfortable and warms to yellow,
  amber and red.
- Fans matched to what they cool (CPU, GPU, case, pump, power supply) from the board's labels or, on boards
  with generic "Fan #N" names, the Super I/O reference wiring; a "Fans" menu corrects any of them. Each fan
  shows its speed and how hard it is driven (PWM duty, or the GPU driver's maximum speed); a GPU fan in
  zero-RPM mode shows as stopped.
- Celsius or Fahrenheit: automatic from the Windows region (Fahrenheit in the U.S.), or chosen in the menu.

#### Hover card

- Hardware names, peaks and a one-minute graph per metric, in tiles; history above 80% / 92% load is drawn
  in amber / red. Neutral inline icons for the details, compact fan lines, and a footer with the version and
  the update status.
- Always fits the screen and never cuts a word: drives sit side by side, long texts wrap, and on short screens
  the card widens, then shortens its graphs. Its size never follows live values, so it never jumps.

#### Everything else

- Context menu: metrics, disks, temperature unit, fans, update interval (0.5–5 s), language, updates,
  advanced sensors, high-load highlight, click action, all taskbars, start with Windows.
- 14 languages, following the Windows display language by default: English, Português, Español, Français,
  Deutsch, Italiano, Polski, Türkçe, Русский, Українська, 日本語, 한국어, 简体中文 and 繁體中文.
- Recovers when Explorer restarts, the GPU driver resets or displays change; pauses while the display is off
  or the session is locked; gives the taskbar its space back on exit, even after a crash or a forced kill.
- Installer (per-user, no administrator rights, x64 and ARM64) and portable zips, with SHA-256 checksums.
- Automatic updates from GitHub Releases: installed copies download the new installer, verify it and install
  it silently at a quiet moment; portable copies show a notice. The sensors service updates itself from the
  same release, without UAC prompts.
- `sys-bar.exe --quit` to close a running instance cleanly; `SYSBAR_LOG` for diagnostic logs.

### Security

- Updates: the installer is checked against the SHA-256 published with the release, kept locked from the
  check until it runs, in a new private folder; once releases are code-signed it must also carry the same
  signature as the running copy. Only per-user installations update themselves.
- The only elevated step (installing the advanced sensors) trusts nothing from the per-user folder it starts
  from: every library must match a SHA-256 manifest built into the executable, and the PawnIO installer a
  pinned hash. A refused install leaves the existing service running. Silent updates never ask for elevation,
  and removal runs the copy in Program Files.
- The sensors service refuses named objects created by other accounts, updates itself all-or-nothing with
  rollback, and backs off for a day after a failed update.
- No telemetry: the only network traffic is the update check, which can be turned off.

[Unreleased]: https://github.com/vitorpelegrini/sys-bar/compare/v1.0.2...HEAD
[1.0.2]: https://github.com/vitorpelegrini/sys-bar/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/vitorpelegrini/sys-bar/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/vitorpelegrini/sys-bar/releases/tag/v1.0.0

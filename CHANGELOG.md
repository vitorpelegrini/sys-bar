# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-10-20

First public release.

### Added

- CPU, RAM, GPU and GPU memory (VRAM) usage embedded in the Windows taskbar, next to the clock,
  with thin animated bars.
- Numbers that match Task Manager: CPU `% Processor Utility`, RAM in use, busiest GPU engine
  across all physical GPUs, dedicated video memory.
- Disk activity (Task Manager's active time) in the strip, with read/write throughput in MB/s in the card.
  Each physical drive gets its own column and card row, labeled with its type and first letter (`NVMe C:`,
  `SSD D:`, `HDD E:`, `USB F:`, detected without administrator rights); the menu can combine them into one
  column. More than four drives are always combined.
- Temperatures in Celsius or Fahrenheit: automatic from the Windows region (Fahrenheit in the U.S.), or
  chosen in the menu.
- The card's "Sensors" section is a grid of tiles, one per device, each with a temperature gauge and the fans
  that cool it. Fans are matched to the CPU, GPU, case, pump or power supply from the board's labels or, on
  boards with generic "Fan #N" names, from the Super I/O reference wiring; a GPU fan in zero-RPM mode shows
  as stopped. A "Fans" menu corrects the role of any motherboard fan.
- Network: download/upload of the physical adapters (no loopback, tunnels or virtual switches), link
  utilization in the strip and a self-scaling graph in the card. Off by default, enabled from the menu.
- Battery (laptops only): charge, charging state, power in watts and time left; amber at 20% and red at 10%
  when unplugged.
- A strip on every taskbar: other monitors too (Windows 10 classic and Windows 11 layouts), each with its own
  size, DPI and hover card; monitors coming and going are followed live. "Show on all taskbars" in the menu.
  On Windows 11, where the clock has no window of its own, its position is measured through UI Automation.
- Icons in the card's details (processor, memory, GPU, drive, read/write, network, peak, battery) and a footer
  with the version and the update status (checking, up to date, new version available).
- The card always fits the screen: drives are shown side by side, and on short screens it widens, then
  shortens its graphs, instead of running off the screen. Long texts wrap instead of being cut.
- Temperatures without drivers or administrator rights: GPU temperature and fan speed (WDDM 2.7+), NVMe/SSD
  temperatures; shown as badges in the strip and in a "Sensors" section of the card. Disk temperatures are
  only read while a drive is busy, so sleeping hard drives are never woken up.
- Hover card with the hardware model, peak values and a 60-sample history graph per metric;
  history above 80% / 92% load is drawn in amber / red.
- Context menu: choose metrics, update interval (0.5–5 s), language, high-load highlight,
  open Task Manager on click, start with Windows.
- 14 languages, following the Windows display language by default: English, Português,
  Español, Français, Deutsch, Italiano, Polski, Türkçe, Русский, Українська, 日本語, 한국어,
  简体中文 and 繁體中文.
- Windows 10 and Windows 11 support, including the Windows 11 classic taskbar restored by
  tools such as ExplorerPatcher or StartAllBack; vertical taskbars and small taskbar buttons on
  Windows 10; per-monitor DPI; light, dark and high-contrast themes.
- Recovers automatically when Explorer restarts, the GPU driver resets or the display layout
  changes; pauses sampling while the display is off or the session is locked.
- Installer (per-user, no administrator rights, x64 and ARM64) and portable zips.
- Hardened elevation: the one elevated step (installing the advanced sensors) takes nothing on trust from the
  per-user folder it starts from: every library must match a SHA-256 manifest built into the executable, and
  the PawnIO installer a pinned hash. Silent updates never ask for elevation; removal runs the copy in Program
  Files. Once releases are code-signed, updates must carry the same signature.
- Optional advanced sensors (x64): CPU temperature, motherboard/controller fans and SATA drive temperatures
  through a small LocalSystem service built on LibreHardwareMonitorLib and the PawnIO driver. It is enabled
  from the installer or the menu (one UAC prompt), runs from Program Files, publishes through read-only shared
  memory and only samples while sys-bar is showing data. It keeps itself up to date without UAC prompts:
  when sys-bar has updated, the service downloads its own package from the release, verifies it and restarts.
- Automatic updates: a daily check of GitHub Releases; installed copies download the new installer,
  verify its SHA-256 and install it silently, while portable copies show a notice. The version and an
  "Updates" submenu (check now, install automatically) are in the context menu.
- `sys-bar.exe --quit` to close a running instance cleanly; `SYSBAR_LOG` environment variable
  for diagnostic logs.

[Unreleased]: https://github.com/vitorpelegrini/sys-bar/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/vitorpelegrini/sys-bar/releases/tag/v1.0.0

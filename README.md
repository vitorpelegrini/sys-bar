<p align="center">
  <img src="docs/images/sys-bar-banner.png" alt="sys-bar: your system at a glance, right in the Windows taskbar" width="100%">
</p>

<p align="center">
  <b>CPU, RAM, GPU and VRAM usage, right next to the clock.</b><br>
  Native, tiny, and with the same numbers as Task Manager.
</p>

<p align="center">
  <a href="https://github.com/vitorpelegrini/sys-bar/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/vitorpelegrini/sys-bar/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/vitorpelegrini/sys-bar/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/vitorpelegrini/sys-bar?sort=semver"></a>
  <a href="https://github.com/vitorpelegrini/sys-bar/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/vitorpelegrini/sys-bar/total"></a>
  <img alt="Windows 10 | 11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4">
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-green"></a>
</p>

<p align="center"><b>English</b> · <a href="README.pt-BR.md">Português</a></p>

---

<p align="center">
  <img src="docs/images/strip-dark.png" alt="The sys-bar strip in a dark taskbar" width="520">
</p>

## Features

- **Lives inside the taskbar.** It is part of the taskbar itself, not a floating window: it never
  hides behind it, follows auto-hide and stays out of full-screen apps. On Windows 10 the task
  buttons move aside to make room, so nothing is ever covered.
- **The same numbers as Task Manager.** CPU uses `% Processor Utility`. RAM is total minus
  available. GPU is the busiest engine of any GPU. VRAM is dedicated video memory.
- **Disk and temperatures.** Disk activity with read/write MB/s. Each drive gets its own column, labeled with
  its type and letter (`NVMe C:`, `SSD D:`, `HDD E:`); the menu can combine them into one. Temperatures appear
  as small badges in the strip, and the card has a sensors section. Celsius or Fahrenheit: automatic from the
  Windows region, or chosen in the menu.
  - GPU temperature and fan, and NVMe temperatures, need no driver.
  - The optional **advanced sensors** add CPU temperature, motherboard and controller fans, and SATA drive
    temperatures (see below). Each fan is matched to what it cools (CPU, GPU, case, pump, power supply), and a
    GPU fan resting in zero-RPM mode shows as stopped.
- **Updates itself.** A daily check of GitHub Releases. Updates are verified by SHA-256 and installed silently.
- **Hover card.** Shows the hardware model, peaks and a one-minute graph per metric. History above
  80% / 92% load turns amber / red.
- **Light on resources.** It is a single ~500 KB native `.exe` (C++20, Win32, Direct2D) with no
  dependencies.
  - About 0.5% of one CPU core and 12 MB of RAM.
  - Software rendering, so it never loads the GPU it measures.
  - Pauses while the screen is off.
- **Feels native.**
  - Segoe UI, tabular figures (numbers never jitter) and smooth bar animations.
  - Light, dark and high-contrast themes, per-monitor DPI.
- **In your language.** English, Português, Español, Français, Deutsch, Italiano, Polski, Türkçe,
  Русский, Українська, 日本語, 한국어, 简体中文 and 繁體中文.
- **Robust.**
  - Survives Explorer restarts, driver resets, sleep, DPI and display changes.
  - Works with vertical taskbars and small taskbar buttons.
  - Works with classic taskbars restored on Windows 11 (ExplorerPatcher, StartAllBack).

<p align="center">
  <img src="docs/images/card-dark.png" alt="Hover card, dark theme" width="49%">
  <img src="docs/images/card-light.png" alt="Hover card, light theme" width="49%">
</p>

## Install

Download the latest version from [**Releases**](https://github.com/vitorpelegrini/sys-bar/releases/latest):

| File | What it is |
| --- | --- |
| `sys-bar-x.y.z-setup.exe` | Installer for x64 and ARM64. Per-user, no administrator rights; optional "start with Windows". |
| `sys-bar-x.y.z-win-x64.zip` | Portable version. Unzip anywhere and run `sys-bar.exe`. |
| `sys-bar-x.y.z-win-arm64.zip` | Portable version for Windows on ARM (Snapdragon). |

Each file comes with a `.sha256` checksum. The binaries are not code-signed yet, so Windows
SmartScreen may show "Windows protected your PC" the first time. Click **More info → Run anyway**.

Requires Windows 10 1809 or later, or Windows 11.

## Usage

| Action | Result |
| --- | --- |
| Hover | Details card with history graphs |
| Click | Opens Task Manager (can be turned off) |
| Right-click | Options: metrics, update interval, language, high-load highlight, click behavior, start with Windows, exit |

### Settings file

Every option in the menu is saved to `%APPDATA%\sys-bar\settings.ini`. The file also has a few
extra keys:

| Key | Default | Meaning |
| --- | --- | --- |
| `IntervalMs` | `1000` | Update interval in milliseconds (250–10000). |
| `OffsetDip` | `0` | Windows 11: extra space between the strip and the notification area. Increase it if the strip covers something, e.g. widgets. |
| `Language` | `auto` | `auto` or a code: `en`, `pt`, `es`, `fr`, `de`, `it`, `pl`, `tr`, `ru`, `uk`, `ja`, `ko`, `zh-Hans`, `zh-Hant`. |
| `CheckUpdates` | `1` | Look for a new release once a day. |
| `AutoUpdate` | `1` | Download, verify (SHA-256) and install updates silently (installed copies only). |

## Advanced sensors

Windows has no public API for CPU temperature or motherboard fans: reading them needs a kernel driver.
sys-bar can add an optional component for this:

- A small `sys-bar sensors` service, built on [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor),
  which supports a very wide range of hardware.
- The [PawnIO](https://github.com/namazso/PawnIO) driver. It is signed and open source, and FanControl and
  LibreHardwareMonitor use it too.

You can enable it in the installer, or later from the menu (**Enable advanced sensors**). It asks once for
administrator rights. The service runs from `Program Files`, uses almost no CPU, and only reads sensors
while sys-bar is showing them. Disabling it from the menu removes it. It is available on x64 only.

## FAQ

**Do the numbers really match Task Manager?**
Yes. sys-bar reads the same performance counters Task Manager uses, with the same aggregation.
RAM and VRAM are shown in GiB, labeled "GB" like Task Manager does.

**Laptop with two GPUs?**
The GPU % is the busiest GPU, so work on either the integrated or the dedicated one shows up. VRAM
is the dedicated memory of the main GPU. On integrated-only systems VRAM is hidden, because they
have no real dedicated memory.

**How do updates work?**
Once a day sys-bar checks GitHub Releases. When there is a new version:
- An installed copy downloads the installer and checks its SHA-256, then installs it silently at a quiet
  moment. The strip disappears for a second and comes back updated.
- A portable copy shows a notice with a download link.

The advanced sensors service follows by itself. It downloads its own verified package from the same release,
with no UAC prompt.

You can check manually or turn this off under **Updates** in the menu.

**Multiple monitors?**
sys-bar shows on the taskbar of the primary monitor.

**How do I uninstall it?**
Use Windows Settings → Apps. For the portable version, choose **Exit** from the menu and delete
the folder.

**Something is wrong. How do I get a log?**
Exit sys-bar and start it with `SYSBAR_LOG` pointing to a file. Then attach that file to a
[bug report](https://github.com/vitorpelegrini/sys-bar/issues/new/choose):

```powershell
$env:SYSBAR_LOG = "$env:TEMP\sys-bar.log"; & "$env:LOCALAPPDATA\Programs\sys-bar\sys-bar.exe"
```

## How it works

- **Sampling thread.** It reads PDH counters (by their English names, so any Windows language
  works), DXGI and `GlobalMemoryStatusEx`. It only posts to the UI thread, which never waits.
- **The strip.** It is a layered child window of the taskbar (`Shell_TrayWnd`).
  - On classic taskbars it sits in the ReBar, after the task list, which is shrunk to make room
    while respecting other toolbars.
  - On Windows 11 it sits next to the notification area.
- **The pure parts are unit-tested** (66 tests): geometry, formatting, GPU aggregation and
  translations. The tests also render previews of every theme, scale and language.

Details are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Building

```powershell
pwsh ./build.ps1          # Visual Studio 2022 (C++ workload) + CMake
```

## Contributing

Issues, translations and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md)
and the [code of conduct](CODE_OF_CONDUCT.md). The project follows
[Semantic Versioning](https://semver.org/); changes are listed in [CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE) © 2026 Vitor Pelegrini de Souza

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
- `sys-bar.exe --quit` to close a running instance cleanly; `SYSBAR_LOG` environment variable
  for diagnostic logs.

[Unreleased]: https://github.com/vitorpelegrini/sys-bar/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/vitorpelegrini/sys-bar/releases/tag/v1.0.0

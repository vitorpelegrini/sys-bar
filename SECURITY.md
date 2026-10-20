# Security policy

## Supported versions

Only the latest release receives fixes.

## Reporting a vulnerability

Please **do not** open a public issue. Report it privately through
[GitHub security advisories](https://github.com/vitorpelegrini/sys-bar/security/advisories/new).
You should get an answer within a few days.

## Scope and design notes

- sys-bar runs with normal user rights (`asInvoker`) and never asks for elevation; the installer is
  per-user.
- **Network:** the only connection is the update check. Once a day it sends an HTTPS GET to
  `api.github.com/repos/vitorpelegrini/sys-bar/releases/latest`. It sends no identifiers, only a
  `sys-bar/x.y.z` user agent. It can be turned off in the menu (Updates) or with `CheckUpdates=0`.
- **Self-update:**
  - Only installed copies update themselves, and only to stable releases.
  - The installer is downloaded over HTTPS (HTTPS-only redirects) into `%TEMP%\sys-bar-update`.
  - It must match the SHA-256 published with the release, or it is deleted without running.
  - It runs with the same user rights as sys-bar, with no elevation.
  - Portable copies only show a notice.
- It reads performance counters, `GlobalMemoryStatusEx` and the DXGI adapter list. It writes only
  `%APPDATA%\sys-bar\settings.ini` and, when you enable it, the
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\sys-bar` value.
- Release binaries are built from tagged sources by GitHub Actions; every artifact is published with
  its SHA-256 checksum.

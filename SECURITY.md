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
- It does not use the network. "About sys-bar" only opens the project page in your browser.
- It reads performance counters, `GlobalMemoryStatusEx` and the DXGI adapter list. It writes only
  `%APPDATA%\sys-bar\settings.ini` and, when you enable it, the
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\sys-bar` value.
- Release binaries are built from tagged sources by GitHub Actions; every artifact is published with
  its SHA-256 checksum.

# Security policy

## Supported versions

Only the latest release receives fixes.

## Reporting a vulnerability

Please **do not** open a public issue. Report it privately through
[GitHub security advisories](https://github.com/vitorpelegrini/sys-bar/security/advisories/new).
You should get an answer within a few days.

## Scope and design notes

### The app

- sys-bar runs with normal user rights (`asInvoker`) and never asks for elevation; the installer is per-user.
- It reads performance counters, `GlobalMemoryStatusEx`, DXGI/D3DKMT adapter data, storage, network and
  battery information. It writes only `%APPDATA%\sys-bar\settings.ini` and, when you enable it, the
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\sys-bar` value.
- **Network:** the only connection is the update check. Once a day it sends an HTTPS GET to
  `api.github.com/repos/vitorpelegrini/sys-bar/releases/latest` with a `sys-bar/x.y.z` user agent and no
  identifiers. It can be turned off in the menu (Updates) or with `CheckUpdates=0`. There is no telemetry.

### Self-update

- Only per-user installations update themselves, and only to stable releases; portable copies and
  installations for all users show a notice instead.
- The installer is downloaded over HTTPS (HTTPS-only redirects, size limits) into a new folder with a random
  name under `%TEMP%`.
- It must match the SHA-256 published with the release. From that check until it starts, the file stays
  open with a sharing mode that lets nobody write, rename or delete it.
- Once releases are code-signed, the installer must also carry a valid Authenticode signature from the same
  signer as the running copy.
- It runs with the same user rights as sys-bar (`/CURRENTUSER`), with no elevation.

### Advanced sensors (optional, x64)

- Enabled only after an explicit choice (installer checkbox or menu) and a UAC prompt; silent updates never
  ask for elevation.
- The elevated `--install` starts from the per-user folder, so it takes nothing from it on trust:
  - every library must match a SHA-256 manifest embedded in the executable at build time, read through a
    handle that denies writers and renames; nothing else is copied;
  - the executable copies its own locked image;
  - the PawnIO installer must match a hash compiled into the executable, and runs from an administrators-only
    folder;
  - everything is checked before the running service is touched: a refused install leaves it as it was.
- The service runs as LocalSystem from `%ProgramFiles%\sys-bar\sensors`, where only administrators can write,
  and logs only there. Removal (menu or uninstaller) runs that installed copy, never the per-user one.
- Readings are published in a shared-memory block that users can read but not write. Clients can only signal
  two events ("someone is looking", "check for an update"); no data travels from clients to the service.
  Named events that already exist and are not owned by SYSTEM are refused (and retried), never trusted.
- **The service updates itself without UAC** from the latest stable release: it downloads
  `sys-bar-x.y.z-sensors-x64.zip`, copies it into an administrators-only staging folder before checking its
  SHA-256 and version, extracts only flat `.exe`/`.dll`/`.config` entries, and, once releases are code-signed,
  requires the same signer on the executable and every library to match that executable's manifest. The swap
  is all-or-nothing with rollback; a failed version is not retried for a day. It checks GitHub at most once
  every 10 minutes.
- PawnIO stays installed when the service is removed, because other tools share it.

### Releases

- Release binaries are built from tagged sources by GitHub Actions. The build job runs with a read-only token;
  a separate job publishes. Every artifact is published with its SHA-256 checksum.
- Code signing through SignPath Foundation is in progress; see the
  [code signing policy](README.md#code-signing-policy).

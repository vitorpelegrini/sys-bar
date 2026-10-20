# Contributing to sys-bar

Thanks for helping! Bug reports, translations, ideas and pull requests are all welcome.

## Development setup

- Windows 10 or 11
- [Visual Studio 2022](https://visualstudio.microsoft.com/) (or the Build Tools) with the
  **Desktop development with C++** workload
- [CMake](https://cmake.org/) 3.20+ and PowerShell 7 (`pwsh`)
- [.NET SDK 8+](https://dotnet.microsoft.com/download) to build the advanced sensors service
- Optional: [Inno Setup 6](https://jrsoftware.org/isdl.php) to build the installer

```powershell
pwsh ./build.ps1            # Release build (warnings are errors) + all tests
pwsh ./build.ps1 -Config Debug
pwsh tools/stop.ps1         # close a running sys-bar cleanly (restores the taskbar)
pwsh tools/package.ps1      # zips + installer in dist/, exactly like the release workflow
```

`build/Release/sys-bar.exe` is the app; `build/Release/sysbar-tests.exe` the test suite.

## Project layout

| Path | Responsibility |
| --- | --- |
| `src/app.*` | Taskbar integration (one strip per taskbar), window procedures, theme, animation, card, menu |
| `src/logic.*` | Pure geometry, visibility, fan-role and threshold decisions (unit-tested) |
| `src/metrics.*` | Sampling thread: PDH (English counter names), DXGI, disks, network, battery |
| `src/hardware.*` | Native sensors: GPU (D3DKMT), drives (storage IOCTLs), network (IP Helper), battery |
| `src/renderer.*` | Layout and drawing of the strip (Direct2D/DirectWrite) |
| `src/card.*` | Hover card: metric tiles, graphs, thermometers, fan lines |
| `src/gfx.*`, `src/icons.h` | Shared graphics: factories, layered bitmaps, palette, inline icon glyphs |
| `src/trayprobe.*` | Windows 11 clock position through UI Automation (off the UI thread) |
| `src/i18n.*` | User-interface strings in every language |
| `src/format.*` | Pure text formatting |
| `src/settings.*` | `settings.ini` and "Start with Windows" |
| `src/sensorfeed.*` | Reader of the advanced sensors service (shared memory) |
| `src/update.*`, `src/json.*`, `src/signature.*` | Self-update through GitHub Releases, Authenticode checks |
| `sensors/` | Advanced sensors service (C#, .NET Framework 4.7.2, LibreHardwareMonitorLib + PawnIO) |
| `tests/` | Test suite (tiny built-in framework, no dependencies), render previews, stand-in taskbars |
| `packaging/` | Inno Setup installer |
| `tools/` | Packaging, screenshots, banner, integration and soak-test scripts |

## Ground rules for code

- **Never block the UI thread.** The strip is a child of the Explorer taskbar and both share an
  input queue: a stall in sys-bar freezes the taskbar. Slow work (PDH, shell calls) runs on other
  threads.
- **Keep decisions pure and tested.** New geometry or visibility rules go into `logic.*` with tests
  in `tests/test_logic.cpp`; formatting goes into `format.*`.
- **No new runtime dependencies.** The app is a single self-contained `.exe`.
- Code, comments and commit messages are in English. User-facing text goes through `i18n`.
- Format with clang-format before committing (the CI checks it):

  ```powershell
  $cf = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-format.exe"
  & $cf -i --style=file (Get-ChildItem src, tests -Include *.cpp, *.h -Recurse).FullName
  ```

## Testing

`pwsh ./build.ps1` runs the whole suite. It also renders PNG previews of the strip and the card
(light/dark themes, 100–200% scaling, compact and vertical taskbars, every language) into
`build/previews`; please look at them, and attach them to pull requests that change visuals.

For changes to the taskbar integration, also run the multi-monitor integration test and the soak test:

```powershell
pwsh tools/test-taskbars.ps1                     # the real app against stand-in taskbars (classic and Windows 11)
pwsh tools/soak.ps1 -Minutes 3 -HoverCycles 20   # CPU, memory, handles and GDI objects over time
```

`test-taskbars.ps1` runs `sysbar-tests --fake-taskbar classic|modern`, which builds windows with the same
classes as Explorer's taskbars on other monitors, off screen, and checks embedding, Explorer resetting the
task list, unplugging and plugging a monitor back, and the Windows 11 placement.

Useful environment variables while testing: `SYSBAR_CONFIG_DIR` (a separate `settings.ini`), `SYSBAR_LOG` (a
log file), `SYSBAR_FAKE_BATTERY=percent,ac,charging[,watts[,secondsLeft]]` (a simulated laptop battery) and
`SYSBAR_FORCE_MODERN=1` (the Windows 11 path on Windows 10, for stand-in taskbars).

When a change is visible, refresh the README images:

```powershell
pwsh tools/update-screenshots.ps1   # strip and card images from build/previews
pwsh tools/update-banner.ps1        # the banner, with the real card over assets/banner-base.png
```

## Advanced sensors service

`sensors/` is a small Windows service. It reads CPU temperature, fans (speed, role and PWM duty) and SATA
temperatures with LibreHardwareMonitorLib and publishes them through a read-only shared-memory block whose
layout is documented in `sensors/SharedBlock.cs` and `src/sensorfeed.h`. The two must change together; the
version field protects older clients, and new per-entry fields use bytes that older services wrote as 0.

Its elevated steps must never trust the per-user folder they start from: `tools/package.ps1` embeds a
SHA-256 manifest of the shipped libraries in the executable (`sensors/Manifest.cs`), and the PawnIO hash is
pinned both in `tools/package.ps1` and `sensors/Installer.cs` (packaging fails if they differ).

```powershell
dotnet build sensors/SysBar.Sensors.csproj -c Release
sensors\bin\Release\sys-bar-sensors.exe --console 15   # elevated terminal: print readings for 15 s
```

## Translations

All strings live in `src/i18n.cpp`, one table per language, in the same order as `enum class Str`.
To add a language: add it to `enum class Lang`, `kInfo` and `kStrings`, keep the `{0}`/`{1}`
placeholders (a test checks them), run the tests and look at `build/previews/card-lang-*.png`.

## Commits and pull requests

- Use [Conventional Commits](https://www.conventionalcommits.org/): `feat: ...`, `fix: ...`,
  `docs: ...`, `refactor: ...`, `test: ...`, `ci: ...`, `chore: ...`.
- One topic per pull request; fill in the template.
- Add an entry under `## [Unreleased]` in `CHANGELOG.md` for every user-visible change.

## Versioning and releases

sys-bar follows [Semantic Versioning](https://semver.org/). The version lives only in
`project(... VERSION x.y.z)` in `CMakeLists.txt`. To release:

1. Bump the version in `CMakeLists.txt`.
2. Move the `[Unreleased]` notes in `CHANGELOG.md` to a new `## [x.y.z] - YYYY-MM-DD` section and
   update the links at the bottom.
3. Commit (`chore: release x.y.z`), tag `vx.y.z` and push the tag.

The [release workflow](.github/workflows/release.yml) then builds and tests with a read-only token, and a
separate job publishes the installer, the portable zips, the sensors update package and their SHA-256
checksums, using the changelog section as notes.

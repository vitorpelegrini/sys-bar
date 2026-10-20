# Builds sys-bar (warnings are errors) and runs the test suite.
# Usage: pwsh ./build.ps1 [-Config Debug] [-SkipTests]
param([string]$Config = 'Release', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if (-not (Test-Path 'assets/sys-bar.ico')) { & pwsh -NoProfile -File tools/make-icon.ps1 }

cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DSYSBAR_WARNINGS_AS_ERRORS=ON | Out-Host
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build build --config $Config -- /m /nologo /v:minimal | Out-Host
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if (-not $SkipTests) {
    Write-Host "`nTests:" -ForegroundColor Cyan
    & "build\$Config\sysbar-tests.exe" --previews "$PSScriptRoot\build\previews"
    if ($LASTEXITCODE) { Write-Host 'Tests failed.' -ForegroundColor Red; exit $LASTEXITCODE }
}

Write-Host "`nDone: build\$Config\sys-bar.exe" -ForegroundColor Green

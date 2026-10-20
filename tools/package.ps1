# Builds release artifacts into dist/: portable zips (x64, arm64) and the installer.
# The same script runs locally and in the release workflow.
# Usage: pwsh tools/package.ps1 [-Architectures x64,arm64] [-SkipTests]
param(
    [string[]]$Architectures = @('x64', 'arm64'),
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$version = & "$PSScriptRoot/version.ps1"
$dist = Join-Path $root 'dist'
if (Test-Path $dist) { Get-ChildItem $dist -Recurse | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null
Write-Host "sys-bar $version" -ForegroundColor Cyan

foreach ($arch in $Architectures) {
    $platform = if ($arch -eq 'arm64') { 'ARM64' } else { 'x64' }
    $buildDir = "build-$arch"
    # Tests only run on the native architecture of the build machine.
    $withTests = ($arch -eq 'x64') -and -not $SkipTests
    cmake -S . -B $buildDir -G 'Visual Studio 17 2022' -A $platform `
        -DSYSBAR_WARNINGS_AS_ERRORS=ON "-DSYSBAR_BUILD_TESTS=$(if ($withTests) { 'ON' } else { 'OFF' })" | Out-Host
    if ($LASTEXITCODE) { throw "configure failed ($arch)" }
    cmake --build $buildDir --config Release -- /m /nologo /v:minimal | Out-Host
    if ($LASTEXITCODE) { throw "build failed ($arch)" }
    if ($withTests) {
        & "$buildDir/Release/sysbar-tests.exe"
        if ($LASTEXITCODE) { throw 'tests failed' }
    }

    $stage = Join-Path $dist $arch
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item "$buildDir/Release/sys-bar.exe" $stage
    Copy-Item LICENSE, README.md, CHANGELOG.md $stage
    Compress-Archive -Path "$stage/*" -DestinationPath (Join-Path $dist "sys-bar-$version-win-$arch.zip") -Force
}

# Installer (Inno Setup 6). Includes every architecture that was built.
$iscc = @(
    (Get-Command iscc -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($iscc) {
    & $iscc /Qp "/DAppVersion=$version" "/DDistDir=$dist" packaging/installer.iss | Out-Host
    if ($LASTEXITCODE) { throw 'installer build failed' }
} else {
    Write-Warning 'Inno Setup 6 not found: skipping the installer (https://jrsoftware.org/isdl.php).'
}

# Checksums for everything that will be published.
Get-ChildItem $dist -File | Where-Object { $_.Extension -in '.zip', '.exe' } | ForEach-Object {
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)" | Set-Content "$($_.FullName).sha256" -NoNewline -Encoding ascii
}
Get-ChildItem $dist -File | Format-Table Name, @{ n = 'KB'; e = { [math]::Round($_.Length / 1KB) } } | Out-Host

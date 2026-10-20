# Refreshes the README screenshots from the deterministic previews rendered by the tests.
# Usage: pwsh ./build.ps1; pwsh tools/update-screenshots.ps1
$root = Split-Path -Parent $PSScriptRoot
$previews = Join-Path $root 'build\previews'
$images = Join-Path $root 'docs\images'
$map = @{
    'bar-dark-200.png'   = 'strip-dark.png'
    'bar-light-100.png'  = 'strip-light.png'
    'card-dark-150.png'  = 'card-dark.png'
    'card-light-150.png' = 'card-light.png'
}
foreach ($source in $map.Keys) {
    $path = Join-Path $previews $source
    if (-not (Test-Path $path)) { throw "missing $path - run pwsh ./build.ps1 first" }
    Copy-Item $path (Join-Path $images $map[$source]) -Force
    Write-Host "docs/images/$($map[$source])"
}

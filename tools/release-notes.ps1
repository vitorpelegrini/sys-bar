# Extracts the CHANGELOG.md section of a version (used as the GitHub release notes).
# Usage: pwsh tools/release-notes.ps1 -Version 1.0.0 [-Out notes.md]
param([Parameter(Mandatory)][string]$Version, [string]$Out)
$root = Split-Path -Parent $PSScriptRoot
$lines = Get-Content (Join-Path $root 'CHANGELOG.md') -Encoding utf8
$start = ($lines | Select-String -Pattern "^## \[$([regex]::Escape($Version))\]" | Select-Object -First 1).LineNumber
if (-not $start) { throw "CHANGELOG.md has no section for $Version" }
$body = New-Object System.Collections.Generic.List[string]
for ($i = $start; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^## \[' -or $lines[$i] -match '^\[[^\]]+\]: ') { break }
    $body.Add($lines[$i])
}
$notes = ($body -join "`n").Trim()
if (-not $notes) { throw "CHANGELOG.md section for $Version is empty" }
if ($Out) { Set-Content -Path $Out -Value $notes -Encoding utf8 } else { $notes }

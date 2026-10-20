# Prints the project version, read from project(... VERSION x.y.z) in CMakeLists.txt.
$root = Split-Path -Parent $PSScriptRoot
$match = Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(sys-bar VERSION (\d+\.\d+\.\d+)'
if (-not $match) { throw 'version not found in CMakeLists.txt' }
$match.Matches[0].Groups[1].Value

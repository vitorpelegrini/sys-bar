# Multi-monitor integration test: runs the real sys-bar against stand-in taskbars that have the same
# window structure as Explorer's taskbars on other monitors (classic Windows 10 and modern Windows 11),
# placed off screen. Checks that a strip is embedded in each, that the task list makes room and keeps it
# when "Explorer" resets it, that unplugging the monitor detaches cleanly, and that plugging it back in
# attaches again. The main taskbar must keep its strip throughout.
# Usage: pwsh ./build.ps1; pwsh tools/test-taskbars.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$app = Join-Path $root 'build\Release\sys-bar.exe'
$tests = Join-Path $root 'build\Release\sysbar-tests.exe'

Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class W {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

$script:failures = 0
function Check([bool]$condition, [string]$what) {
    if ($condition) { Write-Host "  ok    $what" -ForegroundColor Green }
    else { Write-Host "  FAIL  $what" -ForegroundColor Red; $script:failures++ }
}
function Rect([IntPtr]$h) { $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r); $r }
function FakeTray { [W]::FindWindowExW([IntPtr]::Zero, [IntPtr]::Zero, 'Shell_SecondaryTrayWnd', [NullString]::Value) }
function MainStrip {
    $tray = [W]::FindWindowExW([IntPtr]::Zero, [IntPtr]::Zero, 'Shell_TrayWnd', [NullString]::Value)
    $rebar = [W]::FindWindowExW($tray, [IntPtr]::Zero, 'ReBarWindow32', [NullString]::Value)
    $strip = [W]::FindWindowExW($rebar, [IntPtr]::Zero, 'SysBar.Bar', [NullString]::Value)
    if ($strip -eq [IntPtr]::Zero) { $strip = [W]::FindWindowExW($tray, [IntPtr]::Zero, 'SysBar.Bar', [NullString]::Value) }
    $strip
}
function StartFake([string]$kind) {
    $p = Start-Process $tests -ArgumentList '--fake-taskbar', $kind, '120' -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 50 -and (FakeTray) -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100 }
    $p
}
function StopFake($p) {
    $tray = FakeTray
    if ($tray -ne [IntPtr]::Zero) { [void][W]::PostMessageW($tray, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }  # WM_CLOSE
    if (-not $p.WaitForExit(5000)) { $p.Kill() }
}
function StartApp([hashtable]$env = @{}) {
    $config = Join-Path $env:TEMP "sysbar-taskbars-$PID"
    New-Item -ItemType Directory -Force $config | Out-Null
    $psi = New-Object System.Diagnostics.ProcessStartInfo $app
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables['SYSBAR_CONFIG_DIR'] = $config
    $psi.EnvironmentVariables['SYSBAR_LOG'] = Join-Path $config 'log.txt'
    foreach ($k in $env.Keys) { $psi.EnvironmentVariables[$k] = $env[$k] }
    [System.Diagnostics.Process]::Start($psi)
}

& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'stop.ps1') | Out-Null

Write-Host 'Classic taskbar on another monitor (Windows 10)' -ForegroundColor Cyan
$fake = StartFake 'classic'
$sysbar = StartApp
Start-Sleep 4
$tray = FakeTray
$worker = [W]::FindWindowExW($tray, [IntPtr]::Zero, 'WorkerW', [NullString]::Value)
$list = [W]::FindWindowExW($worker, [IntPtr]::Zero, 'MSTaskListWClass', [NullString]::Value)
$strip = [W]::FindWindowExW($worker, [IntPtr]::Zero, 'SysBar.Bar', [NullString]::Value)
Check ($strip -ne [IntPtr]::Zero -and [W]::IsWindowVisible($strip)) 'strip embedded in the secondary taskbar'
Check ((MainStrip) -ne [IntPtr]::Zero) 'main taskbar still has its strip'
$s = Rect $strip; $l = Rect $list; $w = Rect $worker
Check (($l.Right - $l.Left) -lt (1450 - 48)) "task list made room ($($l.Right - $l.Left) px)"
Check ($s.Left -ge $l.Right -and $s.Right -le $w.Right) "strip sits after the task list ($($s.Left)..$($s.Right), list ends $($l.Right))"
Check (($s.Bottom - $s.Top) -eq 40) 'strip is as tall as the taskbar'

Start-Sleep 4  # the fake Explorer resets the task list every 3 s
$l2 = Rect $list; $s2 = Rect $strip
Check ($s2.Left -ge $l2.Right) 'room is taken back after Explorer resets the task list'

StopFake $fake
Start-Sleep 3
Check (-not $sysbar.HasExited) 'sys-bar survives the monitor being unplugged'
Check ((MainStrip) -ne [IntPtr]::Zero) 'main strip unaffected'

$fake = StartFake 'classic'
Start-Sleep 4
$tray = FakeTray
$worker = [W]::FindWindowExW($tray, [IntPtr]::Zero, 'WorkerW', [NullString]::Value)
Check ([W]::FindWindowExW($worker, [IntPtr]::Zero, 'SysBar.Bar', [NullString]::Value) -ne [IntPtr]::Zero) 'strip comes back when the monitor is plugged in again'
$sysbar.Kill(); $sysbar.WaitForExit()
StopFake $fake

Write-Host 'Modern taskbar on another monitor (Windows 11 layout)' -ForegroundColor Cyan
$fake = StartFake 'modern'
$sysbar = StartApp @{ SYSBAR_FORCE_MODERN = '1' }
Start-Sleep 6  # the right-side controls are measured through UI Automation, off the UI thread
$tray = FakeTray
$strip = [W]::FindWindowExW($tray, [IntPtr]::Zero, 'SysBar.Bar', [NullString]::Value)
Check ($strip -ne [IntPtr]::Zero -and [W]::IsWindowVisible($strip)) 'strip embedded in the modern taskbar'
$t = Rect $tray; $s = Rect $strip
$clockLeft = $t.Left + 1480
Check ($s.Right -le $clockLeft -and $s.Right -ge $clockLeft - 12) "strip ends right before the clock (strip right $($s.Right - $t.Left), clock at 1480)"
Check ($s.Left -ge $t.Left + 900) 'strip does not cover the task buttons'
$sysbar.Kill(); $sysbar.WaitForExit()
StopFake $fake

Write-Host ''
Start-Process $app  # back to the user's normal instance
if ($script:failures) { Write-Host "$script:failures check(s) failed" -ForegroundColor Red; exit 1 }
Write-Host 'All taskbar checks passed' -ForegroundColor Green

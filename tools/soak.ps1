# Soak test: tracks sys-bar CPU, memory, handles and GDI/USER objects over time, optionally
# opening/closing the hover card repeatedly.
# Usage: pwsh tools/soak.ps1 [-Minutes 3] [-HoverCycles 40]
param([double]$Minutes = 3, [int]$HoverCycles = 40)

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Soak {
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    public static IntPtr FindBar() {
        IntPtr tray = FindWindowExW(IntPtr.Zero, IntPtr.Zero, "Shell_TrayWnd", null);
        IntPtr rebar = FindWindowExW(tray, IntPtr.Zero, "ReBarWindow32", null);
        IntPtr bar = rebar != IntPtr.Zero ? FindWindowExW(rebar, IntPtr.Zero, "SysBar.Bar", null) : IntPtr.Zero;
        return bar != IntPtr.Zero ? bar : FindWindowExW(tray, IntPtr.Zero, "SysBar.Bar", null);
    }
}
'@

$p = Get-Process sys-bar -ErrorAction SilentlyContinue
if (-not $p) { Write-Error 'sys-bar is not running.'; exit 1 }

function Measure-Now {
    $p.Refresh()
    [pscustomobject]@{
        CpuMs = [math]::Round($p.TotalProcessorTime.TotalMilliseconds)
        PrivateMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        Handles = $p.HandleCount
        Gdi = [Soak]::GetGuiResources($p.Handle, 0)
        User = [Soak]::GetGuiResources($p.Handle, 1)
    }
}

$start = Measure-Now
$t0 = Get-Date
Write-Host ("start: private {0} MB | handles {1} | GDI {2} | USER {3}" -f $start.PrivateMB, $start.Handles, $start.Gdi, $start.User)

$bar = [Soak]::FindBar()
$origin = New-Object Soak+POINT
[Soak]::GetCursorPos([ref]$origin) | Out-Null
if ($HoverCycles -gt 0 -and $bar -ne [IntPtr]::Zero) {
    $r = New-Object Soak+RECT
    [Soak]::GetWindowRect($bar, [ref]$r) | Out-Null
    $cx = [int](($r.Left + $r.Right) / 2); $cy = [int](($r.Top + $r.Bottom) / 2)
    for ($i = 0; $i -lt $HoverCycles; $i++) {
        [Soak]::SetCursorPos($cx, $cy) | Out-Null; Start-Sleep -Milliseconds 30
        [Soak]::SetCursorPos($cx + 1, $cy) | Out-Null; Start-Sleep -Milliseconds 900   # card shows and updates
        [Soak]::SetCursorPos($cx, $r.Top - 200) | Out-Null; Start-Sleep -Milliseconds 400  # card hides
    }
    [Soak]::SetCursorPos($origin.X, $origin.Y) | Out-Null
    Write-Host "$HoverCycles hover cycles done"
}

$remaining = $Minutes * 60 - ((Get-Date) - $t0).TotalSeconds
if ($remaining -gt 0) { Start-Sleep -Seconds $remaining }

$end = Measure-Now
$elapsed = ((Get-Date) - $t0).TotalSeconds
$cpuPct = ($end.CpuMs - $start.CpuMs) / ($elapsed * 1000) * 100
Write-Host ("end:   private {0} MB | handles {1} | GDI {2} | USER {3}" -f $end.PrivateMB, $end.Handles, $end.Gdi, $end.User)
Write-Host ("average CPU: {0:N2}% of one core over {1:N0} s" -f $cpuPct, $elapsed)

$leak = ($end.Gdi - $start.Gdi -gt 5) -or ($end.User - $start.User -gt 5) -or ($end.Handles - $start.Handles -gt 30) -or ($end.PrivateMB - $start.PrivateMB -gt 8)
if ($leak) { Write-Host 'POSSIBLE LEAK' -ForegroundColor Red; exit 1 } else { Write-Host 'No leaks.' -ForegroundColor Green }

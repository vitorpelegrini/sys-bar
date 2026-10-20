# Closes a running sys-bar cleanly (it restores the taskbar layout). Usage: pwsh tools/stop.ps1
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class SysBarWin {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$h = [SysBarWin]::FindWindowW('SysBar.Controller', 'sys-bar')
if ($h -eq [IntPtr]::Zero) { Write-Host 'sys-bar is not running.'; return }
[SysBarWin]::PostMessageW($h, 0x0010 <# WM_CLOSE #>, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
for ($i = 0; $i -lt 50 -and (Get-Process sys-bar -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 100 }
if (Get-Process sys-bar -ErrorAction SilentlyContinue) { Write-Warning 'sys-bar did not exit in time.' } else { Write-Host 'sys-bar closed.' }

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'This test needs a disposable GitHub Actions runner.' }
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class AquaWindowUi {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string type,string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string type,string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window,uint msg,IntPtr w,string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window,uint msg,IntPtr w,StringBuilder text);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window,uint msg,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetDlgItemText(IntPtr dialog, int id, string text);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog,int id);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window,ref Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags,int x,int y,uint data,UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);

}
'@
[void][AquaWindowUi]::SetProcessDPIAware()
$zero = [IntPtr]::Zero
function Window-For([string]$class, [string]$title, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        $handle = [AquaWindowUi]::FindWindow($class, $title)
        if ($handle -ne $zero) { return $handle }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $zero
}
function Position([IntPtr]$window) {
    $screen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if (![AquaWindowUi]::SetWindowPos($window, [IntPtr](-1), 20, 20,
        [Math]::Min(984, $screen.Width - 40), [Math]::Min(690,$screen.Height - 40), 64)) {
        throw 'Unable to position Aqua Frame.'
    }
}
function Open-Address([IntPtr]$window, [string]$url) {
    $edit = [AquaWindowUi]::FindWindowEx($window, $zero, 'Edit', $null)
    if ($edit -eq $zero -or [AquaWindowUi]::SendMessage($edit, 0x000c, $zero, $url) -eq $zero -or
        ![AquaWindowUi]::PostMessage($edit, 0x0100, [IntPtr]13, $zero)) {
        throw "Unable to navigate to $url."
    }
    Start-Sleep -Seconds 2
}
function Get-Address([IntPtr]$window) {
    $edit = [AquaWindowUi]::FindWindowEx($window, $zero, 'Edit', $null)
    $address = [Text.StringBuilder]::new(8193)
    [void][AquaWindowUi]::SendMessage($edit, 0x000d, [IntPtr]$address.Capacity, $address)
    $address.ToString()
}
function Screenshot([IntPtr]$window, [string]$name) {
    $r = New-Object AquaWindowUi+Rect
    if (![AquaWindowUi]::GetWindowRect($window, [ref]$r)) { return }
    $bmp = [Drawing.Bitmap]::new(($r.Right - $r.Left), ($r.Bottom - $r.Top))
    $graphics = [Drawing.Graphics]::FromImage($bmp)
    try {
        $dc = $graphics.GetHdc()
        try { $printed = [AquaWindowUi]::PrintWindow($window, $dc, 2) }
        finally { $graphics.ReleaseHdc($dc) }
        if ($printed) { $bmp.Save((Join-Path $PWD $name), [Drawing.Imaging.ImageFormat]::Png) }
    } finally {
        $graphics.Dispose()
        $bmp.Dispose()
    }
}
$exe = Join-Path $PWD 'runtime/AquaFrame.exe'
$process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
try {
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 30
    if ($window -eq $zero) { throw 'Aqua Frame did not open.' }
    Position $window
    [void][AquaWindowUi]::SetForegroundWindow($window)
    Start-Sleep -Seconds 3
    Open-Address $window 'aqua://settings/#performance'
    Start-Sleep -Seconds 3
    Screenshot $window 'AquaFrame-Performance-Settings.png'
} finally {
    if (!$process.HasExited) {
        $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 1
        if ($window -ne $zero) { [void][AquaWindowUi]::PostMessage($window, 0x0010, $zero, $zero) }
        if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
    }
}

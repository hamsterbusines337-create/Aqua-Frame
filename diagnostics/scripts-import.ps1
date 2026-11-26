$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'This test needs a disposable GitHub Actions runner.' }
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class AquaScriptUi {
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
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);

}
'@
[void][AquaScriptUi]::SetProcessDPIAware()
$zero = [IntPtr]::Zero
function Window-For([string]$class, [string]$title, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        $handle = [AquaScriptUi]::FindWindow($class, $title)
        if ($handle -ne $zero) { return $handle }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $zero
}
function Position([IntPtr]$window) {
    $screen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if (![AquaScriptUi]::SetWindowPos($window, [IntPtr](-1), 20, 20,
        [Math]::Min(984, $screen.Width - 40), [Math]::Min(690,$screen.Height - 40), 64)) {
        throw 'Unable to position Aqua Frame.'
    }
}
function Open-Address([IntPtr]$window, [string]$url) {
    $edit = [AquaScriptUi]::FindWindowEx($window, $zero, 'Edit', $null)
    if ($edit -eq $zero -or [AquaScriptUi]::SendMessage($edit, 0x000c, $zero, $url) -eq $zero -or
        ![AquaScriptUi]::PostMessage($edit, 0x0100, [IntPtr]13, $zero)) {
        throw "Unable to navigate to $url."
    }
    Start-Sleep -Seconds 2
}
function Get-Address([IntPtr]$window) {
    $edit = [AquaScriptUi]::FindWindowEx($window, $zero, 'Edit', $null)
    $address = [Text.StringBuilder]::new(8193)
    [void][AquaScriptUi]::SendMessage($edit, 0x000d, [IntPtr]$address.Capacity, $address)
    $address.ToString()
}
function Screenshot([IntPtr]$window, [string]$name) {
    $r = New-Object AquaScriptUi+Rect
    if (![AquaScriptUi]::GetWindowRect($window, [ref]$r)) { return }
    $bmp = [Drawing.Bitmap]::new(($r.Right - $r.Left), ($r.Bottom - $r.Top))
    $graphics = [Drawing.Graphics]::FromImage($bmp)
    try {
        $dc = $graphics.GetHdc()
        try { $printed = [AquaScriptUi]::PrintWindow($window, $dc, 2) }
        finally { $graphics.ReleaseHdc($dc) }
        if ($printed) { $bmp.Save((Join-Path $PWD $name), [Drawing.Imaging.ImageFormat]::Png) }
    } finally {
        $graphics.Dispose()
        $bmp.Dispose()
    }
}
function Click-At([IntPtr]$window, [int]$x, [int]$y) {
    $scale = [AquaScriptUi]::GetDpiForWindow($window) / 96.0
    $point = New-Object AquaScriptUi+Point
    $point.X = [int]($x * $scale)
    $point.Y = [int]($y * $scale)
    if (![AquaScriptUi]::ClientToScreen($window, [ref]$point) -or
        ![AquaScriptUi]::SetCursorPos($point.X, $point.Y)) {
        throw "Unable to locate the settings button."
    }
    [AquaScriptUi]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [AquaScriptUi]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}
function Import-Script([IntPtr]$window, [string]$path, [string]$label) {
    $dialog = $zero
    foreach ($y in 245, 258, 271, 284, 233, 297, 310, 220) {
        Click-At $window 345 $y
        $dialog = Window-For '#32770' 'Импорт JavaScript в Aqua Frame' 1
        if ($dialog -ne $zero) { break }
    }
    if ($dialog -eq $zero) { throw "The JS file chooser did not open for $label." }
    Screenshot $dialog "scripts-picker-$label.png"
    [void][AquaScriptUi]::SetForegroundWindow($dialog)
    Start-Sleep -Milliseconds 300
    [Windows.Forms.SendKeys]::SendWait($path + '{ENTER}')
    $confirm = Window-For '#32770' 'Разрешение Aqua Scripts' 12
    if ($confirm -eq $zero) {
        Screenshot $dialog "scripts-picker-error-$label.png"
        throw "Aqua Scripts did not ask for site permission for $label."
    }
    Screenshot $confirm "scripts-permission-$label.png"
    $yes = [AquaScriptUi]::GetDlgItem($confirm, 6)
    if ($yes -eq $zero -or ![AquaScriptUi]::PostMessage($yes, 0x00f5, $zero, $zero)) {
        throw "Unable to approve the fixture site for $label."
    }
    Start-Sleep -Seconds 2
}
$port = 39123
$site = "http://127.0.0.1:$port/"
$other = "http://localhost:$port/"
$profile = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data'
$library = Join-Path $profile 'aqua-scripts.json'
$script = Join-Path $env:RUNNER_TEMP 'aqua-scope-fixture.user.js'
$plain = Join-Path $env:RUNNER_TEMP 'aqua-plain-fixture.js'
'document.documentElement.dataset.aquaPlain = "ran";' | Set-Content -LiteralPath $plain -Encoding utf8
@'
// ==UserScript==
// @name Aqua Scope Fixture
// @match http://*/*
// @grant none
// ==/UserScript==
window.aquaIsolatedFixture = 'private';
document.documentElement.dataset.aquaFixture = 'ran';
'@ | Set-Content -LiteralPath $script -Encoding utf8
$env:AQUA_SCRIPT_PROBE = Join-Path $env:RUNNER_TEMP 'aqua-scope-probe.log'
$server = Start-Process python -ArgumentList @('diagnostics/scripts-fixture.py', $port) -PassThru
$exe = Join-Path $PWD 'runtime/AquaFrame.exe'
$process = $null
try {
    Start-Sleep -Seconds 2
    if ($server.HasExited) { throw 'The loopback fixture did not start.' }
    $process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 30
    if ($window -eq $zero) { throw 'Aqua Frame did not open.' }
    Position $window
    Start-Sleep -Seconds 3
    Open-Address $window $site
    Open-Address $window 'aqua://settings/#scripts'
    Screenshot $window 'scripts-before.png'
    Import-Script $window $script 'userscript'
    Import-Script $window $plain 'plain'
    $entries = @(Get-Content $library -Raw | ConvertFrom-Json)
    if ($entries.Count -ne 2) { throw 'Both scripts must be saved.' }
    foreach ($entry in $entries) {
        if ($entry.origin -ne $site.TrimEnd('/') -or !$entry.enabled) { throw 'An approved script was not saved.' }
    }
    Screenshot $window 'scripts-imported.png'
    Open-Address $window ($site + '?probe=approved#board')
    Start-Sleep -Seconds 3
    Screenshot $window 'scripts-approved.png'
    Open-Address $window ($other + '?probe=other')
    Start-Sleep -Seconds 3
    $observed = Get-Content $env:AQUA_SCRIPT_PROBE -Raw
    Write-Host $observed
    if ($observed -notmatch 'approved:ran:undefined:ran') {
        throw 'Both scripts should update the approved document from the isolated world.'
    }
    if ($observed -notmatch 'other:none:undefined:none' -or $observed -match 'other:ran') {
        throw 'The script ran on another origin.'
    }
    Write-Host 'Imported JS and user.js files, verified isolated execution on the approved origin, and rejected another origin.'
} finally {
    if ($process -and !$process.HasExited) {
        $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 1
        if ($window -ne $zero) { [void][AquaScriptUi]::PostMessage($window, 0x0010, $zero, $zero) }
        if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
    }
    if (!$server.HasExited) { Stop-Process -Id $server.Id -Force }
}

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'This test needs a disposable GitHub Actions runner.' }
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class AquaResetUi {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string type,string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string type,string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window,uint msg,IntPtr w,string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window,uint msg,IntPtr w,StringBuilder text);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window,uint msg,IntPtr w,IntPtr l);
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
[void][AquaResetUi]::SetProcessDPIAware()
$zero = [IntPtr]::Zero
function Window-For([string]$class, [string]$title, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        $handle = [AquaResetUi]::FindWindow($class, $title)
        if ($handle -ne $zero) { return $handle }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $zero
}
function Position([IntPtr]$window) {
    $screen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if (![AquaResetUi]::SetWindowPos($window, [IntPtr](-1), 20, 20,
        [Math]::Min(984, $screen.Width - 40), [Math]::Min(690,$screen.Height - 40), 64)) {
        throw 'Unable to position Aqua Frame.'
    }
}
function Open-Address([IntPtr]$window, [string]$url) {
    $edit = [AquaResetUi]::FindWindowEx($window, $zero, 'Edit', $null)
    if ($edit -eq $zero -or [AquaResetUi]::SendMessage($edit, 0x000c, $zero, $url) -eq $zero -or
        ![AquaResetUi]::PostMessage($edit, 0x0100, [IntPtr]13, $zero)) {
        throw "Unable to navigate to $url."
    }
    Start-Sleep -Seconds 2
}
function Get-Address([IntPtr]$window) {
    $edit = [AquaResetUi]::FindWindowEx($window, $zero, 'Edit', $null)
    $address = [Text.StringBuilder]::new(8193)
    [void][AquaResetUi]::SendMessage($edit, 0x000d, [IntPtr]$address.Capacity, $address)
    $address.ToString()
}
function Screenshot([IntPtr]$window, [string]$name) {
    $r = New-Object AquaResetUi+Rect
    if (![AquaResetUi]::GetWindowRect($window, [ref]$r)) { return }
    $bmp = [Drawing.Bitmap]::new(($r.Right - $r.Left), ($r.Bottom - $r.Top))
    $graphics = [Drawing.Graphics]::FromImage($bmp)
    try {
        $dc = $graphics.GetHdc()
        try { $printed = [AquaResetUi]::PrintWindow($window, $dc, 2) }
        finally { $graphics.ReleaseHdc($dc) }
        if ($printed) { $bmp.Save((Join-Path $PWD $name), [Drawing.Imaging.ImageFormat]::Png) }
    } finally {
        $graphics.Dispose()
        $bmp.Dispose()
    }
}
function Click-At([IntPtr]$window, [int]$x, [int]$y) {
    $scale = [AquaResetUi]::GetDpiForWindow($window) / 96.0
    $point = New-Object AquaResetUi+Point
    $point.X = [int]($x * $scale)
    $point.Y = [int]($y * $scale)
    if (![AquaResetUi]::ClientToScreen($window, [ref]$point) -or
        ![AquaResetUi]::SetCursorPos($point.X, $point.Y)) {
        throw "Unable to locate the settings button."
    }
    [AquaResetUi]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [AquaResetUi]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}
function Confirm-Reset([IntPtr]$window, [int]$id) {
    $dialog = $zero
    # Use the actual rendered control and the operating system's confirmation.
    foreach ($y in 290, 302, 314, 326, 338, 350, 362, 374, 386, 398) {
        Click-At $window 385 $y
        $dialog = Window-For '#32770' 'Полный заводской сброс Aqua Frame' 1
        if ($dialog -ne $zero) { break }
    }
    if ($dialog -eq $zero) { throw 'The native factory-reset confirmation did not open.' }
    Screenshot $dialog "reset-confirm-$id.png"
    $button = [AquaResetUi]::GetDlgItem($dialog, $id)
    if ($button -eq $zero -or ![AquaResetUi]::PostMessage($button, 0x00f5, $zero, $zero)) {
        throw 'Unable to answer the native confirmation.'
    }
    Start-Sleep -Milliseconds 500
}

$profile = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data'
if (Test-Path $profile) { throw 'The reset test requires a fresh runner profile.' }
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$fixture = Join-Path $profile 'fixture-data.txt'
$external = Join-Path (Split-Path (Split-Path $profile)) 'aqua-outside-reset.txt'
$preferences = Join-Path $profile 'aqua-settings.json'
Set-Content -Path $fixture -Value 'inside the disposable profile'
Set-Content -Path $external -Value 'outside the disposable profile'
$outside = Join-Path $env:RUNNER_TEMP 'aqua-reset-junction-target'
New-Item -ItemType Directory -Path $outside | Out-Null
Set-Content -Path (Join-Path $outside 'keep.txt') -Value 'this target is not part of the profile'
New-Item -ItemType Junction -Path (Join-Path $profile 'outside-link') -Target $outside | Out-Null
Set-Content -Path $preferences -Value '{"search":"Google","zoom":100,"restore_session":false,"session":[]}'

$exe = Join-Path $PWD 'runtime/AquaFrame.exe'
$process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
try {
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 30
    if ($window -eq $zero) { throw 'The initial Aqua Frame window did not open.' }
    Position $window
    Start-Sleep -Seconds 3
    $client = New-Object AquaResetUi+Rect
    if (![AquaResetUi]::GetClientRect($window, [ref]$client)) { throw 'Unable to measure the browser toolbar.' }
    $dpi = [AquaResetUi]::GetDpiForWindow($window) / 96.0
    Click-At $window ([int]($client.Right / $dpi) - 61) 56
    $extensionWindow = Window-For 'Chrome_WidgetWin_1' 'Расширения — Aqua Frame' 10
    if ($extensionWindow -eq $zero) {
        # Chromium Views uses a platform-specific widget class. Find the
        # opened application window by title instead if the class changes.
        $extensionWindow = [AquaResetUi]::FindWindow([NullString]::Value, 'Расширения — Aqua Frame')
    }
    if ($extensionWindow -eq $zero) { throw 'The toolbar extension button did not open the manager.' }
    [void][AquaResetUi]::PostMessage($extensionWindow, 0x0010, $zero, $zero)
    Start-Sleep -Seconds 1
    Open-Address $window 'aqua://settings/'
    Screenshot $window 'settings-general-default.png'
    Click-At $window 898 264
    Start-Sleep -Seconds 1
    if (!(Get-Content $preferences -Raw | ConvertFrom-Json).restore_session) {
        throw 'Session restore did not persist its setting.'
    }
    Screenshot $window 'settings-general.png'
    Open-Address $window 'aqua://settings/#search'
    Click-At $window 420 250
    Start-Sleep -Milliseconds 500
    Screenshot $window 'settings-search-menu.png'
    Click-At $window 420 352
    Start-Sleep -Seconds 1
    if ((Get-Content $preferences -Raw | ConvertFrom-Json).search -ne 'DuckDuckGo') {
        throw 'Selecting a search provider did not persist its setting.'
    }
    Open-Address $window 'aqua settings fixture'
    if ((Get-Address $window) -notlike 'https://duckduckgo.com/?q=*') {
        throw 'The selected search engine was not used for the address bar.'
    }
    Open-Address $window 'aqua://settings/#appearance'
    Click-At $window 420 218
    Start-Sleep -Milliseconds 500
    Screenshot $window 'settings-theme-menu.png'
    Click-At $window 420 285
    Start-Sleep -Seconds 1
    if ((Get-Content $preferences -Raw | ConvertFrom-Json).theme -ne 'contrast') {
        throw 'The selected theme was not saved.'
    }
    Click-At $window 420 324
    Start-Sleep -Milliseconds 500
    Screenshot $window 'settings-zoom-menu.png'
    Click-At $window 420 445
    Start-Sleep -Seconds 1
    if ((Get-Content $preferences -Raw | ConvertFrom-Json).zoom -ne 125) {
        throw 'The selected page zoom was not saved.'
    }
    Open-Address $window 'aqua://settings/#reset'
    if ((Get-Address $window) -ne 'aqua://settings/#reset') { throw 'The reset settings did not open.' }
    Screenshot $window 'settings-reset.png'
    Click-At $window 350 242
    Start-Sleep -Seconds 1
    $defaults = Get-Content $preferences -Raw | ConvertFrom-Json
    if ($defaults.search -ne 'Google' -or $defaults.zoom -ne 100 -or $defaults.theme -ne 'ocean' -or $defaults.restore_session -or
        !(Test-Path $fixture)) { throw 'The ordinary reset failed to restore preferences or preserved data.' }
    Confirm-Reset $window 7
    if (!(Test-Path $fixture) -or $process.HasExited) { throw 'Cancelling reset wiped data or closed Aqua Frame.' }

    Confirm-Reset $window 6
    if (!$process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw 'The browser did not exit cleanly after confirmation.' }
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 40
    if ($window -eq $zero) {
        $failure = Window-For '#32770' 'Сброс Aqua Frame' 1
        if ($failure -ne $zero) { Screenshot $failure 'settings-reset-error.png' }
        Get-ChildItem -LiteralPath (Split-Path $profile) -Force | Select-Object Name | Write-Host
        Get-Process -Name AquaFrame,AquaFrameReset -ErrorAction SilentlyContinue | Select-Object Name,Id | Write-Host
        throw 'Aqua Frame did not restart.'
    }
    Position $window
    Start-Sleep -Seconds 3
    Screenshot $window 'settings-welcome.png'
    if ((Get-Address $window) -ne 'aqua://welcome/') { throw 'The first-run page did not open after reset.' }
    if ((Test-Path $fixture) -or (Test-Path $preferences) -or !(Test-Path $external) -or
        !(Test-Path (Join-Path $outside 'keep.txt'))) {
        throw 'Reset did not remove the profile fixture and preserve the external file.'
    }
    Write-Host 'Cancelled and confirmed reset through the Windows UI. Profile cleared and first-run page opened.'
} finally {
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 1
    if ($window -ne $zero) { [void][AquaResetUi]::PostMessage($window, 0x0010, $zero, $zero) }
}

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
    private delegate bool EnumChildProc(IntPtr window, IntPtr param);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumChildProc callback, IntPtr param);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern IntPtr SendMessageTimeout(
        IntPtr window, uint msg, UIntPtr w, StringBuilder text, uint flags, uint timeout, out UIntPtr result);
    public static string DialogText(IntPtr dialog) {
        var captions = new StringBuilder();
        EnumChildWindows(dialog, (window, param) => {
            var text = new StringBuilder(2000);
            SendMessageTimeout(window, 0x000d, (UIntPtr)text.Capacity, text, 2, 1000, out _);
            if (text.Length > 0) captions.AppendLine(text.ToString());
            return true;
        }, IntPtr.Zero);
        return captions.ToString();
    }
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
function Click-At([IntPtr]$window, [int]$x, [int]$y) {
    $scale = [AquaWindowUi]::GetDpiForWindow($window) / 96.0
    $point = New-Object AquaWindowUi+Point
    $point.X = [int]($x * $scale)
    $point.Y = [int]($y * $scale)
    if (![AquaWindowUi]::ClientToScreen($window, [ref]$point) -or
        ![AquaWindowUi]::SetCursorPos($point.X, $point.Y)) {
        throw "Unable to locate the settings button."
    }
    [AquaWindowUi]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [AquaWindowUi]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}

function Assert-WindowMode([IntPtr]$window, [bool]$fullscreen, [string]$step) {
    $nativeEdit = [AquaWindowUi]::FindWindowEx($window, $zero, 'Edit', $null)
    if ($nativeEdit -eq $zero) { throw 'The address bar is missing.' }
    $bounds = New-Object AquaWindowUi+Rect
    $screen = [Windows.Forms.Screen]::FromHandle($window).Bounds
    $deadline = (Get-Date).AddSeconds(6)
    do {
        [void][AquaWindowUi]::GetWindowRect($window, [ref]$bounds)
        $matchesMonitor = $bounds.Left -eq $screen.Left -and $bounds.Right -eq $screen.Right -and
                          $bounds.Top -eq $screen.Top -and $bounds.Bottom -eq $screen.Bottom
        $addressHidden = ![AquaWindowUi]::IsWindowVisible($nativeEdit)
        if ($matchesMonitor -eq $fullscreen -and $addressHidden -eq $fullscreen) {
            Write-Host "${step}: fullscreen=$fullscreen, addressHidden=$addressHidden"
            return
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    Screenshot $window "browser-mode-failure.png"
    throw "${step}: Expected fullscreen=$fullscreen; matchesMonitor=$matchesMonitor addressHidden=$addressHidden."
}


function Request-Log {
    if (Test-Path -LiteralPath $env:AQUA_BROWSER_PROBE) {
        return Get-Content -LiteralPath $env:AQUA_BROWSER_PROBE -Raw
    }
    return ''
}


function Wait-Request([string]$expected) {
    $deadline = (Get-Date).AddSeconds(10)
    do {
        $log = Request-Log
        if ($log -match $expected) { return }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    throw "No HTTP request matching $expected. Observed: $log"
}

$port = 39176
$site = "http://127.0.0.1:$port"
$env:AQUA_BROWSER_PROBE = Join-Path $env:RUNNER_TEMP 'aqua-browser-probe.log'
$settings = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data/aqua-settings.json'
$serverErrors = Join-Path $env:RUNNER_TEMP 'aqua-browser-fixture.stderr'
$server = Start-Process python -ArgumentList @('diagnostics/browser-fixture.py', $port) -PassThru -RedirectStandardError $serverErrors
$process = $null
$exe = Join-Path $PWD 'runtime/AquaFrame.exe'
try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try {
            $reply = Invoke-WebRequest "$site/ready" -TimeoutSec 1 -NoProxy
            $ready = $reply.StatusCode -eq 200
        } catch {
            Start-Sleep -Milliseconds 300
        }
        if ($ready -or $server.HasExited) { break }
    }
    if (!$ready) { throw "HTTP fixture did not start: $(Get-Content $serverErrors -Raw)" }
    $process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 30
    if ($window -eq $zero -or $server.HasExited) { throw 'The fixture or Aqua Frame did not start.' }
    Position $window
    [void][AquaWindowUi]::SetForegroundWindow($window)
    Start-Sleep -Seconds 3
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        Open-Address $window "$site/normal"
        if ((Request-Log) -match '(?m)^/normal\|') { break }
    }
    Screenshot $window 'browser-normal.png'
    Wait-Request '(?m)^/normal\|'
    Click-At $window 400 240
    [Windows.Forms.SendKeys]::SendWait('^d')
    Start-Sleep -Seconds 1
    $prefs = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
    if ($prefs.bookmarks.url -ne "$site/normal" -or $prefs.history.url -ne "$site/normal") {
        throw 'The bookmark and history entry did not persist.'
    }
    [Windows.Forms.SendKeys]::SendWait('^+m')
    $dialog = Window-For '#32770' 'Aqua Performance' 10
    if ($dialog -eq $zero) { throw 'The navigation timing report did not open.' }
    Start-Sleep -Milliseconds 500
    Screenshot $dialog 'browser-timing.png'
    $report = [AquaWindowUi]::DialogText($dialog)
    Write-Host "Aqua Performance report: $report"
    if ($report -notmatch 'TTFB\):\s*(\d+)[.,]\d+') {
        $timingError = "No first-byte timing found. Report: $report"
    } elseif ([int]$Matches[1] -lt 110) {
        $timingError = "The first-byte timing missed the fixture delay. Report: $report"
    } elseif ($report -notmatch 'rAF\):\s*[0-9]+[.,][0-9]+' -or
              $report -notmatch 'CPU' -or $report -notmatch 'Chromium 158') {
        $timingError = "The performance report is missing measured browser and page values. Report: $report"
    }
    [void][AquaWindowUi]::SetForegroundWindow($dialog)
    [Windows.Forms.SendKeys]::SendWait('{ENTER}')
    Start-Sleep -Milliseconds 600
    if ([AquaWindowUi]::IsWindowVisible($dialog)) { throw 'The test could not dismiss the performance report.' }
    [void][AquaWindowUi]::SetForegroundWindow($window)

    Click-At $window 100 111
    Assert-WindowMode $window $true 'A web element enters actual fullscreen'
    [Windows.Forms.SendKeys]::SendWait('{ESC}')
    Assert-WindowMode $window $false 'Escape exits web fullscreen'
    Click-At $window 400 240

    [Windows.Forms.SendKeys]::SendWait('^+n')
    Start-Sleep -Seconds 2
    Open-Address $window "$site/private"
    if ((Request-Log) -notmatch '(?m)^/private\|\r?$') { throw 'The private tab reused the normal cookie or did not load.' }
    Click-At $window 400 240
    [Windows.Forms.SendKeys]::SendWait('^w')
    Start-Sleep -Seconds 2
    Open-Address $window "$site/ordinary"
    if ((Request-Log) -notmatch '(?m)^/ordinary\|normal=1\r?$') { throw 'A private cookie reached the normal tab, or the normal cookie was lost.' }
    Start-Sleep -Seconds 4
    $prefs = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
    if ($prefs.history.url -contains "$site/private" -or $prefs.history.url -notcontains "$site/ordinary") {
        throw 'The private navigation reached normal history, or the normal entry was lost.'
    }
    Open-Address $window 'aqua://library/history'
    Screenshot $window 'browser-history.png'
    [void][AquaWindowUi]::PostMessage($window, 0x0010, $zero, $zero)
    if (!$process.WaitForExit(15000) -or $process.ExitCode -ne 0) { throw 'The window could not close normally.' }

    $prefs.restore_session = $true
    $prefs.session = @("$site/deferred", "$site/selected")
    ConvertTo-Json $prefs -Depth 10 | Set-Content -LiteralPath $settings -Encoding utf8
    $process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 30
    if ($window -eq $zero) { throw 'Aqua Frame did not restart.' }
    Position $window
    [void][AquaWindowUi]::SetForegroundWindow($window)
    Start-Sleep -Seconds 4
    $log = Request-Log
    if ($log -notmatch '(?m)^/selected\|' -or $log -match '(?m)^/deferred\|') {
        throw 'Session startup did not defer the background tab.'
    }
    Click-At $window 80 18
    Start-Sleep -Seconds 3
    if ((Request-Log) -notmatch '(?m)^/deferred\|') { throw 'Selecting a deferred tab did not load it.' }
    Write-Host 'Verified fullscreen, history, bookmarks, cookie isolation, and lazy restoration.'
    if ($timingError) { throw $timingError }
    Write-Host 'Verified measured TTFB, page frame intervals and process statistics.'
} finally {
    if ($process -and !$process.HasExited) {
        $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 1
        if ($window -ne $zero) { [void][AquaWindowUi]::PostMessage($window, 0x0010, $zero, $zero) }
        if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
    }
    if (!$server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host (Request-Log)
    Get-Content $serverErrors -ErrorAction Continue
}

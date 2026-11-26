. (Join-Path $PSScriptRoot 'browser-integration.ps1') -HelpersOnly
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes

$port = 39177
$site = "http://127.0.0.1:$port"
$env:AQUA_BROWSER_PROBE = Join-Path $env:RUNNER_TEMP 'aqua-performance-probe.log'
$profile = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data'
$settings = Join-Path $profile 'aqua-settings.json'
$exe = Join-Path $PWD 'runtime/AquaFrame.exe'
$server = Start-Process python -ArgumentList @('diagnostics/browser-fixture.py', $port) -PassThru
$process = $null

function Start-Aqua {
    $script:process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 30
    if ($window -eq $zero) { throw 'Aqua Frame did not appear.' }
    Position $window
    [void][AquaWindowUi]::SetForegroundWindow($window)
    Start-Sleep -Seconds 3
    return $window
}

function Close-Aqua([IntPtr]$window) {
    if ($script:process.HasExited) { return }
    [void][AquaWindowUi]::PostMessage($window, 0x0010, $zero, $zero)
    if (!$script:process.WaitForExit(15000) -or $script:process.ExitCode -ne 0) {
        throw 'Aqua Frame did not exit cleanly.'
    }
}

function RequestCount([string]$path) {
    return [regex]::Matches((Request-Log), "(?m)^$path\|").Count
}

try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try { $ready = (Invoke-WebRequest "$site/ready" -NoProxy -TimeoutSec 1).StatusCode -eq 200 }
        catch { Start-Sleep -Milliseconds 300 }
        if ($ready -or $server.HasExited) { break }
    }
    if (!$ready) { throw 'The loopback fixture did not start.' }

    $window = Start-Aqua
    Screenshot $window 'AquaFrame-Performance-Home.png'
    Open-Address $window 'aqua://settings/#performance'
    Screenshot $window 'AquaFrame-Performance-Settings.png'
    $warm = $false
    foreach ($y in 304, 308, 299, 316, 292, 323) {
        Click-At $window 906 $y
        Start-Sleep -Milliseconds 450
        if (Test-Path -LiteralPath $settings) {
            $prefs = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
            $warm = $prefs.warm_restored_tab
            if ($warm) { break }
        }
    }
    if (!$warm) { throw 'The warm restore toggle did not save.' }
    Close-Aqua $window

    $prefs.restore_session = $true
    $prefs.limit_restored_tabs = $false
    $prefs.session = @("$site/untouched", "$site/warm", "$site/active")
    ConvertTo-Json $prefs -Depth 10 | Set-Content -LiteralPath $settings -Encoding utf8

    $window = Start-Aqua
    Wait-Request '(?m)^/active\|'
    Wait-Request '(?m)^/warm\|'
    if ((RequestCount '/untouched') -ne 0) { throw 'More than one background tab was loaded.' }
    Write-Host 'Aqua Frame loaded the active tab and one additional tab.'

    Start-Sleep -Seconds 1
    $point = New-Object AquaWindowUi+Point
    $point.X = [int](276 * [AquaWindowUi]::GetDpiForWindow($window) / 96)
    $point.Y = [int](18 * [AquaWindowUi]::GetDpiForWindow($window) / 96)
    [void][AquaWindowUi]::ClientToScreen($window, [ref]$point)
    [void][AquaWindowUi]::SetCursorPos($point.X, $point.Y)
    $packed = [IntPtr](($point.Y -shl 16) -bor ($point.X -band 65535))
    [void][AquaWindowUi]::PostMessage($window, 0x007b, $window, $packed)
    $menu = Window-For '#32768' $null 4
    if ($menu -eq $zero) { throw 'The native tab context menu did not open.' }
    Screenshot $menu 'AquaFrame-Performance-Menu.png'
    $menuElement = [Windows.Automation.AutomationElement]::FromHandle($menu)
    $name = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::NameProperty, 'Выгрузить вкладку из памяти')
    $item = $menuElement.FindFirst([Windows.Automation.TreeScope]::Descendants, $name)
    if ($item -and $item.Current.IsEnabled) {
        $bounds = $item.Current.BoundingRectangle
        $x = [int]($bounds.Left + $bounds.Width / 2)
        $y = [int]($bounds.Top + $bounds.Height / 2)
    } else {
        $bounds = New-Object AquaWindowUi+Rect
        if (![AquaWindowUi]::GetWindowRect($menu, [ref]$bounds)) { throw 'Unable to measure the popup menu.' }
        $x = $bounds.Left + 75
        $y = $bounds.Bottom - [int](36 * [AquaWindowUi]::GetDpiForWindow($window) / 96)
    }
    [void][AquaWindowUi]::SetCursorPos($x, $y)
    [AquaWindowUi]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [AquaWindowUi]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    $dialog = Window-For '#32770' 'Освобождение памяти Aqua Frame' 8
    if ($dialog -eq $zero) { throw 'The tab unload confirmation did not open.' }
    Screenshot $dialog 'AquaFrame-Performance-Unload.png'
    [void][AquaWindowUi]::SetForegroundWindow($dialog)
    [Windows.Forms.SendKeys]::SendWait('{LEFT}{ENTER}')
    Start-Sleep -Seconds 2
    if ([AquaWindowUi]::IsWindowVisible($dialog)) { throw 'Could not confirm the unload.' }
    if ((RequestCount '/warm') -ne 1) { throw 'The unloaded tab reloaded before being selected.' }

    [void][AquaWindowUi]::SetForegroundWindow($window)
    Click-At $window 276 18
    $deadline = (Get-Date).AddSeconds(10)
    do {
        if ((RequestCount '/warm') -eq 2) { break }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    if ((RequestCount '/warm') -ne 2 -or (RequestCount '/untouched') -ne 0) {
        throw 'The unloaded tab did not reload once on selection.'
    }
    Write-Host 'Confirmed a real browser reload after unloading and selecting the same tab.'
    Close-Aqua $window
} finally {
    if ($process -and !$process.HasExited) {
        $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 1
        if ($window -ne $zero) { [void][AquaWindowUi]::PostMessage($window, 0x0010, $zero, $zero) }
        if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
    }
    if (!$server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host (Request-Log)
}

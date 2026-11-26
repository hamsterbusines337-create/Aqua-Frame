. (Join-Path $PSScriptRoot 'browser-integration.ps1') -HelpersOnly

$settings = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data/aqua-settings.json'
$exe = Join-Path $PWD 'runtime/AquaFrame.exe'
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

function Get-ChromeReport([IntPtr]$window) {
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        [Windows.Forms.Clipboard]::Clear()
        Click-At $window 475 175
        [Windows.Forms.SendKeys]::SendWait('^a')
        [Windows.Forms.SendKeys]::SendWait('^c')
        Start-Sleep -Milliseconds 350
        $value = [Windows.Forms.Clipboard]::GetText()
        if ($value.Length -gt 50) { return $value }
    }
    return ''
}

try {
    $window = Start-Aqua
    Open-Address $window 'chrome://version/'
    Screenshot $window 'AquaFrame-GPU-Version-Default.png'
    $version = Get-ChromeReport $window
    if ($version -notmatch '(?i)command line' -or $version -match '--force-high-performance-gpu(?:\s|$)') {
        throw "The default Chromium command line could not be verified. Captured characters: $($version.Length)."
    }

    Open-Address $window 'aqua://settings/#performance'
    Click-At $window 730 600
    [Windows.Forms.SendKeys]::SendWait('^{END}')
    Start-Sleep -Milliseconds 500
    Screenshot $window 'AquaFrame-GPU-Settings.png'
    $enabled = $false
    foreach ($y in 516, 530, 544, 558, 572, 586, 600, 502, 488) {
        Click-At $window 906 $y
        Start-Sleep -Milliseconds 450
        if (Test-Path -LiteralPath $settings) {
            $prefs = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
            $enabled = $prefs.prefer_high_performance_gpu
            if ($enabled) { break }
        }
    }
    if (!$enabled) { throw 'The high-performance GPU preference was not saved by the settings page.' }
    Close-Aqua $window

    $window = Start-Aqua
    Open-Address $window 'chrome://version/'
    Screenshot $window 'AquaFrame-GPU-Version-Enabled.png'
    $version = Get-ChromeReport $window
    if ($version -notmatch '--force-high-performance-gpu(?:\s|$)') {
        throw "Chromium did not launch with the selected adapter preference. Captured characters: $($version.Length)."
    }
    Write-Host 'Verified that the saved setting appears in the Chromium command line after restart.'

    Open-Address $window 'aqua://settings/#performance'
    Click-At $window 730 600
    [Windows.Forms.SendKeys]::SendWait('^{END}')
    Start-Sleep -Milliseconds 500
    Screenshot $window 'AquaFrame-GPU-Settings-Enabled.png'
    # Also verify the GPU status page has actual Chromium data.
    Open-Address $window 'chrome://gpu/'
    Start-Sleep -Seconds 3
    Screenshot $window 'AquaFrame-GPU-Report.png'
    $report = Get-ChromeReport $window
    if ($report -notmatch '(?i)graphics feature status' -or $report -notmatch '(?i)compositing') {
        throw "The real Chromium GPU report was not available. Captured characters: $($report.Length)."
    }
    Write-Host (($report -split '\r?\n' | Where-Object { $_ -match '(?i)^\s*(Compositing|Rasterization|OpenGL|WebGL)\b' } | Select-Object -First 8) -join "`n")
    Close-Aqua $window
} finally {
    if ($process -and !$process.HasExited) {
        $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 1
        if ($window -ne $zero) { [void][AquaWindowUi]::PostMessage($window, 0x0010, $zero, $zero) }
        if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
    }
}

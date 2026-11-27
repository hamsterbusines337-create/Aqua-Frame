. (Join-Path $PSScriptRoot 'browser-integration.ps1') -HelpersOnly

$port = 39179
$site = "http://127.0.0.1:$port/frames"
$env:AQUA_FRAMES_REPORT = Join-Path $PWD 'AquaFrame-frame-comparison.jsonl'
$server = Start-Process python -ArgumentList @('diagnostics/frame-fixture.py', $port) -PassThru
$aqua = $null
$chrome = $null

function Wait-Measurement([string]$engine, [string]$workload) {
    $deadline = (Get-Date).AddSeconds(22)
    do {
        if (Test-Path -LiteralPath $env:AQUA_FRAMES_REPORT) {
            $entry = Get-Content -LiteralPath $env:AQUA_FRAMES_REPORT | ConvertFrom-Json |
                Where-Object { $_.engine -eq $engine -and $_.workload -eq $workload } | Select-Object -Last 1
            if ($entry) {
                if ($entry.hidden -or $entry.samples -lt 50) { throw "Invalid visible sample: $engine / $workload." }
                return $entry
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "No visible sample: $engine / $workload."
}

try {
    $deadline = (Get-Date).AddSeconds(10)
    do {
        try { $ready = (Invoke-WebRequest $site -NoProxy -TimeoutSec 1).StatusCode -eq 200 }
        catch { Start-Sleep -Milliseconds 250 }
    } while (!$ready -and !$server.HasExited -and (Get-Date) -lt $deadline)
    if (!$ready) { throw 'The local animation fixture did not start.' }

    $exe = Join-Path $PWD 'runtime/AquaFrame.exe'
    $aqua = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $window = Window-For 'AquaFrame.Main' 'Aqua Frame' 30
    if ($window -eq $zero) { throw 'Aqua Frame did not appear.' }
    Position $window
    [void][AquaWindowUi]::SetForegroundWindow($window)
    Start-Sleep -Seconds 3

    Open-Address $window "${site}?engine=aqua&load=idle"
    $baseline = Wait-Measurement 'aqua' 'idle'
    Open-Address $window "${site}?engine=aqua&load=jank"
    Click-At $window 410 260
    [Windows.Forms.SendKeys]::SendWait('^+m')
    $dialog = Window-For '#32770' 'Aqua Performance' 15
    if ($dialog -eq $zero) { throw 'The 5-second report did not open.' }
    Start-Sleep -Milliseconds 600
    Screenshot $dialog 'AquaFrame-frame-budget.png'
    $report = [AquaWindowUi]::DialogText($dialog)
    Write-Host $report
    if ($report -notmatch 'P95: [0-9]+[.,][0-9]+' -or $report -notmatch 'P99: [0-9]+[.,][0-9]+' -or
        $report -notmatch '120 FPS' -or $report -notmatch '>50 [^:]+: [1-9][0-9]*' -or
        $report -notmatch 'Длительность замера: 5[.,]') {
        throw 'The report did not expose measured frame statistics and detected long frames.'
    }
    [void][AquaWindowUi]::SetForegroundWindow($dialog)
    [Windows.Forms.SendKeys]::SendWait('{ENTER}')
    $loaded = Wait-Measurement 'aqua' 'jank'
    if ($loaded.p99_ms -lt 60 -or $loaded.p99_ms -lt ($baseline.p99_ms + 15)) {
        throw 'The timing fixture did not detect the controlled main-thread stalls.'
    }
    [void][AquaWindowUi]::PostMessage($window, 0x0010, $zero, $zero)
    if (!$aqua.WaitForExit(15000) -or $aqua.ExitCode -ne 0) { throw 'Aqua Frame did not close normally.' }

    $chromeExe = Join-Path $env:ProgramFiles 'Google/Chrome/Application/chrome.exe'
    if (Test-Path -LiteralPath $chromeExe) {
        foreach ($load in 'idle', 'jank') {
            $profile = Join-Path $env:RUNNER_TEMP "aqua-test-chrome-$load"
            $chrome = Start-Process $chromeExe -ArgumentList @(
                "--user-data-dir=`"$profile`"", '--no-first-run',
                '--no-default-browser-check', '--window-position=20,20', '--window-size=984,688',
                "${site}?engine=chrome&load=$load") -PassThru
            $entry = Wait-Measurement 'chrome' $load
            Write-Host ($entry | ConvertTo-Json -Compress)
            if (!$chrome.HasExited) { [void]$chrome.CloseMainWindow() }
            if (!$chrome.WaitForExit(6000)) { Stop-Process -Id $chrome.Id -Force }
        }
    } else {
        Write-Host 'Stock Chrome was unavailable on this runner.'
    }
    Get-Content -LiteralPath $env:AQUA_FRAMES_REPORT
} finally {
    if ($aqua -and !$aqua.HasExited) { Stop-Process -Id $aqua.Id -Force }
    if ($chrome -and !$chrome.HasExited) { Stop-Process -Id $chrome.Id -Force }
    if (!$server.HasExited) { Stop-Process -Id $server.Id -Force }
}

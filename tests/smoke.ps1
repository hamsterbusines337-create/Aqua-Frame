$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $root 'build/Release'
$process = Start-Process (Join-Path $runtime 'AquaFrame.exe') -WorkingDirectory $runtime -PassThru
try {
    $deadline = (Get-Date).AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 400
        $process.Refresh()
        if ($process.HasExited) { throw "Aqua Frame exited before opening: $($process.ExitCode)" }
        $renderer = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($process.Id)" |
            Where-Object CommandLine -match '(?:^|\s)--type=renderer(?:\s|$)'
    } while ((!$process.MainWindowHandle -or !$renderer) -and (Get-Date) -lt $deadline)
    if (!$process.MainWindowHandle -or !$renderer) { throw 'No browser window or renderer process was observed.' }
    if (!$process.CloseMainWindow()) { throw 'Unable to send a close request to Aqua Frame.' }
    if (!$process.WaitForExit(20000)) { throw 'Aqua Frame timed out while closing.' }
    if ($process.ExitCode -ne 0) { throw "Aqua Frame exited with code $($process.ExitCode)." }
    Write-Host 'Observed main window, Chromium renderer and successful shutdown.'
} catch {
    $log = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data/cef.log'
    if (Test-Path $log) { Get-Content $log -Tail 30 | Write-Host }
    throw
} finally {
    if (!$process.HasExited) { & taskkill.exe /F /T /PID $process.Id | Out-Null }
}

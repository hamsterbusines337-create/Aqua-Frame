$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

public static class BrowserWindows {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string className, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr w, string text,
                                                   uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);

    private delegate bool WindowCallback(IntPtr window, IntPtr unused);
    [DllImport("user32.dll")]
    private static extern bool EnumWindows(WindowCallback callback, IntPtr unused);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr window, StringBuilder title, int size);

    public static IntPtr FindExtensionsWindow(int processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, _) => {
            GetWindowThreadProcessId(window, out uint owner);
            if (owner != processId || !IsWindowVisible(window)) return true;
            var title = new StringBuilder(256);
            GetWindowText(window, title, title.Capacity);
            if (title.ToString().IndexOf("Extensions", StringComparison.OrdinalIgnoreCase) < 0 &&
                title.ToString().IndexOf("Расширения", StringComparison.OrdinalIgnoreCase) < 0) return true;
            found = window;
            return false;
        }, IntPtr.Zero);
        return found;
    }
}
'@
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
    $zero = [IntPtr]::Zero
    $main = [BrowserWindows]::FindWindow('AquaFrame.Main', 'Aqua Frame')
    $edit = [BrowserWindows]::FindWindowEx($main, $zero, 'Edit', $null)
    $messageResult = $zero
    if (!$main -or !$edit -or ![BrowserWindows]::SendMessageTimeout(
            $edit, 0x000c, $zero, 'aqua://extensions', 2, 5000, [ref]$messageResult)) {
        throw 'Unable to enter the extensions address.'
    }
    if (![BrowserWindows]::PostMessage($edit, 0x0100, [IntPtr]13, $zero)) {
        throw 'Unable to submit the extensions address.'
    }
    $deadline = (Get-Date).AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 400
        $manager = [BrowserWindows]::FindExtensionsWindow($process.Id)
    } while (!$manager -and (Get-Date) -lt $deadline)
    if (!$manager) { throw 'The Chromium extension manager window did not appear.' }
    if (![BrowserWindows]::PostMessage($main, 0x0010, $zero, $zero)) {
        throw 'Unable to send a close request to Aqua Frame.'
    }
    if (!$process.WaitForExit(20000)) { throw 'Aqua Frame timed out while closing.' }
    if ($process.ExitCode -ne 0) { throw "Aqua Frame exited with code $($process.ExitCode)." }
    Write-Host 'Observed main window, Chromium renderer, extension manager and successful shutdown.'
} catch {
    $log = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data/cef.log'
    if (Test-Path $log) { Get-Content $log -Tail 30 | Write-Host }
    throw
} finally {
    if (!$process.HasExited) { & taskkill.exe /F /T /PID $process.Id | Out-Null }
}

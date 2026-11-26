$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type -ReferencedAssemblies System.Windows.Forms,System.Drawing @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public static class DropFixture {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string name, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr w, string text);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect bounds);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }

    public static DragDropEffects Drag(string path, int x, int y) {
        DragDropEffects result = DragDropEffects.None;
        using (var form = new Form()) {
            form.Text = "Aqua Frame drag fixture source";
            form.StartPosition = FormStartPosition.Manual;
            form.Location = new Point(3, 3);
            form.ClientSize = new Size(110, 48);
            form.TopMost = true;
            using (var start = new Timer { Interval = 350 })
            using (var move = new Timer { Interval = 500 }) {
                int phase = 0;
                move.Tick += (sender, args) => {
                    if (phase++ == 0) SetCursorPos(x, y);
                    else { move.Stop(); mouse_event(4, 0, 0, 0, UIntPtr.Zero); }
                };
                form.MouseDown += (sender, args) => {
                    var data = new DataObject();
                    data.SetData(DataFormats.FileDrop, new[] { path });
                    move.Start();
                    result = form.DoDragDrop(data, DragDropEffects.Copy);
                    move.Stop();
                    form.Close();
                };
                start.Tick += (sender, args) => {
                    start.Stop();
                    var source = form.PointToScreen(new Point(45, 24));
                    SetCursorPos(source.X, source.Y);
                    mouse_event(2, 0, 0, 0, UIntPtr.Zero);
                };
                form.Shown += (sender, args) => { form.Activate(); start.Start(); };
                Application.Run(form);
            }
        }
        return result;
    }
}
'@
[void][DropFixture]::SetProcessDPIAware()
$profile = Join-Path $env:LOCALAPPDATA 'Aqua Frame/User Data'
if (Test-Path $profile) { throw 'The drag fixture expects a fresh, disposable runner profile.' }
$fixture = Join-Path $env:RUNNER_TEMP 'aqua-extension-fixture'
New-Item $fixture -ItemType Directory | Out-Null
'{"manifest_version":3,"name":"Aqua Frame Drag Fixture","version":"1.0","description":"CI drag-and-drop fixture"}' |
    Set-Content (Join-Path $fixture 'manifest.json') -Encoding ascii
$archive = Join-Path $env:RUNNER_TEMP 'aqua-extension-fixture.zip'
Compress-Archive -Path (Join-Path $fixture 'manifest.json') -DestinationPath $archive
$exe = Join-Path $PWD 'runtime/AquaFrame.exe'
$process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
$zero = [IntPtr]::Zero
$main = $zero
function Find-Named($element, [string]$name) {
    $condition = New-Object Windows.Automation.PropertyCondition(
        [Windows.Automation.AutomationElement]::NameProperty, $name)
    $element.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
}
try {
    $deadline = (Get-Date).AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 300
        $main = [DropFixture]::FindWindow('AquaFrame.Main', 'Aqua Frame')
    } until ($main -ne $zero -or (Get-Date) -gt $deadline)
    if ($main -eq $zero) { throw 'Aqua Frame did not open.' }
    $edit = [DropFixture]::FindWindowEx($main, $zero, 'Edit', $null)
    [void][DropFixture]::SendMessage($edit, 0x000c, $zero, 'aqua://extensions')
    [void][DropFixture]::PostMessage($edit, 0x0100, [IntPtr]13, $zero)
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 350
        $manager = [DropFixture]::FindWindow('Chrome_WidgetWin_1', $null)
        if ($manager -ne $zero) {
            $element = [Windows.Automation.AutomationElement]::FromHandle($manager)
            $dev = Find-Named $element 'Developer mode'
        }
    } until ($dev -or (Get-Date) -gt $deadline)
    if (!$dev) { throw 'Unable to find the Chromium Developer mode switch.' }
    $toggle = $dev.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
    if ($toggle.Current.ToggleState -eq 'Off') { $toggle.Toggle() }
    $screen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    [void][DropFixture]::SetWindowPos($manager, $zero, 145, 30, [Math]::Min(850,$screen.Width-160),
        [Math]::Min(700,$screen.Height-65), 0x0040)
    Start-Sleep -Milliseconds 700
    $r = New-Object DropFixture+Rect
    if (![DropFixture]::GetWindowRect($manager, [ref]$r)) { throw 'Unable to locate the manager.' }
    $x = [int](($r.Left + $r.Right) / 2)
    $y = [int](($r.Top + $r.Bottom) / 2)
    $effect = [DropFixture]::Drag($archive, $x, $y)
    Write-Host "OLE drag result: $effect"
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 350
        $installed = Find-Named $element 'Aqua Frame Drag Fixture'
    } until ($installed -or (Get-Date) -gt $deadline)
    if (!$installed) { throw 'The ZIP extension did not appear in the manager.' }
    Write-Host 'The Chromium manager installed the Manifest V3 extension from a Windows OLE ZIP drop.'
} finally {
    if ($main -ne $zero) { [void][DropFixture]::PostMessage($main, 0x0010, $zero, $zero) }
    if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
}

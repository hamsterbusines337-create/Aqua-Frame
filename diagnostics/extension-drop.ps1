param([switch]$HelpersOnly)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class DropFixture {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string name, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr w, string text);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int show);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    private delegate bool Callback(IntPtr hwnd, IntPtr l);
    [DllImport("user32.dll")] private static extern bool EnumWindows(Callback callback, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder text, int size);

    public static IntPtr ManagerWindow(int pid) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((hwnd, unused) => {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner != pid) return true;
            var title = new System.Text.StringBuilder(256);
            GetWindowText(hwnd, title, title.Capacity);
            if (title.ToString().IndexOf("Extensions", StringComparison.OrdinalIgnoreCase) < 0) return true;
            result = hwnd;
            return false;
        }, IntPtr.Zero);
        return result;
    }
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect bounds);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }

    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("00000121-0000-0000-C000-000000000046")]
    public interface IDropSource {
        [PreserveSig] int QueryContinueDrag([MarshalAs(UnmanagedType.Bool)] bool escape, uint keyState);
        [PreserveSig] int GiveFeedback(uint effect);
    }

    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    public sealed class TestDropSource : IDropSource {
        public volatile bool Drop;
        public int QueryContinueDrag(bool escape, uint keyState) {
            return escape ? 0x00040101 : Drop ? 0x00040100 : 0;
        }
        public int GiveFeedback(uint effect) { return 0x00040102; }
    }

    [DllImport("ole32.dll")]
    private static extern int OleInitialize(IntPtr reserved);
    [DllImport("ole32.dll")]
    private static extern void OleUninitialize();
    [DllImport("ole32.dll")]
    private static extern int DoDragDrop(System.Runtime.InteropServices.ComTypes.IDataObject data,
        IDropSource source, uint allowed, out uint effect);

    [DllImport("kernel32.dll")] private static extern void Sleep(uint milliseconds);

    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    private static extern int SHParseDisplayName(string path, IntPtr bindContext,
        out IntPtr pidl, uint mask, out uint attributes);
    [DllImport("shell32.dll")]
    private static extern int SHCreateDataObject(IntPtr folder, uint count, IntPtr[] items,
        IntPtr inner, ref Guid iid, [MarshalAs(UnmanagedType.Interface)] out System.Runtime.InteropServices.ComTypes.IDataObject data);

    public static string Drag(string path, int x, int y) {
        var source = new TestDropSource();
        int init = OleInitialize(IntPtr.Zero);
        if (init < 0) Marshal.ThrowExceptionForHR(init);
        IntPtr item = IntPtr.Zero;
        System.Runtime.InteropServices.ComTypes.IDataObject data = null;
        try {
            Marshal.ThrowExceptionForHR(SHParseDisplayName(path, IntPtr.Zero, out item, 0, out _));
            var iid = typeof(System.Runtime.InteropServices.ComTypes.IDataObject).GUID;
            Marshal.ThrowExceptionForHR(SHCreateDataObject(IntPtr.Zero, 1, new[] { item }, IntPtr.Zero, ref iid, out data));
            SetCursorPos(x - 80, y - 80);
            using (var motion = new System.Threading.Timer(_ => {
                SetCursorPos(x, y);
                Sleep(800);
                source.Drop = true;
                SetCursorPos(x + 1, y);
                mouse_event(4, 0, 0, 0, UIntPtr.Zero);
            }, null, 600, System.Threading.Timeout.Infinite)) {
                uint effect;
                int status = DoDragDrop(data, source, 1, out effect);
                return "HRESULT=0x" + status.ToString("X") + " effect=" + effect;
            }
        } finally {
            if (data != null) Marshal.ReleaseComObject(data);
            if (item != IntPtr.Zero) Marshal.FreeCoTaskMem(item);
            OleUninitialize();
        }
    }

}
'@
[void][DropFixture]::SetProcessDPIAware()
if ($HelpersOnly) { return }
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
    $condition = [Windows.Automation.PropertyCondition]::new(
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
    Start-Sleep -Seconds 3
    $edit = [DropFixture]::FindWindowEx($main, $zero, 'Edit', $null)
    if ($edit -eq $zero) { throw 'Unable to find Aqua Frame address edit control.' }
    if ([DropFixture]::SendMessage($edit, 0x000c, $zero, 'aqua://extensions') -eq $zero) {
        throw 'Editing the address failed.'
    }
    if (![DropFixture]::PostMessage($edit, 0x0100, [IntPtr]13, $zero)) {
        throw 'Submitting the address failed.'
    }
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 350
        $manager = [DropFixture]::ManagerWindow($process.Id)
    } until ($manager -ne $zero -or (Get-Date) -gt $deadline)
    if ($manager -eq $zero) { throw 'The Chromium extension manager did not appear.' }
    $screen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    [void][DropFixture]::ShowWindow($manager, 9)
    [void][DropFixture]::SetWindowPos($manager, [IntPtr](-1), 145, 30,
        [Math]::Min(850,$screen.Width-160), [Math]::Min(700,$screen.Height-65), 0x0040)
    Start-Sleep -Seconds 2
    $r = New-Object DropFixture+Rect
    if (![DropFixture]::GetWindowRect($manager, [ref]$r)) { throw 'Unable to locate the manager.' }
    $size = [Drawing.Size]::new($r.Right - $r.Left, $r.Bottom - $r.Top)
    $bmp = [Drawing.Bitmap]::new($size.Width, $size.Height)
    $graphics = [Drawing.Graphics]::FromImage($bmp)
    try {
        $dc = $graphics.GetHdc()
        try { [void][DropFixture]::PrintWindow($manager, $dc, 2) }
        finally { $graphics.ReleaseHdc($dc) }
        $bmp.Save((Join-Path $PWD 'extension-manager.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bmp.Dispose()
    }
    $element = [Windows.Automation.AutomationElement]::FromHandle($manager)
    $toggle = $null
    $dev = Find-Named $element 'Developer mode'
    if ($dev -and $dev.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern, [ref]$toggle)) {
        if ($toggle.Current.ToggleState -eq 'Off') { $toggle.Toggle() }
    } else {
        # The screenshot confirms the switch is at this offset in the fresh,
        # English Chromium window on this Windows runner.
        [void][DropFixture]::SetCursorPos($r.Right - 33, $r.Top + 115)
        [DropFixture]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
        [DropFixture]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 500
    $x = [int](($r.Left + $r.Right) / 2)
    $y = [int](($r.Top + $r.Bottom) / 2)
    Write-Host "Running OLE drag against $x,$y"
    $effect = [DropFixture]::Drag($archive, $x, $y)
    Write-Host "OLE drag result: $effect"
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 350
        $installed = Get-ChildItem -LiteralPath $profile -Filter manifest.json -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match 'Aqua Frame Drag Fixture' } |
            Select-Object -First 1
    } until ($installed -or (Get-Date) -gt $deadline)
    if (!$installed) { throw 'Chromium did not extract the ZIP extension into its profile.' }
} finally {
    if ($main -ne $zero) { [void][DropFixture]::PostMessage($main, 0x0010, $zero, $zero) }
    if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
}

$registered = Get-ChildItem -LiteralPath $profile -File -Recurse |
    Where-Object { $_.Name -in @('Preferences','Secure Preferences') } |
    Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match 'Aqua Frame Drag Fixture' }
if (!$registered) { throw 'Chromium did not register the dropped extension.' }
Write-Host 'The Chromium manager installed and persisted the Manifest V3 extension from a Windows OLE ZIP drop.'

# Photograph the FreeCAD window from OUTSIDE the process.
#
# The whole point is that this does not ask FreeCAD for a picture.
# docs/DeviceAdoption.md section 10 records two instruments that
# answered the "is the composite on screen" question with a plausible
# image that was not the screen: QWidget::grab() does not capture the 3D
# view's GL content, and View3DInventorViewer::saveImage is served by
# the backend's own portable frame dump, so it renders the scene whether
# the composite runs or not -- a composite-off control returned the same
# picture. Only an image of the DESKTOP necessarily went through the
# composite.
#
# CopyFromScreen reads the composited desktop, which is why it can see
# what the in-process calls cannot. It needs the window visible and
# unoccluded, so this raises it first and fails loudly if it cannot.
param(
    [Parameter(Mandatory=$true)][string]$Ready,
    [Parameter(Mandatory=$true)][string]$Out,
    [int]$TimeoutSec = 900
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Shot {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint a, uint b, IntPtr c, uint d);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

# The scene is staged before the marker is written, so waiting on it
# means the shutter opens on a settled view rather than on the pipeline
# still filling. The composite is pipelined and its first frames
# legitimately show the previous image.
$deadline = (Get-Date).AddSeconds($TimeoutSec)
while (-not (Test-Path $Ready)) {
    if ((Get-Date) -gt $deadline) { Write-Output "TIMEOUT waiting for $Ready"; exit 2 }
    Start-Sleep -Milliseconds 500
}

$proc = Get-Process FreeCAD -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { Write-Output "NO FreeCAD window found"; exit 3 }

$h = $proc.MainWindowHandle

# Raise it, and then PROVE it was raised.
#
# CopyFromScreen photographs a screen RECTANGLE, so anything sitting on
# top of the window is what lands in the file -- and a bare
# SetForegroundWindow from a background process is refused by Windows'
# foreground lock without reporting anything useful. That combination
# once produced a perfectly sharp screenshot of an unrelated terminal,
# saved under the name of a render capture: the exact failure this whole
# exercise is about, an instrument answering with a plausible picture
# instead of admitting it could not see. So the foreground is CHECKED
# after raising, and a capture that cannot get its window on top fails
# loudly rather than saving whatever was there.
# SPI_SETFOREGROUNDLOCKTIMEOUT to 0. Windows refuses a foreground change
# from a process the user did not just interact with, and AttachThreadInput
# alone did not reliably beat it here -- four legs in a row came back NOT
# RAISED. Clearing the timeout is the documented way to allow it.
[void][Win32Shot]::SystemParametersInfo(0x2001, 0, [IntPtr]::Zero, 0)
$raised = $false
for ($try = 1; $try -le 6 -and -not $raised; $try++) {
    # A minimize/restore cycle asks the shell to activate the window,
    # which succeeds in cases where SetForegroundWindow alone does not.
    if ($try -gt 2) { [void][Win32Shot]::ShowWindow($h, 6) ; Start-Sleep -Milliseconds 400 }
    [void][Win32Shot]::ShowWindow($h, 9)      # SW_RESTORE
    # AttachThreadInput to the current foreground thread lifts the
    # foreground lock for the duration of the call; without it
    # SetForegroundWindow only flashes the taskbar button.
    $me = [Win32Shot]::GetCurrentThreadId()
    $fg = [Win32Shot]::GetForegroundWindow()
    $ft = [Win32Shot]::GetWindowThreadProcessId($fg, [IntPtr]::Zero)
    [void][Win32Shot]::AttachThreadInput($me, $ft, $true)
    [void][Win32Shot]::BringWindowToTop($h)
    [void][Win32Shot]::SetForegroundWindow($h)
    [void][Win32Shot]::AttachThreadInput($me, $ft, $false)
    # WScript.Shell AppActivate is the shell's own activation path and
    # succeeds in cases the raw API calls do not -- notably when this
    # script is itself running nested inside another PowerShell, which
    # is how the leg driver invokes it. Tried after the API attempt
    # rather than instead of it, since it needs a process id.
    if ([Win32Shot]::GetForegroundWindow() -ne $h) {
        try {
            $wsh = New-Object -ComObject WScript.Shell
            [void]$wsh.AppActivate($proc.Id)
        } catch { }
    }
    Start-Sleep -Seconds 2
    if ([Win32Shot]::GetForegroundWindow() -eq $h) { $raised = $true }
}
# Not raised is NOT fatal any more, because the usual reason is that a
# person is using the machine and Windows is right to refuse a
# background process the foreground. Stealing focus out from under
# someone mid-keystroke to take a screenshot is worse than the problem.
#
# So fall back to PrintWindow with PW_RENDERFULLCONTENT, which asks the
# window to paint itself into our bitmap and needs no foreground at all.
# The catch is real and documented (DeviceAdoption.md sec 10): this
# class of call is what QWidget::grab uses, and on macOS it returns the
# widget with NO GL content -- a confident picture of nothing. It is NOT
# trusted here on the strength of an argument. It is trusted only
# because the composite-OFF control run comes back BLACK through this
# same path while the composite-ON run comes back with the scene: an
# instrument that discriminates between the two cases is seeing the
# thing that differs between them. If a control ever comes back
# looking correct, this path is blind and its pictures mean nothing.
$mode = "screen"
if (-not $raised) {
    $mode = "printwindow"
    Write-Output "NOT RAISED (someone is using the machine) -- falling back to PrintWindow, which needs no foreground. Trust it only against the composite-off control."
}
Start-Sleep -Seconds 2                         # let it settle either way

# Prefer the 3D SUBWINDOW's rect, which the probe wrote into the marker
# -- that crops the picture to the thing being judged and keeps the rest
# of the desktop out of an image that gets shared. Fall back to the whole
# window when the marker carries no rect.
$x = 0; $y = 0; $w = 0; $hgt = 0
$fields = (Get-Content $Ready -Raw).Trim() -split '\s+'
if ($fields.Count -eq 4 -and ($fields[2] -as [int]) -gt 0) {
    $x = [int]$fields[0]; $y = [int]$fields[1]
    $w = [int]$fields[2]; $hgt = [int]$fields[3]
    Write-Output "cropping to the 3D subwindow $w x $hgt at $x,$y"
}
else {
    $r = New-Object Win32Shot+RECT
    if (-not [Win32Shot]::GetWindowRect($h, [ref]$r)) { Write-Output "NO window rect"; exit 4 }
    $x = $r.Left; $y = $r.Top
    $w = $r.Right - $r.Left
    $hgt = $r.Bottom - $r.Top
    Write-Output "no subwindow rect in the marker -- capturing the whole window"
}
if ($w -le 0 -or $hgt -le 0) { Write-Output "EMPTY rect"; exit 5 }

if ($mode -eq "screen") {
    $bmp = New-Object System.Drawing.Bitmap $w, $hgt
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
    $g.Dispose()
}
else {
    # PrintWindow paints the WHOLE window, so it is captured at window
    # size and then cropped to the same rect the screen path uses --
    # window-relative, since PrintWindow's origin is the window and
    # CopyFromScreen's is the desktop.
    $wr = New-Object Win32Shot+RECT
    if (-not [Win32Shot]::GetWindowRect($h, [ref]$wr)) { Write-Output "NO window rect"; exit 4 }
    $ww = $wr.Right - $wr.Left
    $wh = $wr.Bottom - $wr.Top
    $full = New-Object System.Drawing.Bitmap $ww, $wh
    $fg = [System.Drawing.Graphics]::FromImage($full)
    $hdc = $fg.GetHdc()
    $okp = [Win32Shot]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
    $fg.ReleaseHdc($hdc)
    $fg.Dispose()
    if (-not $okp) { $full.Dispose(); Write-Output "PrintWindow FAILED"; exit 7 }
    $rx = $x - $wr.Left
    $ry = $y - $wr.Top
    if ($rx -lt 0) { $rx = 0 }
    if ($ry -lt 0) { $ry = 0 }
    if ($rx + $w -gt $ww) { $w = $ww - $rx }
    if ($ry + $hgt -gt $wh) { $hgt = $wh - $ry }
    $rect = New-Object System.Drawing.Rectangle $rx, $ry, $w, $hgt
    $bmp = $full.Clone($rect, $full.PixelFormat)
    $full.Dispose()
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "SHOT $Out  ${w}x${hgt} at $x,$y ($mode)"

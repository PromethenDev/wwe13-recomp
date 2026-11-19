param([string]$Shot = "", [string[]]$Keys = @(), [int]$HoldMs = 120, [int]$GapMs = 600)
# Drives the wwe13 test window: optional key presses (virtual-key names) then an optional screenshot of the window.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class GC13 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  public struct RECT { public int L, T, R, B; }
}
"@
$p = Get-Process wwe13 -ErrorAction SilentlyContinue | ? { $_.Path -like ((Resolve-Path "$PSScriptRoot\..\..\..").Path + '\*') } | select -First 1
if (-not $p) { "no game"; exit 1 }
$h = $p.MainWindowHandle
# A minimized game window gets no input and draws nothing: restore it without activating it.
if ([GC13]::IsIconic($h)) { [GC13]::ShowWindow($h, 4) | Out-Null; Start-Sleep -Milliseconds 800; "restored minimized window" }
# Keys are posted to the game window only (never global input), so nothing reaches other windows.
# WM_SETFOCUS tells the game's keyboard driver it may read keys, without taking the real focus.
if ($Keys.Count) { [GC13]::PostMessage($h, 0x0007, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 100 }
$vk = @{ Enter=0x0D; Esc=0x1B; Escape=0x1B; Up=0x26; Down=0x28; Left=0x25; Right=0x27; Space=0x20; Tab=0x09; Shift=0x10; Ctrl=0x11 }
foreach ($k in $Keys) {
  if ($k -match '^wait(\d+)$') { Start-Sleep -Milliseconds ([int]$matches[1]); continue }
  $code = if ($vk.ContainsKey($k)) { $vk[$k] } else { [int][char]$k.ToUpper() }
  $sc = [byte][GC13]::MapVirtualKey([uint32]$code, 0)
  $ext = if ($code -ge 0x25 -and $code -le 0x28) { 1 } else { 0 }
  $lp = 1 -bor ([int]$sc -shl 16) -bor ($ext -shl 24)
  [GC13]::PostMessage($h, 0x0100, [IntPtr]$code, [IntPtr]$lp) | Out-Null; Start-Sleep -Milliseconds $HoldMs
  [GC13]::PostMessage($h, 0x0101, [IntPtr]$code, [IntPtr]([int64]$lp -bor 0xC0000000L)) | Out-Null; Start-Sleep -Milliseconds $GapMs
}
if ($Shot) {
  # PrintWindow reads only the game window, even when another window covers it.
  $r = New-Object GC13+RECT; [GC13]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc(); [GC13]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc)
  $small = New-Object System.Drawing.Bitmap $bmp, ([int]($w * 0.6)), ([int]($hh * 0.6))
  $small.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png); "shot=$Shot ${w}x$hh"
}

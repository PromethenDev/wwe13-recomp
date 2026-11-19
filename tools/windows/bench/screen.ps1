# Dot-source: screen probes on the test game window (PrintWindow, never the desktop).
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Scr13 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  public struct RECT { public int L, T, R, B; }
}
"@
function Get-GameBitmap {
  $p = Get-Process wwe13 -ErrorAction SilentlyContinue | ? { $_.Path -like ((Resolve-Path "$PSScriptRoot\..\..\..").Path + '\*') } | select -First 1
  if (-not $p) { return $null }
  $h = $p.MainWindowHandle; if ($h -eq [IntPtr]::Zero) { return $null }
  $r = New-Object Scr13+RECT; [Scr13]::GetWindowRect($h, [ref]$r) | Out-Null
  if (($r.R - $r.L) -lt 200 -or ($r.B - $r.T) -lt 200) { return $null }
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc(); [Scr13]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $g.Dispose()
  return $bmp
}
# Mean R,G,B of a rectangle given in fractions of the 778x455 reference screenshots.
function Get-Mean($bmp, [double]$x0, [double]$y0, [double]$x1, [double]$y1) {
  $W = $bmp.Width; $H = $bmp.Height; $r = 0; $g = 0; $b = 0; $n = 0
  for ($y = [int]($y0 * $H); $y -le [int]($y1 * $H); $y += 2) {
    for ($x = [int]($x0 * $W); $x -le [int]($x1 * $W); $x += 3) { $c = $bmp.GetPixel($x, $y); $r += $c.R; $g += $c.G; $b += $c.B; $n++ }
  }
  return @([int]($r / $n), [int]($g / $n), [int]($b / $n))
}
# 'notice'  : the white-framed ATTENTION box after START.
# 'mainmenu': the red stripe above the main menu's right-hand list.
function Test-Screen([string]$name) {
  $bmp = Get-GameBitmap; if (-not $bmp) { return $false }
  try {
    switch ($name) {
      'notice'   { $bar = Get-Mean $bmp 0.15 0.30 0.40 0.308; $box = Get-Mean $bmp 0.15 0.345 0.40 0.353
                   return ($bar[0] -gt 120 -and $box[0] -lt 25) }
      'mainmenu' { $m = Get-Mean $bmp 0.56 0.356 0.95 0.372; return ($m[0] -gt 60 -and $m[0] -gt 4 * ($m[1] + 1)) }
    }
  } finally { $bmp.Dispose() }
  return $false
}

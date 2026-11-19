param([int]$Seconds = 15, [int]$Top = 40, [int]$ThreadId = 0, [string]$Out = "")
# Leaf-PC sampler for the hottest wwe13 thread (or -ThreadId). Resolves RVAs with wwe13.map.
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic; using System.Diagnostics;
public static class Samp {
  [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int a, bool i, int id);
  [DllImport("kernel32.dll")] static extern int SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern int ResumeThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("winmm.dll")] static extern int timeBeginPeriod(int p);
  public static Dictionary<ulong,int> Run(int tid, int ms) {
    var d = new Dictionary<ulong,int>();
    IntPtr h = OpenThread(0x0008 | 0x0002 | 0x0040, false, tid);
    if (h == IntPtr.Zero) return d;
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
    timeBeginPeriod(1);
    var sw = Stopwatch.StartNew();
    while (sw.ElapsedMilliseconds < ms) {
      for (int i = 0; i < 1232; i += 8) Marshal.WriteInt64(ctx, i, 0);
      Marshal.WriteInt32(ctx, 0x30, 0x100001); // CONTEXT_CONTROL
      if (SuspendThread(h) != -1) {
        if (GetThreadContext(h, ctx)) { ulong rip = (ulong)Marshal.ReadInt64(ctx, 0xF8); int c; d.TryGetValue(rip, out c); d[rip] = c + 1; }
        ResumeThread(h);
      }
      System.Threading.Thread.Sleep(1);
    }
    Marshal.FreeHGlobal(raw); CloseHandle(h); return d;
  }
}
"@
$p = Get-Process wwe13 | ? { $_.Path -like ((Resolve-Path "$PSScriptRoot\..\..\..").Path + '\*') } | select -First 1
if (-not $ThreadId) {
  $t0 = @{}; $p.Threads | % { $t0[$_.Id] = $_.TotalProcessorTime.TotalMilliseconds }; Start-Sleep 2; $p.Refresh()
  $ThreadId = ($p.Threads | sort { $_.TotalProcessorTime.TotalMilliseconds - $t0[$_.Id] } -Descending | select -First 1).Id
}
$base = [uint64]$p.MainModule.BaseAddress.ToInt64(); $size = [uint64]$p.MainModule.ModuleMemorySize
"thread=$ThreadId base={0:X}" -f $base
$d = [Samp]::Run($ThreadId, $Seconds * 1000)
$total = ($d.Values | measure -Sum).Sum
# map: lines "  0001:00001234  sym  00000001400xxxxx f obj"
$syms = New-Object System.Collections.Generic.List[object]
foreach ($line in [IO.File]::ReadLines("$(Resolve-Path "$PSScriptRoot\..\..\..\out\run")\wwe13.map")) {
  if ($line -match '^\s+[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s') { $syms.Add([pscustomobject]@{ a = [Convert]::ToUInt64($matches[2],16); n = $matches[1] }) }
}
$arr = $syms | sort a; $addrs = [uint64[]]($arr | % a); $names = [string[]]($arr | % n)
$pref = [uint64]0x140000000
$mods = $p.Modules | ? { $_.ModuleName -ne 'wwe13.exe' } | % { [pscustomobject]@{ n = $_.ModuleName; b = [uint64]$_.BaseAddress.ToInt64(); e = [uint64]$_.BaseAddress.ToInt64() + [uint64]$_.ModuleMemorySize } }
$agg = @{}
foreach ($k in $d.Keys) {
  $n = "external"
  foreach ($m in $mods) { if ($k -ge $m.b -and $k -lt $m.e) { $n = "[" + $m.n + "]+" + ('{0:X}' -f [uint64](($k - $m.b) -band 0xFFFFFFF0)); break } }
  if ($k -ge $base -and $k -lt $base + $size) {
    $va = $k - $base + $pref; $i = [Array]::BinarySearch($addrs, $va); if ($i -lt 0) { $i = (-bnot $i) - 1 }
    $n = if ($i -ge 0) { $names[$i] } else { "?" }
  }
  $agg[$n] = $agg[$n] + $d[$k]
}
"samples=$total"
$res = $agg.GetEnumerator() | sort Value -Descending | select -First $Top | % { "{0,6:N2}%  {1}" -f ($_.Value * 100.0 / $total), $_.Key }
$res
if ($Out) { $res | Set-Content $Out }

param([string]$Name = "bench", [int64]$Mask = 0, [hashtable]$EnvVars = @{}, [int]$MatchSeconds = 60, [string[]]$Extra = @(), [switch]$KeepRunning)
# Reproducible one-on-one benchmark: title -> notice -> main menu (screen-detected), Exhibition > 1v1 >
# Normal, two superstars, match with entrances. The optional CPU affinity mask applies to the match only.
$S = $PSScriptRoot
. "$S\screen.ps1"
$c = "$S\gamectl.ps1"
foreach ($k in $EnvVars.Keys) { Set-Item "Env:$k" $EnvVars[$k] }
& "$S\run.ps1" -Name $Name -Keep -Extra $Extra | Out-Null
Start-Sleep 5
$p = Get-Process wwe13 | ? { $_.Path -like ((Resolve-Path "$PSScriptRoot\..\..\..").Path + '\*') } | select -First 1
function Fail($why) { & $c -Shot "$(Resolve-Path "$PSScriptRoot\..\..\..\out\run")\logs\$Name-fail.png" | Out-Null; $p.CloseMainWindow() | Out-Null; Start-Sleep 5; if (-not $p.HasExited) { $p.Kill() }; "$Name FAILED: $why"; exit 1 }
$t0 = Get-Date; $ok = $false
while (((Get-Date) - $t0).TotalSeconds -lt 150) { & $c -Keys Esc, wait2500 | Out-Null; if (Test-Screen 'notice') { $ok = $true; break } }
if (-not $ok) { Fail "no start notice" }
$boot = ((Get-Date) - $t0).TotalSeconds
& $c -Keys L | Out-Null
$t1 = Get-Date; $ok = $false
while (((Get-Date) - $t1).TotalSeconds -lt 90) { Start-Sleep 2; if (Test-Screen 'mainmenu') { $ok = $true; break } }
if (-not $ok) { Fail "no main menu" }
$menuLoad = ((Get-Date) - $t1).TotalSeconds
Start-Sleep 2
& $c -Keys L, wait1500, L, wait8000, L, wait8000 | Out-Null        # Exhibition -> 1v1 -> Normal
& $c -Keys L, wait3000, Right, Right, Right, L, wait4000 | Out-Null
& $c -Keys L, wait3000, Right, Right, Right, Right, Right, L, wait3000, L, wait4000 | Out-Null
& $c -Keys L, wait6000 | Out-Null                                  # manager prompt: OK
& $c -Keys L, wait6000 | Out-Null                                  # -> match setup screen
& $c -Shot "$(Resolve-Path "$PSScriptRoot\..\..\..\out\run")\logs\$Name-pre.png" | Out-Null
if ($Mask) { $p.ProcessorAffinity = [IntPtr]$Mask }
$startTime = Get-Date
& $c -Keys L | Out-Null                                             # start match (entrances on)
$cpu0 = $p.TotalProcessorTime
if ($KeepRunning) { "match started, game left running (pid $($p.Id))"; exit 0 }
Start-Sleep $MatchSeconds
$p.Refresh(); $cpu = ($p.TotalProcessorTime - $cpu0).TotalSeconds / $MatchSeconds * 100
& $c -Shot "$(Resolve-Path "$PSScriptRoot\..\..\..\out\run")\logs\$Name.png" | Out-Null
$p.CloseMainWindow() | Out-Null; Start-Sleep 6; if (-not $p.HasExited) { $p.Kill() }
foreach ($k in $EnvVars.Keys) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
$log = "$(Resolve-Path "$PSScriptRoot\..\..\..\out\run")\logs\$Name.log"
$t = $startTime.AddSeconds(10).ToString("HH:mm:ss")
$rows = Select-String $log -Pattern '\[wwe13-flip\] flips=(\d+) late=(\d+)' | ? { $_.Line.Substring(12, 8) -ge $t } |
  % { [pscustomobject]@{ f = [int]$_.Matches[0].Groups[1].Value; l = [int]$_.Matches[0].Groups[2].Value } }
$fl = $rows | % f
$res = "{0}: boot={1:N0}s menu={2:N0}s seconds={3} avg_flips={4:N2} min={5} p5={6} late_total={7} cpu={8:N0}% fence={9}" -f $Name, $boot, $menuLoad, $rows.Count,
  ($fl | measure -Average).Average, ($fl | measure -Minimum).Minimum, (($fl | sort)[[int]($fl.Count * 0.05)]),
  ($rows | % l | measure -Sum).Sum, $cpu, [bool](Select-String $log -Pattern 'wwe13-fence' -Quiet)
$res
Add-Content "$(Resolve-Path "$PSScriptRoot\..\..\..\out\run")\logs\bench-history.txt" ("{0:yyyy-MM-dd HH:mm} mask=0x{1:X} {2}" -f (Get-Date), $Mask, $res)

param([string]$Name = "run", [int]$Seconds = 60, [string[]]$Extra = @(), [switch]$Keep)
$r = "$(Resolve-Path "$PSScriptRoot\..\..\..\out\run")"
$log = "$r\logs\$Name.log"
Remove-Item $log -ErrorAction SilentlyContinue
if (-not $env:WWE13_F24_PIXEL_RATE) { $env:WWE13_F24_PIXEL_RATE = "1" }
$a = @("`"$r\WWE 13`"", "--async_shader_compilation=false", "--vulkan_async_skip_incomplete_frames=false",
  "--fullscreen=false", "--vulkan_allow_present_mode_immediate=false", "--ignore_offset_for_ranged_allocations=true",
  "--log_verbose=false", "--mnk_mode=true", "--log_file=`"$log`"", "--user_data_root=`"$r\userdata`"", "--debug_ui=false") + $Extra
$p = Start-Process "$r\wwe13.exe" -ArgumentList $a -WorkingDirectory $r -PassThru
"pid=$($p.Id) log=$log"
if ($Keep) { return }
Start-Sleep $Seconds
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep 6 }
if (-not $p.HasExited) { $p.Kill() }
"exit=$($p.ExitCode)"

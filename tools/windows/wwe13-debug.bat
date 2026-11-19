@echo off
title WWE 13 (debug)
rem Developer launcher: enables debug UI/hotkeys and the original read-only telemetry.
rem Usage: wwe13-debug.bat ["C:\path\to\WWE 13"] [extra flags...]
rem Keep in sync with wwe13.bat for the game folder, userdata and gameplay flags.
setlocal
set "HERE=%~dp0"
set "GAME=%~1"
if "%GAME%"=="" set "GAME=%HERE%WWE 13"
if not exist "%GAME%\default.xex" (
  echo Game folder not found.
  echo Put the game in the "WWE 13" folder next to this launcher or pass its folder as the first argument.
  pause
  exit /b 1
)
if not exist "%HERE%logs" mkdir "%HERE%logs"
if not exist "%HERE%userdata" mkdir "%HERE%userdata"
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%t"
set "LOG=%HERE%logs\wwe13-%STAMP%.log"
rem Keyboard controls are enabled; mouse input remains off (cursor is not captured).
rem No tearing: never use the immediate (no-vsync) present mode; the SDK then picks mailbox (tear-free,
rem non-blocking). Do NOT force vsync in the NVIDIA Control Panel: strict FIFO vsync blocks the GPU thread.
rem Windowed instead of fullscreen: add --fullscreen=false. Quit with Alt+F4.
rem Per-second frame/vblank/present counts in the log ([rate-census] lines).
set REX_DEBUG_UI=true
set WWE13_RATE_CENSUS=1
rem Cheaper depth conversion (2026-09-26): shade once per pixel instead of once per MSAA sample (-15% GPU at
rem 1440p, same image). To go back: set WWE13_F24_PIXEL_RATE=0 before starting.
if not defined WWE13_F24_PIXEL_RATE set WWE13_F24_PIXEL_RATE=1
echo Starting WWE 13 (debug)...

rem Read-only telemetry next to the log (built-in tools only; stopped when the game exits):
rem   sys-*.txt  power plan, AC/battery, CPU, display mode, NVIDIA driver (one snapshot)
rem   cpu-*.csv  typeperf every second: CPU performance %% (below 100 = throttled) and utility
rem   gpu-*.csv  nvidia-smi every second: clocks, power, temperature, utilisation, slowdown reasons
powershell -NoProfile -Command "$o=@(); $o+='power plan: '+(powercfg /getactivescheme); $b=Get-CimInstance Win32_Battery; $o+='battery status (2=AC): '+$b.BatteryStatus+' charge: '+$b.EstimatedChargeRemaining; $o+='cpu: '+(Get-CimInstance Win32_Processor).Name; Get-CimInstance Win32_VideoController | ForEach-Object { $o+='gpu: '+$_.Name+' driver '+$_.DriverVersion+' mode '+$_.CurrentHorizontalResolution+'x'+$_.CurrentVerticalResolution+' @ '+$_.CurrentRefreshRate+' Hz' }; $o | Set-Content '%HERE%logs\sys-%STAMP%.txt'" >nul 2>&1
start "" /b typeperf "\Processor Information(_Total)\%% Processor Performance" "\Processor Information(_Total)\%% Processor Utility" -si 1 -f CSV -o "%HERE%logs\cpu-%STAMP%.csv" -y >nul 2>&1
set "TR=clocks_event_reasons.active"
nvidia-smi --query-gpu=%TR% --format=csv >nul 2>&1 || set "TR=clocks_throttle_reasons.active"
start "" /b nvidia-smi --query-gpu=timestamp,pstate,clocks.gr,clocks.mem,power.draw,temperature.gpu,utilization.gpu,%TR% --format=csv -l 1 -f "%HERE%logs\gpu-%STAMP%.csv" >nul 2>&1
"%HERE%wwe13.exe" "%GAME%" --async_shader_compilation=false --vulkan_async_skip_incomplete_frames=false --fullscreen=true --vulkan_allow_present_mode_immediate=false --ignore_offset_for_ranged_allocations=true --log_verbose=false --mnk_mode=true --log_file="%LOG%" --user_data_root="%HERE%userdata" --debug_ui=true %2 %3 %4 %5 %6 %7 %8 %9
set "RC=%ERRORLEVEL%"
taskkill /im typeperf.exe /f >nul 2>&1
taskkill /im nvidia-smi.exe /f >nul 2>&1
echo wwe13.exe exited with code %RC%
endlocal

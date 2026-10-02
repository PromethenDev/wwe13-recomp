@echo off
title WWE 13
rem F11 in game: fullscreen <-> windowed.
rem WWE '13 recomp launcher (Windows x64, Vulkan).
rem Usage: wwe13.bat ["C:\path\to\WWE 13"] [extra flags...]
rem The game folder must hold default.xex + default.xexp (title update 2.0.1.0) and the game files.
rem Saves and DLC: the "userdata" folder next to this file (copy both subfolders of ~/.local/share/wwe13 there).
rem Kept out of Documents on purpose: OneDrive redirects Documents and would upload the DLC.
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
rem Release profile: ignore inherited developer UI and profiling switches.
set REX_DEBUG_UI=false
set WWE13_RATE_CENSUS=
set WWE13_FRAME_TIME=
set WWE13_PERF_OVERLAY=
set WWE13_THREAD_PROFILE=
set WWE13_THREAD_PROFILE_HZ=
set WWE13_THREAD_PROFILE_OUT=
set WWE13_LOCK_OWNER_SAMPLE=
rem Cheaper depth conversion (2026-09-26): shade once per pixel instead of once per MSAA sample (-15% GPU at
rem 1440p, same image). To go back: set WWE13_F24_PIXEL_RATE=0 before starting.
if not defined WWE13_F24_PIXEL_RATE set WWE13_F24_PIXEL_RATE=1
echo Starting WWE 13...
"%HERE%wwe13.exe" "%GAME%" --async_shader_compilation=false --vulkan_async_skip_incomplete_frames=false --fullscreen=true --vulkan_allow_present_mode_immediate=false --ignore_offset_for_ranged_allocations=true --log_verbose=false --mnk_mode=true --log_file="%LOG%" --user_data_root="%HERE%userdata" --debug_ui=false %2 %3 %4 %5 %6 %7 %8 %9
set "RC=%ERRORLEVEL%"
echo wwe13.exe exited with code %RC%
endlocal

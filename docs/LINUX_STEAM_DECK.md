# WWE '13 on Linux and Steam Deck

**Status: not tested on Linux or a Steam Deck by the team yet.** This page comes from a code review,
the measurements made on a Windows PC with an AMD integrated GPU (the closest stand-in we have), and the
same guide written for WWE 2K15 PC. Reports are welcome, whether it works or not: see the end of the page.

You need your own copy of the game (Xbox 360, title update 2.0.1.0). Nothing here links to game files.

## Two ways to play on Linux

| | Native Linux build (`Linux-x86_64/`) | PC port (`wwe13_native.exe`) through Proton |
|---|---|---|
| What it is | The v1.x release line: the game compiled for Linux, the Xbox 360 GPU emulated on Vulkan | The newer port: the game draws through its own Direct3D calls (re:Blue + plume), no GPU emulation |
| Speed | Heavier on the GPU, made for dedicated graphics cards | Lighter: 60 fps in matches on an AMD integrated GPU at 720p |
| Steam Deck | Works in Desktop mode; Game Mode needs a non-Steam shortcut to `wwe13-launcher` | **Recommended**: add the launcher to Steam and run it with Proton |

The rest of this page is about the PC port through Proton, which is the better choice on a Steam Deck and on
PCs with integrated graphics.

## Set it up (Steam Deck or any Linux PC with Steam)

1. Extract the port to a folder whose path contains only plain letters and numbers (no accents), for example
   `~/Games/WWE13/`. Put your game folder as `WWE 13/` beside the launcher, as on Windows.
2. Steam → **Add a Non-Steam Game** → choose `WWE13 Launcher.exe`.
3. Its **Properties → Compatibility**: tick *Force the use of a specific Steam Play compatibility tool* and choose
   **Proton Experimental** (or a recent Proton-GE).
4. Its **Controller** settings: choose the **Gamepad** layout (or *Gamepad with Mouse Trackpad*). The default
   *Desktop* / *Keyboard and Mouse* layout turns the Deck's buttons into keys and mouse clicks, so neither the
   launcher nor the game sees a controller.
5. Start it. In the launcher's **Settings**, keep:
   - **Graphics API: Automatic.** It starts the Vulkan renderer (straight to Mesa's RADV driver). If Vulkan can't
     start it tries Direct3D 12 (vkd3d-proton) and then Direct3D 11 (DXVK) by itself.
   - **Resolution: the recommended one** (the default, or *Use Recommended for This PC*). On the Deck the
     game renders at 720p, the Xbox 360's resolution, and keeps it even when docked. On a stronger Linux PC,
     choose up to 4K.
   - **Display: Fullscreen.**
6. Press **Play**. To skip the launcher next time, tick *Start straight away next
   time*. Hold Shift (or start the launcher with `--settings`) to get it back.

To quit, use **EXIT** in the game's main menu (where the Xbox LIVE shop was). The game closes and Steam
returns to its library.

## First start

All three programs (Vulkan, Direct3D 12, Direct3D 11) have the game's shaders built in, so the first start
doesn't convert anything: the menus come up as fast as on later starts. The first match can still hitch a little
while the graphics driver builds its pipelines. They are saved in `cache/` beside the program and reused
afterwards. Don't copy a cache from another PC. Under Proton, vkd3d-proton and DXVK also translate the Direct3D
shaders once and keep their own cache.

## Steam Deck tips

- **Refresh rate (Steam Deck OLED):** the game runs at 60 fps in matches and 30 fps in entrances. At the OLED's
  90 Hz the picture can judder. Set the refresh rate to **60 Hz** in the Quick Access menu (… button) →
  Performance. The LCD model already runs at 60 Hz.
- **Typing names** (Create a Superstar, Create a Story, …): open Steam's on-screen keyboard with **Steam + X**.
  When the game's own on-screen keyboard is open, letters, Space, Backspace and Enter typed there should go
  straight into the name, as with a real keyboard on Windows (not checked on a Deck yet).
- **Frame limit / TDP:** leave the frame limit off. The game sets its own pace.
- The launcher's text is small on the 7" screen. The touch screen works as a mouse, and the controller moves the
  focus: D-pad or stick to move, A to choose.

## Avoid on Linux

- **Graphics API: Emulated (`wwe13.exe`).** It emulates the Xbox 360 GPU and is much heavier. On drivers without
  the fragment-shader-interlock Vulkan extension, its fallback path can draw split images during entrances.
- **Paths with accents or other non-ASCII letters.** This is untested, and some code converts paths to plain
  text. A path such as `Z:\home\deck\Games\WWE13` is fine.

## Measured on the stand-in

Windows 11, AMD Radeon integrated GPU, 720p (1280×720), Vulkan renderer:

| Case | Frame rate | Worst frame |
|---|---|---|
| 1v1 match | 59.2-59.8 fps | under 25 ms |

The Direct3D 12 and 11 programs also hold 60 fps in matches on that GPU (16.7 ms median frame). The Steam
Deck's GPU (RDNA 2, 8 compute units) is stronger than the stand-in's. Its CPU (Zen 2, 4 cores / 8 threads at
lower clocks) is the unknown.

## What could go wrong under Proton (code review)

| Area | What the port does | What to check |
|---|---|---|
| Shaders | Built into the programs (587 for Vulkan) | The log says "Native shaders: 587 built into the executable". A line "not in the built-in cache" names a shader the game asked for that isn't there: please report it. |
| GPU | Vulkan 1.2 through plume (buffer device address, descriptor indexing) | RADV has both. With two GPUs, pick one in the launcher's Settings. |
| Window | SDL window, fullscreen | Fine under gamescope. The 16:9 picture is letterboxed on the Deck's 16:10 screen. |
| Controller | SDL game controller (Steam Input shows an Xbox pad) | Use the Gamepad layout (see above). |
| Audio | SDL audio through Wine (PipeWire / PulseAudio) | If there is no sound, send the log. |
| Saves and settings | `userdata/`, `logs/` and the settings beside the launcher | The folder must be writable (not inside a read-only Steam library). |
| Crash reports | `crash_*.txt` through Windows' crash handling | It may be partly empty under Wine. The log file is the useful one. |

## Reporting a problem from Linux

Press **Save a Bug Report** in the launcher, or send the latest `logs/wwe13-*.log`. Also tell us:

- the Proton version (or the Wine, DXVK and vkd3d-proton versions);
- the distribution, or Steam Deck LCD / OLED;
- your GPU (`vulkaninfo --summary`).

The first lines of the log name the Vulkan GPU and the chosen graphics API. If the picture stays black, they
also show how far the shader conversion went.

# WWE '13 Recomp

An unofficial static recompilation of the **Xbox 360 version of WWE '13** for 64-bit Windows and Linux. The original PowerPC game code is translated ahead of time to C++ and built into a native executable using the ReXGlue SDK; this is not an Xbox 360 emulator.

## Before you download

You must own a legally obtained Xbox 360 copy of WWE '13 and supply the game files yourself. This project does **not** include the retail `default.xex`, title-update file, game assets, audio, video, DLC, or saves. The executable is a compiled recompilation derived from the game; it is not a substitute for supplying the required game data. See [DISCLAIMER.md](DISCLAIMER.md).

## Required game files

- **Base game** files from your own copy (a game folder, or your own disc image `.iso`).
- **Title update 2.0.1.0** (`default.xexp` and its data files). It is required: the recompilation was built from the updated game, so the game refuses to start without it and tells you why.
- **DLC packs** are optional.

## What you need

- **Windows 10 or 11 (64-bit)**, or **Linux** (glibc 2.38 or newer, e.g. Ubuntu 24.04, with GTK3; on Wayland the game window runs through XWayland, installed by default on most distributions).
- **CPU**: a 64-bit processor with AVX2 (most PCs from 2013 onward: Intel Haswell / AMD Ryzen or newer).
- **Graphics**: a graphics card with up-to-date Vulkan drivers. On integrated graphics, choose 480p.
- About 10 GB of free space for the game files.

## Install and play

1. Download `WWE13-Recomp-v1.0.zip` and extract it to a normal folder such as Documents or Desktop (not `C:\Program Files`).
2. Start **`WWE13 Launcher.exe`** (Linux: `Linux-x86_64/wwe13-launcher`).
3. In **Game files**, do one of the following:
   - put your game folder in the `WWE 13` folder beside the launcher, or
   - press **Find Automatically** to search this PC, or
   - press **Choose a Folder**, or **Use a Disc Image** to extract your own `.iso` once.
4. Add the title update (and DLC packs, if you have them) with **Add TU or DLC**. The launcher accepts the package files you have and installs them where the game expects them.
5. Press **Play**. The very first start takes a few extra seconds while the game prepares its graphics; after that it starts normally.

The launcher checks every file and shows **Game files ready** when everything is correct.

Backup ways to start: `wwe13-enhanced.exe` (the previous launcher) or `wwe13.bat` (starts the game directly).

```text
WWE13-Recomp-x64/
├── WWE13 Launcher.exe          # start here (Windows)
├── wwe13.exe                   # the game (started by the launcher)
├── wwe13.toml                  # keyboard keys (change them on the launcher's Controls page)
├── wwe13-enhanced.exe          # previous launcher (backup)
├── wwe13.bat                   # backup: start the game directly (Classic settings)
├── wwe13-debug.bat             # extra diagnostic logs, only if asked for them in a bug report
├── avcodec-58.dll, avutil-56.dll
├── shader-cache/               # pre-built graphics data so the game starts quickly
├── Linux-x86_64/               # Linux launcher + game
├── README.md, DISCLAIMER.md, LICENSE, SHA256SUMS
├── licenses/, third-party-licenses/, ffmpeg-source-*.zip
├── WWE 13/                     # your game folder (default.xex + default.xexp + data)
├── userdata/                   # saves, DLC and settings (created on first run)
└── logs/                       # game logs (created on first run)
```

## Launcher settings

**Use Recommended for This PC** picks settings for your graphics card. You can also choose:

- **Resolution** – the sharpness the game renders at. 480p is the fastest (handhelds and integrated graphics); 720p is the original Xbox 360 sharpness; 1440p needs a strong graphics card.
- **Frame rate** – *Classic* (60 in matches, 30 in entrances and cutscenes, like the console), *60 Everywhere* (needs a strong PC), or *30 Locked* (steady, best for laptops and handhelds).
- **Display** – fullscreen or windowed (press **F11** in the game to switch at any time); **Graphics card** – for PCs with two GPUs.
- **Start straight away next time** – skip the launcher on later starts. Hold **Shift** while it starts to come back to the settings.
- **Music** – drop your own songs in to use them in the game's custom music.
- **Saves** – **Back Up Now** and **Restore** your saves with one click.

## Controls

An Xbox-compatible controller is recommended. The keyboard also works as a controller: the default keys follow modern WWE 2K games on PC - **WASD** move, **J** strike (X), **L** grapple (A; also confirms in menus), **K** Irish whip / pin (B; back in menus), **Space** signature / finisher (Y), **1** reversal (RT), **Left Shift** run (LT), **U** pick up / in-out of the ring (LB), **Left Ctrl** limb targeting (RB), arrow keys taunts (D-pad), **Esc** pause. See and change every key on the launcher's **Controls** page. When the game's on-screen keyboard is open (for example, naming a story), you can type with your PC keyboard and paste with **Ctrl+V**. Names are upper case, like on the console.

## Saves and DLC

The game keeps saves, DLC and settings in `userdata/` beside the launcher. It is local to this installation; use **Back Up Now** on the launcher's **Saves** page (or copy the folder) before moving or replacing a release.

## Known issues

- **1080p** is hidden in the launcher for now: at that resolution the crowd can show glitches.
- Some shadow and lighting differences remain compared to the console.
- **480p**: the reversal button prompt that appears over a wrestler is drawn too far up and to the left. 720p and higher are not affected.
- **60 Everywhere**: entrances and cutscenes run in slow motion whenever your graphics card cannot keep 60 fps, because the game's timing follows the frame rate. Use **Classic** (the default) if that happens.
- Extract the release to a normal folder, not `C:\Program Files`: the launcher keeps your saves and settings next to itself (it warns you if the folder is read-only).
- Only the exact title update 2.0.1.0 is supported; other regions, game revisions or title updates are refused.
- In menus, the modern keyboard keys confirm with **L** and go back with **K** (they follow the controller's A and B buttons).

## Reporting bugs

Press **Save a Bug Report (logs + settings)** at the bottom of the launcher. It saves a `wwe13-bug-report-<date>.zip` next to the launcher with the latest game logs, your launcher settings and basic PC information (graphics card, system); no game files or saves. Attach it to a new issue on this repository's **Issues** page and describe what happened. If the game crashed, its log ends with a crash report that shows exactly where.

## Building from source

This is a developer build, not a one-command player install. Full guide: [docs/BUILDING.md](docs/BUILDING.md).

1. `./tools/setup.sh` downloads the ReXGlue SDK next to this repository (`../tools/rexglue`), applies this project's
   SDK changes from `rexglue-patches/`, and builds it.
2. Copy your own `default.xex` and `default.xexp` (title update 2.0.1.0) into `game/`, then run `./tools/codegen.sh`.
3. Build with `./tools/build-linux.sh` or (cross-compiled from Linux) `./tools/build-windows.sh`.

`game/`, `generated/` and all retail files stay local: they are never committed or redistributed.

## Credits

- **ReXGlue SDK** — Xbox 360 PowerPC-to-x64 recompilation runtime and tooling; this project builds on the
  [rexglue-sdk-yukes](https://github.com/HollywoodAkeem/rexglue-sdk-yukes) fork. ReXGlue is based in part on work from **Xenia**, the Xbox 360 emulator project.
- **Xenia contributors** — foundational Xbox 360 system and GPU research/code acknowledged by the ReXGlue SDK.
- WWE '13 and all related game material are the property of their respective rights holders. This project is unofficial and is not endorsed by them.

Licensed under the BSD 3-Clause License ([LICENSE](LICENSE)); see also [DISCLAIMER.md](DISCLAIMER.md).

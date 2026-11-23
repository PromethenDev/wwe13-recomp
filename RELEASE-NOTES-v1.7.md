# WWE '13 Recomp v1.7

## New
- **Linux: the native renderer.** v1.6's fast native renderer now runs on Linux too (Vulkan). Open the
  `Linux-x86_64` folder and start `wwe13-launcher`. On the launcher's **Settings** page, **Graphics API** offers
  **Automatic (experimental)**, **Vulkan (experimental)** and **Emulated (Xenos)**. DirectX is Windows only.
- **120 fps mode (EXPERIMENTAL).** The launcher's **Frame rate** row has a new **120 (exp.)** choice: matches and menus run at
  120 frames per second, entrances and cutscenes at 60 (instead of 30). Game speed stays the same.
  It needs a 120 Hz (or faster) screen and a strong PC, and works with the native renderers only
  (Vulkan, DirectX 12, DirectX 11), not with Emulated. Vertical sync is turned off in this mode. If your
  screen or PC can't actually show 120 (for example a 60 Hz screen), the game notices within a few
  seconds and switches back to 60 until the next match, so it never runs in slow motion. It is marked
  experimental because it is new and not yet tested on many PCs: if something looks or feels wrong,
  choose **Classic** and please report it.

## Fixed
- **Custom music is back.** MP3, WAV, FLAC and OGG files in the `music` folder show up again in the
  entrance editor under **User Playlist** (they were missing in v1.6). (#78)
- **Left Shift and Control work in game again.** In v1.6 they did nothing, so the keyboard's
  Left Trigger (Shift) and Right Bumper (Control) were dead. (#78)
- **Stretch to fill works again** with the native renderers: wide screens are filled instead of
  showing black bars. (#79)

## Known issues
- The native renderers and the 120 fps mode are still **experimental**. If a scene looks wrong or the game crashes, choose
  **Emulated (Xenos)** in Settings and please report it.
- The computer opponent still kicks out of pins at the count of 1 or not at all; a fix is in progress.
- 120 fps was tested on an NVIDIA graphics card; reports from AMD and Intel are welcome.

## Notes
- Requires the **Microsoft Visual C++ Redistributable 2015-2022 (x64)** on Windows.
- If you have v1.4, v1.5 or v1.6, the launcher offers this update by itself. Otherwise copy your
  `userdata` folder into the new folder to keep your saves, settings and DLC.
- Same game files as before: WWE '13 Xbox 360 game files with title update 2.0.1.0.
- Thanks to **iMrBurn** for the native renderers and the WWE '13 fork, to **Kaiko** for the DirectX 11
  backend, and to the re:Blue, plume and XenosRecomp projects. See `THIRD_PARTY.md`.

## Source code
- The full source of this release is the `v1.7.0` tag of this repository, GPL-3.0. It is built on
  **iMrBurn**'s WWE '13 fork.

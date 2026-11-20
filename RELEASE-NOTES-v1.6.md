# WWE '13 Recomp v1.6

A joint release with iMrBurn's WWE '13 fork: his native renderers come to the main release.

## New
- **Native renderers (EXPERIMENTAL).** The game can now draw its picture directly with your graphics
  card's own API instead of emulating the Xbox 360 graphics chip. Choose it on the launcher's
  **Settings** page under **Graphics API**:
  - **Automatic (experimental)** - the default: Vulkan, then DirectX 12, then DirectX 11, whichever
    your PC can run.
  - **Vulkan (experimental)**, **DirectX 12 (experimental)**, **DirectX 11 (experimental)** - one
    renderer only.
  - **Emulated (Xenos)** - the renderer of v1.5 and earlier.

  The native renderers are much faster on weak graphics: on an Intel UHD 630 at 720p the attract-mode
  matches run at about 49 frames per second with Vulkan or DirectX 12 and about 42-46 with DirectX 11,
  with very few slow frames. They are marked **experimental** because they are new: some scenes may
  still look wrong or crash. If that happens, choose **Emulated (Xenos)** and please report it.
- **QUIT in the main menu.** The main menu's shop entry is now **QUIT** and closes the game.
- Built on the newer ReXGlue SDK 0.10.

## Fixed
- **Entrance crash with the native renderer.** A crash during some entrances is avoided by finishing
  the game's draw commands in order before its worker threads continue.
- **DirectX 12 on PCs with two graphics cards.** DirectX 12 now uses the graphics card chosen in the
  launcher. Before, on a PC with two cards from the same maker (for example Intel integrated graphics
  plus an Intel Arc card), it could pick the other one.

## Known issues
- The native renderers are **experimental** (see above). Emulated (Xenos) stays available.
- The computer opponent still kicks out of pins at the count of 1 or not at all; a fix is in progress.
- The native renderers are Windows only for now. Linux players can keep using v1.5.

## Notes
- Requires the **Microsoft Visual C++ Redistributable 2015-2022 (x64)**. Most PCs already have it; if
  the game says `MSVCP140.dll` or `VCRUNTIME140.dll` is missing, install it from Microsoft's website.
- If you have v1.4 or v1.5, the launcher offers this update by itself. Otherwise copy your `userdata`
  folder into the new folder to keep your saves, settings and DLC.
- Same game files as before: WWE '13 Xbox 360 game files with title update 2.0.1.0.
- Thanks to **iMrBurn** for the native renderers and the WWE '13 fork, to **Kaiko** for the DirectX 11
  backend, and to the re:Blue, plume and XenosRecomp projects. See `THIRD_PARTY.md`.

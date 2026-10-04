# Changelog

## v1.1 (2026-10-04)

### New
- **Content page in the launcher.** Everything you add to the game is in one place, in categories: Superstars,
  Entrances, Arenas, Logos, Music and Save Data. Add single Xbox 360 creation packages (`.cas`, `.enc`,
  `.car`, `.pt`) or a whole folder; each list shows the in-game name and slot and can be sorted.
- **Creation packs with their save.** Packs that come with their own `SaveData.dat` can be imported in one go.
  They are removed as a whole with **Remove pack**, which also restores your save to how it was before the import.
- **Find Automatically** now also finds the WWE '13 title update and DLC Packs 1-3 by their known names and sizes and
  offers to install them. It stays quick on large drives full of videos, and Cancel stops it straight away.

### Fixed
- **Game froze during Ministry Undertaker's entrance** (and could freeze during other entrances with cloth, such
  as long coats and robes). The game's cloth-physics worker could lose its "finished" signal on a PC.
- The launcher took about 20 seconds to start the game when many creations were installed: the automatic backup
  before each launch no longer copies installed creation packages (your saves are still backed up).
- The launcher now explains clearly when your processor is too old for the game (it needs AVX2), instead of
  nothing happening (GitHub #6).
- If the game closes right after starting, the launcher stays open and asks you to save a bug report (GitHub #6).
- Confirmation windows in the launcher look like the rest of the launcher.
- If the graphics card fails, the game now shows one clear "WWE 13" message and writes the exact graphics error and
  driver details to its log, instead of closing without an explanation (GitHub #1).
- The launcher's Settings page no longer gets cut off on high-resolution or scaled displays, e.g. KDE/Wayland on
  Linux (GitHub #2).
- On Linux, RenderDoc's "Capturing Vulkan" overlay no longer appears over the game when RenderDoc is installed
  (GitHub #3).
- Fixed a black screen on some Intel graphics (UHD 630) when the display runs at 59 Hz, and Intel driver versions
  are now shown correctly in logs and bug reports.
- Only the main launcher ships now (`WWE13 Launcher.exe` on Windows, `wwe13-launcher` on Linux). Use its
  **Save a Bug Report** button when something goes wrong.

### Notes
- Saves, settings, DLC and creations from v1.0 carry over: copy your `userdata` folder into the new folder.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0.

## v1.0 (2026-10-02)

First public release.

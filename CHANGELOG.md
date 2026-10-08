# Changelog

## v1.4 (2026-10-08)

### New
- **The launcher can update itself.** When a new version is out, the launcher shows a banner with
  what's new and an **Update** button. The download is checked before anything is replaced, only the
  program files are updated (your saves, settings, game files, music and DLC are never touched), and
  your old version is backed up so it can be restored if something goes wrong. You can turn the check
  off on the **Settings** page. This works from v1.4 onwards, so this is the last version you need to
  download by hand.
- **Freeze reports (GitHub #21, #25).** If the picture freezes (even when the sound keeps playing),
  just close the game window as usual. The game then writes a small report (`wwe13-exit-...txt`,
  or `wwe13-freeze-...txt` if the game stopped drawing completely) into the `logs` folder, and the
  launcher's bug report includes it. Please attach that bug report to your GitHub issue: it shows
  exactly which part of the game got stuck. It costs no performance while you play.

### Fixed
- **Creations, logos and custom wrestlers no longer crash the game (GitHub #12, #17, #18, #23).**
  The game uses a small picture decoder for logos, letters and custom-character images, and one part
  of it was not converted correctly, so every picture failed to load and the clean-up afterwards
  could close the game. This caused crashes when using **Lettering** in the Paint Tool (v1.3 only
  hid the most common case), and when starting a match or loading saves with imported custom
  wrestlers. Pictures now load the way they do on the console.

### Notes
- Saves, settings and DLC from earlier releases carry over: copy your `userdata` folder into the
  new folder.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0, a 64-bit CPU
  with AVX2, and a graphics card with up-to-date Vulkan drivers.

## v1.3 (2026-10-06)

### New
- **The launcher shows its version.** The bottom-left corner of the side panel and the window title
  now show the release version and build, so you can tell at a glance which version you are running.

### Fixed
- **Paint Tool logos no longer close the game when you open Lettering (GitHub #17, #18).** Choosing
  **Lettering** while making a logo could end the game. It now opens normally and letters can be
  placed as expected.
- **You can type capital letters with your PC keyboard in the game's text entry (GitHub #19).**
  While the on-screen keyboard was open, **Shift** and **Caps Lock** had no effect and everything
  came out lower case. Both now work.
- **Starting the game twice by accident no longer runs two copies (GitHub #6).** If you press
  **Play** again while the game is still loading, the launcher now says **WWE '13 is already
  running** instead of starting a second copy that fights over the graphics card.
- **Some community save packs should no longer crash right after loading (GitHub #12).** Custom
  wrestlers and arenas imported from certain save packs could close the game a moment after loading.
  This should now be fixed. If it still happens to you, please report it with the steps you took.

### Notes
- Saves, settings and DLC from earlier releases carry over: copy your `userdata` folder into the
  new folder.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0, a 64-bit CPU
  with AVX2, and a graphics card with up-to-date Vulkan drivers.

## v1.2 (2026-10-05)

### New
- **"Stretch to fill wide screens" setting.** On a monitor wider than the game's picture, you can now
  stretch the picture to the full width. The default stays the original letterboxed look with black
  bars at the sides. Found on the launcher's **Settings** page (a community contribution).
- **"Sync frames to the monitor" setting.** On by default (the same as before). If fullscreen stutters
  on a high refresh rate or FreeSync / G-SYNC monitor, turn it off on the launcher's **Settings** page.
  This affects Windows only; Linux keeps the setting saved but does not use it (a community contribution).

### Fixed
- **Saves now work when your controller isn't "controller 1" in Windows.** If another pad, an arcade
  stick or a virtual controller (Steam Input, DS4Windows and similar) was connected first, the game
  treated you as not signed in and never created a save. Now the first controller connected to the game
  is always the signed-in player (a community contribution).
- **The game starts from a folder with a non-English name (GitHub #10).** A game folder such as
  `...\Новая папка (3)\...` is now handled correctly, so **Play** no longer fails with a missing
  game files message. This also covers non-English names for update/DLC packages and disc images.
- **A non-English recomp folder no longer breaks the log or your settings.** The game now writes its
  log and reads `wwe13.toml` correctly when the folder it sits in has a non-English name.
- **The launcher no longer closes without doing anything when Windows can't open a graphics window
  (GitHub #8).** It now writes `logs\launcher.log` from the first moment, tries Direct3D 11, then
  Direct3D 12, then OpenGL, then software drawing, and if none works it shows a clear message with the
  location of the log instead of closing silently.
- **The Play page says when the PC can't run the game (GitHub #8).** If no graphics card with Vulkan
  support is found, it says the PC can't run it instead of starting a game that would fail a moment
  later.
- **No more purple or black boxes at 480p (GitHub #11).** An old test setting left in Windows could
  make matches show magenta bands and black rectangles at the 480p setting. The game no longer reacts
  to that setting at all.
- **The game no longer crashes at start-up on some two-graphics-card laptops (GitHub #13).** If the
  discrete card is still waking up when the game starts, the game now waits and checks for about three
  seconds instead of giving up and crashing.
- **A clear message when no graphics card can be used (GitHub #13).** Instead of crashing, the game
  now says it couldn't find a graphics card it can use, suggests updating the graphics driver, and on
  two-card laptops points to Windows Settings > System > Display > Graphics to set the game to
  "High performance". It then exits cleanly.

- On a PC with two identical graphics cards, the launcher now starts the game on the card you picked.

### Notes
- Saves, settings and DLC from v1.0 / v1.0.1 / v1.1 carry over: copy your `userdata` folder into the
  new folder.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0, a 64-bit CPU
  with AVX2, and a graphics card with up-to-date Vulkan drivers.

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

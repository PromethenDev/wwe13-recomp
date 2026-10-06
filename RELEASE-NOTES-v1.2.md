# WWE '13 Recomp v1.2

## New
- **"Stretch to fill wide screens" setting.** On a monitor wider than the game's picture, you can now
  stretch the picture to the full width. The default stays the original letterboxed look with black
  bars at the sides. Found on the launcher's **Settings** page (a community contribution).
- **"Sync frames to the monitor" setting.** On by default (the same as before). If fullscreen stutters
  on a high refresh rate or FreeSync / G-SYNC monitor, turn it off on the launcher's **Settings** page.
  This affects Windows only; Linux keeps the setting saved but does not use it (a community contribution).

## Fixed
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

## Notes
- Saves, settings and DLC from v1.0 / v1.0.1 / v1.1 carry over: copy your `userdata` folder into the
  new folder.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0, a 64-bit CPU
  with AVX2, and a graphics card with up-to-date Vulkan drivers.

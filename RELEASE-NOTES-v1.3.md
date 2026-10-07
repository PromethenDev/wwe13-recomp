# WWE '13 Recomp v1.3

## New
- **The launcher shows its version.** The bottom-left corner of the side panel and the window title
  now show the release version and build, so you can tell at a glance which version you are running.

## Fixed
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

## Notes
- Saves, settings and DLC from earlier releases carry over: copy your `userdata` folder into the
  new folder.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0, a 64-bit CPU
  with AVX2, and a graphics card with up-to-date Vulkan drivers.

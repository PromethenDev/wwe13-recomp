# WWE '13 Recomp v1.4

## New
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

## Fixed
- **Creations, logos and custom wrestlers no longer crash the game (GitHub #12, #17, #18, #23).**
  The game uses a small picture decoder for logos, letters and custom-character images, and one part
  of it was not converted correctly, so every picture failed to load and the clean-up afterwards
  could close the game. This caused crashes when using **Lettering** in the Paint Tool (v1.3 only
  hid the most common case), and when starting a match or loading saves with imported custom
  wrestlers. Pictures now load the way they do on the console.

## Notes
- Saves, settings and DLC from earlier releases carry over: copy your `userdata` folder into the
  new folder.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0, a 64-bit CPU
  with AVX2, and a graphics card with up-to-date Vulkan drivers.

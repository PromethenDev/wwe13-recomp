# WWE '13 Recomp v1.5

## New
- **Game language (GitHub #29).** The launcher's **Settings** page has a **Language** box, so you can
  play in another language than your system's.
- **Your PC stays awake while you play (GitHub #44).** Windows no longer goes to sleep or turns the
  screen off in the middle of a match. It sleeps normally again once the game is closed.
- **A warning instead of a silent close at start-up (GitHub #1, #6, #34, #39).** If the game closes
  during its first 90 seconds, the launcher comes back with a message and a **Save a Bug Report**
  button. On Intel graphics it also suggests updating the graphics driver. This does not fix the
  Intel start-up crash yet, but the bug report helps us find it, so please attach it to your issue.
- **Created arenas (GitHub #28).** The launcher explains when creations such as arenas are not
  listed in your save.
- The launcher minimises as soon as the game starts. The **Play** page reminds you that **Alt+F4**
  quits the game and **F11** switches between fullscreen and windowed (GitHub #43).

## Fixed
- **Closing the game always ends it.** After Alt+F4 or the window's X, the game now always closes
  within a few seconds instead of sometimes staying open in the background.
- **Crash reports are kept (GitHub #31).** A crash report could be overwritten by later log output;
  it is now kept, and **Save a Bug Report** always includes the newest freeze and exit reports.
- **Create-a-Wrestler (GitHub #31).** An extra safety net when the game runs low on memory while
  saving a heavily edited wrestler, and more details in the bug report if it still happens.

## Notes
- If you have v1.4, the launcher offers this update by itself. Otherwise copy your `userdata` folder
  into the new folder to keep your saves, settings and DLC.
- Same requirements as before: WWE '13 Xbox 360 game files with title update 2.0.1.0, a 64-bit CPU
  with AVX2, and a graphics card with up-to-date Vulkan drivers.

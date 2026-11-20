// WWE '13 native-renderer replay: run a recording (recorder.h) back through the
// renderer instead of the game. Driven by the cvars in replay.cpp.
#pragma once

namespace rex::ui {
class Window;
}

namespace svr::replay {

// True when cvar wwe13_replay_dir is non-empty (replay mode chosen at startup).
bool Enabled();

// Replays <wwe13_replay_dir>/recording.w13r into the running renderer: restores guest
// memory and the D3D device block from the page/device records, then runs the renderer
// side of each recorded call with its recorded registers. Blocks until the recording has
// run wwe13_replay_loops times, logs a summary, then requests a clean exit. Returns false
// if it could not start (no guest memory, no recording, or no host renderer device).
bool Run(rex::ui::Window* window);

}  // namespace svr::replay

// SvR 2008 stand-in for re:Blue's engine/engine.h.
//
// re:Blue's renderer reads a few Blue Dragon engine values (movie playback, sparse frames, the FPS
// limit setting). SvR's equivalents are wired here as they're found; until then these keep the
// renderer's default behaviour.

#pragma once

int Wwe13PresentFpsLimit();  // port/src/frame_rate_lock.cpp

namespace bd::engine {

// Blue Dragon skips presenting some frames while its frame interpolation runs; SvR presents every
// frame the game swaps.
inline bool SparseFrame() { return false; }

// Sofdec movie playback (Blue Dragon letterboxes and unlocks FPS during movies). SvR plays Bink
// movies; detection can be added when the renderer needs it.
struct SofdecPlayer {
  static bool Playing() { return false; }
};

struct Settings {
  static Settings& Get() {
    static Settings settings;
    return settings;
  }
  // WWE '13: presents are capped at 60, or at the locked 40 / 50 rate (port/src/frame_rate_lock.cpp,
  // --wwe13_lock_fps): the game's own vblank pacing no longer gates presents (the null GPU skips swaps).
  int FPSLimit() const { return ::Wwe13PresentFpsLimit(); }
};

}  // namespace bd::engine

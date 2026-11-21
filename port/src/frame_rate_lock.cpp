// WWE '13 PC: the launcher's frame-rate choices for the SDK 0.10 build (performance presets, 2026-11-21).
//
// Ported from the older build's src/present_interval.cpp (WWE13_LOCK_30 / WWE13_KEEP_60, shipped there since 1.x).
// The game's frame-rate setting is the struct at 0x835B2DE8 (+8 = rate 25/30/50/60, +40 = ms per frame; also read
// by render_queue_guard.cpp and renderq_sublist.cpp). sub_82AA6410 sets it, sub_82AA65B0 converts 60 -> 30
// (50 -> 25) and loads the 30 fps time steps at the start of an entrance / cutscene / cinematic camera, and
// sub_82AA6650 converts back. The game's own animation follows those time steps, so changing them through the game's
// own conversions keeps game speed right (forcing only the present interval gave double-speed entrances).
//
// --wwe13_lock_30=true: menus and the title stay at 60. The game's first 60 -> 30 conversion runs normally (so the
// matching constants load), then every 30 -> 60 restore is skipped: from the first entrance on, the whole session
// stays in the game's own 30 fps state (steady 30 for PCs that cannot hold 60).
// --wwe13_keep_60=true: the 60 -> 30 conversion is skipped, so entrances and cutscenes stay at 60 (strong PCs).
// Both off (default) = the game's own behaviour. lock_30 wins if both are set.

#include "generated/default/wwe13_init.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_BOOL(wwe13_lock_30, false, "WWE13",
                    "Steady 30 fps: after the first entrance or cutscene the game stays at its own 30 fps rate")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(wwe13_keep_60, false, "WWE13",
                    "60 fps in entrances and cutscenes too (skips the game's 60 -> 30 switch; needs a strong PC)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_82AA65B0);  // 60 -> 30 (50 -> 25)
REX_EXTERN(__imp__sub_82AA6650);  // 30 -> 60 (25 -> 50)

namespace {

constexpr uint32_t kGameFrameRate = 0x835B2DE8 + 8;

uint32_t Rate(uint8_t* base) {
  uint32_t v;
  std::memcpy(&v, base + kGameFrameRate, 4);
  return __builtin_bswap32(v);
}

std::atomic<bool> g_lock30_active{false};
std::atomic<uint32_t> g_logged{0};
bool LogMore() { return g_logged.fetch_add(1, std::memory_order_relaxed) < 200; }

}  // namespace

extern "C" REX_FUNC(sub_82AA65B0) {
  const uint32_t rate = Rate(base);
  if (REXCVAR_GET(wwe13_lock_30)) {
    __imp__sub_82AA65B0(ctx, base);
    const uint32_t now = Rate(base);
    if (!g_lock30_active.load(std::memory_order_relaxed) && now == 30) {
      g_lock30_active.store(true, std::memory_order_relaxed);
      REXLOG_INFO("[wwe13-rate] lock 30 active after the game's first switch ({} -> {})", rate, now);
    } else if (LogMore()) {
      REXLOG_INFO("[wwe13-rate] halve request at rate {} -> {} (lock 30 {})", rate, now,
                  g_lock30_active.load(std::memory_order_relaxed) ? "active" : "not active yet");
    }
    return;
  }
  if (REXCVAR_GET(wwe13_keep_60)) {
    if (LogMore()) REXLOG_INFO("[wwe13-rate] halve request at rate {} skipped (keep 60)", rate);
    return;
  }
  __imp__sub_82AA65B0(ctx, base);
}

extern "C" REX_FUNC(sub_82AA6650) {
  if (g_lock30_active.load(std::memory_order_relaxed)) {
    if (LogMore()) REXLOG_INFO("[wwe13-rate] restore request at rate {} skipped (lock 30)", Rate(base));
    return;
  }
  __imp__sub_82AA6650(ctx, base);
}

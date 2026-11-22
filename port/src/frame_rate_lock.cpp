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
//
// --wwe13_lock_fps=40|50 (owner 11-21 night: locked 40 / 50 modes for PCs between 30 and 60). The rate setter
// sub_82AA6410 writes, per rate, +8 rate, +32 rate (float), +28 1/rate, +60 60/rate, +64 rate/60, then +40 = 1000/rate
// ms and +36 = constant/rate (game tables for 25/30/50/60, read from the xex 11-21). 50 = the game's own PAL rate:
// every setter call is made with 50 and the 50 -> 25 entrance halving is skipped. 40 has no game table: the setter
// runs with 60 and the fields are rescaled to 40; the game's own 60 -> 30 / 30 -> 60 switches do nothing at 40
// (they only act on 50/60 and 25/30). Rate checks elsewhere in the game (37 found) test for 30/25 ("slow scene"),
// 50/25 (PAL) or 60, so 40 takes the 60-fps/NTSC branch except where code tests "== 60" or ">= 60" (like 50 does).
// The game presents every vblank at these rates; the renderer's PaceFrame caps presents at the lock rate
// (Wwe13PresentFpsLimit). Wins over lock_30 / keep_60.

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

REXCVAR_DEFINE_INT32(wwe13_lock_fps, 0, "WWE13",
                     "Locked frame rate for the whole session: 40, 50 or 120 (0 = off; other values are ignored)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_82AA6410);  // set rate (r3 = 0/25/30/50/60)
REX_EXTERN(__imp__sub_82AA65B0);  // 60 -> 30 (50 -> 25)
REX_EXTERN(__imp__sub_82AA6650);  // 30 -> 60 (25 -> 50)

namespace {

constexpr uint32_t kGameFrameRate = 0x835B2DE8 + 8;

uint32_t Rate(uint8_t* base) {
  uint32_t v;
  std::memcpy(&v, base + kGameFrameRate, 4);
  return __builtin_bswap32(v);
}

constexpr uint32_t kRateStruct = 0x835B2DE8;

int LockFps() {
  const int v = REXCVAR_GET(wwe13_lock_fps);
  return v == 40 || v == 50 || v == 120 ? v : 0;
}

void St32(uint8_t* base, uint32_t off, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + kRateStruct + off, &v, 4);
}
float LdF(uint8_t* base, uint32_t off) {
  uint32_t v;
  std::memcpy(&v, base + kRateStruct + off, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StF(uint8_t* base, uint32_t off, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  St32(base, off, v);
}

std::atomic<bool> g_lock30_active{false};
// 120 lock: entrances / cutscenes run at the game's own 60 (a 1/120 s step needs 120 real frames, and entrances
// reached only ~100 fps on an i5-12600KF: 17 % slow). Matches and menus stay at 120.
std::atomic<bool> g_scene60{false};
std::atomic<uint32_t> g_logged{0};
bool LogMore() { return g_logged.fetch_add(1, std::memory_order_relaxed) < 200; }

}  // namespace

// Presents per second the renderer's PaceFrame allows (reblue/gpu/present.cpp via engine.h): the lock rate or 60.
int Wwe13PresentFpsLimit() {
  const int fps = LockFps();
  if (fps == 120 && g_scene60.load(std::memory_order_relaxed)) return 60;
  return fps ? fps : 60;
}

extern "C" REX_FUNC(sub_82AA6410) {
  const int lock = LockFps();
  const uint32_t requested = ctx.r3.u32;
  if (!lock) {
    __imp__sub_82AA6410(ctx, base);
    return;
  }
  ctx.r3.u64 = lock == 50 ? 50 : 60;
  __imp__sub_82AA6410(ctx, base);
  if (lock == 40 || lock == 120) {
    // No game table: the 60 table rescaled to the lock rate (120: 1/120 s steps, the game runs each step
    // twice as often, so game speed stays right as it did at 40).
    const float r = float(lock);
    const float k36 = LdF(base, 36) * (60.0f / r);  // constant/60 -> constant/rate
    // 120: the rate field keeps reading 60 so the game's own scene logic (entrance 60 -> 30 requests, the 37
    // rate checks) behaves exactly as at 60; only the time step and present rate change.
    St32(base, 8, lock == 120 ? 60u : uint32_t(lock));
    StF(base, 32, r);
    StF(base, 28, 1.0f / r);
    StF(base, 60, 60.0f / r);
    StF(base, 64, r / 60.0f);
    St32(base, 40, lock == 40 ? 25u : 8u);  // whole ms per frame (120: 8.33 -> 8)
    StF(base, 36, k36);
  }
  if (LogMore())
    REXLOG_INFO("[wwe13-rate] set rate {} -> locked {} (ms/frame {}, step {:.4f} s)", requested, Rate(base),
                [&] { uint32_t v; std::memcpy(&v, base + kRateStruct + 40, 4); return __builtin_bswap32(v); }(),
                LdF(base, 28));
}

extern "C" REX_FUNC(sub_82AA65B0) {
  const uint32_t rate = Rate(base);
  if (LockFps() == 120) {  // entrance / cutscene: the game's own 60 table, presents capped at 60
    ctx.r3.u64 = 60;
    __imp__sub_82AA6410(ctx, base);
    g_scene60.store(true, std::memory_order_relaxed);
    if (LogMore()) REXLOG_INFO("[wwe13-rate] halve request at rate {} -> 60 for the scene (lock 120)", rate);
    return;
  }
  if (LockFps()) {
    if (LogMore()) REXLOG_INFO("[wwe13-rate] halve request at rate {} skipped (lock {})", rate, LockFps());
    return;
  }
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
  if (LockFps() == 120) {  // back to 120 through the locked setter above
    const bool was60 = g_scene60.exchange(false, std::memory_order_relaxed);
    ctx.r3.u64 = 60;
    sub_82AA6410(ctx, base);
    if (LogMore()) REXLOG_INFO("[wwe13-rate] restore request ({}) -> rate {}", was60 ? "scene 60" : "not halved", Rate(base));
    return;
  }
  if (LockFps()) {  // never halved, nothing to restore; the log line marks the end of an entrance / cutscene
    if (LogMore()) REXLOG_INFO("[wwe13-rate] restore request at rate {} skipped (lock {})", Rate(base), LockFps());
    return;
  }
  if (g_lock30_active.load(std::memory_order_relaxed)) {
    if (LogMore()) REXLOG_INFO("[wwe13-rate] restore request at rate {} skipped (lock 30)", Rate(base));
    return;
  }
  __imp__sub_82AA6650(ctx, base);
}

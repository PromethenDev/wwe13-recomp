// wwe13 - experiment: present entrances and cinematics at 60 FPS.
//
// The game asks for 30 FPS in entrances, cutscenes and cinematic camera angles: sub_82A98190 picks D3D present
// interval 2 when its target-frame-rate global (0x833B2DF0) is 30/25, and every interval change goes through the
// one-instruction setter sub_82B92348 (`stw r4,14000(r3)`: device +0x36B0, read by the swap path). Evidence:
// logs/win1/overnight-fps60-r2-*.log. Animation looks time-based (mftb clock, no fixed 1/30 step), so forcing
// interval 1 should give smooth 60 at normal speed - to be proven by entrance wall-clock length + audio sync.
//
// WWE13_FORCE_INTERVAL1=1 replaces interval 2+ with 1. Unset: the game's own behaviour, unchanged.
// This strong definition overrides the generated weak sub_82B92348 (PPC_WEAK_FUNC alias; verified on ELF and COFF).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/memory.h>

namespace {
bool ForceInterval1() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWE13_FORCE_INTERVAL1");
    return value != nullptr && std::strcmp(value, "1") == 0;
  }();
  return enabled;
}
}  // namespace

extern "C" PPC_FUNC(sub_82B92348) {
  const uint32_t requested = ctx.r4.u32;
  uint32_t interval = requested;
  if (interval > 1 && ForceInterval1()) {
    interval = 1;
  }
  // One line per change of the game's request: marks entrance/cinematic start (2) and end (1) with a timestamp
  // in both modes, so the forced run can be timed against the normal one.
  static uint32_t last_requested = UINT32_MAX;
  if (requested != last_requested) {
    last_requested = requested;
    REXLOG_INFO("[wwe13-interval] game requested {} -> set {}", requested, interval);
  }
  PPC_STORE_U32(ctx.r3.u32 + 14000, interval);
}

// WWE13_LOCK_30=1 leaves startup and menus untouched. Let the game's first entrance/cinematic 60 -> 30 conversion
// load the matching time-step/constants normally, then skip its subsequent 30 -> 60 restore so the session remains
// in the game's own 30 FPS state. The opt-in latch is set only after the game's conversion reads back rate 30.
namespace {
bool Lock30Requested() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWE13_LOCK_30");
    return value != nullptr && std::strcmp(value, "1") == 0;
  }();
  return enabled;
}

std::atomic<bool> g_lock30_active{false};

bool Lock30Active() { return g_lock30_active.load(std::memory_order_relaxed); }
}  // namespace

// Experiment 2 (proper lever, 2026-09-25 overnight): the game's frame-rate setting lives at 0x835B2DF0 (25/30/50/60)
// and 355 functions read it; sub_82AA6410 derives 1000/rate and float time steps from it. sub_82AA65B0 converts
// 60 -> 30 (50 -> 25) and loads the 30 FPS timing constants - apparently at entrance/cinematic start; sub_82AA6650
// converts back. Forcing only the present interval (experiment 1) kept the 1/30 time step -> double-speed entrances.
// WWE13_KEEP_60=1 skips the 60->30 conversion so both the present interval and the time step stay at 60.
namespace {
bool Keep60() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWE13_KEEP_60");
    return value != nullptr && std::strcmp(value, "1") == 0;
  }();
  return enabled;
}
}  // namespace

extern "C" PPC_FUNC(__imp__sub_82AA65B0);
extern "C" PPC_FUNC(sub_82AA65B0) {
  const uint32_t rate = PPC_LOAD_U32(0x835B2DF0);
  if (Lock30Requested()) {
    REXLOG_INFO("[wwe13-rate] halve request at rate {} ({})", rate,
                Lock30Active() ? "lock active; game's conversion allowed" : "first game conversion allowed");
    __imp__sub_82AA65B0(ctx, base);
    const uint32_t converted_rate = PPC_LOAD_U32(0x835B2DF0);
    if (!Lock30Active() && converted_rate == 30) {
      g_lock30_active.store(true, std::memory_order_relaxed);
      REXLOG_INFO("[wwe13-rate] lock30 active after game's first conversion at rate {}", converted_rate);
    } else if (!Lock30Active()) {
      REXLOG_INFO("[wwe13-rate] lock30 not active; game's first conversion left rate {}", converted_rate);
    }
    return;
  }
  REXLOG_INFO("[wwe13-rate] halve request at rate {} ({})", rate, Keep60() ? "suppressed" : "applied");
  if (Keep60()) {
    return;
  }
  __imp__sub_82AA65B0(ctx, base);
}

// sub_82AA6650 converts 30 -> 60 (25 -> 50): logged only (always runs the original) so entrance/cinematic phases are
// timed in both modes: [wwe13-rate] halve ... = phase start, restore ... = phase end.
extern "C" PPC_FUNC(__imp__sub_82AA6650);
extern "C" PPC_FUNC(sub_82AA6650) {
  const uint32_t rate = PPC_LOAD_U32(0x835B2DF0);
  if (Lock30Active()) {
    REXLOG_INFO("[wwe13-rate] restore request at rate {} (suppressed; locked at 30)", rate);
    return;
  }
  REXLOG_INFO("[wwe13-rate] restore request at rate {}", rate);
  __imp__sub_82AA6650(ctx, base);
}

// Late frames flip at once instead of waiting a whole extra vblank (the 60 <-> 30 alternation).
//
// After every Present the game's command stream makes the GPU wait until the frame's flip entry is processed
// (WAIT_REG_MEM on [device+11028]+4 == 0; live profile: ~25 waits of ~16 ms per slow second). The GPU interrupt at
// that point calls sub_82B9A548(r3 = frontbuffer | interval << 8 | threshold), which picks the target vblank: when
// the frame is late (last flip + interval already passed) it flips immediately only if less than `threshold` percent
// of the current vblank period has elapsed (the Xbox 360 D3D "present immediate threshold"); otherwise it queues the
// flip for the next vblank and the GPU stays blocked until then - a 17 ms frame costs 33 ms. Here the host always
// presents whole frames (no tearing), so a 100 % threshold makes every late frame flip immediately and the GPU
// continue; frames on time are unaffected (they still wait for their vblank: the 60 fps cap stays).
//
// WWE13_LATE_FLIP=0 keeps the game's own threshold. (Holding the vblank instead - emulated VRR - was tried first and
// made entrances worse: logs/win1/rejected-adaptive-vblank.patch.)
namespace {
bool LateFlipImmediate() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWE13_LATE_FLIP");
    return !(value != nullptr && std::strcmp(value, "0") == 0);
  }();
  return enabled;
}
}  // namespace

// Per-second flip statistics: a "late" frame reached its flip after the vblank it was due at, i.e. the display showed
// the previous frame once more (a visible hitch even when the per-second count reads 60). Lateness is measured in
// vblank periods since the frame's due vblank, from the game's own vblank time stamp (device+16956, mftb).
struct FlipStats {
  uint64_t window_start_ns = 0;
  uint32_t flips = 0;
  uint32_t late = 0;
  uint32_t late_bucket[4] = {};  // < 25 %, < 50 %, < 100 % of a period, >= 1 period
};

// The D3D device, captured from the GPU interrupt handler's user-data argument (sub_82B9D278(source, device)).
uint32_t g_d3d_device = 0;

uint64_t SteadyNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

extern "C" PPC_FUNC(__imp__sub_82B9A548);
extern "C" PPC_FUNC(sub_82B9A548) {
  static uint32_t last_threshold = UINT32_MAX;
  static FlipStats stats;
  const uint32_t threshold = ctx.r3.u32 & 0xFF;
  const uint32_t interval = (ctx.r3.u32 >> 8) & 0xF;
  if (threshold != last_threshold) {
    last_threshold = threshold;
    REXLOG_INFO("[wwe13-flip] game present-immediate threshold {}% interval {} ({})", threshold, interval,
                LateFlipImmediate() ? "late flips immediate" : "kept");
  }
  // Same inputs the game uses (sub_82B9A548): vblank count +16952, last flip target +16960, time of the last vblank
  // +16956 (mftb, 50 MHz).
  const uint32_t device = g_d3d_device;
  if (device && interval) {
    const uint32_t vblank_count = PPC_LOAD_U32(device + 16952);
    const uint32_t due = PPC_LOAD_U32(device + 16960) + interval;
    ++stats.flips;
    if (int32_t(vblank_count - due) >= 0) {
      ++stats.late;
      const uint32_t periods_late = vblank_count - due;
      const uint32_t since_vblank_ticks = uint32_t(PPC_QUERY_TIMEBASE()) - PPC_LOAD_U32(device + 16956);
      const uint32_t percent = since_vblank_ticks / (50'000'000 / 60 / 100);
      ++stats.late_bucket[periods_late ? 3 : percent < 25 ? 0 : percent < 50 ? 1 : 2];
    }
    const uint64_t now = SteadyNs();
    if (!stats.window_start_ns) {
      stats.window_start_ns = now;
    } else if (now - stats.window_start_ns >= 1'000'000'000ull) {
      REXLOG_INFO("[wwe13-flip] flips={} late={} late_lt25={} late_lt50={} late_lt100={} late_ge1={}", stats.flips,
                  stats.late, stats.late_bucket[0], stats.late_bucket[1], stats.late_bucket[2],
                  stats.late_bucket[3]);
      stats = FlipStats{};
      stats.window_start_ns = now;
    }
  }
  if (LateFlipImmediate()) {
    ctx.r3.u32 = (ctx.r3.u32 & ~0xFFu) | 100;
  }
  __imp__sub_82B9A548(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_82B9D278);
extern "C" PPC_FUNC(sub_82B9D278) {
  g_d3d_device = ctx.r4.u32;
  __imp__sub_82B9D278(ctx, base);
}

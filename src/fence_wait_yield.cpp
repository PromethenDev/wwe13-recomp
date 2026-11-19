// WWE '13 GPU fence wait: yield the host core instead of spinning.
//
// The title's D3D waits for the GPU to reach a command-buffer position in a
// loop (sub_82B9D540, generated/wwe13_recomp.79.cpp) that calls the spin step
// sub_82B99A70 once per iteration. On the console that step burns 32 db16cyc
// delay slots, reads the timebase and returns 1 ("keep waiting"), or 0 when the
// device is lost / the wait is abandoned. Recompiled, db16cyc is a no-op, so the
// loop is a tight host spin.
//
// Measured (Ryzen 7 7800X3D, one-on-one match, 30 fps): the game's main thread
// used 66% of a core and about two thirds of its on-CPU samples were this spin
// (sub_82B99A70 14%, sub_82B9D540 6%, the timebase read, register save/restore),
// waiting for the GPU command thread to pass the next vblank. On 4-6 core CPUs
// (GitHub #68: Ryzen Z2 A) the spin competes with the GPU command, audio and
// worker threads it is waiting for.
//
// Fix: keep the title's step (its timeout and device checks are unchanged) and,
// when it says "keep waiting", give the core away: a few pause instructions
// first (the wait often ends within microseconds), then SwitchToThread / yield.
// No sleep is used, so the wait never ends later than the GPU allows.
//
// Off by default until it has been checked in game: WWE13_FENCE_YIELD=1 turns it on.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include <immintrin.h>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/memory.h>

extern "C" PPC_FUNC(__imp__sub_82B99A70);

namespace {
bool OriginalSpin() {
  static const bool original = [] {
    const char* v = std::getenv("WWE13_FENCE_YIELD");
    return !(v && v[0] == '1');
  }();
  return original;
}

thread_local uint32_t g_spins = 0;
thread_local uint64_t g_last_tsc = 0;
// A gap this long between two steps means a new wait started (~25 us at 4 GHz).
constexpr uint64_t kNewWaitGapTsc = 100000;
}  // namespace

extern "C" PPC_FUNC(sub_82B99A70) {
  __imp__sub_82B99A70(ctx, base);
  if (ctx.r3.u32 == 0 || OriginalSpin()) {
    g_spins = 0;
    return;
  }
  const uint64_t now = __rdtsc();
  if (now - g_last_tsc > kNewWaitGapTsc) {
    g_spins = 0;
  }
  g_last_tsc = now;
  // The caller re-checks the GPU read pointer after every step.
  if (++g_spins < 16) {
    for (int i = 0; i < 32; ++i) {
      _mm_pause();
    }
  } else {
    std::this_thread::yield();
  }

  static std::atomic<bool> logged{false};
  if (!logged.exchange(true, std::memory_order_relaxed)) {
    REXLOG_INFO("[wwe13-fence] GPU fence wait yields the host core (WWE13_FENCE_YIELD=1)");
  }
}

// WWE '13 PC: D3D command ring handshake, ported from WWE 2K15 PC (port/src/render_wait_hook.cpp,
// issue #33). WWE '13 has WWE 2K15's D3D library (docs/d3d_functions.md); the addresses below were
// matched against 2K15's generated code by port/tools/match_functions.py (docs/NATIVE_PORT_PLAN.md).
//
// In 2K15 the game's main thread ran the D3D helper sub_82BA8358 (here sub_82B95718) while the
// render thread wrote packets into the same D3D command ring, with no lock between them; the native
// renderer hit that race within minutes. WWE '13's entrance crash (docs/NATIVE_PORT_PLAN.md) is
// triggered by the native renderer too: this is a lead, not a proven cause, so the handshake is off
// by default. The per-thread log below runs either way and only observes.
//
// wwe13_d3d_ring_lock: a handshake, not a lock held by the render thread. The render thread is only
// between two whole packets at a present and inside its ring and fence waits; a helper call waits
// (at most kHelperWaitMs) until the render thread is parked at one of those points, and the render
// thread stays parked (at most kParkMs) until the helper is done. Past either limit both go on as
// before and it is logged: the worst case is the behaviour without it, never a deadlock.

#include "generated/default/wwe13_init.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

REXCVAR_DEFINE_BOOL(wwe13_d3d_ring_lock, false, "WWE13",
                    "Run the game's D3D helper (sub_82B95718) only while the render thread is between "
                    "two D3D packets (WWE 2K15 issue #33; lead for the entrance crash). Off until seen "
                    "in game");

REX_EXTERN(__imp__sub_82B95718);  // 2K15 sub_82BA8358: D3D helper also run by other threads
REX_EXTERN(__imp__sub_82B9D540);  // 2K15 sub_82BB0180: command ring wait
REX_EXTERN(__imp__sub_82B9CF38);  // 2K15 sub_82BAFB78: fence wait

namespace {
constexpr int kHelperWaitMs = 50;
constexpr int kParkMs = 100;
std::timed_mutex g_helper_mutex;               // one helper call at a time
std::atomic<uint32_t> g_render_thread{0};      // the thread that presents
std::atomic<uint32_t> g_helpers_waiting{0};    // helper calls waiting for a parked render thread
std::atomic<uint32_t> g_helpers_running{0};    // helper calls running under the handshake
std::atomic<bool> g_render_parked{false};
std::atomic<uint32_t> g_helper_calls{0}, g_other_thread_calls{0}, g_helper_timeouts{0},
    g_park_timeouts{0};
thread_local int t_wait_depth = 0;
thread_local bool t_in_helper = false;  // inside a D3D helper call from another thread
thread_local bool t_logged = false;

uint32_t ThisThread() {
#if defined(_WIN32)
  return static_cast<uint32_t>(GetCurrentThreadId());
#else
  return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}
bool IsRenderThread() { return g_render_thread.load(std::memory_order_relaxed) == ThisThread(); }
uint64_t NowMs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

// Render thread at a packet boundary: let any running helper finish before going on.
// Leaving: drop the parked flag, then look at the helpers again (both sides use sequentially
// consistent operations: either the helper sees "not parked" or this sees it running).
void RenderUnparkAfterHelpers() {
  const uint64_t start = NowMs();
  for (;;) {
    while (g_helpers_running.load()) {
      if (NowMs() - start > uint64_t(kParkMs)) {
        const uint32_t n = g_park_timeouts.fetch_add(1, std::memory_order_relaxed);
        if (n < 16 || (n & 255) == 0)
          REXLOG_WARN("[wwe13-ring] render thread went on after {} ms ({} times)", kParkMs, n + 1);
        g_render_parked.store(false);
        return;
      }
      std::this_thread::yield();
    }
    g_render_parked.store(false);
    if (!g_helpers_running.load())
      return;
    g_render_parked.store(true);
  }
}

// Render thread inside one of its waits: parked for the whole wait.
struct RenderWaitScope {
  bool active = false;
  RenderWaitScope() {
    if (REXCVAR_GET(wwe13_d3d_ring_lock) && IsRenderThread() && t_wait_depth++ == 0) {
      active = true;
      g_render_parked.store(true);
    }
  }
  ~RenderWaitScope() {
    if (active && --t_wait_depth == 0)
      RenderUnparkAfterHelpers();
  }
};
}  // namespace

// present_count.cpp, once per guest present on the render thread: identify it and, when a helper
// is waiting, park here (the frame's packets are complete) until it has run.
void Wwe13RingLockOnPresent() {
  const uint32_t me = ThisThread();
  if (g_render_thread.load(std::memory_order_relaxed) != me) {
    g_render_thread.store(me, std::memory_order_relaxed);
    REXLOG_INFO("[wwe13-ring] render thread {} (handshake {})", me,
                REXCVAR_GET(wwe13_d3d_ring_lock) ? "on" : "off");
  }
  if (!REXCVAR_GET(wwe13_d3d_ring_lock) || !g_helpers_waiting.load(std::memory_order_acquire))
    return;
  g_render_parked.store(true);
  // Give the waiting helper(s) the time to start, then wait for them to finish.
  const uint64_t start = NowMs();
  while (g_helpers_waiting.load(std::memory_order_acquire) &&
         !g_helpers_running.load(std::memory_order_acquire) && NowMs() - start < 5)
    std::this_thread::yield();
  RenderUnparkAfterHelpers();
}

extern "C" REX_FUNC(sub_82B95718) {
  const uint32_t calls = g_helper_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  const bool render_known = g_render_thread.load(std::memory_order_relaxed) != 0;
  const bool other = render_known && !IsRenderThread();
  if (other) {
    const uint32_t n = g_other_thread_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!t_logged) {
      t_logged = true;
      REXLOG_INFO("[wwe13-ring] D3D helper called by thread {} (not the render thread), lr {:08X}",
                  ThisThread(), static_cast<uint32_t>(ctx.lr));
    }
    if ((n & 4095) == 0)
      REXLOG_INFO("[wwe13-ring] {} D3D helper calls, {} from other threads, {} alone, {} park "
                  "timeouts",
                  calls, n, g_helper_timeouts.load(std::memory_order_relaxed),
                  g_park_timeouts.load(std::memory_order_relaxed));
  }
  if (!other || !REXCVAR_GET(wwe13_d3d_ring_lock)) {
    t_in_helper = other;
    __imp__sub_82B95718(ctx, base);
    t_in_helper = false;
    return;
  }
  std::unique_lock serial(g_helper_mutex, std::defer_lock);
  bool ok = serial.try_lock_for(std::chrono::milliseconds(kHelperWaitMs));
  if (ok) {
    g_helpers_waiting.fetch_add(1);
    const uint64_t start = NowMs();
    // Counted as running before the parked check (see RenderUnparkAfterHelpers).
    g_helpers_running.fetch_add(1);
    while (!g_render_parked.load()) {
      g_helpers_running.fetch_sub(1);
      if (NowMs() - start > uint64_t(kHelperWaitMs)) {
        ok = false;
        break;
      }
      std::this_thread::yield();
      g_helpers_running.fetch_add(1);
    }
    g_helpers_waiting.fetch_sub(1);
  }
  if (!ok) {
    const uint32_t n = g_helper_timeouts.fetch_add(1, std::memory_order_relaxed);
    if (n < 16 || (n & 255) == 0)
      REXLOG_WARN("[wwe13-ring] helper went on alone after {} ms ({} times)", kHelperWaitMs, n + 1);
    t_in_helper = true;
    __imp__sub_82B95718(ctx, base);
    t_in_helper = false;
    return;
  }
  t_in_helper = true;
  __imp__sub_82B95718(ctx, base);
  t_in_helper = false;
  g_helpers_running.fetch_sub(1);
}

// sub_82B9E9C8: the D3D library's KickOff (moves the ring write pointer the GPU reads up to). The
// null GPU of the native renderer runs what it is given at once, so a kick from another thread while
// the render thread has reserved ring space it has not filled yet would run a half-written packet
// (lead for the entrance crash, docs/NATIVE_PORT_PLAN.md). Observer only: logs kicks made outside
// the render thread and outside a helper call already covered by the handshake.
REX_EXTERN(__imp__sub_82B9E9C8);
namespace {
std::atomic<uint32_t> g_foreign_kicks{0};
constexpr int kMaxKickSites = 32;
std::atomic<uint32_t> g_kick_sites[kMaxKickSites];
}  // namespace

extern "C" REX_FUNC(sub_82B9E9C8) {
  if (!t_in_helper && g_render_thread.load(std::memory_order_relaxed) != 0 && !IsRenderThread()) {
    const uint32_t lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t n = g_foreign_kicks.fetch_add(1, std::memory_order_relaxed) + 1;
    bool known = false;
    for (int i = 0; i < kMaxKickSites && !known; ++i) {
      uint32_t v = g_kick_sites[i].load(std::memory_order_relaxed);
      if (v == lr) known = true;
      else if (v == 0) {
        if (g_kick_sites[i].compare_exchange_strong(v, lr)) {
          REXLOG_INFO("[wwe13-ring] ring kick by thread {} (not the render thread), lr {:08X}, render "
                      "thread parked {}",
                      ThisThread(), lr, g_render_parked.load() ? "yes" : "no");
          known = true;
        } else if (v == lr) known = true;
      }
    }
    if ((n & 1023) == 0)
      REXLOG_INFO("[wwe13-ring] {} ring kicks from other threads", n);
  }
  __imp__sub_82B9E9C8(ctx, base);
}

extern "C" REX_FUNC(sub_82B9D540) {
  RenderWaitScope scope;
  __imp__sub_82B9D540(ctx, base);
}

extern "C" REX_FUNC(sub_82B9CF38) {
  RenderWaitScope scope;
  __imp__sub_82B9CF38(ctx, base);
}

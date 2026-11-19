// WWE '13 PC: bounds check on the game's render command queue replay (crash in some entrances).
//
// sub_82AA5458 replays the render commands the game thread recorded, on its own thread (object
// 0xAF087040 in the dumps, thread handle at +4): for each track (count at +132, 68-byte entries at
// *(+108)), a cursor (+44) starts at track+16; each 16-byte command is read at
// list + cursor * 16 (list = *(obj + (obj[36] + 12) * 4), the active one of two buffers), word 0 is the
// opcode (101 = end of the track, 102 = end of all), word 2 the number of data words that follow, and
// sub_82AA4C70 runs it through the game's D3D library. The cursor then moves by word 2 + 1.
//
// The game never compares the cursor with the track's end (track+12): only a 101 / 102 stops it. Twice
// in Otunga vs Stone Cold entrances (2026-11-17, once Vulkan, once D3D12) the cursor reached 0xE8BB3E5A
// and the read at 0x3A962680 crashed the game, with the same values both times (docs/NATIVE_PORT_PLAN.md).
// With --wwe13_renderq_guard=true this is the same replay, command for command, plus that check: a
// cursor that leaves [start, end] stops that track for this frame (logged with the track's bounds and
// its last commands) instead of reading outside the game's memory. Nothing changes while the queue is
// well formed. Result (docs/NATIVE_PORT_PLAN.md): the broken tracks hold zeros, and the job threads
// crash right after, so this only moves the crash; off by default.

#include "generated/default/wwe13_init.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

// Off by default: tested 2026-11-17, it stops the runaway track but the game then crashes in its job
// threads at the same moment (sub_82B18930 reading null + 8): the queue is one victim of a wider
// corruption. Kept as a diagnostic: its log lines show the broken tracks.
REXCVAR_DEFINE_BOOL(wwe13_renderq_guard, false, "WWE13",
                    "Debug: render command queue replay with a track bounds check, logging broken "
                    "tracks (false = the game's own replay)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Entrance crash (docs/NATIVE_PORT_PLAN.md): the render loop sub_82AA5530 starts the job threads
// that record the next frame (sub_82B24658) before it replays the current one (sub_82AA5458), so
// both run at once. The replay jumps into sub-lists (sub_82AA3928 / sub_82AA3988, caught by
// renderq_watch.cpp) whose contents were no longer valid commands at each crash. On the console the
// replay only writes GPU packets and is over quickly; the native renderer draws inside it, so it
// lasts much longer and overlaps the recording. With --wwe13_renderq_serial=true the job threads
// are started right after the replay instead (same work, in order): 0 crashes in 8 runs vs 8 in 16,
// about 3 ms more render-thread work per frame. On by default since 2026-11-18 (iMrBurn's call).
// To turn it off: wwe13_renderq_serial = false in wwe13.toml.
REXCVAR_DEFINE_BOOL(wwe13_renderq_serial, true, "WWE13",
                    "Start the render job threads after the frame's command replay instead of "
                    "before it (entrance crash lead)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The overlapping writes were only ever caught in entrances (the game's 30 fps scenes, where a frame
// has 33 ms and the ordering costs nothing); in a 60 fps match it costs ~3 ms a frame (59 fps, some
// frames over 25 ms on the RTX). With wwe13_renderq_serial_auto the ordering is used as soon as one
// render loop takes more than 28 ms and dropped only once the median of the last 60 is under 25 ms.
REXCVAR_DEFINE_BOOL(wwe13_renderq_serial_auto, false, "WWE13",
                    "With wwe13_renderq_serial: order the job threads only in 30 fps scenes "
                    "(entrances, loading), keep the game's overlap at 60 fps")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_82AA4C70);  // runs one command (obj, &command pointer)
REX_EXTERN(__imp__sub_82AA5458);  // the game's replay
REX_EXTERN(__imp__sub_82B24658);  // starts the render job threads (only caller: the render loop)

namespace {
thread_local bool t_kick_pending = false;
thread_local uint32_t t_kick_arg = 0;

// Runs the job start the render loop deferred, after the replay (volatile registers only: the
// caller treats them as clobbered by the replay call anyway).
void RunDeferredKick(PPCContext& ctx, uint8_t* base) {
  if (!t_kick_pending)
    return;
  t_kick_pending = false;
  ctx.r3.u64 = t_kick_arg;
  __imp__sub_82B24658(ctx, base);
}
}  // namespace

namespace {
// Render loop cadence (render thread only): true while the ordering is wanted.
bool SerialNow() {
  if (!REXCVAR_GET(wwe13_renderq_serial_auto))
    return true;
  using Clock = std::chrono::steady_clock;
  static Clock::time_point last{};
  static float ring[60] = {};
  static uint32_t n = 0;
  static bool serial = true;
  const Clock::time_point now = Clock::now();
  if (last.time_since_epoch().count() != 0) {
    const float ms = std::chrono::duration<float, std::milli>(now - last).count();
    ring[n++ % 60] = ms;
    if (ms > 28.0f) {
      if (!serial)
        REXLOG_INFO("[wwe13-renderq] slow frame ({:.1f} ms): job threads ordered after the replay", ms);
      serial = true;
      n = 0;  // leave only after 60 new frames
    } else if (serial && n >= 60) {
      float sorted[60];
      std::copy(ring, ring + 60, sorted);
      std::nth_element(sorted, sorted + 30, sorted + 60);
      if (sorted[30] < 25.0f) {
        serial = false;
        REXLOG_INFO("[wwe13-renderq] steady {:.1f} ms frames: job threads overlap the replay again",
                    sorted[30]);
      }
    }
  }
  last = now;
  return serial;
}
}  // namespace

extern "C" REX_FUNC(sub_82B24658) {
  if (REXCVAR_GET(wwe13_renderq_serial) && SerialNow()) {
    static std::atomic<bool> announced{false};
    if (!announced.exchange(true, std::memory_order_relaxed))
      REXLOG_INFO("[wwe13-renderq] job threads start after the replay (wwe13_renderq_serial)");
    t_kick_pending = true;
    t_kick_arg = ctx.r3.u32;
    return;
  }
  __imp__sub_82B24658(ctx, base);
}

namespace {

constexpr uint32_t kOpEndTrack = 101;
constexpr uint32_t kOpEndAll = 102;
// When a track's end is below its start (not seen in the game), only a runaway is stopped.
constexpr uint32_t kMaxTrackCommands = 0x10000;

uint8_t* Host(uint8_t* base, uint32_t a) { return base + a + (a >= 0xE0000000u ? 0x1000u : 0u); }
uint32_t Ld32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Host(base, a), 4);
  return __builtin_bswap32(v);
}
void St32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Host(base, a), &v, 4);
}

struct Step {
  uint32_t cursor, opcode, words;
};

std::atomic<uint32_t> g_stopped{0};

void LogStop(uint32_t obj, uint32_t track_index, uint32_t start, uint32_t end, uint32_t cursor,
             uint32_t list, uint32_t buffer, const Step* last, uint32_t count) {
  const uint32_t n = g_stopped.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n > 16 && (n & (n - 1)) != 0)
    return;  // the first 16, then at powers of two
  REXLOG_WARN("[wwe13-renderq] track {} cursor 0x{:08X} left [0x{:X}, 0x{:X}] (list 0x{:08X}, buffer {}, "
              "obj 0x{:08X}): track stopped for this frame ({} so far)",
              track_index, cursor, start, end, list, buffer, obj, n);
  for (uint32_t i = 0; i < count; ++i)
    REXLOG_WARN("[wwe13-renderq]   earlier command at 0x{:X}: opcode {} + {} words", last[i].cursor,
                last[i].opcode, last[i].words);
}

}  // namespace

namespace {
void GuardedReplay(PPCContext& ctx, uint8_t* base);
}

void Wwe13ReplayMark(bool begin);  // renderq_watch.cpp (diagnostics)

extern "C" REX_FUNC(sub_82AA5458) {
  Wwe13ReplayMark(true);
  if (!REXCVAR_GET(wwe13_renderq_guard))
    __imp__sub_82AA5458(ctx, base);
  else
    GuardedReplay(ctx, base);
  Wwe13ReplayMark(false);
  RunDeferredKick(ctx, base);
}

namespace {
void GuardedReplay(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  static std::atomic<bool> announced{false};
  if (!announced.exchange(true, std::memory_order_relaxed))
    REXLOG_INFO("[wwe13-renderq] render queue replay with track bounds check active");
  // The original's 128-byte frame: sub_82AA4C70 gets the command pointer through 80(r1).
  const uint32_t saved_r1 = ctx.r1.u32;
  const uint32_t frame = saved_r1 - 128;
  St32(base, frame, saved_r1);
  ctx.r1.u64 = frame;
  const uint32_t tracks = Ld32(base, obj + 132);
  for (uint32_t t = 0; t < tracks; ++t) {
    const uint32_t track = Ld32(base, obj + 108) + t * 68;
    const uint32_t start = Ld32(base, track + 16);
    const uint32_t end = Ld32(base, track + 12);
    St32(base, obj + 44, start);
    if (Ld32(base, obj + 44) == end)
      continue;
    Step last[4] = {};
    uint32_t steps = 0;
    for (;;) {
      const uint32_t cursor = Ld32(base, obj + 44);
      const bool outside = start <= end ? (cursor < start || cursor > end)
                                        : (cursor - start > kMaxTrackCommands);
      const uint32_t buffer = Ld32(base, obj + 36);
      const uint32_t list = Ld32(base, obj + (buffer + 12) * 4);
      if (outside) {
        Step ordered[4];
        const uint32_t count = steps < 4 ? steps : 4;
        for (uint32_t i = 0; i < count; ++i)
          ordered[i] = last[(steps - count + i) & 3];
        LogStop(obj, t, start, end, cursor, list, buffer, ordered, count);
        break;
      }
      const uint32_t command = list + cursor * 16;
      const uint32_t opcode = Ld32(base, command);
      if (opcode == kOpEndAll) {
        ctx.r1.u64 = saved_r1;
        return;
      }
      if (opcode == kOpEndTrack)
        break;
      St32(base, frame + 80, command);
      ctx.r3.u64 = obj;
      ctx.r4.u64 = frame + 80;
      ctx.lr = 0x82AA54F8;
      __imp__sub_82AA4C70(ctx, base);
      const uint32_t words = Ld32(base, Ld32(base, frame + 80) + 8);
      last[steps++ & 3] = {cursor, opcode, words};
      St32(base, obj + 44, Ld32(base, obj + 44) + words + 1);
    }
  }
  ctx.r1.u64 = saved_r1;
}
}  // namespace

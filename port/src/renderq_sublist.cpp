// WWE '13 PC: the per-model sub-lists of the render command queue (entrance crash, part 2).
//
// The main command lists are double-buffered (render_queue_guard.cpp), the sub-lists are not: a model
// records a sub-list once into its own bump pool ({capacity, +4 base, +8 used}; sub_82AA4380 begins
// it: r3 = queue object, r4 = slot {list address, valid}, r5 = pool; sub_82AA4438 ends it and
// writes pool.used back) and later frames only carry "opcode 78 -> that address". When the game
// resets a pool (used = 0) the next draw records again AT THE SAME ADDRESS. The render loop
// sub_82AA5530 starts the job threads that record frame N+1 (sub_82B24658) and then replays frame N
// (sub_82AA5458), whose opcode-78 pointers still aim at the old contents; the replay enters a
// sub-list through sub_82AA3928 (r4 = address).
//
// wwe13_renderq_sublist_count (diagnostic, default off): counts sub-list record begins that fall
// between the job start and the end of the replay, and replay entries into a sub-list range that was
// (re)recorded since that job start, split by the game's frame rate (30 = entrances, 60 = match).
//
// wwe13_renderq_sublist_wait (default off): a job thread that begins a sub-list in that window, in
// pool memory the replay in flight may still point into, waits for the end of the replay first (at
// most 50 ms, logged), so only that recording keeps the order wwe13_renderq_serial gives to all job
// work. Meant for wwe13_renderq_serial=false or wwe13_renderq_serial_scene=true (while the jobs are
// started after the replay it never waits).
//
// "May still point into": the replay in flight plays the list recorded in the previous loop, so it
// can only hold addresses below the pool's end as it was when that loop finished (PoolBound). A
// sub-list appended at or above that end is new memory nothing points to yet and is not delayed
// (the usual case in a match); after a pool reset the recording starts below it and waits. A pool
// never seen, or whose base changed, counts as reused.

#include "generated/default/wwe13_init.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_BOOL(wwe13_renderq_sublist_count, false, "WWE13",
                    "Debug: count sub-list recordings that overlap the render queue replay, logged as "
                    "[wwe13-sublist] (entrance crash diagnostic)");

REXCVAR_DEFINE_BOOL(wwe13_renderq_sublist_wait, true, "WWE13",
                    "Job threads that re-record a model's sub-list wait for the end of the frame's "
                    "command replay (entrance crash fix for the job overlap)");

REX_EXTERN(__imp__sub_82AA4380);  // begin a sub-list recording
REX_EXTERN(__imp__sub_82AA4438);  // end it

namespace {

constexpr uint32_t kGameFrameRate = 0x835B2DE8 + 8;
constexpr auto kWaitLimit = std::chrono::milliseconds(50);

uint8_t* Host(uint8_t* base, uint32_t a) { return base + a + (a >= 0xE0000000u ? 0x1000u : 0u); }
uint32_t Ld32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Host(base, a), 4);
  return __builtin_bswap32(v);
}

std::atomic<bool> g_gate{false};       // job threads started, this loop's replay not finished yet
std::atomic<bool> g_in_replay{false};
std::mutex g_gate_mutex;
std::condition_variable g_gate_cv;
thread_local bool t_render_thread = false;

// Wait statistics (fix) and counters (diagnostic), index 0 = game rate 60, 1 = any other rate.
struct Counters {
  std::atomic<uint64_t> begins{0}, in_gate{0}, in_gate_reset{0}, in_replay{0}, in_gate_reused{0};
  std::atomic<uint64_t> enter_open{0}, enter_done{0}, enter_mid{0}, enter_fresh{0};
  std::atomic<uint64_t> waits{0}, wait_us{0}, timeouts{0};
};
Counters g_c[2];
std::atomic<uint32_t> g_event_lines{0};
// Sub-list recordings open right now, and how often one was open at a job start or at the end of the
// job wait (a recorder that is not one of the loop's jobs; the wait cannot order those).
std::atomic<int32_t> g_open{0};
std::atomic<uint64_t> g_open_at_start{0}, g_open_at_replay_end{0};

// Sub-lists recorded since the last job start (diagnostic only).
struct Rec {
  uint32_t start, end, limit;  // end 0 = still being recorded; limit = end of the pool
  bool reused;                 // began below the pool's end of the previous loop (PoolBound)
};
constexpr uint32_t kMaxRecs = 1024;
std::mutex g_rec_mutex;
Rec g_recs[kMaxRecs];
uint32_t g_rec_count = 0;
std::atomic<uint32_t> g_rec_dropped{0};
thread_local int32_t t_open_rec = -1;
thread_local uint32_t t_open_pool = 0;
thread_local uint32_t t_pool = 0;  // pool of the recording this thread has open (always tracked)

// Per pool: where its recorded sub-lists ended, now and when the previous render loop finished.
struct PoolEnd {
  uint32_t base = 0, end = 0, prev_end = 0, loop = 0;
};
std::mutex g_pool_mutex;
std::unordered_map<uint32_t, PoolEnd> g_pools;
std::atomic<uint32_t> g_loop{1};  // render loops (job starts, deferred or not)

// True when a sub-list starting at `start` lies in memory the replay in flight may point into.
bool PoolReused(uint32_t pool, uint32_t pool_base, uint32_t start) {
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  const auto it = g_pools.find(pool);
  if (it == g_pools.end() || it->second.base != pool_base)
    return true;
  const PoolEnd& e = it->second;
  return start < (e.loop == g_loop.load(std::memory_order_relaxed) ? e.prev_end : e.end);
}

void PoolRecorded(uint32_t pool, uint32_t pool_base, uint32_t end) {
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  PoolEnd& e = g_pools[pool];
  const uint32_t loop = g_loop.load(std::memory_order_relaxed);
  if (e.base != pool_base)
    e = {pool_base, end, 0xFFFFFFFFu, loop};  // unknown history: reused for the rest of this loop
  if (e.loop != loop) {
    e.prev_end = e.end;
    e.loop = loop;
  }
  e.end = end;
}
thread_local uint32_t t_open_gen = 0;
std::atomic<uint32_t> g_gen{0};

uint32_t RateIndex(uint8_t* base) { return Ld32(base, kGameFrameRate) == 60 ? 0u : 1u; }

void Summary(uint8_t* base) {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point last = Clock::now();
  const Clock::time_point now = Clock::now();
  if (now - last < std::chrono::seconds(10))
    return;
  last = now;
  for (uint32_t i = 0; i < 2; ++i) {
    Counters& c = g_c[i];
    REXLOG_INFO("[wwe13-sublist] total rate{} begins {} in-window {} (pool-reset {}, during-replay {}, reused-memory {}) | "
                "replay entered re-recorded: open {} done {} mid {} in-new-memory {} | waits {} ({} ms) timeouts {} | now {}",
                i == 0 ? "60" : "!60", c.begins.load(), c.in_gate.load(), c.in_gate_reset.load(),
                c.in_replay.load(), c.in_gate_reused.load(), c.enter_open.load(), c.enter_done.load(),
                c.enter_mid.load(), c.enter_fresh.load(),
                c.waits.load(), c.wait_us.load() / 1000, c.timeouts.load(), Ld32(base, kGameFrameRate));
  }
  REXLOG_INFO("[wwe13-sublist] recordings open at a job start {} | open at a replay end {} | open now {}",
              g_open_at_start.load(), g_open_at_replay_end.load(), g_open.load());
  if (g_rec_dropped.load())
    REXLOG_INFO("[wwe13-sublist] record table full {} times", g_rec_dropped.load());
}

}  // namespace

// render_queue_guard.cpp: a render loop reached its job start (deferred or not).
void Wwe13SublistLoop() { g_loop.fetch_add(1, std::memory_order_relaxed); }

// render_queue_guard.cpp: the job threads are being started before the replay (the game's overlap).
void Wwe13SublistJobsStarted() {
  if (g_open.load(std::memory_order_relaxed) > 0)
    g_open_at_start.fetch_add(1, std::memory_order_relaxed);
  if (REXCVAR_GET(wwe13_renderq_sublist_count)) {
    std::lock_guard<std::mutex> lock(g_rec_mutex);
    g_rec_count = 0;
    g_gen.fetch_add(1, std::memory_order_relaxed);
  }
  g_gate.store(true, std::memory_order_release);
}

void Wwe13SublistReplay(bool begin, uint8_t* base) {
  t_render_thread = true;
  g_in_replay.store(begin, std::memory_order_relaxed);
  if (begin)
    return;
  if (g_open.load(std::memory_order_relaxed) > 0)
    g_open_at_replay_end.fetch_add(1, std::memory_order_relaxed);
  if (g_gate.load(std::memory_order_relaxed)) {
    {
      std::lock_guard<std::mutex> lock(g_gate_mutex);
      g_gate.store(false, std::memory_order_release);
    }
    g_gate_cv.notify_all();
  }
  if (REXCVAR_GET(wwe13_renderq_sublist_count) || REXCVAR_GET(wwe13_renderq_sublist_wait))
    Summary(base);
}

// renderq_watch.cpp (sub_82AA3928): the replay enters the sub-list at `sub`.
void Wwe13SublistReplayEnter(uint8_t* base, uint32_t sub) {
  if (!REXCVAR_GET(wwe13_renderq_sublist_count) || !g_in_replay.load(std::memory_order_relaxed))
    return;
  bool hit = false, open = false, mid = false;
  Rec r{};
  {
    std::lock_guard<std::mutex> lock(g_rec_mutex);
    for (uint32_t i = 0; i < g_rec_count; ++i) {
      const Rec& x = g_recs[i];
      const uint32_t end = x.end ? x.end : x.limit;
      if (sub >= x.start && sub < end) {
        hit = true;
        open = x.end == 0;
        mid = sub != x.start;
        r = x;
        break;
      }
    }
  }
  if (!hit)
    return;
  Counters& c = g_c[RateIndex(base)];
  (open ? c.enter_open : c.enter_done).fetch_add(1, std::memory_order_relaxed);
  if (mid)
    c.enter_mid.fetch_add(1, std::memory_order_relaxed);
  if (!r.reused)
    c.enter_fresh.fetch_add(1, std::memory_order_relaxed);
  if (g_event_lines.fetch_add(1, std::memory_order_relaxed) < 60)
    REXLOG_WARN("[wwe13-sublist] replay entered sub-list 0x{:08X} inside a range recorded since this job "
                "start: 0x{:08X}..0x{:08X} ({}{}{}), game rate {}",
                sub, r.start, r.end ? r.end : r.limit, open ? "STILL BEING WRITTEN" : "finished",
                mid ? ", not at its start" : "", r.reused ? "" : ", NEW MEMORY (rule broken)",
                Ld32(base, kGameFrameRate));
}

extern "C" REX_FUNC(sub_82AA4380) {
  const bool count = REXCVAR_GET(wwe13_renderq_sublist_count);
  const uint32_t slot = ctx.r4.u32, pool = ctx.r5.u32, caller = static_cast<uint32_t>(ctx.lr);
  bool in_gate = g_gate.load(std::memory_order_acquire);
  const bool in_replay = g_in_replay.load(std::memory_order_relaxed);
  const uint32_t pool_base = Ld32(base, pool + 4);
  const bool reused = PoolReused(pool, pool_base, pool_base + Ld32(base, pool + 8) * 16);
  t_pool = pool;
  if (in_gate && reused && !t_render_thread && REXCVAR_GET(wwe13_renderq_sublist_wait)) {
    Counters& c = g_c[RateIndex(base)];
    const auto t0 = std::chrono::steady_clock::now();
    bool done;
    {
      std::unique_lock<std::mutex> lock(g_gate_mutex);
      done = g_gate_cv.wait_for(lock, kWaitLimit, [] { return !g_gate.load(std::memory_order_acquire); });
    }
    const uint64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0).count();
    c.waits.fetch_add(1, std::memory_order_relaxed);
    c.wait_us.fetch_add(us, std::memory_order_relaxed);
    if (!done) {
      const uint64_t n = c.timeouts.fetch_add(1, std::memory_order_relaxed) + 1;
      if (n <= 20 || (n & (n - 1)) == 0)
        REXLOG_WARN("[wwe13-sublist] sub-list recording waited {} ms for the replay and went on "
                    "(slot 0x{:08X}, pool 0x{:08X}, caller 0x{:08X}, {} so far)",
                    us / 1000, slot, pool, caller, n);
    }
    in_gate = !done;  // recorded after the replay, unless the limit ran out
  }
  uint32_t used = 0;
  if (count)
    used = Ld32(base, pool + 8);
  g_open.fetch_add(1, std::memory_order_relaxed);
  __imp__sub_82AA4380(ctx, base);
  if (!count)
    return;
  Counters& c = g_c[RateIndex(base)];
  c.begins.fetch_add(1, std::memory_order_relaxed);
  t_open_rec = -1;
  if (!in_gate)
    return;
  c.in_gate.fetch_add(1, std::memory_order_relaxed);
  if (used == 0)
    c.in_gate_reset.fetch_add(1, std::memory_order_relaxed);
  if (reused)
    c.in_gate_reused.fetch_add(1, std::memory_order_relaxed);
  if (in_replay)
    c.in_replay.fetch_add(1, std::memory_order_relaxed);
  const uint32_t start = Ld32(base, slot);
  const uint32_t limit = Ld32(base, pool + 4) + Ld32(base, pool) * 16;
  {
    std::lock_guard<std::mutex> lock(g_rec_mutex);
    if (g_rec_count < kMaxRecs) {
      t_open_rec = static_cast<int32_t>(g_rec_count);
      t_open_pool = pool;
      t_open_gen = g_gen.load(std::memory_order_relaxed);
      g_recs[g_rec_count++] = {start, 0, limit > start ? limit : start + 16, reused};
    } else {
      g_rec_dropped.fetch_add(1, std::memory_order_relaxed);
    }
  }
  if (g_event_lines.fetch_add(1, std::memory_order_relaxed) < 60)
    REXLOG_INFO("[wwe13-sublist] begin in window{}{}: slot 0x{:08X} pool 0x{:08X} (capacity {}, used {}) "
                "list 0x{:08X} caller 0x{:08X} game rate {}",
                in_replay ? " (replay running)" : " (before the replay)", reused ? ", reused memory" : "", slot, pool, Ld32(base, pool),
                used, start, caller, Ld32(base, kGameFrameRate));
}

extern "C" REX_FUNC(sub_82AA4438) {
  __imp__sub_82AA4438(ctx, base);
  g_open.fetch_sub(1, std::memory_order_relaxed);
  if (t_pool) {
    const uint32_t pool_base = Ld32(base, t_pool + 4);
    PoolRecorded(t_pool, pool_base, pool_base + Ld32(base, t_pool + 8) * 16);
    t_pool = 0;
  }
  if (t_open_rec < 0)
    return;
  const uint32_t end = Ld32(base, t_open_pool + 4) + Ld32(base, t_open_pool + 8) * 16;
  std::lock_guard<std::mutex> lock(g_rec_mutex);
  if (t_open_gen == g_gen.load(std::memory_order_relaxed) && uint32_t(t_open_rec) < g_rec_count)
    g_recs[t_open_rec].end = end;
  t_open_rec = -1;
}

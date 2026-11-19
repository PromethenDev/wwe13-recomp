// WWE '13 PC: CPU kickouts at 2 (the "missing 2-count" of WWE '13, never patched on the console).
//
// A pin is a timing minigame on the pinned wrestler's 'FLMG' component (bar 0..120: cursor +96 (+92), fill
// speed +100, green zone lo +80 / hi +84 (integers), state +72: 3 = success = kickout). A CPU plays it through AI action 477:
//   constructor sub_82406CD0: +352 = FLMG, +356 = delay before pressing (float, frames), +360 = frames it
//     keeps holding past the zone start (overshoot), +364 = 1 if it tries at all, +368 = 0.
//   update sub_8240B330: once +356 runs out it holds the button until the cursor reaches lo+1, then holds
//     +360 more frames and releases. Inside [lo, hi] it kicks out; past hi it misses, sets +364 = 0 and never
//     tries again: it stays down for 3.
// The delay is short and lo sits low on the bar, so a CPU that kicks out does it right after 1; one that misses
// loses at 3. Nothing lands on 2 (found 2026-11-17 by reading the code; players report it in Exhibition and
// Universe, not in Attitude Era, which picks another row of the AI table).
//
// The referee's count is a separate manager, [0x8347C554] (GetRefereeCount = sub_826E9BB0 returns +84):
// +80 ticks (one per frame while not paused), +84 count = (+80 + +240) / +244, +240 = -5 and +244 = 90 at
// 60 fps (constants 0x82003350 / 0x82002D94 times the frame-rate factor at 0x825B2DE8+64): count 1 after
// ~1.6 s, 2 after ~3.1 s, 3 after ~4.6 s. The count runs on its own: it does not wait for the minigame.
//
// wwe13_pin_log (default on): one log line per CPU pin attempt ([wwe13-pin]) with the AI's plan, then each
// change of the bar state with the referee's count at that moment.
// wwe13_pin_two_count (default OFF until seen in game): for wwe13_pin_two_count_pct percent of the CPU's
// planned kickouts, the CPU keeps waiting (+356) until the referee's count reaches 2, then plays the bar as
// before: it kicks out right after the 2. The plan is read when the bar starts (FLMG state 1: lo, hi and
// speed are still 0 when the action is built, seen 2026-11-18 in a CPU vs CPU match), and a kickout is only
// moved when the bar fill plus the overshoot take at most 14 frames (counts are ~25 frames apart). Its planned
// misses (stays down for 3) are left as they are.
//
// CPU vs CPU test: Exhibition, character select, move the controller to the middle (Down): both sides "I.A.".

#include "generated/default/wwe13_init.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_BOOL(wwe13_pin_log, true, "WWE13", "Log each CPU pin kickout attempt ([wwe13-pin])");
REXCVAR_DEFINE_BOOL(wwe13_pin_two_count, false, "WWE13",
                    "CPU kickouts can land after the referee's 2 (WWE '13's missing 2-count); off by default");
REXCVAR_DEFINE_INT32(wwe13_pin_two_count_pct, 50, "WWE13",
                     "With wwe13_pin_two_count: percent of CPU kickouts moved between the referee's 2 and 3");

REX_EXTERN(__imp__sub_82406CD0);  // AI pin action constructor
REX_EXTERN(__imp__sub_8240B330);  // AI pin action update

namespace {

constexpr uint32_t kFlmg = 352, kDelay = 356, kOvershoot = 360, kAttempt = 364;
constexpr uint32_t kFlmgState = 72, kFlmgLo = 80, kFlmgHi = 84, kFlmgCursor = 96, kFlmgSpeed = 100;
constexpr uint32_t kCountManagerPtr = 0x8347C554;
constexpr uint32_t kMgrTicks = 80, kMgrCount = 84, kMgrOffset = 240, kMgrTicksPerCount = 244;

struct RefCount {
  bool ok = false;
  int32_t ticks = 0, count = 0, offset = 0, per_count = 0;
  // Tick at which the count reaches n.
  int32_t TickOf(int n) const { return n * per_count - offset; }
};

uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
float LdF(const uint8_t* base, uint32_t a) {
  const uint32_t u = Ld32(base, a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void StF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  u = __builtin_bswap32(u);
  std::memcpy(base + a, &u, 4);
}

RefCount ReadCount(const uint8_t* base) {
  RefCount r;
  const uint32_t mgr = Ld32(base, kCountManagerPtr);
  if (!mgr) return r;
  r.ticks = int32_t(Ld32(base, mgr + kMgrTicks));
  r.count = int32_t(Ld32(base, mgr + kMgrCount));
  r.offset = int32_t(Ld32(base, mgr + kMgrOffset));
  r.per_count = int32_t(Ld32(base, mgr + kMgrTicksPerCount));
  r.ok = r.per_count > 0 && r.per_count < 1000;
  return r;
}

using Clock = std::chrono::steady_clock;
struct Attempt {
  Clock::time_point start;
  uint32_t flmg = 0;
  uint32_t last_state = 0;
  bool attempting = false;
  bool evaluated = false;  // the plan is read once the bar has started (state 1)
  bool moved = false;      // this kickout waits for the referee's 2
  bool pressing = false;   // moved: the hold was lifted
  int32_t frames = 0;      // update calls since the bar started
  int32_t count_frame[4] = {-1, -1, -1, -1};  // update call at which the referee reached 1, 2, 3
};
std::mutex g_mutex;
std::unordered_map<uint32_t, Attempt> g_attempts;  // by AI action object

double Ms(Clock::time_point since) {
  return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

bool Roll(int pct) {
  static std::mt19937 rng{std::random_device{}()};
  return int(rng() % 100) < pct;
}
}  // namespace

extern "C" REX_FUNC(sub_82406CD0) {
  const uint32_t action = ctx.r3.u32;
  __imp__sub_82406CD0(ctx, base);
  if (!action) return;
  const uint32_t flmg = Ld32(base, action + kFlmg);
  Attempt attempt;
  attempt.start = Clock::now();
  attempt.flmg = flmg;
  attempt.last_state = flmg ? Ld32(base, flmg + kFlmgState) : 0;
  attempt.attempting = Ld32(base, action + kAttempt) != 0;
  {
    std::lock_guard lock(g_mutex);
    g_attempts[action] = attempt;
  }
  if (REXCVAR_GET(wwe13_pin_log)) {
    const RefCount rc = ReadCount(base);
    REXLOG_INFO("[wwe13-pin] CPU pinned (referee count {}): delay {:.0f}, overshoot {}, tries {}", rc.count,
                LdF(base, action + kDelay), int32_t(Ld32(base, action + kOvershoot)),
                attempt.attempting ? "yes" : "no");
  }
}

extern "C" REX_FUNC(sub_8240B330) {
  const uint32_t action = ctx.r3.u32;
  if (action) {
    std::lock_guard lock(g_mutex);
    auto it = g_attempts.find(action);
    if (it != g_attempts.end() && it->second.flmg) {
      Attempt& a = it->second;
      const uint32_t flmg = a.flmg;
      const uint32_t state = Ld32(base, flmg + kFlmgState);
      const RefCount rc = ReadCount(base);
      if (state == 1) {
        if (rc.count >= 1 && rc.count <= 3 && a.count_frame[rc.count] < 0) a.count_frame[rc.count] = a.frames;
        ++a.frames;
      }
      // The bar has started: lo, hi and speed are set. Read the AI's plan once.
      if (state == 1 && !a.evaluated) {
        a.evaluated = true;
        // lo / hi are integers (sub_82551E60 reads +80 with lwz and converts it).
        const float lo = float(int32_t(Ld32(base, flmg + kFlmgLo))), hi = float(int32_t(Ld32(base, flmg + kFlmgHi)));
        const float speed = LdF(base, flmg + kFlmgSpeed), cursor = LdF(base, flmg + kFlmgCursor);
        const int32_t overshoot = int32_t(Ld32(base, action + kOvershoot));
        const bool attempting = Ld32(base, action + kAttempt) != 0;
        // Where the cursor ends up when the CPU lets go: it holds until lo+1, then `overshoot` more frames.
        const bool plans_kickout = attempting && speed > 0.0f && (lo + 1.0f + float(overshoot) * speed) <= hi;
        const int32_t fill = speed > 0.0f ? int32_t((lo + 1.0f - cursor) / speed) + 1 : 0;
        // Moved only when filling the bar after the 2 leaves room before the 3 (counts are ~25 frames
        // apart: fill + overshoot must stay well under that).
        if (REXCVAR_GET(wwe13_pin_two_count) && plans_kickout && rc.count < 2 && fill + overshoot <= 14 &&
            Roll(REXCVAR_GET(wwe13_pin_two_count_pct)))
          a.moved = true;
        // Not moved: let it press from now on, as it already did (its delay has run out by the time the
        // bar starts; the hold below only kept it from pressing before the bar existed).
        if (!a.moved && REXCVAR_GET(wwe13_pin_two_count))
          StF(base, action + kDelay, 0.0f);
        if (REXCVAR_GET(wwe13_pin_log))
          REXLOG_INFO("[wwe13-pin] +{:.0f} ms bar started (referee count {}): delay {:.0f}, zone {:.1f}-{:.1f}, "
                      "speed {:.2f}, fill {} + overshoot {} frames -> plans {}{}",
                      Ms(a.start), rc.count, LdF(base, action + kDelay), lo, hi, speed, fill, overshoot,
                      plans_kickout ? "kickout" : "to stay down", a.moved ? ", moved after the 2" : "");
      }
      // Until the bar starts, the CPU is held (a kickout can only be moved if it has not pressed yet).
      if (REXCVAR_GET(wwe13_pin_two_count) && !a.evaluated && LdF(base, action + kDelay) < 10.0f)
        StF(base, action + kDelay, 10.0f);
      // Moved kickout: the CPU does not start pressing before the referee's 2.
      if (a.moved && !a.pressing) {
        if (rc.count >= 2) {
          StF(base, action + kDelay, 0.0f);
          a.pressing = true;
          if (REXCVAR_GET(wwe13_pin_log))
            REXLOG_INFO("[wwe13-pin] +{:.0f} ms referee says 2 (1 at frame {}, 2 at frame {}): CPU starts pressing",
                        Ms(a.start), a.count_frame[1], a.count_frame[2]);
        } else if (LdF(base, action + kDelay) < 10.0f) {
          StF(base, action + kDelay, 10.0f);
        }
      }
    }
  }
  __imp__sub_8240B330(ctx, base);
  if (!action) return;
  std::lock_guard lock(g_mutex);
  auto it = g_attempts.find(action);
  if (it == g_attempts.end() || !it->second.flmg) return;
  Attempt& a = it->second;
  const uint32_t state = Ld32(base, a.flmg + kFlmgState);
  const bool attempting = Ld32(base, action + kAttempt) != 0;
  // (The finished attempt is dropped below whether or not the log is on: before, with wwe13_pin_log off,
  // the map kept every attempt of the session.)
  if (REXCVAR_GET(wwe13_pin_log) && (state != a.last_state || attempting != a.attempting)) {
    const RefCount rc = ReadCount(base);
    REXLOG_INFO("[wwe13-pin] +{:.0f} ms (referee count {}): bar state {} -> {}{}{}, cursor {:.1f}", Ms(a.start),
                rc.count, a.last_state, state,
                attempting != a.attempting ? (attempting ? ", tries again" : ", gives up") : "",
                state == 3 ? " = KICKOUT" : "", LdF(base, a.flmg + kFlmgCursor));
    a.last_state = state;
    a.attempting = attempting;
  }
  if (state == 3 || (!attempting && state == 0)) g_attempts.erase(it);
}

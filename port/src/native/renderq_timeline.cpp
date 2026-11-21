// WWE '13 PC: per-loop timeline of the game's render loop sub_82AA5530 (test tool, default off).
//
// One loop = job start (sub_82B24658) ... replay of the recorded frame (sub_82AA5458, the native
// renderer draws and presents inside it) ... wait for the job threads (sub_82B24600) ... the six
// end-of-frame callbacks (sub_82B24528) ... next job start. With --wwe13_renderq_timeline=true the
// render thread stamps those points and logs, every 10 s, the mean / p99 of each stage and the
// stage split of every loop over 25 ms ([rq-time] lines). With --wwe13_hook_profile=true as well,
// slow loops also name the hooked D3D calls that took the most time in that loop.

#include "generated/default/wwe13_init.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#include "svr_hook.h"

REXCVAR_DEFINE_BOOL(wwe13_renderq_timeline, false, "WWE13",
                    "Test: time the stages of the game's render loop, [rq-time] lines every 10 s");

REX_EXTERN(__imp__sub_82B24600);  // waits for the render job threads
REX_EXTERN(__imp__sub_82B24528);  // end-of-frame callbacks (list flip among them)

namespace bd::gpu {
extern std::atomic<uint64_t> g_present_wait_ns;  // present.cpp: swap chain + fence + pacing waits
}
namespace svr {
HookProf* HookProfList();  // wwe13_native_stubs.cpp
}

namespace {

enum Mark { kKick, kReplayBegin, kReplayEnd, kJobWaitBegin, kJobWaitEnd, kCallbacksBegin, kCallbacksEnd, kMarks };
const char* const kStageNames[kMarks] = {"pre", "replay", "post", "jobwait", "gap", "callbacks", "rest"};

using Clock = std::chrono::steady_clock;

// Functions the loop calls after the callbacks ("rest" stage), timed one by one on the render thread.
constexpr int kTails = 9;
const char* const kTailNames[kTails] = {"822E51F0", "82AA39C8", "82B24090", "82AA44F8", "82AFFD60",
                                        "82B000F8", "82AFF590", "82A96D50", "82A98190"};
thread_local bool t_loop_thread = false;
uint64_t g_tail_ns[kTails] = {};

struct Loop {
  float tail[kTails];
  float stage[kMarks];
  float total, present_wait, hooks_guest, hooks_host;
  uint32_t rate;
  char top[96];
};

struct State {
  Clock::time_point t[kMarks]{};
  bool have[kMarks]{};
  uint64_t wait0 = 0;
  std::vector<Loop> loops;
  Clock::time_point window{};
  std::vector<uint64_t> prev_guest, prev_host;
} g;

float Ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<float, std::milli>(b - a).count(); }

void HookDeltas(Loop& l) {
  struct Item { const char* name; uint64_t g, h; };
  Item best[2] = {};
  uint64_t sum_g = 0, sum_h = 0;
  size_t i = 0;
  for (svr::HookProf* p = svr::HookProfList(); p; p = p->next, ++i) {
    if (g.prev_guest.size() <= i) {
      g.prev_guest.resize(i + 1, 0);
      g.prev_host.resize(i + 1, 0);
    }
    const uint64_t cg = p->guest_ns.load(std::memory_order_relaxed), ch = p->host_ns.load(std::memory_order_relaxed);
    const uint64_t dg = cg >= g.prev_guest[i] ? cg - g.prev_guest[i] : cg;  // the [hook-cpu] tick resets them
    const uint64_t dh = ch >= g.prev_host[i] ? ch - g.prev_host[i] : ch;
    g.prev_guest[i] = cg, g.prev_host[i] = ch;
    sum_g += dg, sum_h += dh;
    Item it{p->name, dg, dh};
    if (it.g + it.h > best[0].g + best[0].h) {
      best[1] = best[0];
      best[0] = it;
    } else if (it.g + it.h > best[1].g + best[1].h) {
      best[1] = it;
    }
  }
  l.hooks_guest = sum_g / 1e6f, l.hooks_host = sum_h / 1e6f;
  l.top[0] = 0;
  if (best[0].name)
    std::snprintf(l.top, sizeof(l.top), "%s g%.1f r%.1f, %s g%.1f r%.1f", best[0].name, best[0].g / 1e6, best[0].h / 1e6,
                  best[1].name ? best[1].name : "-", best[1].g / 1e6, best[1].h / 1e6);
}

void Flush() {
  const size_t n = g.loops.size();
  if (!n)
    return;
  auto stat = [&](auto get, float& mean, float& p50, float& p99, float& mx) {
    std::vector<float> v(n);
    double s = 0;
    for (size_t i = 0; i < n; ++i)
      s += (v[i] = get(g.loops[i]));
    std::sort(v.begin(), v.end());
    mean = float(s / n), p50 = v[n / 2], p99 = v[std::min(n - 1, n * 99 / 100)], mx = v[n - 1];
  };
  float mean, p50, p99, mx;
  stat([](const Loop& l) { return l.total; }, mean, p50, p99, mx);
  uint32_t late = 0, slow = 0;
  for (const Loop& l : g.loops)
    late += l.total > 17.5f, slow += l.total > 25.0f;
  std::string line;
  char buf[160];
  for (int s = 0; s < kMarks; ++s) {
    float m, a, b, c;
    stat([s](const Loop& l) { return l.stage[s]; }, m, a, b, c);
    std::snprintf(buf, sizeof(buf), " | %s %.2f/%.2f/%.1f", kStageNames[s], m, a, b);
    line += buf;
  }
  float wm, wa, wb, wc, gm, hm;
  stat([](const Loop& l) { return l.present_wait; }, wm, wa, wb, wc);
  stat([](const Loop& l) { return l.hooks_guest; }, gm, wa, wb, wc);
  stat([](const Loop& l) { return l.hooks_host; }, hm, wa, wb, wc);
  std::string tail_means;
  for (int k = 0; k < kTails; ++k) {
    float m, a, b, c;
    stat([k](const Loop& l) { return l.tail[k]; }, m, a, b, c);
    std::snprintf(buf, sizeof(buf), " %s %.2f/%.1f", kTailNames[k], m, c);
    tail_means += buf;
  }
  REXLOG_INFO("[rq-time] rest calls mean/max ms:{}", tail_means);
  REXLOG_INFO("[rq-time] {} loops rate {}: total mean {:.2f} p50 {:.2f} p99 {:.1f} max {:.1f} ms, >17.5 {} >25 {} "
              "(stage mean/p50/p99){} | present-wait {:.2f} | hooks guest {:.2f} renderer {:.2f}",
              n, g.loops.back().rate, mean, p50, p99, mx, late, slow, line, wm, gm, hm);
  uint32_t shown = 0;
  for (size_t i = 0; i < n && shown < 12; ++i) {
    const Loop& l = g.loops[i];
    if (l.total <= 25.0f)
      continue;
    ++shown;
    std::string tails;
    for (int k = 0; k < kTails; ++k)
      if (l.tail[k] >= 0.5f) {
        std::snprintf(buf, sizeof(buf), " %s %.1f", kTailNames[k], l.tail[k]);
        tails += buf;
      }
    REXLOG_INFO("[rq-time] slow loop {} of {}: {:.1f} ms = pre {:.1f} replay {:.1f} post {:.1f} jobwait {:.1f} gap {:.1f} "
                "callbacks {:.1f} rest {:.1f} | present-wait {:.1f} | hooks guest {:.1f} renderer {:.1f} | top: {} | "
                "rest calls >= 0.5 ms:{}",
                i, n, l.total, l.stage[0], l.stage[1], l.stage[2], l.stage[3], l.stage[4], l.stage[5], l.stage[6],
                l.present_wait, l.hooks_guest, l.hooks_host, l.top, tails);
  }
  g.loops.clear();
}

}  // namespace

// Render thread only.
void Wwe13TimelineMark(int mark, uint8_t* base) {
  if (!REXCVAR_GET(wwe13_renderq_timeline))
    return;
  const Clock::time_point now = Clock::now();
  if (mark == kKick) {
    const uint64_t wait = bd::gpu::g_present_wait_ns.load(std::memory_order_relaxed);
    bool all = true;
    for (int i = 0; i < kMarks; ++i)
      all = all && g.have[i];
    if (all) {
      Loop l{};
      for (int i = 0; i + 1 < kMarks; ++i)
        l.stage[i] = Ms(g.t[i], g.t[i + 1]);
      l.stage[kMarks - 1] = Ms(g.t[kMarks - 1], now);
      l.total = Ms(g.t[kKick], now);
      l.present_wait = (wait - g.wait0) / 1e6f;
      uint32_t rate;
      std::memcpy(&rate, base + 0x835B2DE8 + 8, 4);
      l.rate = __builtin_bswap32(rate);
      HookDeltas(l);
      for (int k = 0; k < kTails; ++k)
        l.tail[k] = g_tail_ns[k] / 1e6f;
      g.loops.push_back(l);
    }
    std::fill(std::begin(g.have), std::end(g.have), false);
    std::fill(std::begin(g_tail_ns), std::end(g_tail_ns), 0);
    if (!t_loop_thread) {
      t_loop_thread = true;
      REXLOG_INFO("[rq-time] render loop thread: this line's thread id");
    }
    g.wait0 = wait;
    if (g.window.time_since_epoch().count() == 0)
      g.window = now;
    if (now - g.window >= std::chrono::seconds(10)) {
      Flush();
      g.window = Clock::now();
    }
  }
  g.t[mark] = mark == kKick ? Clock::now() : now;
  g.have[mark] = true;
}

extern "C" REX_FUNC(sub_82B24600) {
  Wwe13TimelineMark(kJobWaitBegin, base);
  __imp__sub_82B24600(ctx, base);
  Wwe13TimelineMark(kJobWaitEnd, base);
}

extern "C" REX_FUNC(sub_82B24528) {
  Wwe13TimelineMark(kCallbacksBegin, base);
  __imp__sub_82B24528(ctx, base);
  Wwe13TimelineMark(kCallbacksEnd, base);
}

#define WWE13_TAIL(sub, index)                                                    \
  REX_EXTERN(__imp__##sub);                                                       \
  extern "C" REX_FUNC(sub) {                                                      \
    if (!t_loop_thread || !REXCVAR_GET(wwe13_renderq_timeline)) {                 \
      __imp__##sub(ctx, base);                                                    \
      return;                                                                     \
    }                                                                             \
    const Clock::time_point t0 = Clock::now();                                    \
    __imp__##sub(ctx, base);                                                      \
    g_tail_ns[index] += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count(); \
  }
WWE13_TAIL(sub_822E51F0, 0)
WWE13_TAIL(sub_82AA39C8, 1)
WWE13_TAIL(sub_82B24090, 2)
WWE13_TAIL(sub_82AA44F8, 3)
WWE13_TAIL(sub_82AFFD60, 4)
WWE13_TAIL(sub_82B000F8, 5)
WWE13_TAIL(sub_82AFF590, 6)
WWE13_TAIL(sub_82A96D50, 7)
WWE13_TAIL(sub_82A98190, 8)

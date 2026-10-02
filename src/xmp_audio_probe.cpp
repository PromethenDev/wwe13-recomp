// Diagnostic (WWE13_XMP_AUDIO_PROBE=1): samples the title's XMP-notification handler and the
// audio-engine mute/restore routines it calls. Default off.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/logging.h>
#include <rex/ppc/context.h>

namespace {
bool Enabled() {
  static const bool on = [] {
    const char* v = std::getenv("WWE13_XMP_AUDIO_PROBE");
    return v && std::strcmp(v, "1") == 0;
  }();
  return on;
}

using ProbeClock = std::chrono::steady_clock;

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             ProbeClock::now().time_since_epoch())
      .count();
}

struct ProbeThrottle {
  std::atomic<uint64_t> calls{0};
  std::atomic<int64_t> last_log_ns{0};
};

uint64_t RecordCall(ProbeThrottle& throttle) {
  return throttle.calls.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool ShouldLogCall(ProbeThrottle& throttle, uint64_t call) {
  constexpr uint64_t kClockCheckMask = 0xFF;
  constexpr int64_t kMinLogIntervalNs = 10'000'000'000LL;
  if (call <= 8) {
    if (call == 8) {
      throttle.last_log_ns.store(NowNs(), std::memory_order_relaxed);
    }
    return true;
  }

  // Avoid reading the clock in a very hot wrapper on every guest invocation.
  if ((call & kClockCheckMask) != 0) {
    return false;
  }
  const int64_t now_ns = NowNs();
  int64_t previous_ns = throttle.last_log_ns.load(std::memory_order_relaxed);
  while (now_ns - previous_ns >= kMinLogIntervalNs) {
    if (throttle.last_log_ns.compare_exchange_weak(previous_ns, now_ns,
                                                    std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

void LogCall(const char* what, uint32_t lr, uint32_t r3) {
  REXLOG_INFO("[xmp-audio-probe] call {} lr=0x{:08x} r3=0x{:08x}", what, lr, r3);
}

void LogSinkCall(uint32_t lr, float sink_f1, uint32_t sink_r6, uint32_t r3) {
  REXLOG_INFO(
      "[xmp-audio-probe] call sub_83231408 lr=0x{:08x} sink_f1={:.9g} sink_r6={} r3=0x{:08x}",
      lr, sink_f1, sink_r6, r3);
}

void LogSub83240C38Summary(uint64_t calls) {
  constexpr uint64_t kClockCheckMask = 0xFF;
  constexpr int64_t kSummaryIntervalNs = 10'000'000'000LL;
  static std::atomic<int64_t> last_summary_ns{0};
  static std::atomic<uint64_t> last_summary_calls{0};
  if ((calls & kClockCheckMask) != 0) {
    return;
  }

  const int64_t now_ns = NowNs();
  int64_t previous_ns = last_summary_ns.load(std::memory_order_relaxed);
  if (!previous_ns) {
    last_summary_ns.compare_exchange_strong(previous_ns, now_ns,
                                            std::memory_order_relaxed);
    return;
  }
  while (now_ns - previous_ns >= kSummaryIntervalNs) {
    if (last_summary_ns.compare_exchange_weak(previous_ns, now_ns,
                                              std::memory_order_relaxed)) {
      const uint64_t previous_calls =
          last_summary_calls.exchange(calls, std::memory_order_relaxed);
      REXLOG_INFO("[xmp-audio-probe] summary sub_83240C38 calls_total={} calls_10s={}",
                  calls, calls - previous_calls);
      return;
    }
  }
}

ProbeThrottle g_83231b98;
ProbeThrottle g_83232838;
ProbeThrottle g_83232298;
ProbeThrottle g_83231c30;
ProbeThrottle g_83231408;
ProbeThrottle g_83240c38;
ProbeThrottle g_82cbbd20;
ProbeThrottle g_829bf3d0;
ProbeThrottle g_829bf738;
}  // namespace

extern "C" PPC_FUNC(__imp__sub_83231B98);
extern "C" PPC_FUNC(sub_83231B98) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_83231b98) : 0;
  const bool log = enabled && ShouldLogCall(g_83231b98, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_83231B98(ctx, base);
  if (log) LogCall("sub_83231B98", lr, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_83232838);
extern "C" PPC_FUNC(sub_83232838) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_83232838) : 0;
  const bool log = enabled && ShouldLogCall(g_83232838, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_83232838(ctx, base);
  if (log) LogCall("sub_83232838", lr, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_83232298);
extern "C" PPC_FUNC(sub_83232298) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_83232298) : 0;
  const bool log = enabled && ShouldLogCall(g_83232298, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_83232298(ctx, base);
  if (log) LogCall("sub_83232298", lr, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_83231C30);
extern "C" PPC_FUNC(sub_83231C30) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_83231c30) : 0;
  const bool log = enabled && ShouldLogCall(g_83231c30, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_83231C30(ctx, base);
  if (log) LogCall("sub_83231C30", lr, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_83231408);
extern "C" PPC_FUNC(sub_83231408) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_83231408) : 0;
  const bool log = enabled && ShouldLogCall(g_83231408, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  // The generated function's outgoing vtable call sets f1=1.0f and r6=1.
  __imp__sub_83231408(ctx, base);
  if (log) LogSinkCall(lr, 1.0f, 1, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_83240C38);
extern "C" PPC_FUNC(sub_83240C38) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_83240c38) : 0;
  const bool log = enabled && ShouldLogCall(g_83240c38, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_83240C38(ctx, base);
  if (enabled) LogSub83240C38Summary(call);
  if (log) LogCall("sub_83240C38", lr, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_82CBBD20);
extern "C" PPC_FUNC(sub_82CBBD20) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_82cbbd20) : 0;
  const bool log = enabled && ShouldLogCall(g_82cbbd20, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_82CBBD20(ctx, base);
  if (log) LogCall("sub_82CBBD20", lr, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_829BF3D0);
extern "C" PPC_FUNC(sub_829BF3D0) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_829bf3d0) : 0;
  const bool log = enabled && ShouldLogCall(g_829bf3d0, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_829BF3D0(ctx, base);
  if (log) LogCall("sub_829BF3D0", lr, ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_829BF738);
extern "C" PPC_FUNC(sub_829BF738) {
  const bool enabled = Enabled();
  const uint64_t call = enabled ? RecordCall(g_829bf738) : 0;
  const bool log = enabled && ShouldLogCall(g_829bf738, call);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_829BF738(ctx, base);
  if (log) LogCall("sub_829BF738", lr, ctx.r3.u32);
}

// wwe13 - env-gated wall-clock sampling profiler for named host threads.
// See thread_profiler.h for the environment interface.

#include "thread_profiler.h"

#if defined(__linux__)
#include <execinfo.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/diagnostics/rate_census.h>
#include <rex/logging.h>

namespace wwe13 {
namespace {

constexpr int kMaxThreads = 8;
constexpr int kMaxDepth = 48;
constexpr uint32_t kRingSlots = 8192;  // Per thread; drained every 50 ms.
constexpr char kGuestThreadPrefix[] = "XThread";

struct Sample {
  uint64_t t_ns;
  uint32_t tid;
  uint32_t depth;
  void* pcs[kMaxDepth];
};

// Single producer (the SIGPROF handler on the target thread), single consumer
// (the drain thread).
struct Ring {
  std::atomic<pid_t> tid{0};
  std::atomic<uint32_t> head{0};
  std::atomic<uint32_t> tail{0};
  std::atomic<uint64_t> dropped{0};
  timer_t timer{};
  Sample slots[kRingSlots];
};

Ring* g_rings[kMaxThreads] = {};

// Samples from threads that are not profiled but received SIGPROF anyway: the
// census sends one to the owner of a contended global lock
// (WWE13_LOCK_OWNER_SAMPLE=1). Multiple producers: slots are reserved with
// fetch_add and published through a per-slot sequence number.
constexpr uint32_t kForeignSlots = 4096;
struct ForeignRing {
  std::atomic<uint32_t> reserve{0};
  std::atomic<uint32_t> tail{0};
  std::atomic<uint64_t> dropped{0};
  std::atomic<uint32_t> seq[kForeignSlots] = {};
  Sample slots[kForeignSlots];
};
ForeignRing* g_foreign = nullptr;
std::atomic<int> g_ring_count{0};
std::atomic<bool> g_running{false};
std::thread g_worker;
std::mutex g_lifecycle;

uint64_t NowNs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

void OnSigprof(int, siginfo_t*, void*) {
  const int saved_errno = errno;
  const pid_t self = pid_t(syscall(SYS_gettid));
  const int count = g_ring_count.load(std::memory_order_acquire);
  for (int i = 0; i < count; ++i) {
    Ring* ring = g_rings[i];
    if (ring->tid.load(std::memory_order_relaxed) != self) {
      continue;
    }
    const uint32_t head = ring->head.load(std::memory_order_relaxed);
    if (head - ring->tail.load(std::memory_order_acquire) >= kRingSlots) {
      ring->dropped.fetch_add(1, std::memory_order_relaxed);
      break;
    }
    Sample& sample = ring->slots[head % kRingSlots];
    sample.t_ns = NowNs();
    sample.tid = uint32_t(self);
    sample.depth = uint32_t(backtrace(sample.pcs, kMaxDepth));
    ring->head.store(head + 1, std::memory_order_release);
    errno = saved_errno;
    return;
  }
  if (ForeignRing* foreign = g_foreign) {
    // Reserve a slot only if one is free, so a published slot is never reused
    // before the drain thread consumed it.
    uint32_t index = foreign->reserve.load(std::memory_order_relaxed);
    do {
      if (index - foreign->tail.load(std::memory_order_acquire) >= kForeignSlots) {
        foreign->dropped.fetch_add(1, std::memory_order_relaxed);
        errno = saved_errno;
        return;
      }
    } while (!foreign->reserve.compare_exchange_weak(index, index + 1, std::memory_order_relaxed));
    Sample& sample = foreign->slots[index % kForeignSlots];
    sample.t_ns = NowNs();
    sample.tid = uint32_t(self);
    sample.depth = uint32_t(backtrace(sample.pcs, kMaxDepth));
    foreign->seq[index % kForeignSlots].store(index + 1, std::memory_order_release);
  }
  errno = saved_errno;
}

std::vector<std::string> SplitTokens(const char* value) {
  std::vector<std::string> result;
  std::string current;
  for (const char* p = value; ; ++p) {
    if (*p == ',' || *p == '\0') {
      if (!current.empty()) {
        result.push_back(current);
      }
      current.clear();
      if (*p == '\0') {
        break;
      }
    } else {
      current += *p;
    }
  }
  return result;
}

struct ProfileTargets {
  std::vector<std::string> prefixes;
  bool vdswap = false;
  size_t topcpu_count = 0;
};

ProfileTargets ParseTargets(const char* value) {
  ProfileTargets targets;
  for (const std::string& token : SplitTokens(value)) {
    if (token == "@vdswap") {
      targets.vdswap = true;
      continue;
    }
    constexpr char kTopCpuPrefix[] = "@topcpu:";
    if (token.compare(0, sizeof(kTopCpuPrefix) - 1, kTopCpuPrefix) == 0) {
      char* end = nullptr;
      errno = 0;
      const long count = std::strtol(token.c_str() + sizeof(kTopCpuPrefix) - 1, &end, 10);
      if (errno == 0 && end != token.c_str() + sizeof(kTopCpuPrefix) - 1 && *end == '\0' &&
          count > 0) {
        targets.topcpu_count = std::min<size_t>(
            kMaxThreads, targets.topcpu_count + static_cast<size_t>(count));
      } else {
        REXLOG_WARN("[wwe13-thread-profile] ignoring invalid target {}", token);
      }
      continue;
    }
    targets.prefixes.push_back(token);
  }
  return targets;
}

bool AlreadyProfiled(pid_t tid) {
  const int count = g_ring_count.load(std::memory_order_acquire);
  for (int i = 0; i < count; ++i) {
    if (g_rings[i]->tid.load(std::memory_order_relaxed) == tid) {
      return true;
    }
  }
  return false;
}

std::string ReadThreadComm(pid_t tid) {
  std::ifstream comm_file("/proc/self/task/" + std::to_string(tid) + "/comm");
  std::string comm;
  if (comm_file) {
    std::getline(comm_file, comm);
  }
  return comm;
}

struct ThreadCpuTicks {
  std::string name;
  uint64_t ticks = 0;
};

using ThreadCpuSnapshot = std::unordered_map<pid_t, ThreadCpuTicks>;

bool ReadThreadStat(const std::filesystem::path& path, ThreadCpuTicks& ticks_out) {
  std::ifstream stream(path);
  std::string line;
  if (!stream || !std::getline(stream, line)) {
    return false;
  }

  const size_t name_start = line.find('(');
  const size_t name_end = line.rfind(") ");
  if (name_start == std::string::npos || name_end <= name_start) {
    return false;
  }

  std::istringstream fields(line.substr(name_end + 2));
  std::string state;
  if (!(fields >> state)) {
    return false;
  }

  uint64_t utime = 0;
  uint64_t stime = 0;
  for (int field = 4; field <= 15; ++field) {
    std::string value;
    if (!(fields >> value)) {
      return false;
    }
    if (field == 14) {
      try {
        utime = std::stoull(value);
      } catch (const std::exception&) {
        return false;
      }
    } else if (field == 15) {
      try {
        stime = std::stoull(value);
      } catch (const std::exception&) {
        return false;
      }
    }
  }

  ticks_out.name = line.substr(name_start + 1, name_end - name_start - 1);
  ticks_out.ticks = utime + stime;
  return true;
}

ThreadCpuSnapshot ReadGuestThreadCpu() {
  ThreadCpuSnapshot result;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", error)) {
    if (error) {
      break;
    }
    const pid_t tid = pid_t(std::strtol(entry.path().filename().c_str(), nullptr, 10));
    ThreadCpuTicks ticks;
    if (ReadThreadStat(entry.path() / "stat", ticks) &&
        ticks.name.compare(0, sizeof(kGuestThreadPrefix) - 1, kGuestThreadPrefix) == 0) {
      result.emplace(tid, std::move(ticks));
    }
  }
  return result;
}

struct TimedThreadCpuSnapshot {
  std::chrono::steady_clock::time_point time;
  ThreadCpuSnapshot threads;
};

bool AttachThread(pid_t tid, const std::string& name, long interval_ns);

bool AttachTopCpuThreads(const ThreadCpuSnapshot& previous, const ThreadCpuSnapshot& current,
                         size_t count, long interval_ns) {
  struct Candidate {
    pid_t tid;
    const std::string* name;
    uint64_t ticks;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(current.size());
  for (const auto& [tid, ticks] : current) {
    const auto previous_it = previous.find(tid);
    if (previous_it == previous.end() || ticks.ticks < previous_it->second.ticks ||
        AlreadyProfiled(tid)) {
      continue;
    }
    candidates.push_back({tid, &ticks.name, ticks.ticks - previous_it->second.ticks});
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
    if (left.ticks != right.ticks) {
      return left.ticks > right.ticks;
    }
    return left.tid < right.tid;
  });

  size_t attached = 0;
  for (const Candidate& candidate : candidates) {
    if (attached >= count || g_ring_count.load(std::memory_order_relaxed) >= kMaxThreads) {
      break;
    }
    if (AttachThread(candidate.tid, *candidate.name, interval_ns)) {
      ++attached;
    }
  }
  return attached != 0;
}

bool ScanTargets(const ProfileTargets& targets, long interval_ns, bool scan_prefixes) {
  bool attached = false;
  if (targets.vdswap) {
    const std::optional<rex::diagnostics::rate_census::Sample> sample =
        rex::diagnostics::rate_census::GetLatestSample();
    if (sample && sample->vdswap_tid != 0 && !AlreadyProfiled(pid_t(sample->vdswap_tid))) {
      std::string name = ReadThreadComm(pid_t(sample->vdswap_tid));
      if (name.empty()) {
        name = "unknown";
      }
      attached |= AttachThread(pid_t(sample->vdswap_tid), name, interval_ns);
    }
  }

  if (!scan_prefixes || targets.prefixes.empty()) {
    return attached;
  }

  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", error)) {
    if (error) {
      break;
    }
    const pid_t tid = pid_t(std::strtol(entry.path().filename().c_str(), nullptr, 10));
    const std::string comm = ReadThreadComm(tid);
    for (const std::string& prefix : targets.prefixes) {
      if (comm.compare(0, prefix.size(), prefix) == 0 && !AlreadyProfiled(tid)) {
        attached |= AttachThread(tid, comm, interval_ns);
      }
    }
  }
  return attached;
}

bool AttachThread(pid_t tid, const std::string& name, long interval_ns) {
  const int index = g_ring_count.load(std::memory_order_relaxed);
  if (index >= kMaxThreads) {
    return false;
  }
  Ring* ring = new Ring();
  ring->tid.store(tid, std::memory_order_relaxed);

  sigevent sev{};
  sev.sigev_notify = SIGEV_THREAD_ID;
  sev.sigev_signo = SIGPROF;
  sev._sigev_un._tid = tid;
  if (timer_create(CLOCK_MONOTONIC, &sev, &ring->timer) != 0) {
    REXLOG_WARN("[wwe13-thread-profile] timer_create failed for tid {} ({})", tid, name);
    delete ring;
    return false;
  }
  g_rings[index] = ring;
  g_ring_count.store(index + 1, std::memory_order_release);

  itimerspec spec{};
  spec.it_interval.tv_nsec = interval_ns;
  spec.it_value.tv_nsec = interval_ns;
  timer_settime(ring->timer, 0, &spec, nullptr);
  REXLOG_INFO("[wwe13-thread-profile] sampling tid={} name={} interval_ns={}", tid, name,
              interval_ns);
  return true;
}

void CopyMaps(const std::string& out_path) {
  std::ifstream in("/proc/self/maps", std::ios::binary);
  std::ofstream out(out_path + ".maps", std::ios::binary | std::ios::trunc);
  out << in.rdbuf();
}

void WriteSample(FILE* file, const Sample& sample) {
  std::fwrite(&sample.t_ns, sizeof(sample.t_ns), 1, file);
  std::fwrite(&sample.tid, sizeof(sample.tid), 1, file);
  std::fwrite(&sample.depth, sizeof(sample.depth), 1, file);
  for (uint32_t d = 0; d < sample.depth; ++d) {
    const uint64_t pc = uint64_t(uintptr_t(sample.pcs[d]));
    std::fwrite(&pc, sizeof(pc), 1, file);
  }
}

void Drain(FILE* file) {
  if (ForeignRing* foreign = g_foreign) {
    uint32_t tail = foreign->tail.load(std::memory_order_relaxed);
    while (foreign->seq[tail % kForeignSlots].load(std::memory_order_acquire) == tail + 1) {
      const Sample& sample = foreign->slots[tail % kForeignSlots];
      WriteSample(file, sample);
      ++tail;
      foreign->tail.store(tail, std::memory_order_release);
    }
  }
  const int count = g_ring_count.load(std::memory_order_acquire);
  for (int i = 0; i < count; ++i) {
    Ring* ring = g_rings[i];
    uint32_t tail = ring->tail.load(std::memory_order_relaxed);
    const uint32_t head = ring->head.load(std::memory_order_acquire);
    for (; tail != head; ++tail) {
      WriteSample(file, ring->slots[tail % kRingSlots]);
    }
    ring->tail.store(tail, std::memory_order_release);
  }
  std::fflush(file);
}

void Worker(ProfileTargets targets, long interval_ns, std::string out_path) {
  FILE* file = std::fopen(out_path.c_str(), "wb");
  if (!file) {
    REXLOG_WARN("[wwe13-thread-profile] cannot open {}", out_path);
    return;
  }
  std::fwrite("WWE13TP1", 1, 8, file);
  const auto start = std::chrono::steady_clock::now();
  auto next_scan = start;
  auto next_cpu_sample = start;
  std::deque<TimedThreadCpuSnapshot> cpu_snapshots;
  if (targets.topcpu_count != 0) {
    cpu_snapshots.push_back({start, ReadGuestThreadCpu()});
    next_cpu_sample += std::chrono::seconds(1);
  }
  bool topcpu_evaluated_120 = false;
  bool topcpu_evaluated_240 = false;

  const auto evaluate_topcpu = [&](bool& evaluated) {
    if (evaluated || cpu_snapshots.size() < 2) {
      return false;
    }
    evaluated = true;
    const TimedThreadCpuSnapshot& current = cpu_snapshots.back();
    const auto cutoff = current.time - std::chrono::seconds(5);
    const TimedThreadCpuSnapshot* previous = nullptr;
    for (auto it = cpu_snapshots.rbegin() + 1; it != cpu_snapshots.rend(); ++it) {
      if (it->time <= cutoff) {
        previous = &*it;
        break;
      }
    }
    return previous != nullptr &&
           AttachTopCpuThreads(previous->threads, current.threads, targets.topcpu_count,
                               interval_ns);
  };

  while (g_running.load(std::memory_order_acquire)) {
    const auto now = std::chrono::steady_clock::now();
    // Look for target threads every 2 s during the first 10 minutes.
    bool attached = false;
    if (now >= next_cpu_sample && targets.topcpu_count != 0) {
      next_cpu_sample = now + std::chrono::seconds(1);
      cpu_snapshots.push_back({now, ReadGuestThreadCpu()});
      while (cpu_snapshots.size() > 8) {
        cpu_snapshots.pop_front();
      }
    }
    if (targets.topcpu_count != 0 && now - start >= std::chrono::seconds(120)) {
      attached |= evaluate_topcpu(topcpu_evaluated_120);
    }
    if (targets.topcpu_count != 0 && now - start >= std::chrono::seconds(240)) {
      attached |= evaluate_topcpu(topcpu_evaluated_240);
    }
    const bool scan_prefixes = now - start < std::chrono::minutes(10);
    if (now >= next_scan && (scan_prefixes ? (!targets.prefixes.empty() || targets.vdswap)
                                           : targets.vdswap)) {
      next_scan = now + std::chrono::seconds(2);
      attached |= ScanTargets(targets, interval_ns, scan_prefixes);
    }
    if (attached) {
      CopyMaps(out_path);
    }
    Drain(file);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  const int count = g_ring_count.load(std::memory_order_acquire);
  for (int i = 0; i < count; ++i) {
    timer_delete(g_rings[i]->timer);
    REXLOG_INFO("[wwe13-thread-profile] tid={} dropped={}", g_rings[i]->tid.load(),
                g_rings[i]->dropped.load());
  }
  Drain(file);
  CopyMaps(out_path);
  std::fclose(file);
}

}  // namespace

void StartThreadProfilerFromEnv() {
  const char* names = std::getenv("WWE13_THREAD_PROFILE");
  if (names == nullptr || *names == '\0') {
    return;
  }
  std::lock_guard<std::mutex> lock(g_lifecycle);
  if (g_running.load()) {
    return;
  }
  long hz = 500;
  if (const char* hz_env = std::getenv("WWE13_THREAD_PROFILE_HZ")) {
    hz = std::max(2L, std::min(5000L, std::strtol(hz_env, nullptr, 10)));
  }
  const char* out_env = std::getenv("WWE13_THREAD_PROFILE_OUT");
  const std::string out_path = out_env && *out_env ? out_env : "thread-profile.bin";

  g_foreign = new ForeignRing();

  // Load the unwinder before any signal can call backtrace().
  void* warm[4];
  backtrace(warm, 4);

  struct sigaction action {};
  action.sa_sigaction = OnSigprof;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  sigaction(SIGPROF, &action, nullptr);

  g_running.store(true, std::memory_order_release);
  g_worker = std::thread(Worker, ParseTargets(names), 1000000000L / hz, out_path);
  REXLOG_INFO("[wwe13-thread-profile] enabled names={} hz={} out={}", names, hz, out_path);
}

void StopThreadProfiler() {
  std::lock_guard<std::mutex> lock(g_lifecycle);
  if (!g_running.exchange(false)) {
    return;
  }
  if (g_worker.joinable()) {
    g_worker.join();
  }
}

}  // namespace wwe13
#else
namespace wwe13 {

void StartThreadProfilerFromEnv() {}

void StopThreadProfiler() {}

}  // namespace wwe13
#endif

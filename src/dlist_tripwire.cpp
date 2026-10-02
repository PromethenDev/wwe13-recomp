// Cycle 182 diagnostic instrumentation for the display-list recording path.
// This file is hand-written; generated code only calls these hooks.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <rex/ppc/memory.h>
#include <rex/system/thread_state.h>

namespace {

constexpr std::size_t kActiveRecordingCapacity = 64;
constexpr std::size_t kRingCapacity = 4096;
constexpr uint64_t kSummaryPeriodMs = 10000;
constexpr uint64_t kFullLineLimit = 200;
constexpr uint64_t kCallNoReturnFullLineLimit = 50;
constexpr uint32_t kMaxWalkCommands = 16384;
constexpr uint32_t kMaxCommandLength = 4096;
constexpr uint64_t kMaxWalkSpanBytes = 512u * 1024u;

struct Recording {
  uint32_t slot = 0;
  uint32_t block = 0;
  uint32_t pointer = 0;
  uint32_t limit = 0;
  uint32_t base = 0;
  uint32_t index = 0;
  uint32_t guest_r13 = 0;
  uint64_t begin_timestamp_ms = 0;
};

struct ActiveRecording {
  bool used = false;
  uint64_t host_tid = 0;
  Recording recording{};
};

struct AbandonedEntry {
  uint32_t pointer = 0;
};

struct UnreadyEntry {
  uint32_t target = 0;
  uint32_t slot = 0;
  uint32_t guest_r13 = 0;
  uint64_t timestamp_ms = 0;
};

enum class EventType : std::size_t {
  Begin,
  End,
  NestedBegin,
  Collide,
  EndWithoutBegin,
  GuestEndWithoutBegin,
  Abandon,
  UnreadyCall,
  ZeroCall,
  ExecThread,
  CallIntoOpenRecording,
  CallNoReturn,
  Count,
};

struct EventRate {
  uint64_t count = 0;
  uint64_t last_summary_ms = 0;
};

std::mutex g_state_mutex;
std::array<ActiveRecording, kActiveRecordingCapacity> g_active_recordings{};
std::atomic<uint32_t> g_active_recording_count{0};
std::array<AbandonedEntry, kRingCapacity> g_abandoned_ring{};
std::size_t g_abandoned_next = 0;
std::size_t g_abandoned_size = 0;
std::array<UnreadyEntry, kRingCapacity> g_unready_ring{};
std::size_t g_unready_next = 0;
std::size_t g_unready_size = 0;

std::mutex g_log_mutex;
std::array<EventRate, static_cast<std::size_t>(EventType::Count)> g_event_rates{};
std::atomic<uint64_t> g_guard_skip_count{0};
uint64_t g_guard_last_summary_ms = 0;

thread_local std::optional<Recording> g_open_recording;
thread_local bool g_exec_thread_logged = false;

uint64_t monotonic_ms() noexcept {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

uint64_t host_tid() noexcept {
#if defined(__linux__) && defined(SYS_gettid)
  return static_cast<uint64_t>(::syscall(SYS_gettid));
#else
  return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

bool tripwire_enabled() noexcept {
  static const bool enabled = []() noexcept {
    const char* value = std::getenv("WWE13_DLIST_TRIPWIRE");
    return value != nullptr && std::strcmp(value, "1") == 0;
  }();
  return enabled;
}

bool dlist_guard_enabled() noexcept {
  static const bool enabled = []() noexcept {
    const char* value = std::getenv("WWE13_DLIST_GUARD");
    return value == nullptr || std::strcmp(value, "0") != 0;
  }();
  return enabled;
}

const char* event_name(EventType type) noexcept {
  switch (type) {
    case EventType::Begin:
      return "BEGIN";
    case EventType::End:
      return "END";
    case EventType::NestedBegin:
      return "NESTED_BEGIN";
    case EventType::Collide:
      return "COLLIDE";
    case EventType::EndWithoutBegin:
      return "END_WITHOUT_BEGIN";
    case EventType::GuestEndWithoutBegin:
      return "GUEST_END_NO_BEGIN";
    case EventType::Abandon:
      return "ABANDON";
    case EventType::UnreadyCall:
      return "UNREADY_CALL";
    case EventType::ZeroCall:
      return "ZERO_CALL";
    case EventType::ExecThread:
      return "EXEC_THREAD";
    case EventType::CallIntoOpenRecording:
      return "CALL_INTO_OPEN_RECORDING";
    case EventType::CallNoReturn:
      return "CALL_NO_RETURN";
    case EventType::Count:
      break;
  }
  return "UNKNOWN";
}

void log_event(EventType type, uint32_t guest_r13, bool full_line, const char* format, ...) {
  const uint64_t timestamp_ms = monotonic_ms();
  const uint64_t tid = host_tid();

  std::lock_guard lock(g_log_mutex);
  EventRate& rate = g_event_rates[static_cast<std::size_t>(type)];
  ++rate.count;

  if (!full_line && rate.count <= kFullLineLimit)
    return;

  const bool summary_due = rate.count > kFullLineLimit &&
                           (rate.last_summary_ms == 0 ||
                            timestamp_ms - rate.last_summary_ms >= kSummaryPeriodMs);

  if (rate.count > kFullLineLimit) {
    if (!summary_due)
      return;
    rate.last_summary_ms = timestamp_ms;
    std::fprintf(stderr,
                 "[dlist] tid=%llu r13=0x%08x t_ms=%llu event=%s count=%llu "
                 "rate_summary=10s\n",
                 static_cast<unsigned long long>(tid), guest_r13,
                 static_cast<unsigned long long>(timestamp_ms), event_name(type),
                 static_cast<unsigned long long>(rate.count));
    std::fflush(stderr);
    return;
  }

  std::fprintf(stderr, "[dlist] tid=%llu r13=0x%08x t_ms=%llu event=%s ",
               static_cast<unsigned long long>(tid), guest_r13,
               static_cast<unsigned long long>(timestamp_ms), event_name(type));
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

void log_event_unlimited(EventType type, uint32_t guest_r13, const char* format, ...) {
  const uint64_t timestamp_ms = monotonic_ms();
  const uint64_t tid = host_tid();

  std::lock_guard lock(g_log_mutex);
  EventRate& rate = g_event_rates[static_cast<std::size_t>(type)];
  ++rate.count;

  std::fprintf(stderr, "[dlist] tid=%llu r13=0x%08x t_ms=%llu event=%s ",
               static_cast<unsigned long long>(tid), guest_r13,
               static_cast<unsigned long long>(timestamp_ms), event_name(type));
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

void log_call_no_return(uint32_t guest_r13, uint32_t target, const char* stop_reason,
                        uint32_t commands_walked, const char* dump) {
  const uint64_t timestamp_ms = monotonic_ms();
  const uint64_t tid = host_tid();

  std::lock_guard lock(g_log_mutex);
  EventRate& rate = g_event_rates[static_cast<std::size_t>(EventType::CallNoReturn)];
  ++rate.count;
  if (rate.count > kCallNoReturnFullLineLimit)
    return;

  std::fprintf(stderr,
               "[dlist] tid=%llu r13=0x%08x t_ms=%llu event=CALL_NO_RETURN "
               "target=0x%08x stop_reason=%s commands_walked=%u %s\n",
               static_cast<unsigned long long>(tid), guest_r13,
               static_cast<unsigned long long>(timestamp_ms), target, stop_reason,
               commands_walked, dump);
  std::fflush(stderr);
}

uint8_t* guest_memory_base() noexcept {
  auto* thread_state = rex::runtime::ThreadState::Get();
  if (thread_state == nullptr || thread_state->memory() == nullptr)
    return nullptr;
  return thread_state->memory()->virtual_membase();
}

bool load_guest_u32(uint32_t address, uint32_t& value) noexcept {
  uint8_t* base = guest_memory_base();
  if (base == nullptr)
    return false;
  value = PPC_LOAD_U32(address);
  return true;
}

bool load_guest_u64(uint32_t address, uint64_t& value) noexcept {
  uint8_t* base = guest_memory_base();
  if (base == nullptr)
    return false;
  value = PPC_LOAD_U64(address);
  return true;
}

uint32_t load_guest_u32_or_zero(uint32_t address) noexcept {
  uint32_t value = 0;
  (void)load_guest_u32(address, value);
  return value;
}

Recording make_recording(uint32_t slot, uint32_t block, uint32_t guest_r13) noexcept {
  Recording recording;
  recording.slot = slot;
  recording.block = block;
  recording.limit = load_guest_u32_or_zero(block);
  recording.base = load_guest_u32_or_zero(block + 4u);
  recording.index = load_guest_u32_or_zero(block + 8u);
  recording.pointer = recording.base + recording.index * 16u;
  recording.guest_r13 = guest_r13;
  return recording;
}

struct DescriptorState {
  uint32_t descriptor_index = 0;
  uint32_t descriptor = 0;
  uint32_t stream_base = 0;
  uint32_t stream_index = 0;
  uint32_t stream_limit = 0;
  uint32_t recording_open = 0;
  uint32_t restore_desc10 = 0;
  uint32_t restore_desc12 = 0;
  uint32_t restore_desc13 = 0;
  uint32_t restore_desc14 = 0;
  uint32_t slot = 0;
  uint32_t block = 0;
};

bool read_current_descriptor(uint32_t vm, uint32_t guest_r13, DescriptorState& state) {
  // Match sub_82AA1B68's current-thread descriptor lookup exactly:
  // t = u32[r13+0]; g = u32[t+1048] ? u32[u32[t+1048]+32] : 0;
  // idx = u32[u32[VM+156] + g*4]; desc = u32[VM+104] + idx*68.
  uint32_t t = 0;
  uint32_t thread_context = 0;
  uint32_t guest_index = 0;
  if (!load_guest_u32(guest_r13 + 0u, t) ||
      !load_guest_u32(t + 1048u, thread_context)) {
    return false;
  }
  if (thread_context != 0 && !load_guest_u32(thread_context + 32u, guest_index)) {
    return false;
  }

  uint32_t descriptor_index_table = 0;
  uint32_t descriptor_base = 0;
  if (!load_guest_u32(vm + 156u, descriptor_index_table) ||
      !load_guest_u32(descriptor_index_table + guest_index * 4u, state.descriptor_index) ||
      !load_guest_u32(vm + 104u, descriptor_base)) {
    return false;
  }

  state.descriptor = descriptor_base + state.descriptor_index * 68u;
  return load_guest_u32(state.descriptor + 0u, state.stream_base) &&
         load_guest_u32(state.descriptor + 4u, state.stream_index) &&
         load_guest_u32(state.descriptor + 8u, state.stream_limit) &&
         load_guest_u32(state.descriptor + 36u, state.recording_open) &&
         load_guest_u32(state.descriptor + 40u, state.restore_desc10) &&
         load_guest_u32(state.descriptor + 48u, state.restore_desc12) &&
         load_guest_u32(state.descriptor + 52u, state.restore_desc13) &&
         load_guest_u32(state.descriptor + 56u, state.restore_desc14) &&
         load_guest_u32(state.descriptor + 60u, state.slot) &&
         load_guest_u32(state.descriptor + 64u, state.block);
}

bool remove_active_recording_locked(uint64_t tid) noexcept {
  for (ActiveRecording& entry : g_active_recordings) {
    if (entry.used && entry.host_tid == tid) {
      entry = ActiveRecording{};
      g_active_recording_count.fetch_sub(1, std::memory_order_release);
      return true;
    }
  }
  return false;
}

void publish_active_recording_locked(uint64_t tid, const Recording& recording) noexcept {
  for (ActiveRecording& entry : g_active_recordings) {
    if (!entry.used) {
      entry.used = true;
      entry.host_tid = tid;
      entry.recording = recording;
      g_active_recording_count.fetch_add(1, std::memory_order_release);
      return;
    }
  }
}

bool abandoned_contains_locked(uint32_t pointer) noexcept {
  for (std::size_t i = 0; i < g_abandoned_size; ++i) {
    const std::size_t index = (g_abandoned_next + kRingCapacity - 1u - i) % kRingCapacity;
    if (g_abandoned_ring[index].pointer == pointer)
      return true;
  }
  return false;
}

void remember_abandoned_locked(uint32_t pointer) noexcept {
  g_abandoned_ring[g_abandoned_next].pointer = pointer;
  g_abandoned_next = (g_abandoned_next + 1u) % kRingCapacity;
  if (g_abandoned_size < kRingCapacity)
    ++g_abandoned_size;
}

void remember_unready_locked(const UnreadyEntry& entry) noexcept {
  g_unready_ring[g_unready_next] = entry;
  g_unready_next = (g_unready_next + 1u) % kRingCapacity;
  if (g_unready_size < kRingCapacity)
    ++g_unready_size;
}

std::optional<UnreadyEntry> last_unready_for_locked(uint32_t target) noexcept {
  for (std::size_t i = 0; i < g_unready_size; ++i) {
    const std::size_t index = (g_unready_next + kRingCapacity - 1u - i) % kRingCapacity;
    if (g_unready_ring[index].target == target)
      return g_unready_ring[index];
  }
  return std::nullopt;
}

void handle_begin(uint32_t slot, uint32_t block, uint32_t guest_r13) {
  Recording recording = make_recording(slot, block, guest_r13);
  recording.begin_timestamp_ms = monotonic_ms();
  const uint64_t tid = host_tid();
  const std::optional<Recording> previous = g_open_recording;

  log_event_unlimited(
      EventType::Begin, guest_r13,
      "slot=0x%08x block=0x%08x P=0x%08x block_limit=0x%08x block_base=0x%08x "
      "block_idx=%u begin_t_ms=%llu",
      recording.slot, recording.block, recording.pointer, recording.limit, recording.base,
      recording.index, static_cast<unsigned long long>(recording.begin_timestamp_ms));
  if (previous) {
    log_event(EventType::NestedBegin, guest_r13, true,
              "old_slot=0x%08x old_block=0x%08x old_P=0x%08x new_slot=0x%08x "
              "new_block=0x%08x new_P=0x%08x",
              previous->slot, previous->block, previous->pointer, recording.slot, recording.block,
              recording.pointer);
  }

  g_open_recording = recording;

  bool collision = false;
  uint64_t other_tid = 0;
  Recording other_recording{};
  {
    std::lock_guard lock(g_state_mutex);
    remove_active_recording_locked(tid);
    for (const ActiveRecording& entry : g_active_recordings) {
      if (!entry.used || entry.host_tid == tid)
        continue;
      if (entry.recording.block == recording.block || entry.recording.pointer == recording.pointer) {
        collision = true;
        other_tid = entry.host_tid;
        other_recording = entry.recording;
        break;
      }
    }
    publish_active_recording_locked(tid, recording);
  }

  if (collision) {
    log_event(EventType::Collide, guest_r13, true,
              "slot=0x%08x block=0x%08x P=0x%08x other_tid=%llu other_slot=0x%08x "
              "other_block=0x%08x other_P=0x%08x",
              recording.slot, recording.block, recording.pointer,
              static_cast<unsigned long long>(other_tid), other_recording.slot,
              other_recording.block, other_recording.pointer);
  }
}

void log_guest_end_without_begin(const DescriptorState& descriptor, uint32_t guest_r13) {
  if (descriptor.recording_open != 0) {
    return;
  }

  log_event(EventType::GuestEndWithoutBegin, guest_r13, true,
            "desc_idx=%u desc0_stream_base=0x%08x desc1_stream_idx=%u "
            "desc2_stream_limit=0x%08x desc10=0x%08x desc12=0x%08x "
            "desc13=0x%08x desc14=0x%08x desc15_slot=0x%08x desc16_block=0x%08x",
            descriptor.descriptor_index, descriptor.stream_base, descriptor.stream_index,
            descriptor.stream_limit, descriptor.restore_desc10, descriptor.restore_desc12,
            descriptor.restore_desc13, descriptor.restore_desc14, descriptor.slot,
            descriptor.block);
}

void handle_end(uint32_t vm, uint32_t guest_r13) {
  const uint64_t tid = host_tid();
  DescriptorState descriptor{};
  const bool descriptor_read = read_current_descriptor(vm, guest_r13, descriptor);
  const std::optional<Recording> closing_recording = g_open_recording;

  if (closing_recording) {
    const Recording& recording = *closing_recording;
    log_event_unlimited(
        EventType::End, guest_r13,
        "slot=0x%08x block=0x%08x P=0x%08x block_limit=0x%08x block_base=0x%08x "
        "block_idx=%u new_write_idx=0x%08x new_write_idx_read=%s close_tid=%llu "
        "close_begin_t_ms=%llu",
        recording.slot, recording.block, recording.pointer, recording.limit, recording.base,
        recording.index, descriptor_read ? descriptor.stream_index : 0u,
        descriptor_read ? "yes" : "no", static_cast<unsigned long long>(tid),
        static_cast<unsigned long long>(recording.begin_timestamp_ms));
  } else {
    log_event_unlimited(
        EventType::End, guest_r13,
        "slot=0x00000000 block=0x00000000 P=0x00000000 block_limit=0x00000000 "
        "block_base=0x00000000 block_idx=0 new_write_idx=0x%08x "
        "new_write_idx_read=%s open_recording=no",
        descriptor_read ? descriptor.stream_index : 0u, descriptor_read ? "yes" : "no");
  }

  if (descriptor_read)
    log_guest_end_without_begin(descriptor, guest_r13);

  if (!g_open_recording) {
    log_event(EventType::EndWithoutBegin, guest_r13, true, "reason=no_open_recording");
    return;
  }

  {
    std::lock_guard lock(g_state_mutex);
    remove_active_recording_locked(tid);
  }
  g_open_recording.reset();
}

void handle_bail(uint32_t resolved, uint32_t object, uint32_t stack, uint32_t guest_r13) {
  if (resolved != 0 || !g_open_recording)
    return;

  uint32_t recording_flag = 0;
  if (!load_guest_u32(stack + 144u, recording_flag) || recording_flag == 0)
    return;

  const Recording recording = *g_open_recording;
  uint64_t handle = 0;
  if (object != 0)
    (void)load_guest_u64(object + 412u, handle);

  {
    std::lock_guard lock(g_state_mutex);
    remember_abandoned_locked(recording.pointer);
  }

  log_event(EventType::Abandon, guest_r13, true,
            "slot=0x%08x P=0x%08x block=0x%08x limit=0x%08x base=0x%08x idx=%u "
            "object=0x%08x handle=0x%016llx",
            recording.slot, recording.pointer, recording.block, recording.limit, recording.base,
            recording.index, object, static_cast<unsigned long long>(handle));
}

void handle_call(uint32_t target, uint32_t slot, uint32_t guest_r13) {
  uint32_t ready = 0;
  if (!load_guest_u32(slot + 4u, ready) || ready != 0)
    return;

  const uint64_t tid = host_tid();
  const uint64_t timestamp_ms = monotonic_ms();
  bool open_elsewhere = false;
  bool abandoned = false;
  {
    std::lock_guard lock(g_state_mutex);
    abandoned = abandoned_contains_locked(target);
    for (const ActiveRecording& entry : g_active_recordings) {
      if (entry.used && entry.host_tid != tid && entry.recording.slot == slot) {
        open_elsewhere = true;
        break;
      }
    }
    remember_unready_locked(UnreadyEntry{target, slot, guest_r13, timestamp_ms});
  }

  log_event(EventType::UnreadyCall, guest_r13, true,
            "target=0x%08x slot=0x%08x ready=0 open_elsewhere=%s abandoned=%s",
            target, slot, open_elsewhere ? "yes" : "no", abandoned ? "yes" : "no");
}

void log_exec_thread_once(uint32_t guest_r13) {
  if (g_exec_thread_logged)
    return;
  g_exec_thread_logged = true;

  const uint64_t tid = host_tid();
  log_event_unlimited(EventType::ExecThread, guest_r13, "exec_tid=%llu",
                      static_cast<unsigned long long>(tid));
}

struct OpenRecordingMatch {
  uint64_t recorder_tid = 0;
  Recording recording{};
};

void log_call_into_open_recording(uint32_t target, uint32_t guest_r13) {
  if (g_active_recording_count.load(std::memory_order_acquire) == 0)
    return;

  std::array<OpenRecordingMatch, kActiveRecordingCapacity> matches{};
  std::size_t match_count = 0;
  const uint64_t target_address = target;
  const uint64_t executor_tid = host_tid();
  {
    std::lock_guard lock(g_state_mutex);
    for (const ActiveRecording& entry : g_active_recordings) {
      if (!entry.used || entry.host_tid == executor_tid || entry.recording.limit == 0)
        continue;

      const uint64_t range_begin = entry.recording.base;
      const uint64_t range_end = range_begin + static_cast<uint64_t>(entry.recording.limit) * 16u;
      if (target_address < range_begin || target_address >= range_end)
        continue;

      if (match_count < matches.size()) {
        matches[match_count].recorder_tid = entry.host_tid;
        matches[match_count].recording = entry.recording;
        ++match_count;
      }
    }
  }

  for (std::size_t i = 0; i < match_count; ++i) {
    const OpenRecordingMatch& match = matches[i];
    uint32_t recorder_write_index = 0;
    const bool write_index_read = load_guest_u32(match.recording.block + 8u, recorder_write_index);
    log_event(EventType::CallIntoOpenRecording, guest_r13, true,
              "target=0x%08x recorder_tid=%llu slot=0x%08x block=0x%08x "
              "P=0x%08x begin_t_ms=%llu recorder_write_idx=%u "
              "recorder_write_idx_read=%s",
              target, static_cast<unsigned long long>(match.recorder_tid), match.recording.slot,
              match.recording.block, match.recording.pointer,
              static_cast<unsigned long long>(match.recording.begin_timestamp_ms),
              recorder_write_index, write_index_read ? "yes" : "no");
  }
}

struct CommandRecord {
  uint64_t index = 0;
  uint32_t address = 0;
  std::array<uint32_t, 4> words{};
  bool readable = false;
};

struct GuestPageTracker {
  rex::memory::BaseHeap* heap = nullptr;
  uint32_t page_base = 0;
  bool checked = false;
  bool readable = false;
};

struct WalkResult {
  uint32_t target = 0;
  bool valid = false;
  const char* stop_reason = nullptr;
  uint32_t commands_walked = 0;
  CommandRecord first_command{};
  bool has_first_command = false;
  std::array<CommandRecord, 8> head_commands{};
  std::size_t head_count = 0;
  std::array<CommandRecord, 4> stop_commands{};
  std::size_t stop_count = 0;
  std::size_t stop_next = 0;
};

bool ensure_guest_page_readable(uint32_t address, GuestPageTracker& tracker,
                                const char*& failure_reason) noexcept {
  auto* thread_state = rex::runtime::ThreadState::Get();
  if (thread_state == nullptr || thread_state->memory() == nullptr) {
    failure_reason = "read_failure";
    return false;
  }

  auto* heap = thread_state->memory()->LookupHeap(address);
  if (heap == nullptr) {
    failure_reason = "page_uncommitted";
    return false;
  }

  const uint32_t page_size = heap->page_size() == 0 ? 0x1000u : heap->page_size();
  const uint32_t page_base = address - (address % page_size);
  if (tracker.checked && tracker.heap == heap && tracker.page_base == page_base) {
    if (!tracker.readable)
      failure_reason = "page_uncommitted";
    return tracker.readable;
  }

  tracker.checked = true;
  tracker.heap = heap;
  tracker.page_base = page_base;
  tracker.readable =
      heap->QueryRangeAccess(address, address) != rex::memory::PageAccess::kNoAccess;
  if (!tracker.readable)
    failure_reason = "page_uncommitted";
  return tracker.readable;
}

bool read_command(uint64_t address, CommandRecord& command, GuestPageTracker& pages,
                  const char*& failure_reason) {
  failure_reason = nullptr;
  if (address > UINT32_MAX) {
    failure_reason = "address_overflow";
    return false;
  }

  command.address = static_cast<uint32_t>(address);
  for (std::size_t i = 0; i < command.words.size(); ++i) {
    const uint64_t word_address = address + i * 4u;
    if (word_address > UINT32_MAX - 3u) {
      failure_reason = "address_overflow";
      return false;
    }
    if (!ensure_guest_page_readable(static_cast<uint32_t>(word_address), pages,
                                    failure_reason) ||
        !load_guest_u32(static_cast<uint32_t>(word_address), command.words[i])) {
      if (failure_reason == nullptr)
        failure_reason = "read_failure";
      return false;
    }
  }
  command.readable = true;
  return true;
}

bool command_is_all_zero(const CommandRecord& command) noexcept {
  return command.readable && command.words[0] == 0 && command.words[1] == 0 &&
         command.words[2] == 0 && command.words[3] == 0;
}

void append_command_dump(char* buffer, std::size_t capacity, std::size_t& length,
                         const CommandRecord& command, bool first) noexcept {
  if (length >= capacity)
    return;

  const int written = std::snprintf(
      buffer + length, capacity - length,
      "%sidx=%llu@0x%08x:[0x%08x,0x%08x,0x%08x,0x%08x]%s", first ? "" : ";",
      static_cast<unsigned long long>(command.index), command.address, command.words[0],
      command.words[1], command.words[2], command.words[3], command.readable ? "" : ":unreadable");
  if (written < 0)
    return;
  if (static_cast<std::size_t>(written) >= capacity - length) {
    length = capacity - 1u;
    buffer[length] = '\0';
  } else {
    length += static_cast<std::size_t>(written);
  }
}

WalkResult walk_call_for_return(uint32_t target) {
  WalkResult result;
  result.target = target;
  GuestPageTracker pages;
  uint64_t index = 0;

  while (result.commands_walked < kMaxWalkCommands) {
    const uint64_t command_offset = index * 16u;
    if (command_offset > kMaxWalkSpanBytes) {
      result.stop_reason = "span";
      break;
    }
    const uint64_t command_address = static_cast<uint64_t>(target) + index * 16u;
    CommandRecord command_record{};
    command_record.index = index;
    const char* read_failure = nullptr;
    if (!read_command(command_address, command_record, pages, read_failure)) {
      if (!result.has_first_command) {
        result.first_command = command_record;
        result.has_first_command = true;
      }
      result.stop_reason = read_failure == nullptr ? "read_failure" : read_failure;
      break;
    }

    if (!result.has_first_command) {
      result.first_command = command_record;
      result.has_first_command = true;
    }
    if (result.head_count < result.head_commands.size())
      result.head_commands[result.head_count++] = command_record;
    result.stop_commands[result.stop_next] = command_record;
    result.stop_next = (result.stop_next + 1u) % result.stop_commands.size();
    if (result.stop_count < result.stop_commands.size())
      ++result.stop_count;
    ++result.commands_walked;

    const uint32_t opcode = command_record.words[0];
    if (opcode == 79) {
      result.valid = true;
      result.stop_reason = "return";
      return result;
    }
    if (opcode >= 101) {
      result.stop_reason =
          opcode == 101 ? "opcode_101" : (opcode == 102 ? "opcode_102" : "opcode_ge_101");
      break;
    }
    if (command_is_all_zero(command_record)) {
      result.stop_reason = "all_zero";
      break;
    }

    if (command_record.words[2] > kMaxCommandLength) {
      result.stop_reason = "bad_len";
      break;
    }

    // Opcode 78 is a nested CALL in the executor. The diagnostic deliberately
    // steps over it with the same stride rather than recursively walking it.
    const uint64_t next_index = index + static_cast<uint64_t>(command_record.words[2]) + 1u;
    const uint64_t next_offset = next_index * 16u;
    if (next_offset > kMaxWalkSpanBytes) {
      result.stop_reason = "span";
      break;
    }
    if (opcode == 78) {
      index = next_index;
      continue;
    }
    index = next_index;
  }

  if (result.stop_reason == nullptr)
    result.stop_reason = "bound";
  return result;
}

void log_walk_failure(uint32_t guest_r13, const WalkResult& result) {
  char dump[4096]{};
  std::size_t dump_length = 0;
  const int head_prefix =
      std::snprintf(dump, sizeof(dump), "head_count=%zu head=[", result.head_count);
  if (head_prefix > 0)
    dump_length = static_cast<std::size_t>(head_prefix);
  for (std::size_t i = 0; i < result.head_count; ++i)
    append_command_dump(dump, sizeof(dump), dump_length, result.head_commands[i], i == 0);

  if (dump_length < sizeof(dump)) {
    const int separator = std::snprintf(dump + dump_length, sizeof(dump) - dump_length,
                                        "] stop_count=%zu stop=[", result.stop_count);
    if (separator > 0)
      dump_length += static_cast<std::size_t>(separator);
  }

  const std::size_t stop_start =
      result.stop_count == result.stop_commands.size() ? result.stop_next : 0;
  for (std::size_t i = 0; i < result.stop_count; ++i) {
    const std::size_t slot = (stop_start + i) % result.stop_commands.size();
    append_command_dump(dump, sizeof(dump), dump_length, result.stop_commands[slot], i == 0);
  }
  if (dump_length < sizeof(dump)) {
    const int suffix = std::snprintf(dump + dump_length, sizeof(dump) - dump_length, "]");
    if (suffix > 0)
      dump_length += static_cast<std::size_t>(suffix);
  }
  dump[sizeof(dump) - 1u] = '\0';
  log_call_no_return(guest_r13, result.target, result.stop_reason, result.commands_walked, dump);
}

std::optional<WalkResult> handle_exec(uint32_t command, uint32_t guest_r13) {
  log_exec_thread_once(guest_r13);

  uint32_t target = 0;
  if (!load_guest_u32(command + 16u, target))
    return std::nullopt;

  log_call_into_open_recording(target, guest_r13);
  WalkResult walk = walk_call_for_return(target);
  if (!walk.valid)
    log_walk_failure(guest_r13, walk);

  if (!walk.has_first_command || !walk.first_command.readable)
    return walk;

  uint32_t target_value = 0;
  if (!load_guest_u32(target, target_value) || target_value != 0)
    return walk;

  bool abandoned = false;
  std::optional<UnreadyEntry> last_unready;
  {
    std::lock_guard lock(g_state_mutex);
    abandoned = abandoned_contains_locked(target);
    last_unready = last_unready_for_locked(target);
  }

  if (last_unready) {
    log_event(EventType::ZeroCall, guest_r13, true,
              "target=0x%08x value=0 abandoned=%s last_unready_target=0x%08x "
              "last_unready_slot=0x%08x last_unready_r13=0x%08x last_unready_t_ms=%llu",
              target, abandoned ? "yes" : "no", last_unready->target, last_unready->slot,
              last_unready->guest_r13,
              static_cast<unsigned long long>(last_unready->timestamp_ms));
  } else {
    log_event(EventType::ZeroCall, guest_r13, true,
              "target=0x%08x value=0 abandoned=%s last_unready=none", target,
              abandoned ? "yes" : "no");
  }

  return walk;
}

void log_guard_skip(const WalkResult& result, uint32_t guest_r13) {
  const uint64_t timestamp_ms = monotonic_ms();
  const uint64_t tid = host_tid();
  const CommandRecord& first = result.first_command;

  std::lock_guard lock(g_log_mutex);
  const uint64_t skip_count = g_guard_skip_count.fetch_add(1, std::memory_order_relaxed) + 1u;
  const bool summary_due =
      skip_count > kFullLineLimit &&
      (g_guard_last_summary_ms == 0 || timestamp_ms - g_guard_last_summary_ms >= kSummaryPeriodMs);
  if (skip_count <= kFullLineLimit || summary_due) {
    if (skip_count > kFullLineLimit)
      g_guard_last_summary_ms = timestamp_ms;
    std::fprintf(
        stderr,
        "[dlist-guard] t_ms=%llu host_tid=%llu guest_r13=0x%08x target=0x%08x "
        "reason=%s commands_walked=%u first_words=[0x%08x,0x%08x,0x%08x,0x%08x] "
        "skip_count=%llu%s\n",
        static_cast<unsigned long long>(timestamp_ms), static_cast<unsigned long long>(tid),
        guest_r13, result.target, result.stop_reason == nullptr ? "unknown" : result.stop_reason,
        result.commands_walked, first.words[0], first.words[1], first.words[2], first.words[3],
        static_cast<unsigned long long>(skip_count),
        skip_count > kFullLineLimit ? " rate_summary=10s" : "");
    std::fflush(stderr);
  }
}

}  // namespace

void dlist_tripwire_begin(PPCRegister& slot, PPCRegister& block, PPCRegister& guest_r13) {
  if (!tripwire_enabled())
    return;
  handle_begin(slot.u32, block.u32, guest_r13.u32);
}

void dlist_tripwire_end(PPCRegister& vm, PPCRegister& guest_r13) {
  if (!tripwire_enabled())
    return;
  handle_end(vm.u32, guest_r13.u32);
}

void dlist_tripwire_bail(PPCRegister& resolved, PPCRegister& object, PPCRegister& stack,
                         PPCRegister& guest_r13) {
  if (!tripwire_enabled())
    return;
  handle_bail(resolved.u32, object.u32, stack.u32, guest_r13.u32);
}

void dlist_tripwire_call(PPCRegister& target, PPCRegister& slot, PPCRegister& guest_r13) {
  if (!tripwire_enabled())
    return;
  handle_call(target.u32, slot.u32, guest_r13.u32);
}

void dlist_tripwire_exec(PPCRegister& command, PPCRegister& guest_r13) {
  if (!tripwire_enabled())
    return;
  (void)handle_exec(command.u32, guest_r13.u32);
}

bool dlist_call_guard(PPCRegister& command, PPCRegister& guest_r13) {
  std::optional<WalkResult> walk;
  if (tripwire_enabled())
    walk = handle_exec(command.u32, guest_r13.u32);

  if (!dlist_guard_enabled())
    return false;

  if (!walk) {
    uint32_t target = 0;
    if (!load_guest_u32(command.u32 + 16u, target)) {
      WalkResult failed_walk;
      failed_walk.stop_reason = "target_read_failure";
      log_guard_skip(failed_walk, guest_r13.u32);
      return true;
    }
    walk = walk_call_for_return(target);
  }

  if (walk->valid)
    return false;

  log_guard_skip(*walk, guest_r13.u32);
  return true;
}

// WWE '13 PC: diagnostic for the entrance crash (docs/NATIVE_PORT_PLAN.md, "0x3A962680").
//
// The render command queue object (constructor sub_82AA3E48, destructor sub_82AA41A8, replay
// sub_82AA5458) holds its command lists at +48 / +52 / +56 (64 KB aligned, allocated once by the
// constructor) and its two track arrays at +104 / +108. In the crashes, the replay reads lists that are
// not 64 KB aligned: these fields get overwritten. With --wwe13_renderq_watch=true, the x64 debug
// registers of every thread watch +48..+59 and +104..+111 of the newest queue object; each write is
// logged with the writing thread and the host code address (exe+offset: llvm-symbolizer with the
// exe's PDB turns it into the generated sub_XXXXXXXX). Creations and destructions of the object are
// logged in all cases. Off by default: diagnostic only (it suspends each thread once to arm it).

#include "generated/default/wwe13_init.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <algorithm>
#include <unordered_set>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

REXCVAR_DEFINE_BOOL(wwe13_renderq_watch, false, "WWE13",
                    "Debug: hardware write watch on the render queue object's list and track "
                    "pointers, logged as [wwe13-rqwatch] (entrance crash diagnostic)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(wwe13_renderq_sublist_watch, false, "WWE13",
                    "Debug: about twice a second, watch one sub-list the replay enters and log who "
                    "writes it while the replay runs, as [wwe13-rqwatch] (cost of the entrance fix)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_82AA3E48);  // render queue object constructor (r3 = object, returns it)
REX_EXTERN(__imp__sub_82AA41A8);  // its destructor
REX_EXTERN(__imp__sub_82AA3928);  // replay enters a sub-list (r3 = object, r4 = sub-list)

namespace {

std::atomic<uint8_t*> g_base{nullptr};
std::atomic<uint32_t> g_obj{0};         // newest queue object (guest address)
std::atomic<uint32_t> g_generation{0};  // bumped when g_obj changes
thread_local bool t_in_ctor = false;
std::atomic<bool> g_replaying{false};      // render_queue_guard.cpp, around the replay
std::atomic<uint32_t> g_sub_target{0};     // sub-list to watch (guest), set by the replay
std::atomic<uint32_t> g_sub_armed{0};      // sub-list the threads are armed on

uint8_t* Host(uint8_t* base, uint32_t a) { return base + a + (a >= 0xE0000000u ? 0x1000u : 0u); }
uint32_t Ld32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Host(base, a), 4);
  return __builtin_bswap32(v);
}

uint32_t Tid() {
#if defined(_WIN32)
  return static_cast<uint32_t>(GetCurrentThreadId());
#else
  return 0;
#endif
}

#if defined(_WIN32)
struct Hit {
  uint32_t tid, dr6, obj;
  uint64_t rip;
  uint32_t v48, v52, v56, v104, v108;
  bool in_ctor;
};
constexpr uint32_t kRing = 256;
Hit g_ring[kRing];
std::atomic<uint32_t> g_head{0};   // next slot to write
std::atomic<uint32_t> g_ready{0};  // slots fully written
std::atomic<uint32_t> g_dropped{0};
struct SubHit {
  uint32_t tid, sub;
  uint64_t rip;
  bool during_replay;
};
constexpr uint32_t kSubRing = 4096;
SubHit g_sub_ring[kSubRing];
std::atomic<uint32_t> g_sub_hits{0};
uint32_t g_sub_logged = 0;

LONG CALLBACK OnException(EXCEPTION_POINTERS* ep) {
  if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
    return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* c = ep->ContextRecord;
  const DWORD64 dr6 = c->Dr6;
  if (dr6 & 0x8) {
    const uint32_t n = g_sub_hits.fetch_add(1, std::memory_order_relaxed);
    if (n < kSubRing)
      g_sub_ring[n] = {Tid(), g_sub_armed.load(std::memory_order_relaxed), c->Rip,
                       g_replaying.load(std::memory_order_relaxed)};
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (!(dr6 & 0x7))
    return EXCEPTION_CONTINUE_SEARCH;
  uint8_t* base = g_base.load(std::memory_order_relaxed);
  const uint32_t obj = g_obj.load(std::memory_order_relaxed);
  const uint32_t slot = g_head.fetch_add(1, std::memory_order_relaxed);
  if (slot - g_ready.load(std::memory_order_acquire) < kRing && base && obj) {
    Hit& h = g_ring[slot % kRing];
    h = {Tid(), static_cast<uint32_t>(dr6), obj, c->Rip, Ld32(base, obj + 48), Ld32(base, obj + 52),
         Ld32(base, obj + 56), Ld32(base, obj + 104), Ld32(base, obj + 108), t_in_ctor};
  } else {
    g_dropped.fetch_add(1, std::memory_order_relaxed);
  }
  c->Dr6 = 0;
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Debug registers of one thread: DR0 = obj+48 (8 bytes), DR1 = obj+56 (4), DR2 = obj+104 (8), all
// on write. A thread is armed once per object generation.
bool Arm(DWORD tid, uint8_t* base, uint32_t obj) {
  HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
  if (!t)
    return false;
  bool ok = false;
  if (SuspendThread(t) != DWORD(-1)) {
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(t, &c)) {
      c.Dr0 = reinterpret_cast<DWORD64>(Host(base, obj + 48));
      c.Dr1 = reinterpret_cast<DWORD64>(Host(base, obj + 56));
      c.Dr2 = reinterpret_cast<DWORD64>(Host(base, obj + 104));
      c.Dr3 = 0;
      c.Dr6 = 0;
      // L0 L1 L2; RW = 01 (write); LEN0 = 10 (8 bytes), LEN1 = 11 (4 bytes), LEN2 = 10 (8 bytes).
      c.Dr7 = (1ull << 0) | (1ull << 2) | (1ull << 4) | (1ull << 16) | (2ull << 18) | (1ull << 20) |
              (3ull << 22) | (1ull << 24) | (2ull << 26);
      ok = SetThreadContext(t, &c) != FALSE;
    }
    ResumeThread(t);
  }
  CloseHandle(t);
  return ok;
}

// DR3 = sub-list +0 (8 bytes, write), keeping DR0..DR2.
bool ArmSub(DWORD tid, uint8_t* base, uint32_t sub) {
  HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
  if (!t)
    return false;
  bool ok = false;
  if (SuspendThread(t) != DWORD(-1)) {
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(t, &c)) {
      c.Dr3 = reinterpret_cast<DWORD64>(Host(base, sub));
      c.Dr7 = (c.Dr7 & ~((3ull << 28) | (3ull << 30))) | (1ull << 6) | (1ull << 28) | (2ull << 30);
      ok = SetThreadContext(t, &c) != FALSE;
    }
    ResumeThread(t);
  }
  CloseHandle(t);
  return ok;
}

void ArmSubAll(uint8_t* base, uint32_t sub) {
  const DWORD self = GetCurrentThreadId();
  const DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return;
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te))
    if (te.th32OwnerProcessID == pid && te.th32ThreadID != self)
      ArmSub(te.th32ThreadID, base, sub);
  CloseHandle(snap);
}

void DrainSub() {
  const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const uint32_t n = std::min<uint32_t>(g_sub_hits.load(), kSubRing);
  for (; g_sub_logged < n; ++g_sub_logged) {
    const SubHit& h = g_sub_ring[g_sub_logged];
    if (h.rip == 0)
      break;  // not fully written yet
    REXLOG_WARN("[wwe13-rqwatch] sub-list 0x{:08X} written {} by thread {} at exe+0x{:X}", h.sub,
                h.during_replay ? "DURING the replay" : "between replays", h.tid,
                static_cast<uint64_t>(h.rip - exe));
  }
}

void Drain() {
  const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  uint32_t ready = g_ready.load(std::memory_order_relaxed);
  const uint32_t head = g_head.load(std::memory_order_acquire);
  // Entries are written right after their slot is taken; give writers a moment by only reading
  // slots taken before this drain started and logging them on the next pass at the latest.
  while (ready != head) {
    const Hit& h = g_ring[ready % kRing];
    const char* what = (h.dr6 & 1) ? "+48/+52 (lists)" : (h.dr6 & 2) ? "+56 (list 3)" : "+104/+108 (tracks)";
    REXLOG_WARN("[wwe13-rqwatch] write to obj 0x{:08X} {} by thread {} at exe+0x{:X}{}: lists "
                "{:08X} {:08X} {:08X}, tracks {:08X} {:08X}",
                h.obj, what, h.tid, static_cast<uint64_t>(h.rip - exe), h.in_ctor ? " (constructor)" : "",
                h.v48, h.v52, h.v56, h.v104, h.v108);
    ++ready;
    g_ready.store(ready, std::memory_order_release);
  }
}

void WatchThread() {
  const DWORD self = GetCurrentThreadId();
  const DWORD pid = GetCurrentProcessId();
  std::unordered_set<DWORD> armed;
  uint32_t armed_gen = 0;
  uint32_t ticks = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Drain();
    DrainSub();
    if (REXCVAR_GET(wwe13_renderq_sublist_watch) && ticks % 25 == 12) {
      const uint32_t sub = g_sub_target.load();
      uint8_t* b = g_base.load();
      if (sub && b && sub != g_sub_armed.load()) {
        g_sub_armed.store(sub);
        ArmSubAll(b, sub);
      }
    }
    if (++ticks % 25)
      continue;  // arm new threads every 500 ms
    uint8_t* base = g_base.load();
    const uint32_t obj = g_obj.load();
    const uint32_t gen = g_generation.load();
    if (!base || !obj || !REXCVAR_GET(wwe13_renderq_watch))
      continue;
    if (gen != armed_gen) {
      armed.clear();
      armed_gen = gen;
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
      continue;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    uint32_t newly = 0;
    for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
      if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || armed.count(te.th32ThreadID))
        continue;
      if (Arm(te.th32ThreadID, base, obj)) {
        armed.insert(te.th32ThreadID);
        ++newly;
      }
    }
    CloseHandle(snap);
    if (newly)
      REXLOG_INFO("[wwe13-rqwatch] watching obj 0x{:08X}: {} more threads armed ({} in all){}", obj,
                  newly, armed.size(),
                  g_dropped.load() ? " - some hits dropped" : "");
  }
}

void StartWatch() {
  static std::atomic<bool> started{false};
  if (started.exchange(true))
    return;
  AddVectoredExceptionHandler(1, OnException);
  std::thread(WatchThread).detach();
  REXLOG_INFO("[wwe13-rqwatch] hardware write watch on the render queue object active");
}
#endif

}  // namespace

extern "C" REX_FUNC(sub_82AA3E48) {
  g_base.store(base, std::memory_order_relaxed);
  t_in_ctor = true;
  __imp__sub_82AA3E48(ctx, base);
  t_in_ctor = false;
  const uint32_t obj = ctx.r3.u32;
  REXLOG_INFO("[wwe13-rqwatch] render queue object 0x{:08X} created by thread {}: lists {:08X} {:08X} "
              "{:08X}, mode {}, tracks {}",
              obj, Tid(), Ld32(base, obj + 48), Ld32(base, obj + 52), Ld32(base, obj + 56),
              Ld32(base, obj + 116), Ld32(base, obj + 132));
  g_obj.store(obj);
  g_generation.fetch_add(1);
#if defined(_WIN32)
  if (REXCVAR_GET(wwe13_renderq_watch) || REXCVAR_GET(wwe13_renderq_sublist_watch))
    StartWatch();
#endif
}

extern "C" REX_FUNC(sub_82AA41A8) {
  const uint32_t obj = ctx.r3.u32;
  // The watch stays on the destroyed object: writes after this line show who reuses its memory.
  REXLOG_INFO("[wwe13-rqwatch] render queue object 0x{:08X} destroyed by thread {}", obj, Tid());
  __imp__sub_82AA41A8(ctx, base);
}

// render_queue_guard.cpp, around each replay of the render queue.
void Wwe13ReplayMark(bool begin) { g_replaying.store(begin, std::memory_order_relaxed); }

extern "C" REX_FUNC(sub_82AA3928) {
  // Remember a sub-list the replay is reading, for the watch thread to arm on (diagnostic only).
  if (REXCVAR_GET(wwe13_renderq_sublist_watch) && g_replaying.load(std::memory_order_relaxed))
    g_sub_target.store(ctx.r4.u32, std::memory_order_relaxed);
  __imp__sub_82AA3928(ctx, base);
}

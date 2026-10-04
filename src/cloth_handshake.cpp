// WWE '13 Havok-cloth handshake race fix (port of the verified 2K14 fix,
// wwe2k14-recomp/src/cloth_handshake.cpp).
//
// The HavokClothThread (guest entry 0x824A0268) is a persistent worker: it
// loops waiting on the manual-reset "StartCloth" event (+0x68), then clears
// StartCloth, runs one job (sub_8249FF60), sets the manual-reset "EndCloth"
// event (+0x6c), and waits again. The game's begin() (0x8249FBE0) and end()
// (0x8249FDE0) drive it:
//
//   begin(): [obj+0x64] = 1; SetEvent(StartCloth); ClearEvent(EndCloth);
//   end():   if ([obj+0x58] > 0 && [obj+0x64] != 0) {
//              [obj+0x64] = 0; WaitForSingleObjectEx(EndCloth, INFINITE);
//              ClearEvent(EndCloth); }
//
// On the Xbox 360 the worker cannot complete a job between begin()'s
// SetEvent(StartCloth) and ClearEvent(EndCloth), so clearing EndCloth after
// signalling StartCloth is harmless. On the recompiled build the worker runs
// on another host core: it wakes on StartCloth, clears it, runs the job and
// sets EndCloth all before begin() reaches ClearEvent(EndCloth), which then
// wipes the completion signal. end()'s infinite wait for EndCloth never
// returns and the game freezes in the cloth/scene teardown that runs after a
// cloth-wearing superstar's entrance (the same bug that froze 2K14's
// Undertaker entrance).
//
// Evidence (this tree's generated code):
//   begin()  generated/wwe13_recomp.22.cpp:746-757
//            stw r11,100(r31) (748) -> bl sub_82200BF0 (750) -> bl sub_82B760B0 (755)
//   end()    generated/wwe13_recomp.22.cpp:1064-1075
//            [obj+0x64]=0 -> Wait(StartCloth obj+0x6c, INFINITE) -> Clear(same)
//   worker   generated/wwe13_recomp.22.cpp:1801-1835
//   offset/name confirmation: constructor generated/wwe13_recomp.22.cpp:1572-1657
//   singleton 0x83478054: generated/wwe13_recomp.22.cpp:732-735
//
// Fix: clear EndCloth BEFORE signalling StartCloth. The worker can then only
// set EndCloth after the job that StartCloth triggers, so the completion
// signal cannot be lost. This is a strong PPC_FUNC override of begin(); the
// generated implementation is a weak alias and is replaced at link time (same
// mechanism as src/present_interval.cpp).
//
// The two teardown paths (sub_8249FC38, destructor sub_8249FE80) have the same
// store-1/set/clear order but are NOT reordered: they signal the worker only to
// make it consume the stop flag [obj+0x10]=1 and exit, never wait EndCloth, and
// the worker skips setting EndCloth once stopped (generated/wwe13_recomp.22.cpp:
// 1814-1837). There is therefore no completion signal for the clear to swallow.
// See docs/CLOTH-RACE-FIX.md section 2.

#include <atomic>
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/memory.h>

namespace {
// Guest addresses from generated/wwe13_recomp.22.cpp (this tree):
//   singleton 0x83478054        (PPC_FUNC_IMPL(__imp__sub_8249FBE0), :732-735)
//   run flag  +0x64             (:748)
//   StartCloth +0x68 "StartCloth" (constructor :1639-1643; name 0x82018820)
//   EndCloth   +0x6c "EndCloth"   (constructor :1654-1657; name 0x82018814)
constexpr uint32_t kClothSingleton = 0x83478054;
constexpr uint32_t kRunFlag = 100;     // +0x64
constexpr uint32_t kStartEvent = 104;  // +0x68, "StartCloth"
constexpr uint32_t kEndEvent = 108;    // +0x6c, "EndCloth"
}  // namespace

extern "C" PPC_FUNC(__imp__sub_82200BF0);  // set-event wrapper -> __imp__NtSetEvent (stub 0x8334DFE4)
extern "C" PPC_FUNC(__imp__sub_82B760B0);  // clear-event wrapper -> __imp__NtClearEvent (stub 0x8334E5F4)

extern "C" PPC_FUNC(sub_8249FBE0) {
  const uint32_t object = PPC_LOAD_U32(kClothSingleton);
  if (object == 0) {
    return;
  }
  const uint32_t start_event = PPC_LOAD_U32(object + kStartEvent);
  if (start_event == 0) {
    return;
  }
  PPC_STORE_U32(object + kRunFlag, 1);

  // Clear the previous completion signal first, then ask the worker to run.
  // ctx.lr values are the guest return addresses the original begin() used:
  // 0x8249FC18 after the SetEvent call (generated/wwe13_recomp.22.cpp:751),
  // 0x8249FC20 after the ClearEvent call (:756).
  ctx.r3.u32 = PPC_LOAD_U32(object + kEndEvent);
  ctx.lr = 0x8249FC20;
  __imp__sub_82B760B0(ctx, base);
  ctx.r3.u32 = PPC_LOAD_U32(object + kStartEvent);
  ctx.lr = 0x8249FC18;
  __imp__sub_82200BF0(ctx, base);

  // One line per process proves the strong override is the code running.
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true, std::memory_order_relaxed)) {
    REXLOG_INFO("[wwe13-cloth] handshake reorder active: ClearEvent(EndCloth) before SetEvent(StartCloth)");
  }
}

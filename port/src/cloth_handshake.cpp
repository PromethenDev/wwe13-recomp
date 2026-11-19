// WWE '13 PC: Havok cloth handshake race fix (the Ministry Undertaker entrance freeze), ported from the SDK
// 0.7.3 build (src/cloth_handshake.cpp at the repository root, original project v1.1, itself a port of the
// WWE 2K14 recomp fix). Same guest code in the SDK 0.10 codegen (sub_8249FBE0, checked 2026-11-17).
//
// The HavokClothThread waits on the manual-reset "StartCloth" event (+0x68), clears it, runs one job and sets
// "EndCloth" (+0x6c). The game's begin() (0x8249FBE0) does: [obj+0x64] = 1; SetEvent(StartCloth);
// ClearEvent(EndCloth). On the console the worker cannot finish between the two calls; on a PC it can, and
// ClearEvent then wipes the completion signal: end() waits forever (freeze after a cloth-wearing superstar's
// entrance). Fix: clear EndCloth BEFORE signalling StartCloth.

#include "generated/default/wwe13_init.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/logging/macros.h>

REX_EXTERN(__imp__sub_82200BF0);  // set-event wrapper (NtSetEvent)
REX_EXTERN(__imp__sub_82B760B0);  // clear-event wrapper (NtClearEvent)

namespace {
constexpr uint32_t kClothSingleton = 0x83478054;
constexpr uint32_t kRunFlag = 100;     // +0x64
constexpr uint32_t kStartEvent = 104;  // +0x68, "StartCloth"
constexpr uint32_t kEndEvent = 108;    // +0x6c, "EndCloth"

uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
void St32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
}  // namespace

extern "C" REX_FUNC(sub_8249FBE0) {
  const uint32_t object = Ld32(base, kClothSingleton);
  if (object == 0) return;
  if (Ld32(base, object + kStartEvent) == 0) return;
  St32(base, object + kRunFlag, 1);
  // ctx.lr: the guest return addresses the original begin() used (after ClearEvent, after SetEvent).
  ctx.r3.u64 = Ld32(base, object + kEndEvent);
  ctx.lr = 0x8249FC20;
  __imp__sub_82B760B0(ctx, base);
  ctx.r3.u64 = Ld32(base, object + kStartEvent);
  ctx.lr = 0x8249FC18;
  __imp__sub_82200BF0(ctx, base);
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true, std::memory_order_relaxed))
    REXLOG_INFO("[wwe13-cloth] handshake reorder active: ClearEvent(EndCloth) before SetEvent(StartCloth)");
}

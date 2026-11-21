// WWE '13 PC: the game's once-per-frame "who controls music playback?" question, answered without a 10 ms sleep.
//
// Found 2026-11-21 (PIX capture nx1, UHD 630, match play): the game's frame thread (XThread F8000030) switches out
// once per frame with DelayExecution for ~10.5 ms, called from sub_829BC400 -> sub_829BF738 -> sub_829BF3D0 ->
// sub_82CBBD20 -> sub_82CBBC90 -> XMsgInProcessCall(app 250 = XMP, message 0x0007001B = XMPGetPlaybackController).
// The SDK's XMP app (from Xenia, src/kernel/xam/apps/xmp_app.cpp) sleeps 10 ms in that message on any thread but the
// title's main thread ("Atrain spawns a thread to call this in a tight loop forever"). The render loop then waits for
// that thread (render thread woken by it), so every frame = the thread's ~7 ms of work + the 10 ms sleep: ~17-19 ms,
// i.e. the ~57-58 fps ceiling seen on the UHD 630 with all three renderers, and part of the laptop's 18.6 ms.
//
// sub_82CBBC90(r3 = XMP client, r4 = controller out, r5 = locked out) only packs those into the message call and
// returns 0 on success. The SDK's answer is fixed: controller 0, locked 0, success. With
// --wwe13_xmp_no_sleep=true (default) this override gives the same answer without entering the message call (so
// without the sleep); false = the original call. Read per call, so --wwe13_live_ab can switch it inside one run.

#include "generated/default/wwe13_init.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_BOOL(wwe13_xmp_no_sleep, true, "WWE13",
                    "Answer the game's per-frame XMPGetPlaybackController call directly (the SDK's answer, without "
                    "its 10 ms sleep on non-main threads)");

REX_EXTERN(__imp__sub_82CBBC90);

namespace {
void StoreBe32(uint8_t* base, uint32_t guest, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + guest + (guest >= 0xE0000000u ? 0x1000u : 0u), &v, 4);
}
std::atomic<uint32_t> g_logged{0};
}  // namespace

extern "C" REX_FUNC(sub_82CBBC90) {
  if (!REXCVAR_GET(wwe13_xmp_no_sleep)) {
    __imp__sub_82CBBC90(ctx, base);
    return;
  }
  const uint32_t client = ctx.r3.u32, controller_ptr = ctx.r4.u32, locked_ptr = ctx.r5.u32;
  if (g_logged.fetch_add(1, std::memory_order_relaxed) == 0)
    REXLOG_INFO("[wwe13-xmp] XMPGetPlaybackController answered directly (client {}, no sleep)", client);
  if (controller_ptr) StoreBe32(base, controller_ptr, 0);
  if (locked_ptr) StoreBe32(base, locked_ptr, 0);
  ctx.r3.u64 = 0;
}

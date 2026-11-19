// WWE '13 PC: the main menu's SHOP entry (Xbox LIVE marketplace, dead today) becomes EXIT and closes the
// game, as in WWE '12 PC and WWE 2K15 PC. Ported from the SDK 0.7.3 build (src/exit_entry.cpp at the
// repository root, checked in game on 2026-11-17).
//
// Label: the menu list drawer (sub_82781BD0) gives each entry's label string id to
// sub_8277E2B0(list, index, label), which sets the entry's text from the game's string tables. SHOP is
// label 41035 ("BOUTIQUE <W>" in French); 45061 is the game's own, already translated "EXIT" (QUITTER /
// ESCI / BEENDEN / SALIR). Only sub_82781BD0 calls sub_8277E2B0, and 41035 is only SHOP.
//
// Action: choosing SHOP first shows a notice, message 52051, through the dialog object's show method
// (vtable 0x820472D0 + 0x6C = sub_828325F0, r4 = params, +4 = message id), used nowhere else: the game
// window is asked to close (the same path as its close button) and the notice is not shown.
//
// wwe13_exit_entry = false keeps the original SHOP entry.

#include "generated/default/wwe13_init.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_BOOL(wwe13_exit_entry, true, "WWE13",
                    "Main menu: SHOP becomes EXIT (the game's own translated label) and closes the game");

REX_EXTERN(__imp__sub_8277E2B0);
REX_EXTERN(__imp__sub_828325F0);

namespace {
constexpr uint32_t kShopLabel = 41035;
constexpr uint32_t kExitLabel = 45061;
constexpr uint32_t kShopNotice = 52051;

uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}

BOOL CALLBACK CloseOwnWindow(HWND hwnd, LPARAM) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
  return TRUE;
}
}  // namespace

extern "C" REX_FUNC(sub_8277E2B0) {
  if (REXCVAR_GET(wwe13_exit_entry) && ctx.r5.u32 == kShopLabel) {
    ctx.r5.u64 = kExitLabel;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_relaxed)) REXLOG_INFO("[wwe13-exit] main menu: SHOP shown as EXIT");
  }
  __imp__sub_8277E2B0(ctx, base);
}

extern "C" REX_FUNC(sub_828325F0) {
  if (REXCVAR_GET(wwe13_exit_entry) && ctx.r4.u32 != 0 && Ld32(base, ctx.r4.u32 + 4) == kShopNotice) {
    static std::atomic<bool> closing{false};
    if (!closing.exchange(true)) {
      REXLOG_INFO("[wwe13-exit] EXIT chosen in the main menu: closing the game");
      EnumWindows(CloseOwnWindow, 0);
    }
    return;
  }
  __imp__sub_828325F0(ctx, base);
}

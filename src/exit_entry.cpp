// WWE '13 main menu: the SHOP entry (Xbox LIVE marketplace, dead today) becomes EXIT and closes
// the game, as in the WWE '12 PC port.
//
// Label: the menu list drawer (sub_82781BD0) gives each entry's label string id to
// sub_8277E2B0(list, index, label), which sets the entry's text from the game's string tables.
// SHOP is label 41035 ("BOUTIQUE <W>" in French); 45061 is the game's own, already translated
// "EXIT" (QUITTER / ESCI / BEENDEN / SALIR), loaded with the menu tables. Only sub_82781BD0
// calls sub_8277E2B0, and 41035 is only the main menu's SHOP entry.
//
// Action: choosing SHOP first shows a notice, message 52051 ("After you purchase an item from
// the Shop, go back to the title screen..."), through the dialog object's show method
// (vtable 0x820472D0 + 0x6C = sub_828325F0, r4 = params, +4 = message id). Message 52051 is used
// nowhere else, so seeing it means the player chose the entry: the game window is asked to close
// (the same path as its close button) and the notice is not shown.
//
// Found 2026-11-17 with a hardware watchpoint on the in-memory menu records (0x4C bytes:
// +0 id, +4 screen 0x75, +8 page 1, +24 label, +44 target screen 1525 for SHOP).
//
// WWE13_EXIT_ENTRY=0 keeps the original SHOP entry.
//
// Closing: on Windows the game's own windows get WM_CLOSE (what the SDK's Window::RequestClose sends);
// on Linux (GTK window, no <windows.h>) wwe13_app.h registers a handler that asks the window to close
// on the UI thread (Wwe13SetExitHandler).

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/memory.h>

extern "C" PPC_FUNC(__imp__sub_8277E2B0);
extern "C" PPC_FUNC(__imp__sub_828325F0);

namespace {
std::mutex g_exit_handler_mutex;
std::function<void()> g_exit_handler;
}  // namespace

void Wwe13SetExitHandler(std::function<void()> handler) {
  std::lock_guard lock(g_exit_handler_mutex);
  g_exit_handler = std::move(handler);
}

namespace {
constexpr uint32_t kShopLabel = 41035;
constexpr uint32_t kExitLabel = 45061;
constexpr uint32_t kShopNotice = 52051;

bool ExitEntryEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("WWE13_EXIT_ENTRY");
    return !(v && v[0] == '0');
  }();
  return enabled;
}

#if defined(_WIN32)
BOOL CALLBACK CloseOwnWindow(HWND hwnd, LPARAM) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
  return TRUE;
}
#endif

void CloseGame() {
#if defined(_WIN32)
  EnumWindows(CloseOwnWindow, 0);
#else
  std::function<void()> handler;
  {
    std::lock_guard lock(g_exit_handler_mutex);
    handler = g_exit_handler;
  }
  if (handler) {
    handler();
  } else {
    REXLOG_WARN("[wwe13-exit] no window to close yet");
  }
#endif
}
}  // namespace

extern "C" PPC_FUNC(sub_8277E2B0) {
  if (ExitEntryEnabled() && ctx.r5.u32 == kShopLabel) {
    ctx.r5.u32 = kExitLabel;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_relaxed)) {
      REXLOG_INFO("[wwe13-exit] main menu: SHOP shown as EXIT");
    }
  }
  __imp__sub_8277E2B0(ctx, base);
}

extern "C" PPC_FUNC(sub_828325F0) {
  if (ExitEntryEnabled() && ctx.r4.u32 != 0 && PPC_LOAD_U32(ctx.r4.u32 + 4) == kShopNotice) {
    static std::atomic<bool> closing{false};
    if (!closing.exchange(true)) {
      REXLOG_INFO("[wwe13-exit] EXIT chosen in the main menu: closing the game");
      CloseGame();
    }
    return;
  }
  __imp__sub_828325F0(ctx, base);
}

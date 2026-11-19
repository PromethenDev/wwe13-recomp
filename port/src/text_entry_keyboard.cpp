// WWE '13 PC: type with the PC keyboard in the game's on-screen keyboard (Create a Wrestler names, Create a
// Story text, titles...), on the ReXGlue SDK 0.10 without SDK changes.
//
// Ported from the SDK 0.7.3 build (src/text_entry_keyboard.cpp at the repository root + that SDK's patched
// keyboard driver). The game's own on-screen keyboard is a block created by sub_82367730 and destroyed by
// sub_82367348 / sub_82367798; while one is up (its "SPACE" label at +0x40 and +0x38 == 2), each typed
// character moves the keyboard's cursor (+0x28 key index, +0x20 shift layer) onto that key and a pad A press
// is injected; Space = Y, Backspace = X, Enter = Start, Escape = B (the on-screen keyboard's own buttons).
// Every action is a 50 ms press followed by 50 ms of nothing, so the game sees clean edges. The presses are
// added to player 1's pad state in the game's only gamepad read (sub_82B77A68 -> XamInputGetState), so the
// same path serves every screen that uses this keyboard.
//
// Keys come from the game window (as in WWE 2K15 PC's text_entry_kbd.cpp): OnKeyChar is the text Windows
// produces (AZERTY, Shift, AltGr work); while the keyboard is up the typing keys are kept away from mnk_mode
// (keyboard-as-controller), the arrows still move the cursor.
//
// wwe13_keyboard_typing = false turns it off.

#include "generated/default/wwe13_init.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/logging/macros.h>
#include <rex/ui/window.h>

REXCVAR_DEFINE_BOOL(wwe13_keyboard_typing, true, "WWE13",
                    "Type with the PC keyboard in the game's on-screen keyboard (CAW names, Create a Story)");
REXCVAR_DEFINE_BOOL(wwe13_keyboard_typing_log, false, "WWE13", "Debug: log each typed key");

REX_EXTERN(__imp__sub_82367730);  // on-screen keyboard: create (r3 = block)
REX_EXTERN(__imp__sub_82367348);  // destroy
REX_EXTERN(__imp__sub_82367798);  // destroy (other path)
REX_EXTERN(__imp__sub_82B77A68);  // gamepad read: (user, XINPUT_STATE*) -> XamInputGetState

namespace {

constexpr uint16_t kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000, kPadStart = 0x0010;
constexpr auto kHold = std::chrono::milliseconds(50);
constexpr auto kGap = std::chrono::milliseconds(50);

struct Action {
  char16_t character;  // 0 for a plain button
  uint16_t button;
};

uint8_t* g_base = nullptr;  // guest memory base, seen in the hooks
rex::ui::Window* g_window = nullptr;
std::mutex g_mutex;
std::unordered_set<uint32_t> g_blocks;  // live on-screen keyboard blocks
std::deque<Action> g_queue;
uint16_t g_held = 0;
std::chrono::steady_clock::time_point g_phase_end{};

uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
void St32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}

// The keyboard is recognised by its Space key's label (string 40600) at +0x40, in the game's language: the
// SDK 0.7.3 build only knew the English "SPACE", so it never typed in a French, Italian, German or Spanish
// game (found 2026-11-17: "ESPACE" in a French game).
bool IsActiveKeyboard(const uint8_t* base, uint32_t block) {
  if (!block || Ld32(base, block + 0x38) != 2) return false;
  static const char* const kSpaceLabels[] = {"SPACE", "ESPACE", "SPAZIO", "LEERTASTE", "ESPACIO"};
  const char* label = reinterpret_cast<const char*>(base + block + 0x40);
  for (const char* want : kSpaceLabels)
    if (std::strncmp(label, want, std::strlen(want) + 1) == 0) return true;
  return false;
}

// Caller holds g_mutex.
uint32_t ActiveBlock() {
  if (!g_base) return 0;
  for (uint32_t block : g_blocks)
    if (IsActiveKeyboard(g_base, block)) return block;
  return 0;
}

struct KeyLocation {
  uint32_t index;
  uint32_t shifted;
};

// The on-screen keyboard's layout (US): key index and shift layer for each character.
bool MapCharacter(char16_t c, KeyLocation* k) {
  static const char16_t kDigits[] = u"1234567890", kShiftedDigits[] = u"!@#$%^&*()";
  static const char16_t kTop[] = u"QWERTYUIOP", kHome[] = u"ASDFGHJKL", kBottom[] = u"ZXCVBNM";
  for (uint32_t i = 0; i < 10; ++i) {
    if (c == kDigits[i]) return *k = {2 + i, 0}, true;
    if (c == kShiftedDigits[i]) return *k = {2 + i, 1}, true;
  }
  const auto row = [&](const char16_t* letters, uint32_t first) {
    for (uint32_t i = 0; letters[i]; ++i) {
      if (c == letters[i]) return *k = {first + i, 1}, true;
      if (c == letters[i] - u'A' + u'a') return *k = {first + i, 0}, true;
    }
    return false;
  };
  if (row(kTop, 16) || row(kHome, 31) || row(kBottom, 44)) return true;
  switch (c) {
    case u'-': return *k = {12, 0}, true;
    case u'_': return *k = {12, 1}, true;
    case u'=': return *k = {13, 0}, true;
    case u'+': return *k = {13, 1}, true;
    case u'[': return *k = {26, 0}, true;
    case u'{': return *k = {26, 1}, true;
    case u'|': return *k = {27, 0}, true;
    case u'`': return *k = {27, 1}, true;
    case u']': return *k = {28, 0}, true;
    case u'}': return *k = {28, 1}, true;
    case u';': return *k = {40, 0}, true;
    case u':': return *k = {40, 1}, true;
    case u'\'': return *k = {41, 0}, true;
    case u'"': return *k = {41, 1}, true;
    case u',': return *k = {51, 0}, true;
    case u'<': return *k = {51, 1}, true;
    case u'.': return *k = {52, 0}, true;
    case u'>': return *k = {52, 1}, true;
    case u'/': return *k = {53, 0}, true;
    case u'?': return *k = {53, 1}, true;
    default: return false;
  }
}

// Accented letters typed on a French keyboard become their plain letter (the game's font is ASCII).
char16_t Plain(uint32_t cp) {
  if (cp >= 0x20 && cp < 0x7F) return char16_t(cp);
  switch (cp) {
    case 0xE0: case 0xE1: case 0xE2: case 0xE4: return u'a';
    case 0xC0: case 0xC1: case 0xC2: case 0xC4: return u'A';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return u'e';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return u'E';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return u'i';
    case 0xF2: case 0xF3: case 0xF4: case 0xF6: return u'o';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return u'u';
    case 0xE7: return u'c';
    case 0xC7: return u'C';
    default: return 0;
  }
}

bool IsTypingKey(uint16_t vk) {
  return vk == 0x08 || vk == 0x20 || (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') ||
         (vk >= 0x60 && vk <= 0x6F) || (vk >= 0xBA && vk <= 0xC0) || (vk >= 0xDB && vk <= 0xDF) || vk == 0xE2;
}

void Push(Action a) {
  if (g_queue.size() < 256) g_queue.push_back(a);
}

class KbdListener final : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!REXCVAR_GET(wwe13_keyboard_typing) || e.is_ctrl_pressed()) return;
    std::lock_guard lock(g_mutex);
    if (!ActiveBlock()) return;
    const auto vk = static_cast<uint16_t>(e.virtual_key());
    switch (vk) {
      case 0x08: Push({0, kPadX}); e.set_handled(true); return;      // Backspace: delete
      case 0x0D: Push({0, kPadStart}); e.set_handled(true); return;  // Enter: done
      case 0x1B: Push({0, kPadB}); e.set_handled(true); return;      // Escape: back
      default:
        if (IsTypingKey(vk)) e.set_handled(true);  // typed through OnKeyChar, not as a pad button
    }
  }

  void OnKeyChar(rex::ui::KeyEvent& e) override {
    if (!REXCVAR_GET(wwe13_keyboard_typing)) return;
    std::lock_guard lock(g_mutex);
    if (!ActiveBlock()) return;
    const char16_t c = Plain(static_cast<uint32_t>(e.virtual_key()));
    if (c == u' ') Push({0, kPadY});
    else if (c) Push({c, kPadA});
    e.set_handled(true);
  }
};

KbdListener g_listener;

// SDL only sends typed text while text input is on (the SDK's ImGui turns it off when its boxes close).
void KeepTextInputOn() {
  rex::ui::Window* window = g_window;
  if (!window || window->IsTextInputActive()) return;
  window->app_context().CallInUIThread([window] { window->SetTextInputActive(true); });
}

// Game thread, inside the gamepad read: the button to add to player 1's state now (0 = none).
uint16_t NextButton(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  const uint32_t block = ActiveBlock();
  if (!block) {
    g_queue.clear();
    g_held = 0;
    return 0;
  }
  KeepTextInputOn();
  const auto now = std::chrono::steady_clock::now();
  if (g_held) {
    if (now < g_phase_end) return g_held;
    g_held = 0;
    g_phase_end = now + kGap;
    return 0;
  }
  if (now < g_phase_end) return 0;
  while (!g_queue.empty()) {
    const Action a = g_queue.front();
    g_queue.pop_front();
    if (a.character) {
      KeyLocation k{};
      if (!MapCharacter(a.character, &k)) continue;
      St32(base, block + 0x28, k.index);
      St32(base, block + 0x20, k.shifted);
      if (REXCVAR_GET(wwe13_keyboard_typing_log))
        REXLOG_INFO("[wwe13-keyboard] U+{:04X} -> key {} shift {}", uint32_t(a.character), k.index, k.shifted);
    }
    g_held = a.button;
    g_phase_end = now + kHold;
    return g_held;
  }
  return 0;
}

}  // namespace

// Call once the window exists (Wwe13App::OnPreLaunchModule).
void Wwe13KeyboardTypingInit(rex::ui::Window* window) {
  g_window = window;
  if (window) window->AddInputListener(&g_listener, 1000);  // before mnk_mode's listener
}

extern "C" REX_FUNC(sub_82367730) {
  const uint32_t block = ctx.r3.u32;
  __imp__sub_82367730(ctx, base);
  if (!REXCVAR_GET(wwe13_keyboard_typing) || !block) return;
  std::lock_guard lock(g_mutex);
  g_base = base;
  g_blocks.insert(block);
  if (REXCVAR_GET(wwe13_keyboard_typing_log)) {
    char label[9] = {};
    std::memcpy(label, base + block + 0x40, 8);
    for (char& ch : label)
      if (ch && (ch < 0x20 || ch > 0x7E)) ch = '?';
    REXLOG_INFO("[wwe13-keyboard] keyboard block {:08X} created: label '{}' state {}", block, label,
                Ld32(base, block + 0x38));
  }
}

extern "C" REX_FUNC(sub_82367348) {
  {
    std::lock_guard lock(g_mutex);
    g_blocks.erase(ctx.r3.u32);
  }
  __imp__sub_82367348(ctx, base);
}

extern "C" REX_FUNC(sub_82367798) {
  {
    std::lock_guard lock(g_mutex);
    g_blocks.erase(ctx.r3.u32);
  }
  __imp__sub_82367798(ctx, base);
}

extern "C" REX_FUNC(sub_82B77A68) {
  const uint32_t user = ctx.r3.u32, state = ctx.r4.u32;
  __imp__sub_82B77A68(ctx, base);
  if (user != 0 || !state || !REXCVAR_GET(wwe13_keyboard_typing)) return;
  if (const uint16_t button = NextButton(base)) {
    uint16_t buttons;
    std::memcpy(&buttons, base + state + 4, 2);
    buttons = __builtin_bswap16(uint16_t(__builtin_bswap16(buttons) | button));
    std::memcpy(base + state + 4, &buttons, 2);
  }
}

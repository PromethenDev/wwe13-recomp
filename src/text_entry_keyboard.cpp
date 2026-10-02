// Route PC keyboard text into WWE '13's pad-driven on-screen keyboard.
// Kill switch: WWE13_KEYBOARD_TYPING=0.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_set>

#include <rex/input/input_system.h>
#include <rex/input/text_entry.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace {

std::mutex g_keyboard_blocks_mutex;
std::unordered_set<uint32_t> g_keyboard_blocks;
std::atomic<rex::memory::Memory*> g_guest_memory{nullptr};

bool IsKeyboardTypingEnabled() {
  const char* const value = std::getenv("WWE13_KEYBOARD_TYPING");
  return !(value && std::strcmp(value, "0") == 0);
}

uint32_t ReadBe32(const rex::memory::Memory* memory, uint32_t address) {
  const uint8_t* const bytes = memory->TranslateVirtual<const uint8_t*>(address);
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
         (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

void WriteBe32(rex::memory::Memory* memory, uint32_t address, uint32_t value) {
  uint8_t* const bytes = memory->TranslateVirtual<uint8_t*>(address);
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
}

bool IsActiveKeyboard(const rex::memory::Memory* memory, uint32_t address) {
  if (address == 0) {
    return false;
  }
  constexpr char kSpaceLabel[] = "SPACE";
  const char* const label = memory->TranslateVirtual<const char*>(address + 0x40);
  return std::memcmp(label, kSpaceLabel, sizeof(kSpaceLabel)) == 0 &&
         ReadBe32(memory, address + 0x38) == 2;
}

struct KeyLocation {
  uint32_t index;
  uint32_t shifted;
};

bool MapCharacter(char16_t character, KeyLocation* location) {
  if (character >= u'a' && character <= u'z') {
    character = static_cast<char16_t>(character - (u'a' - u'A'));
  }

  constexpr char16_t kNormalDigits[] = u"1234567890";
  constexpr char16_t kShiftedDigits[] = u"!@#$%^&*()";
  for (uint32_t i = 0; i < 10; ++i) {
    if (character == kNormalDigits[i]) {
      *location = {2 + i, 0};
      return true;
    }
    if (character == kShiftedDigits[i]) {
      *location = {2 + i, 1};
      return true;
    }
  }

  switch (character) {
    case u'-':
      *location = {12, 0};
      return true;
    case u'_':
      *location = {12, 1};
      return true;
    case u'=':
      *location = {13, 0};
      return true;
    case u'+':
      *location = {13, 1};
      return true;
    case u'[':
      *location = {26, 0};
      return true;
    case u'{':
      *location = {26, 1};
      return true;
    case u'|':
      *location = {27, 0};
      return true;
    case u'`':
      *location = {27, 1};
      return true;
    case u']':
      *location = {28, 0};
      return true;
    case u'}':
      *location = {28, 1};
      return true;
    case u';':
      *location = {40, 0};
      return true;
    case u':':
      *location = {40, 1};
      return true;
    case u'\'':
      *location = {41, 0};
      return true;
    case u'"':
      *location = {41, 1};
      return true;
    case u',':
      *location = {51, 0};
      return true;
    case u'<':
      *location = {51, 1};
      return true;
    case u'.':
      *location = {52, 0};
      return true;
    case u'>':
      *location = {52, 1};
      return true;
    case u'/':
      *location = {53, 0};
      return true;
    case u'?':
      *location = {53, 1};
      return true;
    default:
      break;
  }

  constexpr char16_t kTopRow[] = u"QWERTYUIOP";
  constexpr char16_t kHomeRow[] = u"ASDFGHJKL";
  constexpr char16_t kBottomRow[] = u"ZXCVBNM";
  for (uint32_t i = 0; i < 10; ++i) {
    if (character == kTopRow[i]) {
      *location = {16 + i, 0};
      return true;
    }
  }
  for (uint32_t i = 0; i < 9; ++i) {
    if (character == kHomeRow[i]) {
      *location = {31 + i, 0};
      return true;
    }
  }
  for (uint32_t i = 0; i < 7; ++i) {
    if (character == kBottomRow[i]) {
      *location = {44 + i, 0};
      return true;
    }
  }
  return false;
}

class Wwe13TextEntryTarget final : public rex::input::TextEntryTarget {
 public:
  bool IsActive() override {
    const rex::memory::Memory* const memory =
        g_guest_memory.load(std::memory_order_acquire);
    if (!memory) {
      return false;
    }
    std::lock_guard lock(g_keyboard_blocks_mutex);
    for (const uint32_t address : g_keyboard_blocks) {
      if (IsActiveKeyboard(memory, address)) {
        return true;
      }
    }
    return false;
  }

  bool SelectChar(char16_t character) override {
    KeyLocation location{};
    if (!MapCharacter(character, &location)) {
      return false;
    }
    rex::memory::Memory* const memory =
        g_guest_memory.load(std::memory_order_acquire);
    if (!memory) {
      return false;
    }
    std::lock_guard lock(g_keyboard_blocks_mutex);
    for (const uint32_t address : g_keyboard_blocks) {
      if (!IsActiveKeyboard(memory, address)) {
        continue;
      }
      WriteBe32(memory, address + 0x28, location.index);
      WriteBe32(memory, address + 0x20, location.shifted);
      return true;
    }
    return false;
  }
};

Wwe13TextEntryTarget g_text_entry_target;

void RegisterTextEntryTarget(rex::system::KernelState* kernel_state) {
  if (!kernel_state) {
    return;
  }
  rex::Runtime* const runtime = kernel_state->emulator();
  if (!runtime) {
    return;
  }
  auto* const input_system =
      dynamic_cast<rex::input::InputSystem*>(runtime->input_system());
  if (input_system) {
    input_system->SetTextEntryTarget(&g_text_entry_target);
  }
}

void RecordKeyboardBlock(uint32_t address, uint8_t* base) {
  if (!IsKeyboardTypingEnabled()) {
    return;
  }
  rex::system::KernelState* const kernel_state = REX_KERNEL_STATE();
  if (kernel_state) {
    g_guest_memory.store(kernel_state->memory(), std::memory_order_release);
  }
  if (address != 0) {
    std::lock_guard lock(g_keyboard_blocks_mutex);
    g_keyboard_blocks.insert(address);
  }
  RegisterTextEntryTarget(kernel_state);
  (void)base;
}

void RemoveKeyboardBlock(uint32_t address) {
  std::lock_guard lock(g_keyboard_blocks_mutex);
  g_keyboard_blocks.erase(address);
}

}  // namespace

extern "C" PPC_FUNC(__imp__sub_82367730);
extern "C" PPC_FUNC(sub_82367730) {
  const uint32_t keyboard_address = ctx.r3.u32;
  __imp__sub_82367730(ctx, base);
  RecordKeyboardBlock(keyboard_address, base);
}

extern "C" PPC_FUNC(__imp__sub_82367348);
extern "C" PPC_FUNC(sub_82367348) {
  RemoveKeyboardBlock(ctx.r3.u32);
  __imp__sub_82367348(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_82367798);
extern "C" PPC_FUNC(sub_82367798) {
  RemoveKeyboardBlock(ctx.r3.u32);
  __imp__sub_82367798(ctx, base);
}

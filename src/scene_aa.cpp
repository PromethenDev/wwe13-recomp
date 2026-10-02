// Optional cap on the game's own scene-AA enqueue; unset/invalid preserves the game choice.

#include "scene_aa.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/logging.h>
#include <rex/ppc/context.h>

namespace {

struct SceneAaChoice {
  bool enabled;
  uint32_t maximum_level;
  const char* requested_value;
  const char* description;
};

const SceneAaChoice& Choice() {
  static const SceneAaChoice choice = [] {
    const char* value = std::getenv("WWE13_SCENE_AA");
    if (value == nullptr || *value == '\0') {
      return SceneAaChoice{false, 0, "unset", "game choice unchanged"};
    }
    if (std::strcmp(value, "4x") == 0) {
      return SceneAaChoice{true, 2, value, "maximum 4x MSAA"};
    }
    if (std::strcmp(value, "2x") == 0) {
      return SceneAaChoice{true, 1, value, "maximum 2x MSAA"};
    }
    if (std::strcmp(value, "off") == 0) {
      return SceneAaChoice{false, 0, value,
                           "off is unsupported because it corrupts the scene; game choice unchanged"};
    }
    return SceneAaChoice{false, 0, value, "invalid; game choice unchanged"};
  }();
  return choice;
}

}  // namespace

namespace wwe13 {

void InitializeSceneAaOption() {
  const SceneAaChoice& choice = Choice();
  REXLOG_INFO("[wwe13-scene-aa] WWE13_SCENE_AA='{}': {}", choice.requested_value,
              choice.description);
}

}  // namespace wwe13

extern "C" PPC_FUNC(__imp__sub_82AA68C0);
extern "C" PPC_FUNC(sub_82AA68C0) {
  const SceneAaChoice& choice = Choice();
  const uint32_t requested = ctx.r4.u32;
  if (choice.enabled) {
    const uint32_t forwarded = std::min(requested, choice.maximum_level);
    if (forwarded != requested) {
      ctx.r4.u32 = forwarded;
      static std::atomic<uint32_t> logged_clamps{0};
      uint32_t clamp = logged_clamps.load(std::memory_order_relaxed);
      while (clamp < 8 &&
             !logged_clamps.compare_exchange_weak(clamp, clamp + 1,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
      }
      if (clamp < 8) {
        REXLOG_INFO(
            "[wwe13-scene-aa] clamp {}: requested level {} -> {} (WWE13_SCENE_AA={})",
            clamp + 1, requested, forwarded, choice.requested_value);
      }
    }
  }
  __imp__sub_82AA68C0(ctx, base);
}

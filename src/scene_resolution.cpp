// Player-facing internal scene-resolution selection.
// Unset WWE13_INTERNAL_RES leaves the game's own scene-size requests unchanged.

#include "scene_resolution.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/memory.h>

namespace {

struct SceneResolutionChoice {
  bool enabled;
  uint32_t index;
  uint32_t width;
  uint32_t height;
  const char* name;
};

constexpr std::array<uint64_t, 4> kUiVertexShaderHashes = {
    UINT64_C(0x61AEE5ACB21291C7),  // 0x0FDB3040+36: pause-menu sprite VS
    UINT64_C(0x8586E774714B131B),  // 0x0FDC9040+30: second captured UI VS
    UINT64_C(0xB9A6E3FC8DDA5A78),  // 0x0FDC8040+33: captured main-menu sprite VS
    UINT64_C(0xEDFAD014BAF70CAE),  // 0x0FDB1040+39: captured main-menu bar/sprite VS
};

std::string HexValue(uint64_t value, int width) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
  return out.str();
}

std::string UiVertexShaderHashList() {
  std::string hashes;
  for (const uint64_t hash : kUiVertexShaderHashes) {
    if (!hashes.empty()) {
      hashes.push_back(',');
    }
    hashes += HexValue(hash, 16);
  }
  return hashes;
}

std::string MakeUiC0RemapSetting(const SceneResolutionChoice& choice,
                                 uint32_t& from_x, uint32_t& from_y,
                                 uint32_t& to_x, uint32_t& to_y) {
  from_x = std::bit_cast<uint32_t>(2.0f / static_cast<float>(choice.width));
  from_y = std::bit_cast<uint32_t>(2.0f / static_cast<float>(choice.height));
  to_x = std::bit_cast<uint32_t>(1.0f / 640.0f);
  to_y = std::bit_cast<uint32_t>(1.0f / 360.0f);
  return "v1:" + HexValue(from_x, 8) + ":" + HexValue(from_y, 8) + ":" +
         HexValue(to_x, 8) + ":" + HexValue(to_y, 8) + ":" +
         UiVertexShaderHashList();
}

const std::pair<uint32_t, uint32_t>& CustomSceneSize() {
  static const std::pair<uint32_t, uint32_t> size = [] {
    const char* value = std::getenv("WWE13_SCENE_CUSTOM");
    unsigned width = 0;
    unsigned height = 0;
    if (value && std::sscanf(value, "%ux%u", &width, &height) == 2 && width >= 320 &&
        height >= 240 && width <= 4096 && height <= 2160) {
      return std::pair<uint32_t, uint32_t>(width, height);
    }
    value = std::getenv("WWE13_INTERNAL_RES");
    if (value && std::strcmp(value, "1080") == 0) {
      return std::pair<uint32_t, uint32_t>(1920, 1080);
    }
    return std::pair<uint32_t, uint32_t>(0, 0);
  }();
  return size;
}

const SceneResolutionChoice& Choice() {
  static const SceneResolutionChoice choice = [] {
    const auto& custom_size = CustomSceneSize();
    if (custom_size.first != 0) {
      return SceneResolutionChoice{true, 5, custom_size.first, custom_size.second,
                                   custom_size.first == 1920 && custom_size.second == 1080
                                       ? "1080p"
                                       : "custom scene size"};
    }
    const char* value = std::getenv("WWE13_INTERNAL_RES");
    if (value == nullptr || *value == '\0') {
      return SceneResolutionChoice{false, 5, 1280, 720,
                                   "720p (default; game setting unchanged)"};
    }
    if (std::strcmp(value, "720") == 0) {
      return SceneResolutionChoice{true, 5, 1280, 720, "720p"};
    }
    if (std::strcmp(value, "576") == 0) {
      return SceneResolutionChoice{true, 3, 1024, 576, "576p"};
    }
    if (std::strcmp(value, "480") == 0) {
      return SceneResolutionChoice{true, 0, 640, 480, "480p"};
    }
    REXLOG_WARN(
        "[wwe13-internal-res] ignoring invalid WWE13_INTERNAL_RES='{}'; keeping game setting",
        value);
    return SceneResolutionChoice{false, 5, 1280, 720,
                                 "720p (default; game setting unchanged)"};
  }();
  return choice;
}

}  // namespace

namespace wwe13 {

void InitializeSceneResolutionOption() {
  const SceneResolutionChoice& choice = Choice();
  REXLOG_INFO("[wwe13-internal-res] {}{}", choice.name,
              choice.enabled ? " (WWE13_INTERNAL_RES override enabled)" : "");
  const bool is_1080p = choice.width == 1920 && choice.height == 1080;
  const bool disable_dof_composite = choice.enabled && is_1080p;
  const bool dof_cvar_configured = rex::cvar::SetFlagByName(
      "wwe13_dof_composite_nop", disable_dof_composite ? "true" : "false");
  REXLOG_INFO("[wwe13-dof] composite_nop={} for {} cvar_set={}",
              disable_dof_composite, choice.name, dof_cvar_configured);
  const bool remap_enabled =
      choice.enabled && (choice.index == 0 || choice.index == 3 || is_1080p);
  if (remap_enabled) {
    uint32_t from_x = 0, from_y = 0, to_x = 0, to_y = 0;
    const std::string setting = MakeUiC0RemapSetting(choice, from_x, from_y, to_x, to_y);
    const bool configured =
        rex::cvar::SetFlagByName("wwe13_ui_c0_remap", setting);
    REXLOG_INFO(
        "[wwe13-ui-c0-remap] selected mode={} scene={}x{} from={:08X}/{:08X} to={:08X}/{:08X} VS hashes={} cvar_set={}",
        choice.name, choice.width, choice.height, from_x, from_y, to_x, to_y,
        UiVertexShaderHashList(), configured);
  } else {
    const bool configured = rex::cvar::SetFlagByName("wwe13_ui_c0_remap", "");
    REXLOG_INFO("[wwe13-ui-c0-remap] inactive for {}; SDK config cleared={}",
                choice.name, configured);
  }
}

bool GetCustomSceneSize(uint32_t* width, uint32_t* height) {
  const auto& size = CustomSceneSize();
  *width = size.first;
  *height = size.second;
  return size.first != 0;
}

void InitializeEdramTilesForSceneResolution() {
  uint32_t width = 0;
  uint32_t height = 0;
  const bool custom_size = GetCustomSceneSize(&width, &height);
  if (custom_size && width == 1920 && height == 1080) {
    if (rex::cvar::SetFlagByName("edram_tiles", "4096")) {
      REXLOG_INFO("[wwe13-edram] selected 4096 tiles for opt-in 1920x1080 scene");
    } else {
      REXLOG_ERROR("[wwe13-edram] failed to select edram_tiles=4096 for 1920x1080 scene");
    }
  }
  const std::string scene = custom_size ? std::to_string(width) + "x" + std::to_string(height)
                                        : "game default";
  REXLOG_INFO("[wwe13-edram] startup edram_tiles={} scene={}",
              rex::cvar::GetFlagByName("edram_tiles"), scene);
}

}  // namespace wwe13

extern "C" PPC_FUNC(__imp__sub_82A97348);
extern "C" PPC_FUNC(sub_82A97348) {
  const SceneResolutionChoice& choice = Choice();
  const uint32_t requested = ctx.r4.u32;
  if (choice.enabled) {
    if (requested == 5) {
      ctx.r4.u32 = choice.index;
      static std::atomic<uint32_t> matching_calls{0};
      const uint32_t call = matching_calls.fetch_add(1, std::memory_order_relaxed) + 1;
      REXLOG_INFO(
          "[wwe13-internal-res] scene-size setter call={} caller_lr=0x{:08x} request={} forwarded={} ({})",
          call, static_cast<uint32_t>(ctx.lr), requested, choice.index, choice.name);
    } else {
      static std::atomic_flag other_request_logged = ATOMIC_FLAG_INIT;
      if (!other_request_logged.test_and_set(std::memory_order_relaxed)) {
        REXLOG_INFO("[wwe13-internal-res] leaving non-default scene-size request {} unchanged",
                    requested);
      }
    }
  }
  uint32_t custom_width = 0;
  uint32_t custom_height = 0;
  if (requested == 5 && wwe13::GetCustomSceneSize(&custom_width, &custom_height) &&
      ctx.r3.u32 != 0) {
    // Make the engine's normal +284 != +288 comparison rebuild the current
    // resource from the custom size-table entry on this setter call.
    PPC_STORE_U32(ctx.r3.u32 + 284, 0xFFFFFFFFu);
    static std::atomic_flag relayout_logged = ATOMIC_FLAG_INIT;
    if (!relayout_logged.test_and_set(std::memory_order_relaxed)) {
      REXLOG_INFO("[wwe13-internal-res] custom scene {}x{} forces the game's normal relayout",
                  custom_width, custom_height);
    }
  }
  __imp__sub_82A97348(ctx, base);
}

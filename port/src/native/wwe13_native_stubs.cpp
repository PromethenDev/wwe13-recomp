// WWE 2K15 native renderer: stand-ins for the WWE '12 PC features that re:Blue and the svr_* glue
// call (texture packs, FPS counter, quit entry, loading turbo, belt probe). They do nothing yet;
// each can be ported from C:\Dev\wwe12-pc\port\src\native when 2K15 needs it.

#include <cstdint>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "wwe12_display.h"
#include "wwe12_texture_packs.h"

namespace wwe12::texture_packs {

bool Active() { return false; }

uint64_t HashBlocks(const uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }

void Dump(uint64_t, uint32_t, uint32_t, Format, const uint8_t*, uint32_t, uint32_t) {}

std::shared_ptr<const Replacement> Find(uint64_t) { return nullptr; }

}  // namespace wwe12::texture_packs

void Wwe12DisplayInit(rex::ui::ImGuiDrawer*) {}

void Wwe13OnGuestPresent(uint8_t* base);  // framerate_hook.cpp: pacing log, FPS counter

// Called by the renderer once per presented guest frame (svr_resources.cpp).
void Wwe12DisplayOnFrame() { Wwe13OnGuestPresent(nullptr); }

namespace bd::gpu {
void Wwe12QuitEntryTick(uint64_t) {}
}  // namespace bd::gpu

// WWE '12 PC's hang watchdog (native/crash_trace.cpp there); 2K15 has its own crash_report.cpp.
void SvrWatchdogHeartbeat() {}


bool Wwe12LoadTurboSkipPresent() { return false; }

bool Wwe12BeltProbe(uint32_t, uint64_t, uint32_t, uint32_t) { return false; }

REXCVAR_DEFINE_STRING(svr_hooks_off, "", "SvR",
                      "Native renderer bring-up: comma list of D3D hooks whose renderer side is "
                      "skipped (D3DDevice_Swap, D3DDevice_Resolve, ..., or all)");

namespace svr {
bool HookEnabled(const char* name) {
  const std::string list = "," + REXCVAR_GET(svr_hooks_off) + ",";
  const bool off = list.find("," + std::string(name) + ",") != std::string::npos ||
                   list.find(",all,") != std::string::npos;
  if (off) {
    REXLOG_INFO("Native renderer: hook {} observed by nobody (svr_hooks_off)", name);
  }
  return !off;
}
}  // namespace svr

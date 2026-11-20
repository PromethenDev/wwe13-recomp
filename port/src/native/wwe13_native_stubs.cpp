// WWE 2K15 native renderer: stand-ins for the WWE '12 PC features that re:Blue and the svr_* glue
// call (texture packs, FPS counter, quit entry, loading turbo, belt probe). They do nothing yet;
// each can be ported from C:\Dev\wwe12-pc\port\src\native when 2K15 needs it.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "svr_hook.h"
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

REXCVAR_DEFINE_BOOL(wwe13_hook_profile, false, "WWE13",
                    "Test: time the game's own D3D function and the renderer's side of every "
                    "hooked call, [hook-cpu] lines every 600 frames");

namespace svr {
bool g_hook_profile = false;
namespace {
std::atomic<HookProf*> g_hook_profs{nullptr};
}
HookProf* RegisterHookProf(const char* name) {
  auto* p = new HookProf;
  p->name = name;
  p->next = g_hook_profs.load(std::memory_order_relaxed);
  while (!g_hook_profs.compare_exchange_weak(p->next, p, std::memory_order_relaxed)) {
  }
  return p;
}
void HookProfileTick(uint64_t frame) {
  const bool on = REXCVAR_GET(wwe13_hook_profile);
  static uint64_t frames = 0;
  if (on && g_hook_profile)
    ++frames;
  g_hook_profile = on;
  if (!on || frame % 600 != 0 || frames == 0)
    return;
  double guest_total = 0, host_total = 0;
  for (HookProf* p = g_hook_profs.load(std::memory_order_relaxed); p; p = p->next) {
    const uint64_t g = p->guest_ns.exchange(0, std::memory_order_relaxed);
    const uint64_t h = p->host_ns.exchange(0, std::memory_order_relaxed);
    const uint64_t c = p->calls.exchange(0, std::memory_order_relaxed);
    if (!c)
      continue;
    guest_total += double(g), host_total += double(h);
    REXLOG_INFO("[hook-cpu] {:<34} guest {:6.3f} ms/frame ({:5.2f} us/call) | renderer {:6.3f} ms/frame "
                "({:5.2f} us/call) | {:7.1f} calls/frame",
                p->name + 0, g / 1e6 / frames, g / 1e3 / c, h / 1e6 / frames, h / 1e3 / c,
                double(c) / frames);
  }
  REXLOG_INFO("[hook-cpu] total over {} frames: guest {:.3f} ms/frame | renderer {:.3f} ms/frame", frames,
              guest_total / 1e6 / frames, host_total / 1e6 / frames);
  frames = 0;
}
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

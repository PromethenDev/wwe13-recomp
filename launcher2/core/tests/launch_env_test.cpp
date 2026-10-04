#ifdef NDEBUG
#undef NDEBUG
#endif

#include "launcher_core.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace wwe13::launcher;

namespace {

const std::string* EnvironmentValue(const LaunchPlan& plan, const std::string& name) {
  for (const auto& [key, value] : plan.environment) {
    if (key == name) return &value;
  }
  return nullptr;
}

}  // namespace

int main() {
#ifdef _WIN32
  {
    // A game that closes right away is reported; one that keeps running is not.
    LaunchPlan quits;
    quits.executable = "C:\\windows\\system32\\cmd.exe";
    quits.arguments = {"/c", "exit 1"};
    const Result early = StartGame(quits, 3.0);
    assert(!early.ok && early.error == kGameClosedEarlyMessage);
    LaunchPlan stays;
    stays.executable = "C:\\windows\\system32\\ping.exe";
    stays.arguments = {"-n", "6", "127.0.0.1"};
    assert(StartGame(stays, 1.0).ok);
    std::cout << "PASS: a game that closes within the watch window is reported to the player.\n";
  }
  std::cout << "SKIP: RenderDoc implicit-layer suppression is a Linux launcher setting.\n";
  return 0;
#else
  const char* const previous = std::getenv("VK_LOADER_LAYERS_DISABLE");
  const bool had_previous = previous != nullptr;
  const std::string saved = previous ? previous : "";
  assert(::unsetenv("VK_LOADER_LAYERS_DISABLE") == 0);

  Paths paths;
  paths.exe_dir = fs::current_path();
  paths.game_exe = paths.exe_dir / "wwe13";
  paths.logs_dir = paths.exe_dir / "logs";
  paths.userdata_dir = paths.exe_dir / "userdata";
  paths.default_game_folder = paths.exe_dir / "WWE 13";
  Settings settings;
  settings.explicit_choice = true;

  const LaunchPlan renderdoc_only = BuildLaunchPlan(paths, settings, {});
  assert(EnvironmentValue(renderdoc_only, "VK_LOADER_LAYERS_DISABLE"));
  assert(*EnvironmentValue(renderdoc_only, "VK_LOADER_LAYERS_DISABLE") ==
         "VK_LAYER_RENDERDOC_Capture");

  assert(::setenv("VK_LOADER_LAYERS_DISABLE", "VK_LAYER_MANGOHUD_overlay", 1) == 0);
  const LaunchPlan preserve_existing = BuildLaunchPlan(paths, settings, {});
  assert(EnvironmentValue(preserve_existing, "VK_LOADER_LAYERS_DISABLE"));
  assert(*EnvironmentValue(preserve_existing, "VK_LOADER_LAYERS_DISABLE") ==
         "VK_LAYER_MANGOHUD_overlay,VK_LAYER_RENDERDOC_Capture");

  if (had_previous) {
    assert(::setenv("VK_LOADER_LAYERS_DISABLE", saved.c_str(), 1) == 0);
  } else {
    assert(::unsetenv("VK_LOADER_LAYERS_DISABLE") == 0);
  }
  // A game that closes right away is reported; one that keeps running is not.
  LaunchPlan quits;
  quits.executable = "/bin/false";
  const Result early = StartGame(quits, 2.0);
  assert(!early.ok && early.error == kGameClosedEarlyMessage);
  LaunchPlan stays;
  stays.executable = "/bin/sleep";
  stays.arguments = {"5"};
  assert(StartGame(stays, 1.0).ok);
  std::cout << "PASS: a game that closes within the watch window is reported to the player.\n";
  std::cout << "PASS: RenderDoc filter is child-only and preserves an existing MangoHud filter.\n";
  return 0;
#endif
}

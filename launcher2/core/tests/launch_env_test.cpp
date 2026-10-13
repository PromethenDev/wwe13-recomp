#ifdef NDEBUG
#undef NDEBUG
#endif

#include "launcher_core.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace wwe13::launcher;

namespace {

const std::string* EnvironmentValue(const LaunchPlan& plan, const std::string& name) {
  for (const auto& [key, value] : plan.environment) {
    if (key == name) return &value;
  }
  return nullptr;
}

// Returns the first --vulkan_device* argument in the plan, or "" when the plan passes none.
std::string GpuArgument(const LaunchPlan& plan) {
  for (const std::string& argument : plan.arguments) {
    if (argument.starts_with("--vulkan_device")) return argument;
  }
  return {};
}

void TestProcessStartedBeforeWatchReturns() {
  LaunchPlan stays;
#ifdef _WIN32
  stays.executable = "C:\\windows\\system32\\ping.exe";
  stays.arguments = {"-n", "5", "127.0.0.1"};
#else
  stays.executable = "/bin/sleep";
  stays.arguments = {"3"};
#endif
  std::atomic<bool> process_started{false};
  std::atomic<bool> returned{false};
  Result result;
  std::thread watcher([&] {
    result = StartGame(stays, 1.0, &process_started);
    returned.store(true, std::memory_order_release);
  });
  for (int attempt = 0; attempt < 500 && !process_started.load() && !returned.load(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const bool notified_before_watch_finished = process_started.load() && !returned.load();
  watcher.join();
  assert(notified_before_watch_finished && result.ok);
  std::cout << "PASS: process-start notification arrives before the watch returns.\n";
}

// Two identical GPUs are stored as "vendor:device:index" (gpus.cpp). The game's
// --vulkan_device_id parser accepts exactly one colon and silently falls back to automatic device
// selection for anything else, so BuildLaunchPlan must pass the enumerated index instead.
void TestGpuDeviceArgument() {
  Paths paths;
  paths.exe_dir = fs::current_path();
  paths.game_exe = paths.exe_dir / "wwe13";
  paths.logs_dir = paths.exe_dir / "logs";
  paths.userdata_dir = paths.exe_dir / "userdata";
  paths.default_game_folder = paths.exe_dir / "WWE 13";

  Settings settings;
  settings.explicit_choice = true;

  // A single device keeps the stable vendor:device identity.
  const std::vector<GpuInfo> unique = {{"10DE:2684", "Discrete", false, 0},
                                       {"1002:164E", "Integrated", true, 1}};
  settings.gpu_id = "10DE:2684";
  assert(GpuArgument(BuildLaunchPlan(paths, settings, unique)) == "--vulkan_device_id=10DE:2684");

  // Two identical devices are disambiguated by index; the game cannot parse the extra colon.
  const std::vector<GpuInfo> duplicate = {{"10DE:2504:1", "RTX 3060", false, 1},
                                          {"10DE:2504:3", "RTX 3060", false, 3}};
  settings.gpu_id = "10DE:2504:3";
  assert(GpuArgument(BuildLaunchPlan(paths, settings, duplicate)) == "--vulkan_device=3");

  // Automatic choice passes neither flag.
  settings.gpu_id.clear();
  assert(GpuArgument(BuildLaunchPlan(paths, settings, duplicate)).empty());

  std::cout << "PASS GPU argument: unique -> --vulkan_device_id=vendor:device, duplicate -> "
               "--vulkan_device=index, automatic -> no flag\n";
}

}  // namespace

int main() {
  TestGpuDeviceArgument();
#ifdef _WIN32
  {
    // An unsuccessful early exit is reported; a normal exit or a still-running process is not.
    LaunchPlan quits;
    quits.executable = "C:\\windows\\system32\\cmd.exe";
    quits.arguments = {"/c", "exit 1"};
    std::atomic<bool> process_started{false};
    const Result early = StartGame(quits, 3.0, &process_started);
    assert(!early.ok && early.error == kGameClosedEarlyMessage && process_started.load());
    LaunchPlan exits_normally;
    exits_normally.executable = "C:\\windows\\system32\\cmd.exe";
    exits_normally.arguments = {"/c", "exit 0"};
    assert(StartGame(exits_normally, 3.0, &process_started).ok && process_started.load());
    LaunchPlan stays;
    stays.executable = "C:\\windows\\system32\\ping.exe";
    stays.arguments = {"-n", "6", "127.0.0.1"};
    assert(StartGame(stays, 1.0, &process_started).ok && process_started.load());
    TestProcessStartedBeforeWatchReturns();
    std::cout << "PASS: process-start signal is set before watch completion; unsuccessful early exit is "
                 "reported and a normal early exit is accepted.\n";
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
  // An unsuccessful early exit or a signal termination is reported; normal exit is accepted.
  LaunchPlan quits;
  quits.executable = "/bin/false";
  std::atomic<bool> process_started{false};
  const Result early = StartGame(quits, 2.0, &process_started);
  assert(!early.ok && early.error == kGameClosedEarlyMessage && process_started.load());
  LaunchPlan exits_normally;
  exits_normally.executable = "/bin/true";
  assert(StartGame(exits_normally, 2.0, &process_started).ok && process_started.load());
  LaunchPlan crashes;
  crashes.executable = "/bin/sh";
  crashes.arguments = {"-c", "kill -KILL $$"};
  const Result crashed = StartGame(crashes, 2.0, &process_started);
  assert(!crashed.ok && crashed.error == kGameClosedEarlyMessage && process_started.load());
  LaunchPlan stays;
  stays.executable = "/bin/sleep";
  stays.arguments = {"5"};
  assert(StartGame(stays, 1.0, &process_started).ok && process_started.load());
  TestProcessStartedBeforeWatchReturns();
  std::cout << "PASS: process-start signal is set before watch completion; nonzero exit and signal "
               "termination are reported; normal exit is accepted.\n";
  std::cout << "PASS: RenderDoc filter is child-only and preserves an existing MangoHud filter.\n";
  return 0;
#endif
}

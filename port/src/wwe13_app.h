// WWE '13 PC (native port, SDK 0.10): application hooks.
// Path and cache setup follow WWE 2K15 PC (port/src/wwe2k15_app.h).

#pragma once

#include <filesystem>
#include <memory>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging/macros.h>
#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>

#if defined(SVR_NATIVE_RENDERER)
// Native renderer (wwe13_native.exe, wwe13_d3d12.exe, wwe13_d3d11.exe; cmake/native_renderer.cmake):
// re:Blue draws through plume (Vulkan, Direct3D 12 or Direct3D 11) from the game's own D3D calls
// instead of emulating the Xenos GPU (from WWE 2K15 PC and WWE '12 PC).
#include <cstdlib>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <rex/system/gpu_plugin.h>
#include "gpu/device.h"
#include "gpu/imgui_overlay_drawer.h"
#if defined(WWE13_RUNTIME_SHADERS) && !defined(WWE13_BUILTIN_SPIRV)
bool Wwe13DxcUsable();  // native/wwe13_shader_cache.cpp
#endif
// AUTO: starts the next exe (wwe13_native.exe -> wwe13_d3d12.exe -> wwe13_d3d11.exe); false otherwise.
bool Wwe13TryNextBackend();  // native/wwe13_backend_chain.cpp
#endif

void InstallWwe13CrashReporter();  // crash_report.cpp
namespace rex::ui { class Window; }
void Wwe13KeyboardTypingInit(rex::ui::Window* window);  // text_entry_keyboard.cpp

class Wwe13App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Wwe13App>(new Wwe13App(ctx, "wwe13", PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    // Graphics/pipeline caches beside this portable build (the Documents folder can be read-only).
    paths.cache_root = rex::filesystem::GetExecutableFolder() / "cache";
    user_data_root_ = paths.user_data_root;
  }

  void OnPostSetup() override {
    InstallWwe13CrashReporter();
    MountXboxCache();
  }

  // The window exists from here on (the SDK titles it "wwe13 <SDK build stamp>").
  void OnPreLaunchModule() override {
    if (auto* w = window()) w->SetTitle("WWE '13");
    Wwe13KeyboardTypingInit(window());
#if defined(SVR_NATIVE_RENDERER)
    StartNativeRenderer();
#endif
  }

#if defined(SVR_NATIVE_RENDERER)
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    // The Xenos plugin's "null" backend keeps the game's D3D command stream moving (fences,
    // interrupts, vblank) without drawing; the re:Blue hooks draw instead.
    if (!config.graphics) {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, "null");
      REXLOG_INFO("Native renderer: guest GPU {}", config.graphics ? "null (sync only)" : "MISSING");
    }
  }

  // The plume device draws the ImGui overlays at Present time.
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    return std::make_unique<bd::gpu::ImGuiOverlayDrawer>();
  }

  void StartNativeRenderer() {
    if (!bd::gpu::Video::CreateHostDevice(window())) {
      // The launcher looks for "could not create the host device" in the log: keep it.
      REXLOG_ERROR("Native renderer: could not create the host device ({} unavailable or too old)",
                   bd::gpu::Video::BackendName());
      // AUTO (gpu_backend = "any"): Vulkan, then Direct3D 12, then Direct3D 11, as WWE '12 PC.
      if (Wwe13TryNextBackend()) {
        REXLOG_ERROR("Native renderer: graphics API unavailable, AUTO starts the next executable");
        rex::FlushLogging();
        // Exit code 0: the launcher watches this process and reports a non-zero exit as a crash.
        std::_Exit(0);
      }
      REXLOG_ERROR("Native renderer: no graphics API left; use the normal renderer (wwe13.exe)");
      rex::FlushLogging();
      Fail(L"The native renderer could not start: none of Vulkan 1.2, Direct3D 12 or Direct3D 11 works "
           L"on this PC (or the graphics API chosen in the launcher doesn't), or the graphics driver is "
           L"too old.\n\nUpdate the graphics driver, set Graphics API to Automatic in the launcher, or "
           L"play with the normal renderer.");
    }
#if defined(WWE13_RUNTIME_SHADERS) && !defined(WWE13_BUILTIN_SPIRV)
    // Without a usable dxcompiler.dll no shader can be converted and the window stays black.
    // The launcher looks for "shader compiler (dxcompiler.dll) missing or unusable": keep it.
    // (Direct3D 12 / 11, and Vulkan built with WWE13_BUILTIN_SPIRV, have the game's shaders built
    // in: DXC only converts a shader missing from that cache, when dxcompiler.dll is there.)
    if (!Wwe13DxcUsable()) {
      REXLOG_ERROR("Native renderer: shader compiler (dxcompiler.dll) missing or unusable; use the "
                   "normal renderer (wwe13.exe)");
      rex::FlushLogging();
      Fail(L"The native renderer could not start: dxcompiler.dll is missing or unusable (it must sit "
           L"next to the program).\n\nReinstall the port, or play with the normal renderer.");
    }
#endif
    // Present runs on the game thread and ImGui on the UI thread: draw the overlay synchronously on
    // the UI thread into the presented image.
    bd::gpu::Video::SetOverlayDrawHook([this](plume::RenderCommandList* cmd, plume::RenderFramebuffer* fb,
                                              uint32_t w, uint32_t h) {
      if (!imgui_drawer() || !imgui_drawer()->HasDialogs())
        return;
      app_context().CallInUIThreadSynchronous([this, cmd, fb, w, h] {
        bd::gpu::ReblueUIDrawContext ctx(w, h, cmd, fb);
        imgui_drawer()->Draw(ctx);
      });
    });
  }

  [[noreturn]] static void Fail(const wchar_t* message) {
#if defined(_WIN32)
    MessageBoxW(nullptr, message, L"WWE '13 PC", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#endif
    std::_Exit(1);
  }
#endif

 private:
  // The title reads and writes its Xbox HDD cache (cache:\EditEx.cac, ...). The SDK does not mount
  // cache: by default, so give it a writable folder in the user data.
  void MountXboxCache() {
    if (user_data_root_.empty()) {
      REXLOG_WARN("Xbox cache is unavailable because user_data_root is empty");
      return;
    }
    const auto cache_root = user_data_root_ / "xbox_cache";
    std::error_code ec;
    std::filesystem::create_directories(cache_root, ec);
    auto* fs = REX_KERNEL_FS();
    auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\CACHE", cache_root, false);
    if (!fs || !device->Initialize() || !fs->RegisterDevice(std::move(device))) {
      REXLOG_ERROR("Unable to mount the Xbox cache folder {}", cache_root.string());
      return;
    }
    fs->RegisterSymbolicLink("cache:", "\\CACHE");
    fs->RegisterSymbolicLink("cache0:", "\\CACHE");
    fs->RegisterSymbolicLink("cache1:", "\\CACHE");
    REXLOG_INFO("Xbox cache mounted at {}", cache_root.string());
  }

  std::filesystem::path user_data_root_;
};

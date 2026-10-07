// wwe13 - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <gtk/gtk.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>
#include <spdlog/sinks/base_sink.h>

#include "perf_overlay.h"
#include "single_instance.h"
#include "thread_profiler.h"
#include "game_version_check.h"
#include "release_diagnostics.h"
#include "scene_aa.h"
#include "scene_resolution.h"
#include "shader_cache_seed.h"

namespace wwe13 {
void InitializeEdramTilesForSceneResolution();
}

REXCVAR_DECLARE(bool, debug_ui);

#ifdef _WIN32
// kernel32 (avoids pulling <windows.h> and its macros into this header).
extern "C" __declspec(dllimport) int __stdcall SetConsoleTitleW(const wchar_t* title);
#endif

class Wwe13App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Wwe13App>(new Wwe13App(ctx, "wwe13",
        PPCImageConfig));
  }

  std::string_view GetWindowTitle() const override { return "WWE 13"; }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    game_data_root_ = paths.game_data_root;
    user_data_root_ = paths.user_data_root;
    game_file_check_result_ = wwe13::CheckSupportedGameFiles(paths.game_data_root);
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    (void)config;
    // GitHub #6: a second start-up against the same user-data folder becomes a second game
    // process fighting over the GPU (players hit this by clicking Play again while the first,
    // slow shader-preparation start-up still looks frozen). Acquire the per-user-data lock
    // before any graphics setup; a second instance says so and exits. Lanes/installations with
    // their own --user_data_root keep working side by side.
    if (!wwe13::AcquireUserDataInstanceLock(user_data_root_)) {
      constexpr char kAlreadyRunningMessage[] =
          "WWE '13 is already running with this game folder. Close the other game window (or "
          "end wwe13.exe in Task Manager) and try again.\n";
      REXLOG_ERROR("[wwe13] another game instance is already using {}; exiting",
                   rex::path_to_utf8(user_data_root_));
      rex::FlushLogging();
#ifdef _WIN32
      MessageBoxW(nullptr,
                  L"WWE '13 is already running. Close the other game window first, then try "
                  L"again.",
                  L"WWE '13", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
#else
      std::fputs(kAlreadyRunningMessage, stderr);
#endif
      std::exit(EXIT_FAILURE);
    }
    wwe13::InitializeReleaseDiagnostics(game_data_root_, game_file_check_result_);
    wwe13::InitializeSceneResolutionOption();
    wwe13::InitializeEdramTilesForSceneResolution();
    wwe13::InitializeSceneAaOption();
    wwe13::SeedShaderCache(user_data_root_);
    if (game_file_check_result_ == wwe13::GameFileCheckResult::kSupported) {
      REXLOG_INFO("[wwe13] validated title ID 545108B4 and title update 2.0.1.0");
    }
#ifdef _WIN32
    // Owner request 2026-09-27: no folder paths in the console window. While a program started from a .bat runs,
    // cmd shows "<title> - <full command line incl. game folder>" in the title bar; set it back to plain text.
    SetConsoleTitleW(L"WWE 13");
#endif
    const char* env = std::getenv("WWE13_DEPTH_F24_CONVERT");
    if (env == nullptr || std::strcmp(env, "0") != 0) {
      rex::cvar::SetFlagByName("depth_float24_convert_in_pixel_shader", "true");
    }
    rex::cvar::SetFlagByName("vulkan_float24_depth_pixel_rate",
                             EnvIsOne("WWE13_F24_PIXEL_RATE") ? "true" : "false");
    // Guest vblank on the real display's vblank when it runs at a multiple of 60 Hz (Windows), see
    // vsync_host_display in the SDK graphics system. Owner A/B 2026-09-26 (wwe13-sync.bat vs wwe13.bat): entrances
    // "far far better", less stutter -> default on; WWE13_HOST_VSYNC=0 keeps the free-running timer.
    // [display-pacing] (census launchers): per display vblank, new guest frames (0 = repeat, 2+ = replaced).
    const char* host_vsync = std::getenv("WWE13_HOST_VSYNC");
    rex::cvar::SetFlagByName("vsync_host_display",
                             host_vsync && std::strcmp(host_vsync, "0") == 0 ? "false" : "true");
    rex::cvar::SetFlagByName("display_pacing_stats", EnvIsOne("WWE13_RATE_CENSUS") ? "true" : "false");
    REXLOG_INFO("[wwe13] depth_float24_convert_in_pixel_shader={} (WWE13_DEPTH_F24_CONVERT), "
                "vulkan_float24_depth_pixel_rate={} (WWE13_F24_PIXEL_RATE), vsync_host_display={} "
                "(WWE13_HOST_VSYNC)",
                rex::cvar::GetFlagByName("depth_float24_convert_in_pixel_shader"),
                rex::cvar::GetFlagByName("vulkan_float24_depth_pixel_rate"),
                rex::cvar::GetFlagByName("vsync_host_display"));
  }

  void OnPostSetup() override {
    if (REXCVAR_GET(debug_ui)) {
      wwe13::StartThreadProfilerFromEnv();
    }
    rex::ui::RegisterBind("bind_toggle_fullscreen", "F11", "Toggle fullscreen / windowed",
                          [this] {
                            if (auto* w = window()) {
                              const bool fullscreen = !w->IsFullscreen();
                              w->SetFullscreen(fullscreen);
                              REXLOG_INFO("[wwe13] fullscreen {} (F11)",
                                          fullscreen ? "on" : "off");
                            }
                          });
    if (!DiagnosticSessionEnabled()) {
      return;
    }
    frame_history_ = std::make_shared<Wwe13FrameHistory>();
    frame_time_sink_ = std::make_shared<FrameTimeSink>(frame_history_);
    rex::AddSink(rex::log::GPU, frame_time_sink_);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    if (REXCVAR_GET(debug_ui) && EnvIsOne("WWE13_PERF_OVERLAY")) {
      perf_overlay_ = std::make_unique<Wwe13PerfOverlay>(drawer, frame_history_);
    }
  }

  void OnPreLaunchModule() override {
    // Owner 2026-10-02: the mouse pointer hides after 4 s without mouse movement (shown again when the mouse moves
    // or clicks), in fullscreen and windowed. Runs on the UI thread once the window is open (OnPostSetup is too
    // early: the window does not exist yet). WWE13_SHOW_CURSOR=1 keeps it always visible.
    if (auto* w = window(); w && !EnvIsOne("WWE13_SHOW_CURSOR")) {
      w->SetCursorVisibility(rex::ui::Window::CursorVisibility::kAutoHidden);
    }
    if (game_file_check_result_ == wwe13::GameFileCheckResult::kSupported) {
      return;
    }
#ifdef _WIN32
    const HWND owner = window() ? static_cast<HWND>(window()->GetNativeWindowHandle()) : nullptr;
    MessageBoxW(owner,
                L"The selected files are not a supported WWE '13 title update 2.0.1.0 set.\n\n"
                L"Use your own matching default.xex and default.xexp files from the same game folder.",
                L"WWE '13", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
    wwe13::ShowUnsupportedGameFilesMessage(game_file_check_result_);
    if (gtk_init_check(nullptr, nullptr)) {
      GtkWindow* parent = nullptr;
      if (window()) {
        parent = static_cast<GtkWindow*>(window()->GetNativeWindowHandle());
      }
      constexpr char kUnsupportedFilesMessage[] =
          "The selected files are not a supported WWE '13 title update 2.0.1.0 set.\n\n"
          "Use your own matching default.xex and default.xexp files from the same game folder.";
      GtkWidget* dialog = gtk_message_dialog_new(
          parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s",
          kUnsupportedFilesMessage);
      gtk_window_set_title(GTK_WINDOW(dialog), "WWE '13");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
    }
#endif
    std::exit(EXIT_FAILURE);
  }

  void OnGraphicsSetupFailed(rex::X_STATUS status) override {
    (void)status;
    constexpr char kNoGpuMessage[] =
        "WWE '13 couldn't find a graphics card it can use. Update your graphics driver, then try "
        "again.\n\n"
        "On laptops with two graphics chips, set wwe13.exe to 'High performance' in Windows "
        "Settings > System > Display > Graphics.";
    REXLOG_ERROR("[wwe13] no usable graphics card at startup: {}", kNoGpuMessage);
    rex::FlushLogging();
#ifdef _WIN32
    MessageBoxW(nullptr, L"WWE '13 couldn't find a graphics card it can use. Update your graphics "
                         L"driver, then try again.\n\n"
                         L"On laptops with two graphics chips, set wwe13.exe to 'High performance' "
                         L"in Windows Settings > System > Display > Graphics.",
                L"WWE '13", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
    // Linux: the log and stderr are enough (no modal dialog to hang headless
    // CI); the platform entry point exits with a normal failure status.
    std::fputs(
        "WWE '13 couldn't find a graphics card it can use. Update your graphics driver, then try "
        "again.\n"
        "On laptops with two graphics chips, set wwe13.exe to 'High performance' in Windows "
        "Settings > System > Display > Graphics.\n",
        stderr);
#endif
  }

  void OnShutdown() override {
    wwe13::StopThreadProfiler();
    rex::ui::UnregisterBind("bind_toggle_fullscreen");
    perf_overlay_.reset();
    if (frame_time_sink_) {
      rex::RemoveSink(rex::log::GPU, frame_time_sink_);
      frame_time_sink_.reset();
    }
    frame_history_.reset();
    wwe13::FlushReleaseLogs();
  }

 private:
  static bool EnvIsOne(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
  }

  static bool DiagnosticSessionEnabled() {
    return EnvIsOne("WWE13_FRAME_TIME") || EnvIsOne("WWE13_RATE_CENSUS") ||
           EnvIsOne("WWE13_PERF_OVERLAY");
  }

  class FrameTimeSink final : public spdlog::sinks::base_sink<std::mutex> {
   public:
    explicit FrameTimeSink(std::shared_ptr<Wwe13FrameHistory> frame_history)
        : frame_history_(std::move(frame_history)) {}

    protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
      const std::string_view payload(msg.payload.data(), msg.payload.size());
      if (payload.find("XELOG_GPU PRESENT:") == std::string_view::npos) {
        return;
      }

      const auto monotonic_us = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      frame_history_->Record(static_cast<std::uint64_t>(monotonic_us));
      REXLOG_INFO("[wwe13-frame-time] seq={} t_us={} present", sequence_++, monotonic_us);
    }

    void flush_() override {}

   private:
    std::uint64_t sequence_ = 0;
    std::shared_ptr<Wwe13FrameHistory> frame_history_;
  };

  std::shared_ptr<FrameTimeSink> frame_time_sink_;
  std::shared_ptr<Wwe13FrameHistory> frame_history_;
  std::unique_ptr<Wwe13PerfOverlay> perf_overlay_;
  std::filesystem::path game_data_root_;
  std::filesystem::path user_data_root_;
  wwe13::GameFileCheckResult game_file_check_result_ =
      wwe13::GameFileCheckResult::kSupported;
};

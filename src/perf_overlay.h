// wwe13 - ReXGlue Recompiled Project
//
// Opt-in presentation diagnostics used by the WWE13_PERF_OVERLAY session mode.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <rex/diagnostics/rate_census.h>
#include <rex/ui/imgui_dialog.h>

struct ImGuiIO;

struct Wwe13FrameStats {
  double fps_now = 0.0;
  double fps_10s = 0.0;
  double worst_frame_ms = 0.0;
};

class Wwe13FrameHistory final {
 public:
  void Record(uint64_t timestamp_us);
  Wwe13FrameStats Snapshot() const;

 private:
  struct Slot {
    std::atomic<uint64_t> sequence{0};
    std::atomic<uint64_t> timestamp_us{0};
  };

  static constexpr std::size_t kCapacity = 4096;

  std::array<Slot, kCapacity> timestamps_{};
  std::atomic<uint64_t> write_index_{0};
};

class Wwe13PerfOverlay final : public rex::ui::ImGuiDialog {
 public:
  Wwe13PerfOverlay(rex::ui::ImGuiDrawer* imgui_drawer,
                   std::shared_ptr<Wwe13FrameHistory> frame_history);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void RefreshMetrics();

  std::shared_ptr<Wwe13FrameHistory> frame_history_;
  Wwe13FrameStats frame_stats_;
  std::optional<rex::diagnostics::rate_census::Sample> census_sample_;
  std::chrono::steady_clock::time_point next_refresh_{};
  bool has_refreshed_ = false;
};

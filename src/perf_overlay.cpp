// wwe13 - ReXGlue Recompiled Project

#include "perf_overlay.h"

#include <algorithm>
#include <imgui.h>

void Wwe13FrameHistory::Record(uint64_t timestamp_us) {
  const uint64_t index = write_index_.fetch_add(1, std::memory_order_relaxed);
  Slot& slot = timestamps_[index % kCapacity];
  slot.timestamp_us.store(timestamp_us, std::memory_order_relaxed);
  slot.sequence.store(index + 1, std::memory_order_release);
}

Wwe13FrameStats Wwe13FrameHistory::Snapshot() const {
  const uint64_t end = write_index_.load(std::memory_order_acquire);
  const uint64_t begin = end > kCapacity ? end - kCapacity : 0;

  std::vector<uint64_t> timestamps;
  timestamps.reserve(static_cast<size_t>(end - begin));
  for (uint64_t index = begin; index < end; ++index) {
    const Slot& slot = timestamps_[index % kCapacity];
    const uint64_t sequence = slot.sequence.load(std::memory_order_acquire);
    if (sequence != index + 1) {
      continue;
    }
    const uint64_t timestamp = slot.timestamp_us.load(std::memory_order_relaxed);
    if (slot.sequence.load(std::memory_order_acquire) == sequence && timestamp != 0) {
      timestamps.push_back(timestamp);
    }
  }
  if (timestamps.empty()) {
    return {};
  }
  std::sort(timestamps.begin(), timestamps.end());

  const uint64_t latest = timestamps.back();
  const uint64_t first = timestamps.front();
  const auto frames_per_second = [&](uint64_t window_us) {
    const uint64_t cutoff = latest > window_us ? latest - window_us : 0;
    const auto first_in_window = std::lower_bound(timestamps.begin(), timestamps.end(), cutoff);
    const size_t frame_count = static_cast<size_t>(timestamps.end() - first_in_window);
    const uint64_t window_start = std::max(first, cutoff);
    const uint64_t elapsed_us = latest > window_start ? latest - window_start : 0;
    if (frame_count < 2 || elapsed_us == 0) {
      return 0.0;
    }
    return static_cast<double>(frame_count - 1) * 1000000.0 /
           static_cast<double>(elapsed_us);
  };

  const uint64_t one_second_ago = latest > 1000000 ? latest - 1000000 : 0;
  double worst_gap_us = 0.0;
  for (size_t i = 1; i < timestamps.size(); ++i) {
    if (timestamps[i] >= one_second_ago) {
      worst_gap_us = std::max(
          worst_gap_us, static_cast<double>(timestamps[i] - timestamps[i - 1]));
    }
  }

  return {
      frames_per_second(1000000),
      frames_per_second(10000000),
      worst_gap_us / 1000.0,
  };
}

Wwe13PerfOverlay::Wwe13PerfOverlay(
    rex::ui::ImGuiDrawer* imgui_drawer,
    std::shared_ptr<Wwe13FrameHistory> frame_history)
    : ImGuiDialog(imgui_drawer), frame_history_(std::move(frame_history)) {}

void Wwe13PerfOverlay::RefreshMetrics() {
  frame_stats_ = frame_history_->Snapshot();
  census_sample_ = rex::diagnostics::rate_census::GetLatestSample();
}

void Wwe13PerfOverlay::OnDraw(ImGuiIO& io) {
  const auto now = std::chrono::steady_clock::now();
  if (!has_refreshed_ || now >= next_refresh_) {
    RefreshMetrics();
    next_refresh_ = now + std::chrono::seconds(1);
    has_refreshed_ = true;
  }

  ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.55f);
  constexpr ImGuiWindowFlags kFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_AlwaysAutoResize;
  if (!ImGui::Begin("WWE13 performance##overlay", nullptr, kFlags)) {
    ImGui::End();
    return;
  }

  // Game FPS = guest VdSwap calls per second (rate census): the frames the game itself
  // finished, independent of how often the host re-presents them.
  if (census_sample_) {
    ImGui::Text("Game FPS: %.0f", census_sample_->vdswap);
  } else {
    ImGui::TextUnformatted("Game FPS: --");
  }
  // Present-log frame timing only has data with verbose GPU logging (WWE13_FRAME_TIME runs).
  if (frame_stats_.fps_10s > 0.0) {
    ImGui::Text("FPS now: %.1f", frame_stats_.fps_now);
    ImGui::Text("FPS 10 s: %.1f", frame_stats_.fps_10s);
    ImGui::Text("Worst 1 s: %.2f ms", frame_stats_.worst_frame_ms);
  }

  if (census_sample_) {
    const auto& sample = *census_sample_;
    ImGui::Text("vblank/s: %.1f", sample.vblank);
    ImGui::Text("vdswap/s: %.1f", sample.vdswap);
    ImGui::Text("present/s: %.1f", sample.present);
    ImGui::Text("guest clock: %.1f ms/s (%.1f%%)", sample.guest_ms_per_s,
                sample.guest_ms_per_s / 10.0);
  } else {
    ImGui::TextUnformatted("vblank/s: --");
    ImGui::TextUnformatted("vdswap/s: --");
    ImGui::TextUnformatted("present/s: --");
    ImGui::TextUnformatted("guest clock: --");
  }
  ImGui::End();
  (void)io;
}

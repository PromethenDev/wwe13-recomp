#include "recorder.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_INT32(wwe13_record_frames, 0, "WWE13", "Record this many native-renderer frames (0 = off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(wwe13_record_dir, "frames", "WWE13", "Output folder for native renderer frame recordings")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace svr::recorder {
namespace {
std::mutex g_mutex;
std::FILE* g_file = nullptr;
int g_requested = 0;
int g_frames = 0;
bool g_done = false;  // one recording per run: never reopen (and overwrite) after the last frame
std::chrono::steady_clock::time_point g_start;

uint32_t HookId(const char* name) {
  // Stable FNV-1a identifier; names are in the symbol list and the hash is backend-independent.
  uint32_t h = 2166136261u;
  for (; *name; ++name) h = (h ^ static_cast<uint8_t>(*name)) * 16777619u;
  return h;
}
void Close() {
  if (!g_file) return;
  std::fclose(g_file);
  g_file = nullptr;
  g_done = true;
  const auto bytes = std::filesystem::file_size(REXCVAR_GET(wwe13_record_dir) + "/calls.bin");
  REXLOG_INFO("[recorder] wrote {} frames, {:.1f} MB", g_frames, bytes / (1024.0 * 1024.0));
}
void Begin() {
  if (g_file || g_done || g_requested <= 0) return;
  const std::string dir = REXCVAR_GET(wwe13_record_dir);
  std::filesystem::create_directories(dir);
  g_file = std::fopen((dir + "/calls.bin").c_str(), "wb");
  if (!g_file) { REXLOG_ERROR("[recorder] cannot open output folder {}", dir); g_requested = 0; return; }
  FileHeader header;
  std::fwrite(&header, sizeof(header), 1, g_file);
  g_start = std::chrono::steady_clock::now();
}
}
void RecordHook(const char* name, const PPCContext& args, uint32_t return_value, bool has_return) {
  static const bool on = REXCVAR_GET(wwe13_record_frames) > 0;
  if (!on) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_requested) g_requested = REXCVAR_GET(wwe13_record_frames);
  const bool swap = std::string(name) == "D3DDevice_Swap";
  if (swap && !g_file) Begin();
  if (g_file) {
    EventRecord event{};
    event.hook_id = HookId(name);
    event.flags = has_return ? 1u : 0u;
    event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - g_start).count();
    event.return_value = return_value;
    const uint64_t integer_regs[] = {args.r1.u64,args.r2.u64,args.r3.u64,args.r4.u64,args.r5.u64,args.r6.u64,args.r7.u64,
      args.r8.u64,args.r9.u64,args.r10.u64,args.r11.u64,args.r12.u64,args.r13.u64};
    const uint64_t float_regs[] = {args.f1.u64,args.f2.u64,args.f3.u64,args.f4.u64,args.f5.u64,args.f6.u64,args.f7.u64,args.f8.u64};
    std::copy(integer_regs, integer_regs + 13, event.registers);
    std::copy(float_regs, float_regs + 8, event.registers + 13);
    std::fwrite(&event, sizeof(event), 1, g_file);
    if (swap && ++g_frames >= g_requested) Close();
  }
}
}  // namespace svr::recorder

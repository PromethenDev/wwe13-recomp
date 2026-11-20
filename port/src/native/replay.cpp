// WWE '13 native-renderer replay (replay.h). Runs a recording.w13r back through the
// renderer: guest memory and the D3D device block are restored from the page/device
// records, and each recorded call's renderer side is re-run with its recorded registers.
// The game itself never runs (see wwe13_app.h, which suppresses the module launch).
#include "replay.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging/macros.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/window.h>

#include "recorder.h"
#include "svr_hook.h"

#include "gpu/device.h"
#include "svr_resources.h"

#if __has_include(<miniz.h>)
#include <miniz.h>
#define W13R_INFLATE 1
#endif

REXCVAR_DEFINE_STRING(wwe13_replay_dir, "", "WWE13",
                      "Folder holding recording.w13r: replay it instead of running the game")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(wwe13_replay_png, "", "WWE13",
                      "Replay: comma list of frame indices to dump as PNG (e.g. 0,5,10)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(wwe13_replay_loops, 1, "WWE13",
                     "Replay: run the recording this many times")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace svr::replay {

bool Enabled() {
  return !REXCVAR_GET(wwe13_replay_dir).empty();
}

namespace {

struct ReplayState {
  rex::memory::Memory* memory = nullptr;
  std::unordered_set<uint32_t> png_frames;
  uint32_t front_buffer_va = 0;  // last Swap's front buffer (r4), for PNG capture
  uint32_t frames = 0;
  uint32_t calls = 0;
  std::unordered_set<uint32_t> committed;  // guest heap pages already allocated
  uint32_t alloc_failures = 0;
};

// Reads <dir>/recording.w13r into one flat byte stream: each chunk is uncompressed (miniz
// mz_uncompress) when stored_bytes < raw_bytes, and concatenated. The result is a stream of
// Records (RecordHeader + body).
bool ReadRecording(const std::string& dir, std::vector<uint8_t>& out) {
  const std::string path = dir + "/recording.w13r";
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  svr::recorder::FileHeader fh{};
  const bool header_ok = std::fread(&fh, sizeof(fh), 1, f) == 1 &&
                         std::memcmp(fh.magic, svr::recorder::kMagic, sizeof(fh.magic)) == 0;
  if (!header_ok) {
    std::fclose(f);
    return false;
  }
  out.clear();
  svr::recorder::ChunkHeader ch{};
  while (std::fread(&ch, sizeof(ch), 1, f) == 1) {
    if (!ch.stored_bytes && ch.raw_bytes) {  // empty chunk (writer never emits these): stop safe
      std::fclose(f);
      return false;
    }
    std::vector<uint8_t> packed(ch.stored_bytes);
    if (ch.stored_bytes && std::fread(packed.data(), 1, ch.stored_bytes, f) != ch.stored_bytes) {
      std::fclose(f);
      return false;
    }
    if (ch.stored_bytes == ch.raw_bytes) {
      out.insert(out.end(), packed.begin(), packed.end());
    } else {
#if defined(W13R_INFLATE)
      std::vector<uint8_t> raw(ch.raw_bytes);
      mz_ulong dest = mz_ulong(raw.size());
      if (mz_uncompress(raw.data(), &dest, packed.data(), mz_ulong(packed.size())) != MZ_OK) {
        std::fclose(f);
        return false;
      }
      raw.resize(dest);
      out.insert(out.end(), raw.begin(), raw.end());
#else
      // This build has no miniz (Vulkan-only): only uncompressed recordings replay.
      std::fclose(f);
      return false;
#endif
    }
  }
  std::fclose(f);
  return !out.empty();
}

// Restores guest memory from a kBasePage/kFramePage body {space, offset, 4096 bytes}.
void ApplyPage(ReplayState& s, const uint8_t* body, uint32_t bytes) {
  if (bytes < 8 + svr::recorder::kPageBytes) return;
  uint32_t space = 0, offset = 0;
  std::memcpy(&space, body, 4);
  std::memcpy(&offset, body + 4, 4);
  if (space == 0) {
    // The game never ran, so its virtual heaps hold nothing: allocate each recorded page through
    // the guest heap (host commit + the heap's page table, which try_at validation consults).
    // Physical memory is host-committed at startup (xmemory.cpp) and needs nothing.
    auto* heap = s.memory->LookupHeap(offset);
    if (!heap) return;
    const uint32_t ps = heap->page_size();
    const uint32_t page = offset & ~(ps - 1);
    if (s.committed.insert(page).second) {
      rex::memory::HeapAllocationInfo info{};
      const bool known = heap->QueryRegionInfo(page, &info);
      const uint32_t rw = rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite;
      if (known && (info.state & rex::memory::kMemoryAllocationCommit) &&
          (info.protect & rw) != rw) {
        // Already committed but not writable (the loaded game image's code and read-only data):
        // make it writable so the recorded bytes can be restored. The game never executes here.
        heap->Protect(page, ps, rw);
      }
      if (!known || !(info.state & rex::memory::kMemoryAllocationCommit)) {
        if (!heap->AllocFixed(page, ps, ps,
                              rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit, rw) &&
            !heap->AllocFixed(page, ps, ps, rex::memory::kMemoryAllocationCommit, rw)) {
          if (++s.alloc_failures <= 8)
            REXLOG_WARN("[replay] cannot commit guest page 0x{:08X} (heap page {} bytes)", page, ps);
          return;
        }
      }
    }
  }
  uint8_t* base = space == 0 ? s.memory->virtual_membase() : s.memory->physical_membase();
  std::memcpy(base + offset, body + 8, svr::recorder::kPageBytes);
}

// Restores changed bytes of the D3D device block {device, {off,len,bytes} runs}.
void ApplyDevice(ReplayState& s, const uint8_t* body, uint32_t bytes) {
  if (bytes < 4) return;
  uint32_t device = 0;
  std::memcpy(&device, body, 4);
  uint8_t* dev = s.memory->TranslateVirtual<uint8_t*>(device);
  uint32_t o = 4;
  while (o + 4 <= bytes) {
    uint16_t off = 0, len = 0;
    std::memcpy(&off, body + o, 2);
    std::memcpy(&len, body + o + 2, 2);
    o += 4;
    if (o + len > bytes) break;
    std::memcpy(dev + off, body + o, len);
    o += len;
  }
}

// Rebuilds the argument registers of one call and runs its renderer side. The recorded
// registers are r1-r13 then f1-f8 (see EventRecord / recorder.cpp RecordHook); only r3-r10,
// r1 (stack args) and f1-f8 are read by HostToGuestFunction, the rest is filled for the
// RESULT observers that peek at raw args.
void ApplyCall(ReplayState& s, const uint8_t* body, uint32_t bytes) {
  if (bytes < sizeof(svr::recorder::EventRecord)) return;
  svr::recorder::EventRecord ev{};
  std::memcpy(&ev, body, sizeof(ev));
  const svr::recorder::HookEntry* hook = svr::recorder::FindHook(ev.hook_id);
  if (!hook) return;  // a call this build does not hook (compiled out): skip
  // Create calls were registered from their kShader / kDecl copies; re-running them would read the
  // game's (possibly reused) buffer.
  if (std::strncmp(hook->name, "D3DDevice_Create", 16) == 0) return;

  PPCContext ctx{};
  const uint64_t* r = ev.registers;
  ctx.r1.u64 = r[0];
  ctx.r2.u64 = r[1];
  ctx.r3.u64 = r[2];
  ctx.r4.u64 = r[3];
  ctx.r5.u64 = r[4];
  ctx.r6.u64 = r[5];
  ctx.r7.u64 = r[6];
  ctx.r8.u64 = r[7];
  ctx.r9.u64 = r[8];
  ctx.r10.u64 = r[9];
  ctx.r11.u64 = r[10];
  ctx.r12.u64 = r[11];
  ctx.r13.u64 = r[12];
  ctx.f1.u64 = r[13];
  ctx.f2.u64 = r[14];
  ctx.f3.u64 = r[15];
  ctx.f4.u64 = r[16];
  ctx.f5.u64 = r[17];
  ctx.f6.u64 = r[18];
  ctx.f7.u64 = r[19];
  ctx.f8.u64 = r[20];

  if (std::strcmp(hook->name, "D3DDevice_Swap") == 0)
    s.front_buffer_va = ctx.r4.u32;  // Swap's second argument is the front buffer

  hook->fn(ctx, uint32_t(ev.return_value), s.memory->virtual_membase());
  ++s.calls;
}

// ---- PNG (optional, cvar wwe13_replay_png) ---------------------------------

// Reads the presented front buffer back and writes it as <dir>/frame_<n>.png. Best-effort:
// the front buffer must be a 4-byte-per-texel target; the byte order follows the target's
// format (RGBA8 here, so a BGRA8 front buffer would come out red/blue-swapped). Needs miniz
// (the D3D12 target has it) for the zlib IDAT.
void DumpPng(ReplayState& s, uint32_t index) {
  if (!s.front_buffer_va) return;
  bd::gpu::GuestTexture* tex = bd::gpu::SvrResolveTarget(s.front_buffer_va);
  if (!tex) return;
  std::vector<uint8_t> pixels;
  uint32_t row_pitch = 0;
  if (!bd::gpu::Video::ReadbackTextureLevel(tex, 0, 0, pixels, row_pitch)) {
    REXLOG_WARN("[replay] PNG {}: readback failed", index);
    return;
  }
  if (!tex->width || !tex->height || pixels.size() < size_t(row_pitch) * tex->height) return;
#if defined(W13R_INFLATE)
  const std::string path =
      REXCVAR_GET(wwe13_replay_dir) + "/frame_" + std::to_string(index) + ".png";
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;

  const auto put32 = [](std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
  };
  const auto chunk = [&](const char type[4], const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hdr;
    put32(hdr, uint32_t(data.size()));
    hdr.insert(hdr.end(), type, type + 4);
    std::fwrite(hdr.data(), 1, hdr.size(), f);
    if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
    mz_ulong crc = mz_crc32(0, reinterpret_cast<const unsigned char*>(type), 4);
    if (!data.empty()) crc = mz_crc32(crc, data.data(), data.size());
    std::vector<uint8_t> crcb;
    put32(crcb, uint32_t(crc));
    std::fwrite(crcb.data(), 1, 4, f);
  };

  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  std::fwrite(sig, 1, 8, f);

  std::vector<uint8_t> ihdr;
  put32(ihdr, tex->width);
  put32(ihdr, tex->height);
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(6);  // color type: RGBA
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace
  chunk("IHDR", ihdr);

  // One scanline per row, each prefixed with filter byte 0 (None).
  std::vector<uint8_t> raw;
  raw.reserve(size_t(row_pitch + 1) * tex->height);
  for (uint32_t y = 0; y < tex->height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), pixels.data() + size_t(y) * row_pitch,
               pixels.data() + size_t(y) * row_pitch + size_t(tex->width) * 4);
  }
  std::vector<uint8_t> idat(mz_compressBound(mz_ulong(raw.size())));
  mz_ulong idat_len = idat.size();
  if (mz_compress(idat.data(), &idat_len, raw.data(), mz_ulong(raw.size())) == MZ_OK) {
    idat.resize(idat_len);
    chunk("IDAT", idat);
    chunk("IEND", {});
  }
  std::fclose(f);
#else
  REXLOG_WARN("[replay] PNG {}: this build has no miniz, cannot write", index);
#endif
}

// Runs one pass over the record stream.
void RunPass(ReplayState& s, const std::vector<uint8_t>& stream) {
  size_t p = 0;
  while (p < stream.size()) {
    if (p + sizeof(svr::recorder::RecordHeader) > stream.size()) break;
    svr::recorder::RecordHeader h{};
    std::memcpy(&h, stream.data() + p, sizeof(h));
    p += sizeof(h);
    if (p + h.bytes > stream.size()) break;
    const uint8_t* body = stream.data() + p;
    switch (h.type) {
      case svr::recorder::kBasePage:
      case svr::recorder::kFramePage:
        ApplyPage(s, body, h.bytes);
        break;
      case svr::recorder::kShader:
        if (h.bytes > 8) {
          uint32_t obj = 0, pixel = 0;
          std::memcpy(&obj, body, 4);
          std::memcpy(&pixel, body + 4, 4);
          bd::gpu::SvrRegisterShaderContainer(obj, body + 8, pixel != 0);
        }
        break;
      case svr::recorder::kDecl:
        if (h.bytes > 8) {
          uint32_t obj = 0;
          std::memcpy(&obj, body, 4);
          bd::gpu::SvrRegisterVertexDeclaration(obj, reinterpret_cast<const bd::gpu::GuestVertexElement*>(body + 8));
        }
        break;
      case svr::recorder::kDevice:
        ApplyDevice(s, body, h.bytes);
        break;
      case svr::recorder::kCall:
        ApplyCall(s, body, h.bytes);
        break;
      case svr::recorder::kFrameEnd: {
        uint32_t index = 0;
        if (h.bytes >= 4) std::memcpy(&index, body, 4);
        ++s.frames;
        if (s.png_frames.count(index)) DumpPng(s, index);
        break;
      }
      default:
        return;  // unknown record type: stop this pass
    }
    p += h.bytes;
  }
}

void ParsePngList(const std::string& text, std::unordered_set<uint32_t>& out) {
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find(',', i);
    if (j == std::string::npos) j = text.size();
    const std::string tok = text.substr(i, j - i);
    i = j + 1;
    if (tok.empty()) continue;
    char* end = nullptr;
    const long v = std::strtol(tok.c_str(), &end, 10);
    if (end != tok.c_str() && *end == '\0' && v >= 0) out.insert(uint32_t(v));
  }
}

}  // namespace

bool Run(rex::ui::Window* window) {
  auto* memory = REX_KERNEL_MEMORY();
  if (!memory) {
    REXLOG_ERROR("[replay] guest memory is not up");
    return false;
  }

  // The renderer is normally initialised by Wwe13App::StartNativeRenderer (the game's
  // D3D device-creation path) in OnPreLaunchModule. In replay mode the game never runs, so
  // create the host device here: Video::CreateHostDevice needs only the window, not a guest
  // D3D device.
  if (!bd::gpu::Video::HostDevice() && !bd::gpu::Video::CreateHostDevice(window)) {
    REXLOG_ERROR("[replay] could not create the host renderer device");
    return false;
  }

  std::vector<uint8_t> stream;
  if (!ReadRecording(REXCVAR_GET(wwe13_replay_dir), stream)) {
    REXLOG_ERROR("[replay] cannot read {}/recording.w13r", REXCVAR_GET(wwe13_replay_dir));
    return false;
  }

  ReplayState s;
  s.memory = memory;
  ParsePngList(REXCVAR_GET(wwe13_replay_png), s.png_frames);

  uint32_t hook_count = 0;
  svr::recorder::ForEachHook([&](const svr::recorder::HookEntry& e) {
    (void)e;
    ++hook_count;
  });
  REXLOG_INFO("[replay] {} hooks registered, {} PNG frames requested", hook_count,
              s.png_frames.size());

  const int loops = std::max(1, REXCVAR_GET(wwe13_replay_loops));
  uint64_t total_frames = 0, total_calls = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < loops; ++i) {
    s.frames = 0;
    s.calls = 0;
    RunPass(s, stream);
    total_frames += s.frames;
    total_calls += s.calls;
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double ms = total_frames ? secs * 1000.0 / double(total_frames) : 0.0;
  REXLOG_INFO("[replay] {} frames, avg {:.1f} ms/frame ({} calls, {} loop{})", total_frames, ms,
              total_calls, loops, loops == 1 ? "" : "s");

  window->RequestClose();
  return true;
}

}  // namespace svr::replay

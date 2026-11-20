// WWE '13 native-renderer recorder (recorder.h). Recording only: every cost below is paid only while
// wwe13_record_frames > 0, and the game simply runs slower then (frames are still produced in order).
#include "recorder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if __has_include(<miniz.h>)
#include <miniz.h>
#define W13R_DEFLATE 1
#endif

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/logging/macros.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "gpu/resources.h"

REXCVAR_DEFINE_INT32(wwe13_record_frames, 0, "WWE13",
                     "Record this many native-renderer frames for offline replay (0 = off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(wwe13_record_after, 0, "WWE13",
                     "Start recording after this many frames (Swaps), e.g. to skip boot and menus")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(wwe13_record_dir, "recording", "WWE13", "Output folder for a native-renderer recording")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace svr::recorder {

uint32_t HookId(const char* name) {
  uint32_t h = 2166136261u;
  for (; *name; ++name) h = (h ^ static_cast<uint8_t>(*name)) * 16777619u;
  return h;
}

namespace {
// Replay hook table: populated by the SVR_HOOK_* macros at static-init time, read
// by the replay. A Meyers singleton so registration order across TUs never matters.
struct HookRegistry {
  std::mutex mutex;
  std::vector<HookEntry> entries;
};
HookRegistry& R() {
  static HookRegistry r;
  return r;
}
}  // namespace

const HookEntry* RegisterHook(const char* name, ReplayHookFn fn, HookKind kind) {
  if (!name || !fn) return nullptr;
  HookRegistry& r = R();
  std::lock_guard lock(r.mutex);
  for (const HookEntry& e : r.entries)
    if (std::strcmp(e.name, name) == 0) return &e;
  r.entries.push_back(HookEntry{HookId(name), name, fn, kind});
  return &r.entries.back();
}

const HookEntry* FindHook(uint32_t id) {
  HookRegistry& r = R();
  std::lock_guard lock(r.mutex);
  for (const HookEntry& e : r.entries)
    if (e.id == id) return &e;
  return nullptr;
}

void ForEachHook(const std::function<void(const HookEntry&)>& visitor) {
  if (!visitor) return;
  HookRegistry& r = R();
  std::lock_guard lock(r.mutex);
  for (const HookEntry& e : r.entries) visitor(e);
}

namespace {
constexpr size_t kChunkBytes = 32u << 20;

// Background writer: compresses and writes full chunks so the hooks only append to a buffer.
class Writer {
 public:
  bool Open(const std::string& path) {
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return false;
    FileHeader h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.version = kVersion;
    h.page_bytes = kPageBytes;
    std::fwrite(&h, sizeof(h), 1, file_);
    thread_ = std::thread([this] { Run(); });
    return true;
  }
  void Submit(std::vector<uint8_t>&& chunk) {
    if (chunk.empty()) return;
    std::unique_lock lock(mutex_);
    // Bound memory: wait while 8 chunks (256 MB) are queued.
    space_.wait(lock, [this] { return queue_.size() < 8; });
    queue_.push_back(std::move(chunk));
    ready_.notify_one();
  }
  uint64_t Close() {
    {
      std::lock_guard lock(mutex_);
      closing_ = true;
      ready_.notify_one();
    }
    if (thread_.joinable()) thread_.join();
    uint64_t bytes = 0;
    if (file_) {
      bytes = uint64_t(std::ftell(file_));
      std::fclose(file_);
      file_ = nullptr;
    }
    return bytes;
  }

 private:
  void Run() {
    for (;;) {
      std::vector<uint8_t> raw;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return closing_ || !queue_.empty(); });
        if (queue_.empty()) return;
        raw = std::move(queue_.front());
        queue_.pop_front();
        space_.notify_one();
      }
      ChunkHeader ch{uint32_t(raw.size()), uint32_t(raw.size())};
      const uint8_t* data = raw.data();
      std::vector<uint8_t> packed;
#if defined(W13R_DEFLATE)
      mz_ulong packed_size = mz_compressBound(mz_ulong(raw.size()));
      packed.resize(packed_size);
      if (mz_compress2(packed.data(), &packed_size, raw.data(), mz_ulong(raw.size()), 1) == MZ_OK &&
          packed_size < raw.size()) {
        ch.stored_bytes = uint32_t(packed_size);
        data = packed.data();
      }
#endif
      std::fwrite(&ch, sizeof(ch), 1, file_);
      std::fwrite(data, 1, ch.stored_bytes, file_);
    }
  }
  std::FILE* file_ = nullptr;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable ready_, space_;
  std::deque<std::vector<uint8_t>> queue_;
  bool closing_ = false;
};

struct State {
  std::mutex mutex;
  Writer writer;
  std::vector<uint8_t> chunk;
  bool active = false, done = false, scan_pending = false, base_taken = false;
  int requested = 0, frames = 0, skipped = 0;
  uint32_t device = 0;
  std::vector<uint8_t> device_copy;
  std::unordered_map<uint32_t, std::vector<uint8_t>> shaders, decls;  // object -> kShader / kDecl body
  std::unordered_map<uint64_t, uint64_t> page_hash;  // (space << 32 | offset) -> XXH3
  uint64_t base_pages = 0, frame_pages = 0, device_bytes = 0, calls = 0;
  double scan_ms = 0;
  std::chrono::steady_clock::time_point start;
};
State& S() {
  static State s;
  return s;
}

void Put(State& s, const void* p, size_t n) {
  const auto* b = static_cast<const uint8_t*>(p);
  s.chunk.insert(s.chunk.end(), b, b + n);
}
void PutRecord(State& s, uint32_t type, const void* body, uint32_t bytes) {
  RecordHeader h{type, bytes};
  Put(s, &h, sizeof(h));
  Put(s, body, bytes);
  if (s.chunk.size() >= kChunkBytes) {
    s.writer.Submit(std::move(s.chunk));
    s.chunk.clear();
    s.chunk.reserve(kChunkBytes + (1u << 20));
  }
}

// Every committed, readable host page of one guest space: [base, base + size).
template <typename F>
void ForEachReadablePage(const uint8_t* base, uint64_t size, F&& f) {
#if defined(_WIN32)
  uint64_t off = 0;
  while (off < size) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(base + off, &mbi, sizeof(mbi))) break;
    const uint64_t region_end =
        std::min<uint64_t>(size, uint64_t(static_cast<const uint8_t*>(mbi.BaseAddress) - base) + mbi.RegionSize);
    const DWORD prot = mbi.Protect & 0xFF;
    const bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                          (prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                           prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
                           prot == PAGE_EXECUTE_WRITECOPY);
    if (readable)
      for (uint64_t p = off & ~uint64_t(kPageBytes - 1); p + kPageBytes <= region_end; p += kPageBytes)
        f(uint32_t(p), base + p);
    off = region_end > off ? region_end : off + kPageBytes;
  }
#else
  (void)base;
  (void)size;
  (void)f;
#endif
}

// Base snapshot (base == true) or the pages changed since the last scan.
void Scan(State& s, bool base) {
  auto* memory = REX_KERNEL_MEMORY();
  if (!memory) return;
  const auto t0 = std::chrono::steady_clock::now();
  struct Space {
    uint32_t id;
    const uint8_t* base;
    uint64_t size;
  };
  // Virtual 0x00000000-0x9FFFFFFF; 0xA0000000+ are views of the 512 MB physical memory, taken once.
  const Space spaces[] = {{0, memory->virtual_membase(), 0xA0000000ull},
                          {1, memory->physical_membase(), 0x20000000ull}};
  for (const Space& sp : spaces) {
    ForEachReadablePage(sp.base, sp.size, [&](uint32_t offset, const uint8_t* page) {
      const uint64_t key = (uint64_t(sp.id) << 32) | offset;
      const uint64_t h = XXH3_64bits(page, kPageBytes);
      auto [it, inserted] = s.page_hash.try_emplace(key, h);
      if (!inserted) {
        if (it->second == h) return;
        it->second = h;
      }
      // A page first seen after the base snapshot that is all zero needs no record (replay memory starts zeroed).
      static const uint64_t kZeroHash = [] {
        static const uint8_t zero[kPageBytes] = {};
        return XXH3_64bits(zero, kPageBytes);
      }();
      if (!base && inserted && h == kZeroHash) return;
      uint8_t body[8 + kPageBytes];
      std::memcpy(body, &sp.id, 4);
      std::memcpy(body + 4, &offset, 4);
      std::memcpy(body + 8, page, kPageBytes);
      PutRecord(s, base ? kBasePage : kFramePage, body, sizeof(body));
      (base ? s.base_pages : s.frame_pages)++;
    });
  }
  s.scan_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Changed bytes of the D3D device block since the last hook, as {offset, length, bytes} runs.
void DeviceDelta(State& s) {
  if (!s.device) return;
  auto* memory = REX_KERNEL_MEMORY();
  if (!memory) return;
  const auto* dev = memory->TranslateVirtual<const uint8_t*>(s.device);
  if (s.device_copy.size() != kDeviceBytes) s.device_copy.assign(kDeviceBytes, 0xCD);  // forces a full first record
  std::vector<uint8_t> body(4);
  std::memcpy(body.data(), &s.device, 4);
  constexpr uint32_t kBlock = 16;
  for (uint32_t o = 0; o < kDeviceBytes;) {
    if (std::memcmp(dev + o, s.device_copy.data() + o, std::min(kBlock, kDeviceBytes - o)) == 0) {
      o += kBlock;
      continue;
    }
    uint32_t e = o;
    while (e < kDeviceBytes && e - o < 0xF000 &&
           std::memcmp(dev + e, s.device_copy.data() + e, std::min(kBlock, kDeviceBytes - e)) != 0)
      e += kBlock;
    e = std::min(e, kDeviceBytes);
    const uint16_t off = uint16_t(o), len = uint16_t(e - o);
    body.insert(body.end(), reinterpret_cast<const uint8_t*>(&off), reinterpret_cast<const uint8_t*>(&off) + 2);
    body.insert(body.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 2);
    body.insert(body.end(), dev + o, dev + e);
    std::memcpy(s.device_copy.data() + o, dev + o, len);
    o = e;
  }
  if (body.size() > 4) {
    PutRecord(s, kDevice, body.data(), uint32_t(body.size()));
    s.device_bytes += body.size();
  }
}

// Keeps a copy of a created shader / vertex declaration (from boot, while recording is enabled).
const std::vector<uint8_t>* NoteCreate(State& s, const char* name, const PPCContext& args, uint32_t object) {
  const bool vs = std::strcmp(name, "D3DDevice_CreateVertexShader") == 0;
  const bool ps = std::strcmp(name, "D3DDevice_CreatePixelShader") == 0;
  const bool decl = std::strcmp(name, "D3DDevice_CreateVertexDeclaration") == 0;
  if ((!vs && !ps && !decl) || !object) return nullptr;
  std::vector<uint8_t> body(8);
  std::memcpy(body.data(), &object, 4);
  if (decl) {
    const auto* e = bd::mem::at<const bd::gpu::GuestVertexElement>(args.r3.u32);
    if (!e) return nullptr;
    uint32_t n = 0;
    while (n < 64 && uint16_t(e[n].stream) != 0xFF) ++n;
    ++n;  // the terminator
    std::memcpy(body.data() + 4, &n, 4);
    body.insert(body.end(), reinterpret_cast<const uint8_t*>(e), reinterpret_cast<const uint8_t*>(e + n));
    return &(s.decls[object] = std::move(body));
  }
  const auto* c = bd::mem::at<const bd::gpu::ShaderContainer>(args.r3.u32);
  if (!c) return nullptr;
  const uint32_t length = uint32_t(c->virtualSize) + uint32_t(c->physicalSize);
  if (length < sizeof(bd::gpu::ShaderContainer) || length > (4u << 20)) return nullptr;
  const uint32_t pixel = ps ? 1u : 0u;
  std::memcpy(body.data() + 4, &pixel, 4);
  body.insert(body.end(), reinterpret_cast<const uint8_t*>(c), reinterpret_cast<const uint8_t*>(c) + length);
  return &(s.shaders[object] = std::move(body));
}

void Finish(State& s) {
  if (!s.chunk.empty()) s.writer.Submit(std::move(s.chunk));
  s.chunk.clear();
  const uint64_t bytes = s.writer.Close();
  s.active = false;
  s.done = true;
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
  REXLOG_INFO(
      "[recorder] wrote {} frames, {:.1f} MB: {} calls, {} base pages, {} changed pages, {:.1f} MB device deltas, "
      "{:.0f} ms scanning, {:.1f} s",
      s.frames, bytes / 1048576.0, s.calls, s.base_pages, s.frame_pages, s.device_bytes / 1048576.0, s.scan_ms, secs);
}

void WriteMeta(const std::string& dir, uint32_t device) {
  std::FILE* f = std::fopen((dir + "/meta.txt").c_str(), "w");
  if (!f) return;
  std::fprintf(f, "format=W13REC2\ndevice=0x%08X\nframes=%d\n", device, REXCVAR_GET(wwe13_record_frames));
  std::fclose(f);
}
}  // namespace

void RecordHook(const char* name, const PPCContext& args, uint32_t return_value, bool has_return) {
  static const bool on = REXCVAR_GET(wwe13_record_frames) > 0;
  if (!on) return;
  State& s = S();
  std::lock_guard lock(s.mutex);
  if (s.done) return;
  const std::vector<uint8_t>* created = has_return ? NoteCreate(s, name, args, return_value) : nullptr;
  const bool swap = std::strcmp(name, "D3DDevice_Swap") == 0;
  if (!s.active) {
    // Start at a frame boundary: the first Swap; its device argument is the D3D device.
    if (!swap) return;
    if (s.skipped++ < REXCVAR_GET(wwe13_record_after)) return;
    const std::string dir = REXCVAR_GET(wwe13_record_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!s.writer.Open(dir + "/recording.w13r")) {
      REXLOG_ERROR("[recorder] cannot write to {}", dir);
      s.done = true;
      return;
    }
    s.requested = REXCVAR_GET(wwe13_record_frames);
    s.device = args.r3.u32;
    s.chunk.reserve(kChunkBytes + (1u << 20));
    s.active = true;
    s.scan_pending = true;
    s.start = std::chrono::steady_clock::now();
    WriteMeta(dir, s.device);
    for (const auto& [obj, body] : s.shaders) PutRecord(s, kShader, body.data(), uint32_t(body.size()));
    for (const auto& [obj, body] : s.decls) PutRecord(s, kDecl, body.data(), uint32_t(body.size()));
    REXLOG_INFO("[recorder] recording {} frames to {} (device 0x{:08X}, {} shaders, {} declarations)", s.requested,
                dir, s.device, s.shaders.size(), s.decls.size());
    return;
  }
  if (s.scan_pending) {
    Scan(s, !s.base_taken);
    s.base_taken = true;
    s.scan_pending = false;
  }
  if (created)
    PutRecord(s, std::strcmp(name, "D3DDevice_CreateVertexDeclaration") == 0 ? kDecl : kShader, created->data(),
              uint32_t(created->size()));
  DeviceDelta(s);
  EventRecord event{};
  event.hook_id = HookId(name);
  event.flags = has_return ? 1u : 0u;
  event.timestamp_ns = uint64_t(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - s.start).count());
  event.return_value = return_value;
  const uint64_t regs[21] = {args.r1.u64,  args.r2.u64,  args.r3.u64, args.r4.u64, args.r5.u64, args.r6.u64,
                             args.r7.u64,  args.r8.u64,  args.r9.u64, args.r10.u64, args.r11.u64, args.r12.u64,
                             args.r13.u64, args.f1.u64,  args.f2.u64, args.f3.u64, args.f4.u64, args.f5.u64,
                             args.f6.u64,  args.f7.u64,  args.f8.u64};
  std::memcpy(event.registers, regs, sizeof(regs));
  PutRecord(s, kCall, &event, sizeof(event));
  ++s.calls;
  if (swap) {
    const uint32_t index = uint32_t(s.frames);
    PutRecord(s, kFrameEnd, &index, sizeof(index));
    s.scan_pending = true;  // the next frame's data is written after this Swap
    if (++s.frames >= s.requested) Finish(s);
  }
}
}  // namespace svr::recorder

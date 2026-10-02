// Default-off runtime diagnostics for the internal-resolution investigation.
// WWE13_RES_PROBE=1 is sampled once, on the first overridden guest call.
// WWE13_SIZE_PROBE=1 adds observational scene-size/root logging; resolution
// changes are owned by scene_resolution.cpp's setter override.

#include <array>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/memory.h>

namespace wwe13 {
bool GetCustomSceneSize(uint32_t* width, uint32_t* height);
}

namespace {

bool ProbeEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWE13_RES_PROBE");
    const bool requested = value != nullptr && std::string(value) == "1";
    if (requested) {
      REXLOG_INFO("[wwe13-res-probe] enabled (WWE13_RES_PROBE=1)");
    }
    return requested;
  }();
  return enabled;
}

struct CallCounts {
  uint64_t tile_caller = 0;
  uint64_t tile_helper = 0;
  uint64_t mode_table = 0;
  uint64_t size_relayout = 0;   // sub_82A978C0
  uint64_t size_template = 0;   // sub_82B0A730
  uint64_t size_record = 0;     // sub_82A96D80
};

// --- Scene-size index probe (probe 4) --------------------------------------

// Root object global, static: `lis r11,-31909; stw r31,-5112(r11)` in the root
// constructor (generated/wwe13_recomp.69.cpp:72506). 0x835B0000 - 5112.
constexpr uint32_t kRootGlobal = 0x835AEC08;

// Guest memory base for helpers called outside a PPC_FUNC; set by every override.
uint8_t* g_guest_base = nullptr;
std::string Hex32(uint32_t value);
constexpr uint32_t kRootSizeIndex = 288;  // requested scene-size index
constexpr uint32_t kRootSizeApplied = 284;  // last applied scene-size index
constexpr uint32_t kRootWidth = 300;  // table 0x833D2BB0[index].w
constexpr uint32_t kRootHeight = 304;  // table 0x833D2BB0[index].h
constexpr uint32_t kRootKey2 = 320;
constexpr uint32_t kRootSelector = 324;
constexpr uint32_t kSizeTable = 0x833D2BB0;

uint32_t RootPtr() {
  uint8_t* const base = g_guest_base;
  return base ? PPC_LOAD_U32(kRootGlobal) : 0;
}

std::string FormatRootState() {
  uint8_t* const base = g_guest_base;
  const uint32_t root = RootPtr();
  if (root == 0) {
    return "root=<null>";
  }
  std::ostringstream out;
  out << "root=" << Hex32(root) << " +284=" << PPC_LOAD_U32(root + kRootSizeApplied)
      << " +288=" << PPC_LOAD_U32(root + kRootSizeIndex) << " +292="
      << PPC_LOAD_U32(root + 292) << " +300=" << PPC_LOAD_U32(root + kRootWidth)
      << " +304=" << PPC_LOAD_U32(root + kRootHeight) << " +320="
      << PPC_LOAD_U32(root + kRootKey2) << " +324=" << PPC_LOAD_U32(root + kRootSelector);
  return out.str();
}

// Table 0x833D2BB0 entry [index] = {width, height}; PPC_LOAD_U32 is a
// big-endian load, so the raw words are already 640/480/... as integers.
std::string FormatSizeTableEntry(uint32_t index) {
  uint8_t* const base = g_guest_base;
  if (index > 5) {
    return "<out-of-range>";
  }
  const uint32_t w = PPC_LOAD_U32(kSizeTable + index * 8);
  const uint32_t h = PPC_LOAD_U32(kSizeTable + index * 8 + 4);
  return std::to_string(w) + 'x' + std::to_string(h);
}

std::mutex g_probe_mutex;
std::unordered_set<std::string> g_outer_tile_keys;
std::unordered_set<std::string> g_tile_tuple_keys;
std::unordered_set<std::string> g_mode_keys;
CallCounts g_window_counts;
std::chrono::steady_clock::time_point g_window_start{};
bool g_window_started = false;

void RecordCall(std::size_t which) {
  CallCounts report{};
  bool should_report = false;
  {
    std::lock_guard lock(g_probe_mutex);
    if (which == 0) {
      ++g_window_counts.tile_caller;
    } else if (which == 1) {
      ++g_window_counts.tile_helper;
    } else if (which == 2) {
      ++g_window_counts.mode_table;
    } else if (which == 3) {
      ++g_window_counts.size_relayout;
    } else if (which == 5) {
      ++g_window_counts.size_template;
    } else {
      ++g_window_counts.size_record;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!g_window_started) {
      g_window_started = true;
      g_window_start = now;
    } else if (now - g_window_start >= std::chrono::seconds(10)) {
      report = g_window_counts;
      g_window_counts = {};
      g_window_start = now;
      should_report = true;
    }
  }
  if (should_report) {
    REXLOG_INFO("[wwe13-res-probe] calls/10s sub_82B0A840={} sub_82B9F400={} sub_82AA62D8={}",
                report.tile_caller, report.tile_helper, report.mode_table);
    if (report.size_relayout != 0 || report.size_template != 0 ||
        report.size_record != 0) {
      REXLOG_INFO("[wwe13-size-probe] calls/10s relayout(sub_82A978C0)={} "
                  "template(sub_82B0A730)={} sizerecord(sub_82A96D80)={}",
                  report.size_relayout, report.size_template, report.size_record);
    }
  }
}

bool FirstTuple(std::unordered_set<std::string>& keys, const std::string& key) {
  std::lock_guard lock(g_probe_mutex);
  return keys.insert(key).second;
}

std::string Hex32(uint32_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return out.str();
}

std::string FormatArgs(const uint32_t* values, std::size_t count, uint32_t first_register) {
  std::ostringstream out;
  out << '{';
  for (std::size_t i = 0; i < count; ++i) {
    if (i) {
      out << ", ";
    }
    out << 'r' << (first_register + i) << '=' << Hex32(values[i]);
  }
  out << '}';
  return out.str();
}

std::string ReadRects(uint8_t* base, uint32_t address, uint32_t count) {
  if (count != 0 && address == 0) {
    return "<null>";
  }
  std::ostringstream out;
  out << '[';
  const uint32_t records = count < 8 ? count : 8;
  for (uint32_t i = 0; i < records; ++i) {
    if (i) {
      out << ", ";
    }
    out << '(';
    for (uint32_t field = 0; field < 4; ++field) {
      if (field) {
        out << ',';
      }
      const uint32_t value = PPC_LOAD_U32(address + i * 16 + field * 4);
      out << std::bit_cast<int32_t>(value);
    }
    out << ')';
  }
  if (count > records) {
    if (records) {
      out << ", ";
    }
    out << "... (" << count - records << " more; capped at 8)";
  }
  out << ']';
  return out.str();
}

std::string ReadBounds(uint8_t* base, uint32_t address, std::array<uint32_t, 4>& raw) {
  if (address == 0) {
    return "<null>";
  }
  std::ostringstream out;
  out << '[';
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (i) {
      out << ", ";
    }
    raw[i] = PPC_LOAD_U32(address + static_cast<uint32_t>(i * 4));
    const float value = std::bit_cast<float>(raw[i]);
    out << Hex32(raw[i]) << '/' << std::setprecision(9) << value;
  }
  out << ']';
  return out.str();
}

}  // namespace

bool CustomSize(uint32_t* width, uint32_t* height);
void PatchSizeTableForCustom();
void PatchCompositeTexelConstantsForCustom();

extern "C" PPC_FUNC(__imp__sub_82B0A840);
extern "C" PPC_FUNC(sub_82B0A840) {
  g_guest_base = base;
  if (ProbeEnabled()) {
    RecordCall(0);
    const std::array<uint32_t, 6> args = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
                                          ctx.r6.u32, ctx.r7.u32, ctx.r8.u32};
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t object = ctx.r3.u32;
    if (object != 0) {
      const uint32_t count = PPC_LOAD_U32(object + 24);
      const uint32_t rects_address = object + 28;
      const std::string rects = ReadRects(base, rects_address, count);
      const std::string key = Hex32(caller_lr) + ':' + std::to_string(count) + ':' + rects;
      if (FirstTuple(g_outer_tile_keys, key)) {
        REXLOG_INFO("[wwe13-res-probe] sub_82B0A840 caller_lr={} entry_args={} object={} count={} rects_ptr={} rects={} (computed bounds are passed to nested sub_82B9F400)",
                    Hex32(caller_lr), FormatArgs(args.data(), args.size(), 3), Hex32(object), count,
                    Hex32(rects_address), rects);
      }
    } else {
      REXLOG_INFO("[wwe13-res-probe] sub_82B0A840 caller_lr={} entry_args={} null object; list not read",
                  Hex32(caller_lr), FormatArgs(args.data(), args.size(), 3));
    }
  }
  __imp__sub_82B0A840(ctx, base);
  PatchCompositeTexelConstantsForCustom();
}

extern "C" PPC_FUNC(__imp__sub_82B9F400);
extern "C" PPC_FUNC(sub_82B9F400) {
  g_guest_base = base;
  if (ProbeEnabled()) {
    RecordCall(1);
    const std::array<uint32_t, 8> args = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
                                          ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t count = ctx.r5.u32;
    const uint32_t rects_address = ctx.r6.u32;
    const uint32_t bounds_address = ctx.r7.u32;
    const std::string rects = ReadRects(base, rects_address, count);
    std::array<uint32_t, 4> bounds_raw{};
    const std::string bounds = ReadBounds(base, bounds_address, bounds_raw);
    std::ostringstream key;
    key << count << ':' << rects << ':';
    for (uint32_t value : bounds_raw) {
      key << Hex32(value) << ':';
    }
    if (FirstTuple(g_tile_tuple_keys, key.str())) {
      REXLOG_INFO("[wwe13-res-probe] sub_82B9F400 caller_lr={} entry_args={} count={} rects_ptr={} rects={} bounds_ptr={} bounds(raw/float)={}",
                  Hex32(caller_lr), FormatArgs(args.data(), args.size(), 3), count,
                  Hex32(rects_address), rects, Hex32(bounds_address), bounds);
    }
  }
  __imp__sub_82B9F400(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_82AA62D8);
extern "C" PPC_FUNC(sub_82AA62D8) {
  g_guest_base = base;
  PatchSizeTableForCustom();
  const bool enabled = ProbeEnabled();
  std::array<uint32_t, 6> args{};
  uint32_t caller_lr = 0;
  if (enabled) {
    RecordCall(2);
    args = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32};
    caller_lr = static_cast<uint32_t>(ctx.lr);
  }
  __imp__sub_82AA62D8(ctx, base);
  const uint32_t guest_width = PPC_LOAD_U32(0x835B2E1C);
  const uint32_t guest_height = PPC_LOAD_U32(0x835B2E20);
  uint32_t custom_width = 0;
  uint32_t custom_height = 0;
  if (CustomSize(&custom_width, &custom_height) &&
      (guest_width != custom_width || guest_height != custom_height)) {
    PPC_STORE_U32(0x835B2E1C, custom_width);
    PPC_STORE_U32(0x835B2E20, custom_height);
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("[wwe13-size-probe] custom mode dimensions patched {}x{} -> {}x{}",
                  guest_width, guest_height, custom_width, custom_height);
    }
  }
  if (enabled) {
    // sub_82AA62D8 stores width/height at global 0x835B2DE8 + 52/+56.
    const uint32_t width = PPC_LOAD_U32(0x835B2E1C);
    const uint32_t height = PPC_LOAD_U32(0x835B2E20);
    const std::string key = std::to_string(args[0]) + ':' + std::to_string(width) + ':' +
                            std::to_string(height);
    if (FirstTuple(g_mode_keys, key)) {
      REXLOG_INFO("[wwe13-res-probe] sub_82AA62D8 caller_lr={} entry_args={} wrote_width={} wrote_height={}",
                  Hex32(caller_lr), FormatArgs(args.data(), args.size(), 3), width, height);
    }
  }
}

bool CustomSize(uint32_t* width, uint32_t* height) {
  return wwe13::GetCustomSceneSize(width, height);
}

void PatchSizeTableForCustom() {
  uint8_t* const base = g_guest_base;
  uint32_t width = 0;
  uint32_t height = 0;
  if (!base || !CustomSize(&width, &height)) {
    return;
  }
  if (PPC_LOAD_U32(kSizeTable + 5 * 8) != width ||
      PPC_LOAD_U32(kSizeTable + 5 * 8 + 4) != height) {
    PPC_STORE_U32(kSizeTable + 5 * 8, width);
    PPC_STORE_U32(kSizeTable + 5 * 8 + 4, height);
    REXLOG_INFO("[wwe13-size-probe] size table entry 5 patched to {}x{}", width, height);
  }
}

void PatchCompositeTexelConstantsForCustom() {
  uint8_t* const base = g_guest_base;
  uint32_t width = 0;
  uint32_t height = 0;
  if (!base || !CustomSize(&width, &height)) {
    return;
  }

  constexpr uint32_t kCompositeConstantPhysical = 0x0F819000;
  constexpr uint32_t kPpcPhysicalAlias = 0xA0000000;
  constexpr uint32_t kTexelPairOffset = 12 * sizeof(uint32_t);  // c507.xy, block starts at c504
  constexpr uint32_t kOldWidth = 1280;
  constexpr uint32_t kOldHeight = 720;
  const uint32_t old_inv_width = std::bit_cast<uint32_t>(1.0f / static_cast<float>(kOldWidth));
  const uint32_t old_inv_height = std::bit_cast<uint32_t>(1.0f / static_cast<float>(kOldHeight));
  const uint32_t target_inv_width = std::bit_cast<uint32_t>(1.0f / static_cast<float>(width));
  const uint32_t target_inv_height = std::bit_cast<uint32_t>(1.0f / static_cast<float>(height));
  const uint32_t physical_address = kCompositeConstantPhysical + kTexelPairOffset;
  const uint32_t address = kPpcPhysicalAlias + physical_address;
  const uint32_t current_width = PPC_LOAD_U32(address);
  const uint32_t current_height = PPC_LOAD_U32(address + 4);
  if (current_width == target_inv_width && current_height == target_inv_height) {
    return;
  }
  if (current_width != old_inv_width || current_height != old_inv_height) {
    static bool mismatch_logged = false;
    if (!mismatch_logged) {
      mismatch_logged = true;
      REXLOG_WARN("[wwe13-size-probe] composite c507 alias patch skipped: physical {} direct {}/{} alias {} has {}/{}; expected old {}/{} or target {}/{}",
                  Hex32(physical_address), Hex32(PPC_LOAD_U32(physical_address)),
                  Hex32(PPC_LOAD_U32(physical_address + 4)), Hex32(address),
                  Hex32(current_width), Hex32(current_height), Hex32(old_inv_width),
                  Hex32(old_inv_height), Hex32(target_inv_width), Hex32(target_inv_height));
    }
    return;
  }

  PPC_STORE_U32(address, target_inv_width);
  PPC_STORE_U32(address + 4, target_inv_height);
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("[wwe13-size-probe] composite c507 alias {} patched from 1/1280,1/720 to 1/{},1/{}",
                Hex32(address), width, height);
  }
}

// --- Scene-size index probe hooks (probe 4) --------------------------------

bool SizeProbeEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWE13_SIZE_PROBE");
    const bool requested = value != nullptr && std::string(value) == "1";
    if (requested) {
      REXLOG_INFO("[wwe13-size-probe] enabled (observational only)");
    }
    return requested;
  }();
  return enabled;
}

extern "C" PPC_FUNC(__imp__sub_82A97178);
extern "C" PPC_FUNC(sub_82A97178) {
  g_guest_base = base;
  if (SizeProbeEnabled()) {
    static std::atomic_flag entry_logged = ATOMIC_FLAG_INIT;
    if (!entry_logged.test_and_set(std::memory_order_relaxed)) {
      const uint32_t root = RootPtr();
      const uint32_t index = root != 0 ? PPC_LOAD_U32(root + kRootSizeIndex) : 0xFFFFFFFFu;
      REXLOG_INFO("[wwe13-size-probe] sub_82A97178 entry {} size_table[index]={}",
                  FormatRootState(), root != 0 ? FormatSizeTableEntry(index) : "n/a");
    }
  }
  __imp__sub_82A97178(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_82A978C0);
extern "C" PPC_FUNC(sub_82A978C0) {
  g_guest_base = base;
  PatchSizeTableForCustom();
  if (SizeProbeEnabled()) {
    RecordCall(3);
    const uint32_t root = ctx.r3.u32;
    const std::string before = FormatRootState();
    __imp__sub_82A978C0(ctx, base);
    PatchCompositeTexelConstantsForCustom();
    const std::string after = FormatRootState();
    std::ostringstream key;
    key << "relayout:" << before << " -> " << after;
    if (FirstTuple(g_tile_tuple_keys, key.str())) {
      REXLOG_INFO("[wwe13-size-probe] sub_82A978C0 root={} {} -> {} size_table[+288]={}",
                  Hex32(root), before, after,
                  root != 0 ? FormatSizeTableEntry(PPC_LOAD_U32(root + kRootSizeIndex)) : "n/a");
    }
    return;
  }
  __imp__sub_82A978C0(ctx, base);
  PatchCompositeTexelConstantsForCustom();
}

// r4=record {index at +0, w at +4, h at +8, flag at +12}, r5=dest (root+100).
extern "C" PPC_FUNC(__imp__sub_82A96D80);
extern "C" PPC_FUNC(sub_82A96D80) {
  g_guest_base = base;
  if (SizeProbeEnabled()) {
    RecordCall(6);
    const uint32_t record = ctx.r3.u32;
    const uint32_t dest = ctx.r4.u32;
    const uint32_t index = record != 0 ? PPC_LOAD_U32(record + 0) : 0;
    const uint32_t w = record != 0 ? PPC_LOAD_U32(record + 4) : 0;
    const uint32_t h = record != 0 ? PPC_LOAD_U32(record + 8) : 0;
    const uint32_t flag = record != 0 ? PPC_LOAD_U32(record + 12) : 0;
    std::ostringstream key;
    key << "sizerecord:" << index << ':' << w << ':' << h << ':' << flag;
    if (FirstTuple(g_tile_tuple_keys, key.str())) {
      REXLOG_INFO("[wwe13-size-probe] sub_82A96D80 record={} index={} w={} h={} flag={} dest={} {}",
                  Hex32(record), index, w, h, flag, Hex32(dest), FormatRootState());
    }
    __imp__sub_82A96D80(ctx, base);
    if (dest != 0) {
      std::ostringstream post;
      post << "sizerecord-post:" << index << ':' << w << ':' << h;
      if (FirstTuple(g_tile_tuple_keys, post.str())) {
        REXLOG_INFO("[wwe13-size-probe] sub_82A96D80 dest[0..6]={} {} {} {} {} {} {}",
                    PPC_LOAD_U32(dest + 0), PPC_LOAD_U32(dest + 4), PPC_LOAD_U32(dest + 8),
                    PPC_LOAD_U32(dest + 12), PPC_LOAD_U32(dest + 16), PPC_LOAD_U32(dest + 20),
                    PPC_LOAD_U32(dest + 24));
      }
    }
    return;
  }
  __imp__sub_82A96D80(ctx, base);
}

// r3=child, r4=key0 (root+300), r5=key1 (root+304), r6=root; the template
// selector r7 inside is [root+324] (see sub_82B0AD40 :68338-68354).
extern "C" PPC_FUNC(__imp__sub_82B0AD40);
extern "C" PPC_FUNC(sub_82B0AD40) {
  g_guest_base = base;
  if (SizeProbeEnabled()) {
    RecordCall(5);
    const uint32_t root = ctx.r6.u32;
    std::ostringstream key;
    key << "ad40:" << ctx.r4.u32 << ':' << ctx.r5.u32 << ':'
        << (root != 0 ? PPC_LOAD_U32(root + kRootKey2) : 0) << ':'
        << (root != 0 ? PPC_LOAD_U32(root + kRootSelector) : 0);
    if (FirstTuple(g_tile_tuple_keys, key.str())) {
      REXLOG_INFO("[wwe13-size-probe] sub_82B0AD40 key0={} key1={} key2(+320)={} selector(+324)={} child={} {}",
                  ctx.r4.u32, ctx.r5.u32, root != 0 ? PPC_LOAD_U32(root + kRootKey2) : 0,
                  root != 0 ? PPC_LOAD_U32(root + kRootSelector) : 0, Hex32(ctx.r3.u32),
                  FormatRootState());
    }
  }
  __imp__sub_82B0AD40(ctx, base);
}

// r4=key0 (w), r5=key1 (h), r6=key2, r7=table selector (0 = vertical-strip
// table 0x820679D8, non-zero = horizontal-band table 0x820672D8), r8=out.
extern "C" PPC_FUNC(__imp__sub_82B0A730);
extern "C" PPC_FUNC(sub_82B0A730) {
  g_guest_base = base;
  uint32_t custom_width = 0;
  uint32_t custom_height = 0;
  if (CustomSize(&custom_width, &custom_height) && ctx.r4.u32 == custom_width &&
      ctx.r5.u32 == custom_height && ctx.r8.u32 != 0) {
    const uint32_t out = ctx.r8.u32;
    uint32_t strip = [] {
      const char* value = std::getenv("WWE13_SCENE_STRIP");
      const unsigned configured =
          value ? static_cast<unsigned>(std::strtoul(value, nullptr, 10)) : 0;
      return configured >= 80 && configured <= 600 ? configured : 240u;
    }();
    if ((custom_width + strip - 1) / strip > 15) {
      const uint32_t min_strip = (custom_width + 14) / 15;
      strip = ((min_strip + 39) / 40) * 40;
    }
    uint32_t count = 0;
    for (uint32_t x = 0; x < custom_width && count < 15; x += strip, ++count) {
      const uint32_t rect = out + 16 + count * 16;
      PPC_STORE_U32(rect + 0, x);
      PPC_STORE_U32(rect + 4, 0);
      PPC_STORE_U32(rect + 8, std::min(x + strip, custom_width));
      PPC_STORE_U32(rect + 12, custom_height);
    }
    PPC_STORE_U32(out + 0, custom_width);
    PPC_STORE_U32(out + 4, custom_height);
    PPC_STORE_U32(out + 8, ctx.r6.u32);
    PPC_STORE_U32(out + 12, count);
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("[wwe13-size-probe] custom template {}x{} key2={} -> {} strips of <= {} px",
                  custom_width, custom_height, ctx.r6.u32, count, strip);
    }
    return;
  }
  if (SizeProbeEnabled()) {
    RecordCall(5);
    std::ostringstream key;
    key << "a730:" << ctx.r4.u32 << ':' << ctx.r5.u32 << ':' << ctx.r6.u32 << ':' << ctx.r7.u32;
    if (FirstTuple(g_tile_tuple_keys, key.str())) {
      REXLOG_INFO("[wwe13-size-probe] sub_82B0A730 key0={} key1={} key2={} selector={} out={}",
                  ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, Hex32(ctx.r8.u32));
    }
  }
  __imp__sub_82B0A730(ctx, base);
}

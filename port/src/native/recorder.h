#pragma once

#include <cstdint>
#include <rex/ppc/context.h>

// WWE '13 native-renderer recorder (cvar wwe13_record_frames, default 0 = off), for offline replay.
//
// <dir>/recording.w13r = FileHeader, then chunks (ChunkHeader + payload, deflate unless stored_bytes ==
// raw_bytes). The decompressed payloads, concatenated, are a stream of Records (RecordHeader + body):
//   kBasePage / kFramePage  {u32 space (0 virtual, 1 physical), u32 page offset in that space, 4096 bytes}
//       kBasePage: full snapshot of readable game memory, taken at the first recorded frame.
//       kFramePage: a page that changed since the previous scan. A scan runs at the first hook of
//       every frame, after the game's job threads have written that frame's commands and data.
//   kDevice    {u32 device address, then runs {u16 offset, u16 length, bytes...}}: changed bytes of the
//              D3D device block (register shadows, constants) since the previous hook.
//   kCall      EventRecord: one hooked D3D call, in order.
//   kFrameEnd  {u32 frame index}
// Replay: restore memory from page records, the device block from kDevice before each call, then run
// the renderer side of the hook with the recorded registers.
namespace svr::recorder {
constexpr char kMagic[8] = {'W', '1', '3', 'R', 'E', 'C', '2', 0};
constexpr uint32_t kVersion = 2;
constexpr uint32_t kPageBytes = 4096;
constexpr uint32_t kDeviceBytes = 0x5FB0;  // sizeof(bd::gpu::D3DDevice), reblue/gpu/d3d.h

struct FileHeader {
  char magic[8];
  uint32_t version;
  uint32_t page_bytes;
};
struct ChunkHeader {
  uint32_t raw_bytes;
  uint32_t stored_bytes;
};
enum RecordType : uint32_t { kBasePage = 1, kFramePage = 2, kDevice = 3, kCall = 4, kFrameEnd = 5 };
struct RecordHeader {
  uint32_t type;
  uint32_t bytes;  // body size
};
struct EventRecord {
  uint32_t hook_id;  // HookId(name of the hooked subroutine, e.g. "D3DDevice_DrawIndexedVertices")
  uint32_t flags;    // bit 0: return_value is meaningful (SVR_HOOK_RESULT)
  uint64_t timestamp_ns;
  uint64_t return_value;
  uint64_t registers[21];  // r1-r13 followed by f1-f8, raw PPCContext register bits
};

// FNV-1a of the hooked subroutine name: stable across builds and backends.
uint32_t HookId(const char* name);
void RecordHook(const char* name, const PPCContext& args, uint32_t return_value, bool has_return);
}  // namespace svr::recorder

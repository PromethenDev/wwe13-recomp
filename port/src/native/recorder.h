#pragma once

#include <cstdint>
#include <rex/ppc/context.h>

// R1 stream format (little-endian): file starts with FileHeader; each EventRecord is fixed-size.
// R2 extends this envelope with zstd chunks: chunk header {type, raw_bytes, stored_bytes, crc32},
// followed by call events and page records {guest_page, 4096 bytes}. Event registers are copied from
// the PPCContext at the hook boundary; return_value is meaningful for RESULT hooks.
namespace svr::recorder {
struct FileHeader {
  char magic[8] = {'W','1','3','R','1',0,0,0};
  uint32_t version = 1;
  uint32_t header_bytes = 24;
  uint32_t page_bytes = 4096;
  uint32_t reserved = 0;
};
struct EventRecord {
  uint32_t hook_id;
  uint32_t flags;
  uint64_t timestamp_ns;
  uint64_t return_value;
  uint64_t registers[21];  // r1-r13 followed by f1-f8, raw PPCContext register bits
};
void RecordHook(const char* name, const PPCContext& args, uint32_t return_value, bool has_return);
}  // namespace svr::recorder

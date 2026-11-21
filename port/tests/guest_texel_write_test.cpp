// Host test: guest_texel_write.h gives the same bytes as the SDK functions it replaces.
// build: g++ -O2 -std=c++17 -I../src/reblue tests/guest_texel_write_test.cpp -o /tmp/gtw && /tmp/gtw
#include "gpu/guest_texel_write.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ref {
// Verbatim from rexglue-sdk src/graphics/pipeline/texture/util.cpp (rex::align inlined).
int32_t GetTiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2) {
  pitch = (pitch + 32 - 1) / 32 * 32;
  int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}
// rexglue-sdk src/core/memory.cpp (x86: SIMD body + scalar residual; the residual alone is
// what runs for counts below 8 / 4).
void copy_and_swap_16_unaligned(void *d, const void *s, size_t count) {
  auto *dst = static_cast<uint16_t *>(d);
  auto *src = static_cast<const uint16_t *>(s);
  for (size_t i = 0; i < count; ++i) {
    uint16_t v;
    std::memcpy(&v, src + i, 2);
    v = uint16_t((v >> 8) | (v << 8));
    std::memcpy(dst + i, &v, 2);
  }
}
void copy_and_swap_32_unaligned(void *d, const void *s, size_t count) {
  auto *dst = static_cast<uint32_t *>(d);
  auto *src = static_cast<const uint32_t *>(s);
  for (size_t i = 0; i < count; ++i) {
    uint32_t v;
    std::memcpy(&v, src + i, 4);
    v = __builtin_bswap32(v);
    std::memcpy(dst + i, &v, 4);
  }
}
// texture_conversion::CopySwapBlock without the k16in32 case.
void CopySwapBlock(uint32_t endian, void *out, const void *in, size_t length) {
  switch (endian) {
    case 1: copy_and_swap_16_unaligned(out, in, length / 2); break;
    case 2: copy_and_swap_32_unaligned(out, in, length / 4); break;
    default: std::memcpy(out, in, length); break;
  }
}
}  // namespace ref

int main() {
  using bd::gpu::texel_write::CopySwapSmall;
  using bd::gpu::texel_write::TiledOffset2D;
  long checks = 0, bad = 0;
  const uint32_t pitches[] = {32, 64, 96, 128, 256, 320, 512, 736, 1024, 1280, 2048, 33, 100};
  for (uint32_t pitch : pitches)
    for (uint32_t lg = 0; lg <= 4; ++lg)
      for (int32_t y = 0; y < 1300; y += (y < 64 ? 1 : 7))
        for (int32_t x = 0; x < int32_t(pitch); ++x) {
          ++checks;
          if (TiledOffset2D(x, y, pitch, lg) != ref::GetTiledOffset2D(x, y, pitch, lg) && bad++ < 5)
            std::printf("tile mismatch pitch %u lg %u x %d y %d\n", pitch, lg, x, y);
        }
  std::srand(1234);
  for (uint32_t endian = 0; endian <= 2; ++endian)
    for (uint32_t len : {2u, 4u})
      for (int i = 0; i < 100000; ++i) {
        uint8_t in[4], a[8], b[8];
        for (auto &v : in) v = uint8_t(std::rand());
        for (int k = 0; k < 8; ++k) a[k] = b[k] = uint8_t(std::rand());
        ++checks;
        const bool ok = CopySwapSmall(endian, a, in, len);
        ref::CopySwapBlock(endian, b, in, len);
        if ((!ok || std::memcmp(a, b, 8) != 0) && bad++ < 5)
          std::printf("swap mismatch endian %u len %u\n", endian, len);
      }
  // k16in32 must be refused (caller keeps the SDK call).
  uint8_t t[4] = {}, o[4] = {};
  ++checks;
  if (CopySwapSmall(3, o, t, 4) || CopySwapSmall(3, o, t, 2)) { ++bad; std::printf("k16in32 not refused\n"); }
  std::printf("%ld checks, %ld mismatches: %s\n", checks, bad, bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}

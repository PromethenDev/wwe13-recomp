// Standalone semantic test for the ReXGlue `mulchw` builder lowering.
//
// PowerPC 405/440 "multiply cross halfword" (mulchw rD,rA,rB):
//     rD = (int16)rA * rB
// The ReXGlue builder emits exactly:
//     rD.s64 = int32_t(int64_t(int16_t(rA.s32)) * int64_t(rB.s32));
// The architectural result is the low 32 bits of the product, sign-extended into
// the framework's 64-bit register field (the same signed-32-bit idiom as
// extsw/lwa). This test asserts the FULL modeled 64-bit register value, not just
// its low word, and includes truncation-sensitive inputs where storing the
// untruncated product would differ. Record-form (mulchw.) compares rD.s32.
//
// Build/run:
//     clang++ -std=c++20 tests/test_mulchw.cpp -o /tmp/test_mulchw && /tmp/test_mulchw

#include <cstdint>
#include <cstdio>

namespace {

// Mirrors the expression emitted by build_mulchw.
int64_t generated_mulchw(int32_t a, int32_t b) {
  return int32_t(int64_t(int16_t(a)) * int64_t(b));
}

// The pre-fix emission: full 47-bit product, no truncation.
int64_t untruncated_mulchw(int32_t a, int32_t b) {
  return int64_t(int16_t(a)) * int64_t(b);
}

int failures = 0;

void check_case(const char* name, int32_t a, int32_t b, int64_t expected,
                bool truncation_sensitive = false) {
  const int64_t actual = generated_mulchw(a, b);
  bool ok = actual == expected;
  // For truncation-sensitive cases, prove the test discriminates the old behavior.
  if (truncation_sensitive && untruncated_mulchw(a, b) == expected) {
    ok = false;
  }
  if (!ok) {
    ++failures;
  }
  std::printf("%s %-28s a=0x%08X b=0x%08X actual=0x%016llX expected=0x%016llX\n",
              ok ? "PASS" : "FAIL", name, static_cast<uint32_t>(a), static_cast<uint32_t>(b),
              static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
}

}  // namespace

int main() {
  // Hardcoded golden values (independently computed with Python from
  // int16(a) * b, then low-32 truncation and sign-extension).
  check_case("INT16_MIN * -1", 0x00008000, -1, 0x0000000000008000LL);              // 32768
  check_case("INT16_MIN * INT16_MIN", 0x00008000, -32768, 0x0000000040000000LL);   // 2^30
  check_case("0x7FFF * -1", 0x00007FFF, -1, 0xFFFFFFFFFFFF8001LL);                 // -32767
  check_case("negative * negative", -12345, -67890, 0x0000000031F46C22LL);         // 838102050
  check_case("mixed signs (neg*pos)", -12345, 67890, 0xFFFFFFFFCE0B93DELL);        // -838102050
  check_case("mixed signs (pos*neg)", 12345, -67890, 0xFFFFFFFFCE0B93DELL);        // -838102050
  check_case("only low 16 bits of rA used", 0x12348000, 3, 0xFFFFFFFFFFFE8000LL);  // -98304

  // Truncation-sensitive: the product needs more than 32 bits, so the old
  // untruncated .s64 store would leave product bits above bit 31.
  //   0x7FFF * INT32_MAX   full = 0x00003FFF7FFF8001 -> low32 = 0x7FFF8001
  //   INT16_MIN * INT32_MIN full = 0x0000400000000000 -> low32 = 0x00000000
  //   0x7FFF * INT32_MIN   full = 0xFFFFC00080000000 -> low32 = 0x80000000
  check_case("0x7FFF * INT32_MAX", 0x00007FFF, INT32_MAX, 0x000000007FFF8001LL, true);
  check_case("INT16_MIN * INT32_MIN", 0x00008000, INT32_MIN, 0x0000000000000000LL, true);
  check_case("0x7FFF * INT32_MIN", 0x00007FFF, INT32_MIN, 0xFFFFFFFF80000000LL, true);

  // Cross-check a grid of inputs against an independent __int128 computation:
  // exact product -> low 32 bits -> sign-extend into the modeled 64-bit value.
  const int32_t values[] = {INT32_MIN, INT32_MIN + 1, -70000, -32768, -12345, -1,
                            0,          1,            0x7FFF, 0x10000, 67890, 0x12348000,
                            INT32_MAX};
  int grid_cases = 0;
  int grid_truncation_sensitive = 0;
  for (int32_t a : values) {
    for (int32_t b : values) {
      const int64_t actual = generated_mulchw(a, b);
      const __int128 exact = static_cast<__int128>(static_cast<int16_t>(a)) * b;
      const int32_t low32 = static_cast<int32_t>(static_cast<uint32_t>(exact));
      const int64_t expected = low32;  // sign-extend
      ++grid_cases;
      if (untruncated_mulchw(a, b) != expected) {
        ++grid_truncation_sensitive;
      }
      if (actual != expected) {
        std::printf("FAIL grid a=0x%08X b=0x%08X got=0x%016llX expected=0x%016llX\n",
                    static_cast<uint32_t>(a), static_cast<uint32_t>(b),
                    static_cast<unsigned long long>(actual),
                    static_cast<unsigned long long>(expected));
        ++failures;
      }
    }
  }
  std::printf("grid: %d cases checked, %d truncation-sensitive\n", grid_cases,
              grid_truncation_sensitive);

  if (failures == 0) {
    std::printf("All mulchw semantic checks passed.\n");
    return 0;
  }
  std::printf("%d mulchw semantic check(s) failed.\n", failures);
  return 1;
}

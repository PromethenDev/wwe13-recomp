// Differential-oracle gate (diagnostic, env-gated, off by default).
//
// A generated-code wrapper calls wwe13_oracle_gate() at entry (phase 0) and exit (phase 1) of a
// guest function. For the selected calls it writes the full guest register state to
// $WWE13_ORACLE_DIR/state<phase>.bin and blocks until an external tool
// (tools/ppc-oracle.py) removes that file. While blocked, the tool reads guest memory through
// /proc/<pid>/mem (this process allows it via PR_SET_PTRACER) and runs an independent PPC
// interpreter from the entry state, then compares against the recompiled result at exit.
//
//   WWE13_ORACLE_DIR   output directory (required to enable)
//   WWE13_ORACLE_FN    guest function address (hex) to capture
//   WWE13_ORACLE_SKIP  calls to let through before capturing (default 0)
//   WWE13_ORACLE_COUNT calls to capture (default 1)

#include <rex/ppc/context.h>

#if defined(__linux__)
#include <sys/prctl.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

struct Config {
  const char* dir = nullptr;
  uint32_t fn = 0;
  uint64_t skip = 0;
  uint64_t count = 1;
};

const Config& config() {
  static const Config c = [] {
    Config c;
    c.dir = std::getenv("WWE13_ORACLE_DIR");
    if (const char* s = std::getenv("WWE13_ORACLE_FN")) c.fn = std::strtoul(s, nullptr, 16);
    if (const char* s = std::getenv("WWE13_ORACLE_SKIP")) c.skip = std::strtoull(s, nullptr, 10);
    if (const char* s = std::getenv("WWE13_ORACLE_COUNT")) c.count = std::strtoull(s, nullptr, 10);
    if (c.dir) prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    return c;
  }();
  return c;
}

void put(FILE* f, const void* p, size_t n) { std::fwrite(p, 1, n, f); }

void put_vr(FILE* f, const PPCVRegister& v) {
  // Host storage is element-reversed; write PPC (big-endian element) order.
  uint8_t b[16];
  for (int i = 0; i < 16; ++i) b[i] = v.u8[15 - i];
  put(f, b, 16);
}

void write_state(PPCContext& ctx, uint8_t* base, uint32_t fn, int phase, uint64_t call) {
  const Config& c = config();
  std::string tmp = std::string(c.dir) + "/state" + std::to_string(phase) + ".tmp";
  std::string fin = std::string(c.dir) + "/state" + std::to_string(phase) + ".bin";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return;
  uint32_t magic = 0x4F52434C, pid = uint32_t(getpid());
  uint64_t base_addr = reinterpret_cast<uint64_t>(base);
  put(f, &magic, 4);
  put(f, &fn, 4);
  put(f, &phase, 4);
  put(f, &pid, 4);
  put(f, &base_addr, 8);
  put(f, &call, 8);
#define R(n) put(f, &ctx.r##n.u64, 8);
  R(0) R(1) R(2) R(3) R(4) R(5) R(6) R(7) R(8) R(9) R(10) R(11) R(12) R(13) R(14) R(15)
  R(16) R(17) R(18) R(19) R(20) R(21) R(22) R(23) R(24) R(25) R(26) R(27) R(28) R(29) R(30) R(31)
#undef R
#define F(n) put(f, &ctx.f##n.u64, 8);
  F(0) F(1) F(2) F(3) F(4) F(5) F(6) F(7) F(8) F(9) F(10) F(11) F(12) F(13) F(14) F(15)
  F(16) F(17) F(18) F(19) F(20) F(21) F(22) F(23) F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31)
#undef F
#define V(n) put_vr(f, ctx.v##n);
#define V8(a) V(a##0) V(a##1) V(a##2) V(a##3) V(a##4) V(a##5) V(a##6) V(a##7) V(a##8) V(a##9)
  V(0) V(1) V(2) V(3) V(4) V(5) V(6) V(7) V(8) V(9)
  V8(1) V8(2) V8(3) V8(4) V8(5) V8(6) V8(7) V8(8) V8(9) V8(10) V8(11)
  V(120) V(121) V(122) V(123) V(124) V(125) V(126) V(127)
#undef V8
#undef V
  uint32_t cr = (ctx.cr0.raw() << 28) | (ctx.cr1.raw() << 24) | (ctx.cr2.raw() << 20) |
                (ctx.cr3.raw() << 16) | (ctx.cr4.raw() << 12) | (ctx.cr5.raw() << 8) |
                (ctx.cr6.raw() << 4) | ctx.cr7.raw();
  uint32_t xer = (uint32_t(ctx.xer.so) << 31) | (uint32_t(ctx.xer.ov) << 30) |
                 (uint32_t(ctx.xer.ca) << 29);
  uint64_t lr = ctx.lr, ctr = ctx.ctr.u64;
  put(f, &cr, 4);
  put(f, &xer, 4);
  put(f, &lr, 8);
  put(f, &ctr, 8);
  std::fclose(f);
  std::rename(tmp.c_str(), fin.c_str());
  // Block until the oracle consumed the state (it deletes the file).
  while (access(fin.c_str(), F_OK) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

}  // namespace

extern "C" void wwe13_oracle_gate(PPCContext& ctx, uint8_t* base, uint32_t fn, int phase) {
  const Config& c = config();
  if (!c.dir || fn != c.fn) return;
  static std::atomic<uint64_t> calls{0};
  static thread_local uint64_t current = UINT64_MAX;
  if (phase == 0) {
    uint64_t n = calls.fetch_add(1);
    current = (n >= c.skip && n < c.skip + c.count) ? n : UINT64_MAX;
  }
  if (current == UINT64_MAX) return;
  write_state(ctx, base, fn, phase, current);
}
#else
extern "C" void wwe13_oracle_gate(PPCContext&, uint8_t*, uint32_t, int) {}
#endif

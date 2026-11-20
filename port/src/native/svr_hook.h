// SvR 2008: native renderer hooks that observe the game's D3D calls.
//
// SvR's D3D library stays in charge of every D3D object and all device state: it runs unmodified
// on the null GPU, exactly as in the playable build. A hook runs the game's own function and hands
// the same arguments to the renderer, which mirrors what it sees on the host GPU.
//
// re:Blue replaces these functions outright, which suits Blue Dragon (its resources live in
// re:Blue's own heap). In SvR the objects are the game's: a shader made by the renderer and later
// freed by the game's own Release corrupts the guest heap, and skipped bookkeeping (fences,
// reference counts, device shadows) desynchronises the D3D library.

#pragma once

#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <rex/ppc/function.h>

#include "recorder.h"

namespace svr {

// WWE 2K15 bring-up switch (cvar svr_hooks_off, native/wwe13_native_stubs.cpp): a comma list of
// hook names (D3DDevice_Swap, ...) whose renderer side is skipped; the game's own D3D function
// always runs. Read once per hook.
bool HookEnabled(const char *name);

// The argument registers of a guest call: r3-r10 (integers and pointers), f1-f8 (floats), and r1
// for arguments passed on the stack (still in the caller's frame after the call returns).
inline void CopyArgs(PPCContext &dst, const PPCContext &src) {
  dst.r1 = src.r1;
  dst.r2 = src.r2;
  dst.r13 = src.r13;
  dst.r3 = src.r3;
  dst.r4 = src.r4;
  dst.r5 = src.r5;
  dst.r6 = src.r6;
  dst.r7 = src.r7;
  dst.r8 = src.r8;
  dst.r9 = src.r9;
  dst.r10 = src.r10;
  dst.f1 = src.f1;
  dst.f2 = src.f2;
  dst.f3 = src.f3;
  dst.f4 = src.f4;
  dst.f5 = src.f5;
  dst.f6 = src.f6;
  dst.f7 = src.f7;
  dst.f8 = src.f8;
}

}  // namespace svr

// The game's function, then the renderer's (with the same arguments; its return value is dropped,
// the game's stays in r3).
#define SVR_HOOK_AFTER(subroutine, function)                     \
  REX_EXTERN(__imp__##subroutine);                               \
  extern "C" REX_FUNC(subroutine) {                              \
    PPCContext svr_args;                                         \
    ::svr::CopyArgs(svr_args, ctx);                              \
    __imp__##subroutine(ctx, base);                              \
    ::svr::recorder::RecordHook(#subroutine, svr_args, ctx.r3.u32, false); \
    static const bool svr_on = ::svr::HookEnabled(#subroutine);  \
    if (svr_on)                                                  \
      rex::ppc::HostToGuestFunction<function>(svr_args, base);   \
  }

// The renderer's function (reading the state the call is about to consume), then the game's.
#define SVR_HOOK_BEFORE(subroutine, function)                    \
  REX_EXTERN(__imp__##subroutine);                               \
  extern "C" REX_FUNC(subroutine) {                              \
    PPCContext svr_args;                                         \
    ::svr::CopyArgs(svr_args, ctx);                              \
    ::svr::recorder::RecordHook(#subroutine, svr_args, ctx.r3.u32, false); \
    static const bool svr_on = ::svr::HookEnabled(#subroutine);  \
    if (svr_on)                                                  \
      rex::ppc::HostToGuestFunction<function>(svr_args, base);   \
    __imp__##subroutine(ctx, base);                              \
  }

// The game's function, then the renderer's with the arguments and the game's return value (r3).
#define SVR_HOOK_RESULT(subroutine, function)                    \
  REX_EXTERN(__imp__##subroutine);                               \
  extern "C" REX_FUNC(subroutine) {                              \
    PPCContext svr_args;                                         \
    ::svr::CopyArgs(svr_args, ctx);                              \
    __imp__##subroutine(ctx, base);                              \
    ::svr::recorder::RecordHook(#subroutine, svr_args, ctx.r3.u32, true); \
    static const bool svr_on = ::svr::HookEnabled(#subroutine);  \
    if (svr_on)                                                  \
      function(svr_args, ctx.r3.u32, base);                      \
  }

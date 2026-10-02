// GPU->CPU coherency for render-to-texture data the game's CPU code reads (cycle 213).
//
// WWE '13 composites some wrestler textures on the GPU (e.g. retro Shawn Michaels in Attitude Era
// chapter 1): it renders 2048x1024 layers, resolves them into guest memory, then untiles/converts
// them on the CPU with XGUntileSurface (sub_82BE19E8). The recompiled GPU keeps resolve results in
// its own buffer, so without a readback the CPU untiles stale memory and the model renders black.
// Reading back every resolve (readback_resolve=full) fixes it but halves the in-ring frame rate;
// this hook reads back only the source surface of each untile, which happens at load time.
//
// Kill switch: WWE13_UNTILE_COHERENCY=0.

#include <cstdint>
#include <cstdlib>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/ppc/memory.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

// XGUntileSurface(dest=r3, row_pitch=r4, point=r5, source=r6, width=r7, height=r8, rect=r9,
// texel_pitch=r10). The tiled source spans align32(width) * align32(height) * texel_pitch bytes,
// the size the function itself uses for its in-place copy.
void wwe13_untile_coherency(PPCRegister& source, PPCRegister& width, PPCRegister& height,
                            PPCRegister& texel_pitch) {
  static const bool enabled = [] {
    const char* v = std::getenv("WWE13_UNTILE_COHERENCY");
    return !(v && v[0] == '0');
  }();
  if (!enabled) {
    return;
  }
  uint64_t size = uint64_t((width.u32 + 31) & ~31u) * ((height.u32 + 31) & ~31u) * texel_pitch.u32;
  if (!size || size > 64u * 1024 * 1024) {
    return;
  }
  auto* kernel_state = REX_KERNEL_STATE();
  auto* graphics = kernel_state
                       ? static_cast<rex::graphics::GraphicsSystem*>(
                             kernel_state->emulator()->graphics_system())
                       : nullptr;
  if (graphics && graphics->command_processor()) {
    graphics->command_processor()->MakeGpuWritesVisibleToCpu(source.u32, uint32_t(size));
  }
}

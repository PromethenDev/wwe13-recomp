# Performance presets (native renderers, SDK 0.10 build) - 2026-11-21

The launcher's PERFORMANCE PRESET row sets two things at once: the internal resolution (`--svr_render_scale`) and
the frame-rate mode. A preset is not stored; it shows as chosen while the resolution and frame rate match it.
Changing either one by hand shows "Custom".

| Preset | Resolution | Frame rate | Aimed at |
|---|---|---|---|
| Steady 30 | 720p | 30 Locked (`--wwe13_lock_30`) | weakest PCs, laptops on battery: a steady 30 instead of an uneven 60 |
| Light | 540p | Classic | weak integrated graphics |
| Balanced | 720p | Classic | Intel UHD 630, AMD Radeon 780M class (the default on integrated graphics) |
| Sharp | 1080p | Classic | entry graphics cards (Intel Arc A380, GTX 1650 class) |
| Best | 1440p | Classic | mid-range graphics cards and up |

Classic = the game's own pacing (60 in matches, 30 in entrances and cutscenes). 30 Locked: the game's first
entrance/cutscene switch to 30 runs normally, later switches back to 60 are skipped (port/src/frame_rate_lock.cpp,
ported from the older build's WWE13_LOCK_30). 60 Everywhere (`--wwe13_keep_60`) skips the switch to 30.

The native renderer draws without MSAA (the anti-aliasing panel has no effect on it), so resolution is the main
GPU cost knob. On the Intel UHD 630 PC match play is CPU-bound at 720p (quarter pixel work = +3 fps, run sp0), so
going below 720p there buys little; the presets matter most for GPU-bound cards at 1080p/1440p.

## Measurements (media server: i5-10400, Intel UHD 630 and Intel Arc A380; entrance route + match)
To be filled from the 2026-11-21 runs.

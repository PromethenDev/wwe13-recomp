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
GPU cost knob for Vulkan and D3D12. On the Intel UHD 630 Vulkan/D3D12 now hold 60 at 720p with ~0.7 ms GPU margin
(Balanced); the presets matter most for GPU-bound cards at 1080p/1440p and weaker integrated graphics (Light).

## Measurements (2026-11-21, media server: i5-10400, Intel UHD 630 and Intel Arc A380; build 8d1ce3f/f3946e3)
Match play, fps from 10 s segments / render-loop means; GPU busy = PresentMon median in the match window.

| GPU | Renderer | Resolution | Match | GPU busy | Fits preset |
|---|---|---|---|---|---|
| UHD 630 | Vulkan | 720p | 58.5-60.0 fps (loop 16.7 ms) | 16.0 ms | Balanced |
| UHD 630 | D3D12 | 720p | 58.8-59.9 fps (loop 16.7 ms) | - | Balanced |
| UHD 630 | D3D11 | 720p | 52.6-58.9 fps (loop 17-20 ms) | 17.7 ms | Steady 30 for a locked rate |
| UHD 630 | D3D11 | 540p | ~57-58 fps (demo matches, loop 17.0-17.5 ms) | 15.2 ms | (540p barely helps D3D11) |
| UHD 630 | D3D12 | 720p, 30 Locked | 33.3 ms loops everywhere after the first entrance | - | Steady 30 |
| Arc A380 | Vulkan | 1080p | 60 locked (loop 16.7 ms, p99 18) | 9.3 ms | Sharp |
| Arc A380 | Vulkan | 1440p | 60 locked in demo matches (p99 18) | 13.6 ms | Best (little margin) |

Before 2026-11-21 every renderer on the UHD 630 was capped near 57 fps by a 10 ms sleep per frame inside the SDK's
XMPGetPlaybackController (fixed game-side: port/src/xmp_controller_fast.cpp). D3D11 on the UHD 630 stays a few fps
short of 60 at any resolution: its GPU time barely follows resolution (17.7 -> 15.2 ms for 44 % fewer pixels), so its
cost is per draw, not per pixel (not investigated yet). Automatic picks Vulkan first, which holds 60.
Evidence: wwe2k14-native out/w13ab/xab-xmp-no-sleep.txt, lock30-presets.txt, presets-arc.txt.

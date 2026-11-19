# Windows test tools (test builds only)

PowerShell helpers that drive the game built in `out\run` (see `docs/BUILDING.md`). They post keys to the
game window only and capture that window only (never global input or the desktop).

- `run.ps1 -Name x [-Keep]`: start `out\run\wwe13.exe` windowed, log in `out\run\logs\x.log`.
- `bench.ps1 -Name x [-Mask 0x3] [-MatchSeconds 150] [-EnvVars @{...}]`: title -> main menu (screen-detected)
  -> Exhibition one-on-one with entrances; prints frames per second, late frames and CPU, and appends
  `bench-history.txt`. `-Mask` limits the match to some logical CPUs (weak-CPU model).
- `gamectl.ps1 -Keys L,wait2000,Down -Shot x.png`: keys (A = L, B = K, Start = Esc) and a window capture.
- `sample.ps1 [-ThreadId n]`: leaf-PC sampler of the busiest game thread, resolved with `wwe13.map`.

GPU time per frame by category: run with `-EnvVars @{WWE13_GPU_TIMING="1"}` and read the
`[rate-census-gputime]` lines.

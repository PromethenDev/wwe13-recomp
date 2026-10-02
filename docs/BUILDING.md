# Building WWE '13 Recomp from source

This guide builds the Linux x86-64 executable and the Windows x64 executable
from Linux. It does not distribute the original game or ReXGlue source. Supply
your own WWE '13 files and `tools/setup.sh` fetches and patches the ReXGlue SDK.

## Supported build hosts and requirements

The verified route is **64-bit Ubuntu 24.04** with LLVM 20. The Windows target
is cross-compiled from Linux with Clang's MSVC ABI, `lld-link`, and Microsoft's
Windows SDK/MSVC CRT files supplied by `xwin`.

Minimums and versions used for this build:

- x86-64 Ubuntu/Linux; Ubuntu 24.04 is the tested host.
- CMake 3.25 or newer; tested with 3.28.3.
- Ninja; tested with 1.13.0 (Ninja Multi-Config support is required).
- LLVM/Clang 20, including `clang`, `clang++`, `lld`, `lld-link`, `llvm-ar`,
  `llvm-ranlib`, `llvm-rc`, and `llvm-mt`; tested with 20.1.2.
- Git with recursive submodule support; tested with 2.43.0.
- Python 3.11+ for the manifest checker (`tomllib`); tested with 3.12.3.
- For Windows cross-builds: `xwin` 0.10.0 (or a compatible newer version) and
  its downloaded/splatted x86-64 Windows SDK + MSVC CRT.
- Recommended: 32 GB RAM and at least 10 GB free disk space for SDK and game
  build products. Reserve 30–120 minutes for first-time dependency/codegen/build
  work; hardware, network speed, and build parallelism affect both estimates.
  `BUILD_JOBS=8` is the default maximum; lower it on smaller machines.

Install the Linux toolchain and libraries (Ubuntu package names):

```bash
sudo apt-get update
sudo apt-get install -y wget gnupg lsb-release
wget -O /tmp/llvm.sh https://apt.llvm.org/llvm.sh
sudo bash /tmp/llvm.sh 20
sudo apt-get install -y clang-20 lld-20
sudo apt-get install -y cmake ninja-build build-essential git curl unzip autoconf \
  python3.12-venv libgtk-3-dev libx11-xcb-dev wine
```

The clean-build host was Ubuntu 24.04.4 with `build-essential` 12.10ubuntu1,
`libgtk-3-dev` 3.24.41, `libx11-dev` 1.8.7, `libxkbcommon-dev` 1.6.0,
`python3.12-venv` 3.12.3, and Wine 9.0. Package versions may advance with Ubuntu
updates; the CMake/Ninja/LLVM/Python minimums above are the supported constraints.

Install Vulkan SDK from LunarG's Ubuntu 24.04 (`noble`) package repository
(or follow [LunarG's current installation instructions](https://vulkan.lunarg.com/sdk/home)):

```bash
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc \
  | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc >/dev/null
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list \
  http://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt-get update
sudo apt-get install -y vulkan-sdk
```

ReXGlue's Linux CI installs `vulkan-sdk` as well as the packages above. Set
LLVM 20 first in `PATH` for all following SDK/build commands:

```bash
export PATH=/usr/lib/llvm-20/bin:$PATH
```

For the Windows cross-target, install Rust/Cargo if needed, then install and
populate `xwin` outside the repository:

```bash
if ! command -v cargo >/dev/null 2>&1; then
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
  source "$HOME/.cargo/env"
fi
cargo install xwin --version 0.10.0
xwin --accept-license --arch x86_64 --cache-dir "$HOME/.cache/xwin" download
xwin --accept-license --arch x86_64 --cache-dir "$HOME/.cache/xwin" unpack
xwin --accept-license --arch x86_64 --cache-dir "$HOME/.cache/xwin" splat \
  --output "$HOME/xwin"
```

The build expects `"$HOME/xwin/sdk/include/um/windows.h"`. If the SDK is in a
different location, set `XWIN_DIR` to the splat root for the Windows build.

## ReXGlue SDK (fetched and patched by `tools/setup.sh`)

This project runs on the ReXGlue recompilation SDK. The release was built with the public fork
[`HollywoodAkeem/rexglue-sdk-yukes`](https://github.com/HollywoodAkeem/rexglue-sdk-yukes) at commit
`987593c0fc6612850a2e1b247b2b03aa5281e663` plus this project's SDK changes, which are kept in
`rexglue-patches/` (one `git am` patch per change). The SDK goes **next to** this repository at
`../tools/rexglue`; all build scripts use that layout.

```bash
mkdir wwe13 && cd wwe13
git clone <this repository URL> wwe13-recomp
cd wwe13-recomp
nice -n 10 env BUILD_JOBS=8 ./tools/setup.sh
```

On the first run `tools/setup.sh` clones the fork into `../tools/rexglue`, checks out the pinned commit, applies every
patch in `rexglue-patches/`, fetches the SDK's own submodules, then configures the SDK once into
`../tools/rexglue/out/build/linux-amd64-clang20` (LLVM 20, `-march=x86-64-v3`, Vulkan, shared FFmpeg) and installs it
to `../tools/rexglue/out/install/linux-amd64`. The codegen program is
`../tools/rexglue/out/linux-amd64/RelWithDebInfo/rexglue`. Later runs reuse the existing checkout.

## Provide your own game files and generate `generated/`

This project's checked-in function and hook metadata is for WWE '13 title
update **2.0.1.0**. The following are the two executable inputs used for code
generation. The hashes below are from the files used in the clean-clone test;
they are identification references, not hashes that the scripts enforce.

| File | Required version/role | Test-file size | Test-file SHA-256 |
|---|---|---:|---|
| `default.xex` | WWE '13 executable used with TU 2.0.1.0 | 21,708,800 bytes | `c4b35f3328e3612376cf9d16d26282c164e9b79842debbaca3ef06a373fefced` |
| `default.xexp` | WWE '13 title-update patch 2.0.1.0 | 1,611,776 bytes | `41dac3acff682e88a15b982d0595170b808e70f4c05b004c2312edee191ad037` |

Copy those files from the corresponding game folder you own. Keep the XEXP
beside the XEX; ReXGlue applies the adjacent `default.xexp` while loading
`default.xex`. Do not substitute the base-game binary or mix files from different
versions. Only these two files are needed to generate and build; the remaining
retail game data is required when running the game, not when compiling it.

```bash
GAME_DIR="/path/to/your/own/WWE 13"
install -D -m 0644 "$GAME_DIR/default.xex" game/default.xex
install -D -m 0644 "$GAME_DIR/default.xexp" game/default.xexp
sha256sum game/default.xex game/default.xexp
```

Generate the recompiled C++ and the matching patched image:

```bash
nice -n 10 ./tools/codegen.sh
```

`wwe13_config.toml` is the codegen entrypoint: it reads `game/default.xex`,
writes to ignored `generated/`, and includes `config/functions.toml` and
`config/hooks.toml`. ReXGlue's normal XEX loader applies the adjacent XEXP. The
script sets `REX_DUMP_PATCHED_IMAGE` and requires the resulting
`out/tu-image.bin`, so both generated code and the integrity checker use the TU
image. The generated C++ and patched image are local derived outputs; do not
commit them or any game files.

Run the manifest/codegen consistency check (the reports are written beneath
ignored `out/`):

```bash
tools/check-manifest-integrity.py \
  --manifest config/functions.toml \
  --image out/tu-image.bin \
  --generated generated \
  --report-dir out/manifest-integrity
```

`tools/check-manifest-integrity.py` checks configured boundaries against the
patched flat image and generated bodies. To also have it fail on `[error]`
entries in codegen's output, pass `--codegen-log <path>` using the log path
printed at the end of `tools/codegen.sh`. Its defaults are repository-relative;
it no longer relies on another checkout's absolute TU-image path.

## Build Linux x86-64

After `tools/codegen.sh` has produced `generated/`:

```bash
nice -n 10 env BUILD_JOBS=8 ./tools/build-linux.sh
```

The script selects LLVM 20 from `/usr/lib/llvm-20/bin` (override with
`LLVM_BIN`), configures the Linux game target against the SDK install in
`../tools/rexglue`, and builds with at most eight jobs. Output:
`out/build/linux-amd64-relwithdebinfo/RelWithDebInfo/wwe13`.

## Build Windows x64 from Linux

After preparing `xwin` above, run the cross-build script. It builds/installs the Windows SDK before building the game; Vulkan is
enabled and D3D12 disabled for this project's cross-target.

```bash
nice -n 10 env BUILD_JOBS=8 XWIN_DIR="$HOME/xwin" \
  LLVM_BIN=/usr/lib/llvm-20/bin ./tools/build-windows.sh
```

Output: `out/build/win-amd64-cross/RelWithDebInfo/wwe13.exe`.

**A native Windows/MSVC or clang-cl build is not currently supported or
verified.** The supported Windows route is the Linux-hosted Clang/xwin cross
build above. No game launch is necessary to produce either binary.

## Troubleshooting

- **`git am` fails in `tools/setup.sh`:** `../tools/rexglue` must be a fresh clone at the pinned commit. Delete
  `../tools/rexglue` and run `tools/setup.sh` again.
- **Missing SDK submodules:** run `git -C ../tools/rexglue submodule update --init --recursive` and rerun
  `tools/setup.sh`.
- **Wrong compiler selected:** check
  `/usr/lib/llvm-20/bin/clang --version`. `tools/setup.sh` and
  `tools/build-linux.sh` use this toolchain by default; the Windows build script
  uses it unless `LLVM_BIN` is set. The SDK configure is cached; if the SDK
  build directory was configured with another compiler, remove only
  `../tools/rexglue/out/build/linux-amd64-clang20` and rerun `tools/setup.sh`. If the
  Linux game build directory has an old compiler cached, remove only
  `out/build/linux-amd64-relwithdebinfo` and rerun `tools/build-linux.sh`.
- **Codegen reports a missing XEXP or no `out/tu-image.bin`:** ensure both files
  are named exactly `game/default.xex` and `game/default.xexp`, are from TU
  2.0.1.0, and have not been mixed with the base-game image.
- **Missing generated sources:** run `tools/codegen.sh` before either build.
  `generated/` is intentionally ignored and is never shipped by Git.
- **Windows SDK not found:** rerun the `xwin` download/unpack/splat commands and
  set `XWIN_DIR` to the splat directory containing `sdk/include/um/windows.h`.
- **Out of memory:** lower `BUILD_JOBS` (for example `BUILD_JOBS=4`) on the
  `nice`/`env` build commands; the default cap is eight.

## Packaging

`tools/package-release.sh` builds the player ZIP from frozen builds, for example:

```bash
WIN_BUILD=<folder with wwe13.exe + avcodec/avutil DLLs> \
LINUX_BUILD=<folder with wwe13 + libavcodec/libavutil .so> \
OUT_DIR=out/release ARCHIVE=out/release/WWE13-Recomp.zip bash tools/package-release.sh
```

It also needs the two launcher builds (`launcher2/`, see the variables at the top of the script). The ZIP never
contains game files.

## Launcher

The launcher (`launcher2/`, SDL3 + Dear ImGui) builds with the SDK's bundled third-party sources:

```bash
cmake -S launcher2 -B out/launcher2 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/launcher2
```

Its core tests (`out/launcher2/core/*_test`) need small fixtures made from your own game files under
`out/l1/testdata`; without them the game-file checks stop early.

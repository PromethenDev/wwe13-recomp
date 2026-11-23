#!/usr/bin/env bash
# Windows release bundle of the ReXGlue SDK 0.10 build (port/): the native renderers (wwe13_native.exe Vulkan,
# wwe13_d3d12.exe, wwe13_d3d11.exe; EXPERIMENTAL), the Xenos-emulated wwe13.exe, the runtime DLLs and launcher2.
# Same layout, notices and checks as tools/package-release.sh (the older SDK's bundle), without the Linux folder.
# No game data, shaders derived from game data outside the executables, symbols/PDBs or generated code.
#   tools/package-port-release.sh            (PORT_BUILD, LAUNCHER_EXE, SDK_ROOT, OUT_DIR override the inputs)
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PORT_BUILD=${PORT_BUILD:-$ROOT/port/out/build/win-cross}
LAUNCHER_EXE=${LAUNCHER_EXE:-$ROOT/out/launcher2-win/wwe13-launcher.exe}
SDK_ROOT=${SDK_ROOT:-$ROOT/../rexglue-sdk}
NATIVE_DEPS=${NATIVE_DEPS:-$ROOT/../native-deps}
OUT_DIR=${OUT_DIR:-$ROOT/out/release-port}
VERSION=$(tr -d '[:space:]' < "$ROOT/VERSION")
NOTES=${NOTES:-$ROOT/RELEASE-NOTES-v$(echo "$VERSION" | cut -d. -f1,2).md}
ARCHIVE=${ARCHIVE:-$OUT_DIR/WWE13-Recomp-v$VERSION-x64.zip}
SDK_FFMPEG="$SDK_ROOT/thirdparty/FFmpeg"
FFMPEG_COMMIT=$(git -C "$SDK_FFMPEG" rev-parse HEAD)
SOURCE_ARCHIVE="$OUT_DIR/ffmpeg-source-$FFMPEG_COMMIT.zip"
GAME_FILES=(wwe13.exe wwe13_native.exe wwe13_d3d12.exe wwe13_d3d11.exe rexruntime.dll rexgpu-xenos.dll)

for f in "${GAME_FILES[@]}"; do
  test -f "$PORT_BUILD/$f" || { printf 'Missing build output: %s\n' "$PORT_BUILD/$f" >&2; exit 2; }
done
for input in "$LAUNCHER_EXE" "$NOTES" "$ROOT/README.md" "$ROOT/DISCLAIMER.md" "$ROOT/LICENSE" \
             "$ROOT/LICENSE.BSD-3-Clause-PromethenDev" "$ROOT/THIRD_PARTY.md" "$ROOT/CHANGELOG.md" \
             "$ROOT/launcher2/thirdparty/miniz/miniz.h" "$ROOT/launcher2/thirdparty/picosha2/picosha2.h" \
             "$ROOT/port/src/reblue/LICENSE.reblue" "$ROOT/port/third_party/reblue_thirdparty/zstd/LICENSE" \
             "$NATIVE_DEPS/plume/LICENSE" "$SDK_ROOT/LICENSE" "$SDK_FFMPEG/COPYING.LGPLv2.1" \
             "$SDK_ROOT/thirdparty/CMakeLists.txt"; do
  test -f "$input" || { printf 'Missing required package input: %s\n' "$input" >&2; exit 2; }
done
# The D3D12 executable must not need DXC at run time (pre-linked, signed shaders; dxcompiler.dll delay-loaded),
# and no build may import a debug runtime.
python3 - "$PORT_BUILD" "${GAME_FILES[@]}" <<'PY'
import struct, sys
from pathlib import Path
def imports(path):
    d = Path(path).read_bytes(); pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = pe + 24; sec = opt + struct.unpack_from('<H', d, pe + 20)[0]
    rva = struct.unpack_from('<I', d, opt + 112 + 8)[0]  # PE32+ import directory
    secs = [struct.unpack_from('<IIII', d, sec + 40 * i + 8) for i in range(nsec)]
    def off(r):
        for vs, va, rs, ro in secs:
            if va <= r < va + max(vs, rs): return r - va + ro
        raise ValueError(r)
    out, o = [], off(rva)
    while True:
        name = struct.unpack_from('<I', d, o + 12)[0]
        if not name: return out
        n = off(name); out.append(d[n:d.index(b'\0', n)].decode().lower()); o += 20
root = Path(sys.argv[1]); bad = []
for f in sys.argv[2:]:
    imp = imports(root / f)
    bad += [f"{f}: {i}" for i in imp if i in ('dxcompiler.dll', 'dxil.dll') or i.endswith('d.dll') and i.startswith(('msvcp', 'vcruntime', 'ucrtbase'))]
if bad: raise SystemExit('Unexpected imports: ' + ', '.join(bad))
print('Imports OK')
PY
test ! -e "$ARCHIVE" || { printf 'Refusing to overwrite existing archive: %s\n' "$ARCHIVE" >&2; exit 2; }
test ! -e "$SOURCE_ARCHIVE" || { printf 'Refusing to overwrite existing source archive: %s\n' "$SOURCE_ARCHIVE" >&2; exit 2; }

mkdir -p "$OUT_DIR"
WORK=$(mktemp -d "$OUT_DIR/.package-work.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
STAGE=$WORK/WWE13-Recomp-x64
mkdir -p "$STAGE/WWE 13" "$STAGE/licenses/launcher" "$STAGE/licenses/renderer" "$STAGE/third-party-licenses/rexglue-sdk"

for f in "${GAME_FILES[@]}"; do cp "$PORT_BUILD/$f" "$STAGE/"; done
cp "$LAUNCHER_EXE" "$STAGE/WWE13 Launcher.exe"
# Keyboard keys and the port's runtime settings (port/wwe13_runtime.toml, the file the build copies beside the
# exe). It must not set user_language: the launcher treats a missing key as English (the fork once shipped French).
cp "$ROOT/port/wwe13_runtime.toml" "$STAGE/wwe13.toml"
if grep -q '^[[:space:]]*user_language' "$STAGE/wwe13.toml"; then echo "wwe13.toml must not set user_language" >&2; exit 2; fi
grep -q '^keybind_start' "$STAGE/wwe13.toml" || { echo "wwe13.toml has no keymap" >&2; exit 2; }
# Pipeline seed (tools/merge-pso-seed.py over test sessions' userdata/cache/pipelines.bin; not in git): the game
# pre-builds these at start, so a first session in a new folder does not stall on shader builds mid-match.
PSO_SEED=${PSO_SEED:-$ROOT/port/pso-seed/pipelines_seed.w12p}
if [ -f "$PSO_SEED" ]; then cp "$PSO_SEED" "$STAGE/pipelines_seed.w12p"; else echo "warning: no pipeline seed at $PSO_SEED" >&2; fi
python3 "$ROOT/tools/scrub-build-paths.py" --old "$HOME/" --new "$(python3 -c 'import sys; n=len(sys.argv[1]); print(("/build/src/" + "x" * n)[:n-1] + "/")' "$HOME/")" \
  --account "$(basename "$HOME")" "$STAGE"/*.exe "$STAGE"/*.dll
# Linux build (v1.7, optional): LINUX_DIR holds wwe13-launcher, wwe13_native (Vulkan), wwe13 (Xenos) and the SDK .so files.
if [ -n "${LINUX_DIR:-}" ]; then
  L="$STAGE/Linux-x86_64"; mkdir -p "$L/WWE 13"
  for f in wwe13-launcher wwe13_native wwe13 librexruntime.so librexgpu-xenos.so; do
    test -f "$LINUX_DIR/$f" || { printf 'Missing Linux file: %s\n' "$LINUX_DIR/$f" >&2; exit 2; }
    cp "$LINUX_DIR/$f" "$L/"
  done
  cp "$ROOT/port/wwe13_runtime.toml" "$L/wwe13.toml"
  if [ -f "$PSO_SEED" ]; then cp "$PSO_SEED" "$L/pipelines_seed.w12p"; fi
  strip --strip-debug "$L/wwe13-launcher" "$L/wwe13_native" "$L/wwe13" "$L"/*.so
  python3 "$ROOT/tools/scrub-build-paths.py" --old "$HOME/" --new "$(python3 -c 'import sys; n=len(sys.argv[1]); print(("/build/src/" + "x" * n)[:n-1] + "/")' "$HOME/")" \
    --account "$(basename "$HOME")" "$L/wwe13-launcher" "$L/wwe13_native" "$L/wwe13" "$L"/*.so
  [ -f "$ROOT/docs/LINUX_STEAM_DECK.md" ] && cp "$ROOT/docs/LINUX_STEAM_DECK.md" "$L/"
  # Empty folders are not zipped: give the Linux game folder its note like the Windows one.
  printf 'Copy your own complete WWE 13 title update 2.0.1.0 game folder here (Linux build).\n' > "$L/WWE 13/PUT-GAME-FILES-HERE.txt"
fi
cp "$ROOT"/launcher2/ui/fonts/*-OFL.txt "$STAGE/licenses/launcher/"
python3 - "$ROOT/launcher2/thirdparty/miniz/miniz.h" "$STAGE/licenses/launcher/miniz-UNLICENSE.txt" \
          "$ROOT/launcher2/thirdparty/picosha2/picosha2.h" "$STAGE/licenses/launcher/picosha2-MIT.txt" <<'PY'
import sys
for src, dst, marker in ((sys.argv[1], sys.argv[2], "This is free and unencumbered software"),
                         (sys.argv[3], sys.argv[4], "The MIT License (MIT)")):
    text = open(src, encoding="utf-8", errors="replace").read()
    start = text.rfind(marker) if "unencumbered" in marker else text.find(marker)
    if start < 0: raise SystemExit(f"licence statement not found in {src}")
    end = text.find("*/", start)
    open(dst, "w", encoding="utf-8").write(text[start:end if end > 0 else None].strip() + "\n")
PY
# Native renderer components (re:Blue BSD-3, plume MIT, XenosRecomp MIT, DXC, smol-v, xxHash, fmt, zstd, ...).
cp "$ROOT"/port/third_party/licenses/* "$STAGE/licenses/renderer/"
cp "$ROOT/port/src/reblue/LICENSE.reblue" "$STAGE/licenses/renderer/reblue-LICENSE.txt"
cp "$NATIVE_DEPS/plume/LICENSE" "$STAGE/licenses/renderer/plume-LICENSE.txt"
cp "$ROOT/port/third_party/reblue_thirdparty/zstd/LICENSE" "$STAGE/licenses/renderer/zstd-LICENSE.txt"
cp "$ROOT/README.md" "$ROOT/DISCLAIMER.md" "$ROOT/LICENSE" "$ROOT/LICENSE.BSD-3-Clause-PromethenDev" \
  "$ROOT/THIRD_PARTY.md" "$ROOT/CHANGELOG.md" "$NOTES" "$STAGE/"
cp "$SDK_FFMPEG/COPYING.LGPLv2.1" "$STAGE/licenses/FFmpeg-LGPL-2.1.txt"

python3 - "$SDK_ROOT" "$STAGE/third-party-licenses/rexglue-sdk" <<'PY'
import os, shutil, sys
from pathlib import Path
source, destination = map(Path, sys.argv[1:])
paths = [source / "LICENSE"] if (source / "LICENSE").is_file() else []
seen = set()
for root, dirs, files in os.walk(source / "thirdparty", followlinks=True):
    real = os.path.realpath(root)
    if real in seen: dirs[:] = []; continue
    seen.add(real); paths.extend(Path(root) / n for n in files)
copied = 0
for path in paths:
    n = path.name.lower()
    if path.is_file() and any(n == k or n.startswith(k + ".") for k in ("license", "copying", "notice")):
        target = destination / path.relative_to(source); target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target); copied += 1
if not copied: raise SystemExit("No SDK license/notice files found")
print(f"Copied {copied} SDK LICENSE/COPYING/NOTICE files")
PY

# FFmpeg (LGPL-2.1, built into rexruntime.dll for XMA audio): the exact source commit plus the SDK's overlay
# lists and its thirdparty/CMakeLists.txt, beside the ZIP and inside it.
python3 - "$SDK_ROOT" "$FFMPEG_COMMIT" "$SOURCE_ARCHIVE" <<'PY'
import subprocess, sys, tarfile, zipfile
from pathlib import Path
sdk, commit, destination = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
ffmpeg, overlay = sdk / "thirdparty" / "FFmpeg", sdk / "thirdparty" / "ffmpeg-overlay"
def put(zf, name, data):
    info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)); info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16; zf.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
with zipfile.ZipFile(destination, "x") as zf:
    proc = subprocess.Popen(["git", "-C", str(ffmpeg), "archive", "--format=tar", "--prefix=thirdparty/FFmpeg/", "HEAD"],
                            stdout=subprocess.PIPE)
    with tarfile.open(fileobj=proc.stdout, mode="r|") as archive:
        for m in archive:
            if m.isfile(): put(zf, m.name, archive.extractfile(m).read())
    if proc.wait() != 0: raise SystemExit("git archive failed for the pinned FFmpeg source commit")
    for p in sorted(overlay.rglob("*")):
        if p.is_file(): put(zf, f"thirdparty/ffmpeg-overlay/{p.relative_to(overlay).as_posix()}", p.read_bytes())
    put(zf, "thirdparty/CMakeLists.txt", (sdk / "thirdparty" / "CMakeLists.txt").read_bytes())
    put(zf, "SOURCE-README.txt", (f"FFmpeg source commit: {commit}\n"
        "thirdparty/FFmpeg is archived from the exact git commit used by the SDK build (statically linked into\n"
        "rexruntime.dll). thirdparty/ffmpeg-overlay holds the SDK's codec/parser/bsf lists; thirdparty/CMakeLists.txt\n"
        "(section 'FFmpeg - Audio/video codec library') is the build recipe.\n").encode())
print(f"Created {destination}")
PY
cp "$SOURCE_ARCHIVE" "$STAGE/"

python3 - "$STAGE" "$VERSION" <<'PY'
import hashlib, sys
from pathlib import Path
root = Path(sys.argv[1])
(root / "WWE 13" / "PUT-GAME-FILES-HERE.txt").write_bytes((
    "Copy your own complete WWE '13 title update 2.0.1.0 game folder here.\n"
    "Required: your matching default.xex and default.xexp plus the rest of the game files.\n"
    "No game files, title update, DLC, or saves are included in this release.\n").encode())
for p in root.rglob("*"):
    if p.is_file() and p.suffix.lower() in (".txt", ".bat"):
        p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
entries = []
for p in sorted(x for x in root.rglob("*") if x.is_file() and x.name != "SHA256SUMS"):
    h = hashlib.sha256()
    with p.open("rb") as s:
        for b in iter(lambda: s.read(1 << 20), b""): h.update(b)
    entries.append(f"{h.hexdigest()}  {p.relative_to(root).as_posix()}")
(root / "SHA256SUMS").write_text("\n".join(entries) + "\n", encoding="ascii", newline="\n")
print(f"Prepared {len(entries)} SHA256SUMS entries")
# Nothing of the build account or game data may ship.
bad = [p for p in root.rglob("*") if p.suffix.lower() in (".pdb", ".xex", ".xexp", ".xsc", ".map")]
if bad: raise SystemExit(f"forbidden files staged: {bad}")
PY

python3 - "$WORK" "$ARCHIVE" <<'PY'
import sys, zipfile
from pathlib import Path
root, archive = map(Path, sys.argv[1:])
with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
    for p in sorted(x for x in root.rglob("*") if x.is_file()):
        zf.write(p, p.relative_to(root).as_posix())
PY
printf 'Created %s\n' "$ARCHIVE"
printf 'FFmpeg source: %s\n' "$SOURCE_ARCHIVE"

#!/usr/bin/env bash
# Build the Windows/Linux review bundle from frozen REL-3 game outputs.
# No game data, symbols/PDBs, generated code, or test launchers are included.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WIN_BUILD=${WIN_BUILD:-$ROOT/out/ffmpeg/win}
LINUX_BUILD=${LINUX_BUILD:-$ROOT/out/ffmpeg/linux}
LAUNCHER_BUILD=${LAUNCHER_BUILD:-$ROOT/out/build/launcher-win}
DEFAULT_SDK_ROOT=$ROOT/../tools/rexglue/.worktrees/ffmpeg
[[ -d "$DEFAULT_SDK_ROOT" ]] || DEFAULT_SDK_ROOT=$ROOT/../tools/rexglue
SDK_ROOT=${SDK_ROOT:-$DEFAULT_SDK_ROOT}
OUT_DIR=${OUT_DIR:-$ROOT/out/release}
# Release version from the VERSION file (the same one the game and launcher are built with).
VERSION=$(tr -d '[:space:]' < "$ROOT/VERSION")
ARCHIVE=${ARCHIVE:-$OUT_DIR/WWE13-Recomp-v$VERSION-x64-$(date -u +%Y%m%d).zip}
WIN_EXE=${WIN_EXE:-}
# New SDL3 + Dear ImGui launcher (launcher2/), built for both platforms.
LAUNCHER2_WIN_EXE=${LAUNCHER2_WIN_EXE:-$ROOT/out/build/launcher2-ui-win/wwe13-launcher.exe}
LAUNCHER2_LINUX_EXE=${LAUNCHER2_LINUX_EXE:-$ROOT/out/build/launcher2-ui-real-linux/wwe13-launcher}
LINUX_EXE=${LINUX_EXE:-}
# Optional pre-built shader caches (src/shader_cache_seed.cpp): dirs holding 545108B4.xsh + 545108B4.fbo.vk.xpso, made
# with tools/merge-shader-storage.py from same-platform runs (Windows 68-byte, Linux 66-byte pipeline records). They
# contain the game's own shader microcode, so they are NOT shipped unless the owner opts in by setting these.
WIN_SHADER_SEED=${WIN_SHADER_SEED:-}
LINUX_SHADER_SEED=${LINUX_SHADER_SEED:-}
# v1.0.1: only the main launcher ships (WWE13 Launcher.exe / wwe13-launcher). The old
# wwe13-enhanced.exe and the wwe13.bat / wwe13-debug.bat start scripts are no longer packaged;
# the launcher's "Save a Bug Report" button and the game log's crash report cover bug reports.

shopt -s nullglob
if [[ -z "$WIN_EXE" ]]; then
  if [[ -f "$WIN_BUILD/wwe13.exe" ]]; then
    WIN_EXE=$WIN_BUILD/wwe13.exe
  else
    win_candidates=("$WIN_BUILD"/wwe13-*.exe)
    ((${#win_candidates[@]} == 1)) || {
      printf 'Expected one frozen wwe13-*.exe under %s; found %s\n' "$WIN_BUILD" "${#win_candidates[@]}" >&2
      exit 2
    }
    WIN_EXE=${win_candidates[0]}
  fi
fi
if [[ -z "$LINUX_EXE" ]]; then
  if [[ -f "$LINUX_BUILD/wwe13" ]]; then
    LINUX_EXE=$LINUX_BUILD/wwe13
  else
    linux_candidates=("$LINUX_BUILD"/wwe13-*)
    ((${#linux_candidates[@]} == 1)) || {
      printf 'Expected one frozen wwe13-* binary under %s; found %s\n' "$LINUX_BUILD" "${#linux_candidates[@]}" >&2
      exit 2
    }
    LINUX_EXE=${linux_candidates[0]}
  fi
fi
WIN_AVCODEC_DLLS=("$WIN_BUILD"/avcodec-*.dll)
WIN_AVUTIL_DLLS=("$WIN_BUILD"/avutil-*.dll)
LINUX_AVCODEC_SOS=("$LINUX_BUILD"/libavcodec.so.*)
LINUX_AVUTIL_SOS=("$LINUX_BUILD"/libavutil.so.*)

SDK_FFMPEG="$SDK_ROOT/thirdparty/FFmpeg"
FFMPEG_OVERLAY="$SDK_ROOT/thirdparty/ffmpeg-overlay"
FFMPEG_COMMIT=$(git -C "$SDK_FFMPEG" rev-parse HEAD)
SOURCE_ARCHIVE="$OUT_DIR/ffmpeg-source-$FFMPEG_COMMIT.zip"

for input in "$WIN_EXE" "$LINUX_EXE" \
             "$LAUNCHER2_WIN_EXE" "$LAUNCHER2_LINUX_EXE" \
             "$ROOT/launcher2/thirdparty/miniz/miniz.h" \
             "$ROOT/README.md" "$ROOT/DISCLAIMER.md" "$ROOT/LICENSE" \
             "$ROOT/RELEASE-NOTES-v1.3.md" \
             "$SDK_ROOT/LICENSE" "$SDK_FFMPEG/COPYING.LGPLv2.1" \
             "$SDK_ROOT/thirdparty/CMakeLists.txt"; do
  test -f "$input" || { printf 'Missing required package input: %s\n' "$input" >&2; exit 2; }
done
(( ${#WIN_AVCODEC_DLLS[@]} > 0 && ${#WIN_AVUTIL_DLLS[@]} > 0 )) || {
  printf 'Missing adjacent avcodec/avutil DLLs under %s\n' "$WIN_BUILD" >&2; exit 2;
}
(( ${#LINUX_AVCODEC_SOS[@]} > 0 && ${#LINUX_AVUTIL_SOS[@]} > 0 )) || {
  printf 'Missing adjacent avcodec/avutil shared objects under %s\n' "$LINUX_BUILD" >&2; exit 2;
}
test ! -e "$ARCHIVE" || { printf 'Refusing to overwrite existing archive: %s\n' "$ARCHIVE" >&2; exit 2; }
test ! -e "$SOURCE_ARCHIVE" || { printf 'Refusing to overwrite existing source archive: %s\n' "$SOURCE_ARCHIVE" >&2; exit 2; }

mkdir -p "$OUT_DIR"
WORK=$(mktemp -d "$OUT_DIR/.package-work.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
STAGE=$WORK/WWE13-Recomp-x64
mkdir -p "$STAGE/WWE 13" "$STAGE/Linux-x86_64" \
  "$STAGE/third-party-licenses/rexglue-sdk" "$STAGE/licenses"

cp "$WIN_EXE" "$STAGE/wwe13.exe"
cp "${WIN_AVCODEC_DLLS[@]}" "${WIN_AVUTIL_DLLS[@]}" "$STAGE/"
cp "$LINUX_EXE" "$STAGE/Linux-x86_64/wwe13"
cp "${LINUX_AVCODEC_SOS[@]}" "${LINUX_AVUTIL_SOS[@]}" "$STAGE/Linux-x86_64/"
cp "$LAUNCHER2_WIN_EXE" "$STAGE/WWE13 Launcher.exe"
cp "$LAUNCHER2_LINUX_EXE" "$STAGE/Linux-x86_64/wwe13-launcher"
for seed in "$WIN_SHADER_SEED:$STAGE/shader-cache:68" "$LINUX_SHADER_SEED:$STAGE/Linux-x86_64/shader-cache:66"; do
  IFS=: read -r src dst rec <<< "$seed"
  [[ -n "$src" ]] || continue
  pso="$src/545108B4.fbo.vk.xpso"
  test -f "$src/545108B4.xsh" -a -f "$pso" || { printf 'Missing shader seed files in %s\n' "$src" >&2; exit 2; }
  (( ($(stat -c %s "$pso") - 12) % rec == 0 )) || { printf 'Wrong pipeline record size in %s (want %s)\n' "$pso" "$rec" >&2; exit 2; }
  mkdir -p "$dst"; cp "$src/545108B4.xsh" "$pso" "$dst/"
done
# Keyboard keys: the release's modern (WWE 2K-style) layout, read by the game from wwe13.toml beside it. Same 24 rows
# and keys as launcher2/core/controls.cpp kBindings (the launcher also adds any row a player's file is missing).
keymap='keybind_a = "L"
keybind_b = "K"
keybind_x = "J"
keybind_y = "Space"
keybind_left_trigger = "Shift"
keybind_right_trigger = "1"
keybind_left_shoulder = "U"
keybind_right_shoulder = "Control"
keybind_lstick_up = "W"
keybind_lstick_down = "S"
keybind_lstick_left = "A"
keybind_lstick_right = "D"
keybind_lstick_press = "C"
keybind_rstick_up = "T"
keybind_rstick_down = "G"
keybind_rstick_left = "F"
keybind_rstick_right = "H"
keybind_rstick_press = "V"
keybind_dpad_up = "Up"
keybind_dpad_down = "Down"
keybind_dpad_left = "Left"
keybind_dpad_right = "Right"
keybind_back = "Tab"
keybind_start = "Escape"'
printf '%s\n' "$keymap" > "$STAGE/wwe13.toml"
printf '%s\n' "$keymap" > "$STAGE/Linux-x86_64/wwe13.toml"
# Like the Windows exe (whose debug info stays in the unshipped .pdb), ship Linux binaries without
# DWARF debug info: 342 -> 109 MB for the game. --strip-debug keeps the symbol table and code.
strip --strip-debug "$STAGE/Linux-x86_64/wwe13" "$STAGE/Linux-x86_64/wwe13-launcher"
# Build paths embedded by the compilers name the build account (/home/<user>/...): rewrite them to a neutral path of the
# same length in every shipped binary (tools/scrub-build-paths.py fails if the account name would remain).
python3 "$ROOT/tools/scrub-build-paths.py" --old "$HOME/" --new "$(python3 -c 'import sys; n=len(sys.argv[1]); print(("/build/src/" + "x" * n)[:n-1] + "/")' "$HOME/")" \
  --account "$(basename "$HOME")" "$STAGE/wwe13.exe" "$STAGE/WWE13 Launcher.exe" "$STAGE"/*.dll \
  "$STAGE/Linux-x86_64/wwe13" "$STAGE/Linux-x86_64/wwe13-launcher" "$STAGE"/Linux-x86_64/lib*.so*
mkdir -p "$STAGE/licenses/launcher"
cp "$ROOT"/launcher2/ui/fonts/*-OFL.txt "$STAGE/licenses/launcher/"
# miniz keeps its public-domain (unlicense) dedication at the end of the header.
python3 - "$ROOT/launcher2/thirdparty/miniz/miniz.h" "$STAGE/licenses/launcher/miniz-UNLICENSE.txt" <<'PY'
import sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
start = text.rfind("This is free and unencumbered software")
if start < 0:
    raise SystemExit("miniz unlicense statement not found")
end = text.find("*/", start)
open(sys.argv[2], "w", encoding="utf-8").write(text[start:end if end > 0 else None].strip() + "\n")
PY
cp "$ROOT/README.md" "$ROOT/DISCLAIMER.md" "$ROOT/LICENSE" "$ROOT/RELEASE-NOTES-v1.3.md" "$STAGE/"
cp "$SDK_FFMPEG/COPYING.LGPLv2.1" "$STAGE/licenses/FFmpeg-LGPL-2.1.txt"

# Preserve SDK and vendored dependency LICENSE/COPYING/NOTICE texts. Keep their
# original relative paths so notices remain attributable to their components.
python3 - "$SDK_ROOT" "$STAGE/third-party-licenses/rexglue-sdk" <<'PY'
import os
from pathlib import Path
import shutil
import sys

source, destination = map(Path, sys.argv[1:])
copied = 0
paths = [source / "LICENSE"] if (source / "LICENSE").is_file() else []
seen_directories = set()
for root, directories, filenames in os.walk(source / "thirdparty", followlinks=True):
    real_root = os.path.realpath(root)
    if real_root in seen_directories:
        directories[:] = []
        continue
    seen_directories.add(real_root)
    paths.extend(Path(root) / name for name in filenames)
for path in paths:
    name = path.name.lower()
    if (path.is_file() and
            (name == "license" or name.startswith("license.") or
             name == "copying" or name.startswith("copying.") or
             name == "notice" or name.startswith("notice."))):
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        copied += 1
if copied == 0:
    raise SystemExit("No SDK license/notice files found")
print(f"Copied {copied} SDK LICENSE/COPYING/NOTICE files")
PY

# Preserve the exact source commit, overlay list files, and the CMake fragment
# that builds these two libraries. The same source archive is placed beside
# the release ZIP and inside its top-level folder.
python3 - "$SDK_ROOT" "$FFMPEG_COMMIT" "$SOURCE_ARCHIVE" <<'PY'
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import zipfile

sdk, commit, destination = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
ffmpeg = sdk / "thirdparty" / "FFmpeg"
overlay = sdk / "thirdparty" / "ffmpeg-overlay"
cmake_source = sdk / "thirdparty" / "CMakeLists.txt"
prefix = "thirdparty/FFmpeg/"

def put(zf, name, data):
    info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    zf.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)

with zipfile.ZipFile(destination, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
    proc = subprocess.Popen(
        ["git", "-C", str(ffmpeg), "archive", "--format=tar", f"--prefix={prefix}", "HEAD"],
        stdout=subprocess.PIPE,
    )
    assert proc.stdout is not None
    with tarfile.open(fileobj=proc.stdout, mode="r|") as archive:
        for member in archive:
            if member.isfile():
                stream = archive.extractfile(member)
                if stream is not None:
                    put(zf, member.name, stream.read())
    if proc.wait() != 0:
        raise SystemExit("git archive failed for the pinned FFmpeg source commit")

    for path in sorted(overlay.rglob("*")):
        if path.is_file():
            put(zf, f"thirdparty/ffmpeg-overlay/{path.relative_to(overlay).as_posix()}", path.read_bytes())

    cmake_text = cmake_source.read_text(encoding="utf-8")
    start_marker = "# FFmpeg - Audio/video codec library (for XMA decoding)"
    end_marker = "# IDE folder organization"
    start = cmake_text.index(start_marker)
    end = cmake_text.index(end_marker, start)
    put(zf, "build/ffmpeg-build-fragment.cmake", cmake_text[start:end].encode("utf-8"))
    put(zf, "thirdparty/CMakeLists.txt", cmake_text.encode("utf-8"))
    private_def = sdk / "cmake" / "ffmpeg-avutil-private.def"
    if private_def.is_file():
        put(zf, "cmake/ffmpeg-avutil-private.def", private_def.read_bytes())
    else:
        raise SystemExit(f"missing FFmpeg private export file: {private_def}")

    versions = {}
    for library, header in (
        ("libavutil", ffmpeg / "libavutil" / "version.h"),
        ("libavcodec", ffmpeg / "libavcodec" / "version.h"),
    ):
        text = header.read_text(encoding="utf-8")
        components = [
            re.search(rf"^#define {library.upper()}_VERSION_{part}\s+(\d+)", text, re.MULTILINE)
            for part in ("MAJOR", "MINOR", "MICRO")
        ]
        if not all(components):
            raise SystemExit(f"could not parse version macros from {header}")
        versions[library] = ".".join(match.group(1) for match in components if match)
    readme = (
        f"FFmpeg source commit: {commit}\n"
        f"Libraries built: libavutil {versions['libavutil']}, libavcodec {versions['libavcodec']}\n"
        "thirdparty/FFmpeg is archived from the exact git commit used by the SDK build.\n"
        "thirdparty/ffmpeg-overlay contains this SDK's generated codec/parser/bsf lists.\n"
        "build/ffmpeg-build-fragment.cmake is the FFmpeg section extracted verbatim\n"
        "from thirdparty/CMakeLists.txt at the SDK source revision used here.\n"
        "cmake/ffmpeg-avutil-private.def is the CMake-merged Windows export for ff_reverse.\n"
    )
    put(zf, "SOURCE-README.txt", readme.encode("utf-8"))
print(f"Created {destination}")
PY
cp "$SOURCE_ARCHIVE" "$STAGE/$(basename "$SOURCE_ARCHIVE")"

python3 - "$SDK_ROOT" "$FFMPEG_COMMIT" "$STAGE/licenses/FFmpeg-NOTICE.txt" <<'PY'
from pathlib import Path
import re
import sys

sdk, commit, destination = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
ffmpeg = sdk / "thirdparty" / "FFmpeg"

def version(library):
    text = (ffmpeg / library / "version.h").read_text(encoding="utf-8")
    components = [
        re.search(rf"^#define {library.upper()}_VERSION_{part}\s+(\d+)", text, re.MULTILINE)
        for part in ("MAJOR", "MINOR", "MICRO")
    ]
    if not all(components):
        raise SystemExit(f"could not parse version for {library}")
    return ".".join(match.group(1) for match in components if match)

avutil_version = version("libavutil")
avcodec_version = version("libavcodec")
avutil_major = avutil_version.split(".", 1)[0]
avcodec_major = avcodec_version.split(".", 1)[0]

notice = (
    "FFmpeg shared libraries included with WWE '13 Recomp\n\n"
    f"libavutil {avutil_version} (Windows avutil-{avutil_major}.dll; Linux libavutil.so.{avutil_major})\n"
    f"libavcodec {avcodec_version} (Windows avcodec-{avcodec_major}.dll; Linux libavcodec.so.{avcodec_major})\n\n"
    "You may replace these DLLs with your own compatible build. The Linux shared\n"
    "objects may likewise be replaced with compatible builds of the same SONAMEs.\n"
    f"Corresponding source: ffmpeg-source-{commit}.zip (included here and supplied\n"
    "beside this release archive). It contains the exact FFmpeg git commit, the\n"
    "SDK's ffmpeg-overlay files, and the CMake build fragment.\n"
)
destination.write_text(notice, encoding="utf-8", newline="\n")
PY

python3 - "$STAGE" <<'PY'
from pathlib import Path
import hashlib
import sys

root = Path(sys.argv[1])
marker = root / "WWE 13" / "PUT-GAME-FILES-HERE.txt"
marker.write_bytes((
    "Copy your own complete WWE '13 title update 2.0.1.0 game folder here.\n"
    "Required: your matching default.xex and default.xexp plus the rest of the game files.\n"
    "No game files, title update, DLC, or saves are included in this release.\n"
).replace("\n", "\r\n").encode("utf-8"))

# Windows batch and plain-text files must have CRLF regardless of checkout settings.
for path in root.rglob("*"):
    if path.is_file() and (path.suffix.lower() == ".bat" or path.suffix.lower() == ".txt"):
        data = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        path.write_bytes(data)

# Checksums cover all package files except this checksum manifest itself.
entries = []
for path in sorted(p for p in root.rglob("*") if p.is_file() and p.name != "SHA256SUMS"):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    entries.append(f"{digest.hexdigest()}  {path.relative_to(root).as_posix()}")
(root / "SHA256SUMS").write_text("\n".join(entries) + "\n", encoding="ascii", newline="\n")
print(f"Prepared {len(entries)} SHA256SUMS entries")
PY

# Archive includes a single top-level folder and does not collect any .pdb files.
python3 - "$WORK" "$ARCHIVE" <<'PY'
from pathlib import Path
import sys
import zipfile

root, archive = map(Path, sys.argv[1:])
with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        zf.write(path, path.relative_to(root).as_posix())
PY

printf 'Created %s\n' "$ARCHIVE"
printf 'FFmpeg source: %s\n' "$SOURCE_ARCHIVE"

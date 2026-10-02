#!/usr/bin/env python3
"""Merge ReXGlue Vulkan shader/pipeline storages (the "shareable" shader cache) into one.

  ./tools/merge-shader-storage.py OUT_DIR IN_DIR [IN_DIR ...] [--title 545108B4] [--pso-record 68]

Each IN_DIR holds <title>.xsh (guest shader microcode) and <title>.fbo.vk.xpso (pipeline descriptions), as in
userdata/cache/shaders/shareable/. The runtime rebuilds real pipelines from the descriptions at boot with the local
driver, so a merged store pre-compiles, on any PC, every pipeline any of the inputs has seen.

Only merge stores written by the same platform build: the pipeline description is a C++ bitfield struct and its
layout differs between the Windows (MSVC ABI, 68-byte records) and Linux (66-byte) builds; a checksum over the
bytes cannot catch a record read with the other layout. The tool refuses inputs whose file headers
(magic/version) differ or whose record size does not divide the pipeline file.

Every record is verified with its XXH3-64 hash (as the runtime does); corrupt tails are dropped, duplicates
(same hash) are kept once, first input first.
"""
import argparse
import os
import struct
import sys

import xxhash

XSH_HEADER = 8    # magic 'XESH', version (byte-swapped)
XPSO_HEADER = 12  # magic 'XEPS', api, version (byte-swapped)


def read_xsh(path):
    data = open(path, "rb").read()
    if len(data) < XSH_HEADER or data[:4] != b"XESH":
        raise SystemExit(f"{path}: not a shader storage file")
    header, records, pos = data[:XSH_HEADER], [], XSH_HEADER
    while pos + 12 <= len(data):
        ucode_hash, packed = struct.unpack_from("<QI", data, pos)
        size = (packed & 0x7FFFFFFF) * 4
        if pos + 12 + size > len(data):
            break
        ucode = data[pos + 12:pos + 12 + size]
        if xxhash.xxh3_64_intdigest(ucode) != ucode_hash:
            print(f"  {path}: corrupt shader record at {pos}, dropping the rest", file=sys.stderr)
            break
        records.append((ucode_hash, data[pos:pos + 12 + size]))
        pos += 12 + size
    return header, records


def read_xpso(path, record_size):
    data = open(path, "rb").read()
    if len(data) < XPSO_HEADER or data[:4] != b"XEPS":
        raise SystemExit(f"{path}: not a pipeline storage file")
    if (len(data) - XPSO_HEADER) % record_size:
        raise SystemExit(f"{path}: {len(data) - XPSO_HEADER} record bytes are not a multiple of {record_size} - "
                         "written by a build with another description layout?")
    header, records = data[:XPSO_HEADER], []
    for pos in range(XPSO_HEADER, len(data), record_size):
        (description_hash,) = struct.unpack_from("<Q", data, pos)
        description = data[pos + 8:pos + record_size]
        if xxhash.xxh3_64_intdigest(description) != description_hash:
            print(f"  {path}: corrupt pipeline record at {pos}, dropping the rest", file=sys.stderr)
            break
        records.append((description_hash, data[pos:pos + record_size]))
    return header, records


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir")
    ap.add_argument("in_dirs", nargs="+")
    ap.add_argument("--title", default="545108B4")
    ap.add_argument("--pso-record", type=int, default=68, help="pipeline record size (68 = Windows build)")
    args = ap.parse_args()

    xsh_name, xpso_name = f"{args.title}.xsh", f"{args.title}.fbo.vk.xpso"
    xsh_header = xpso_header = None
    shaders, pipelines = {}, {}
    for d in args.in_dirs:
        h, recs = read_xsh(os.path.join(d, xsh_name))
        if xsh_header is None:
            xsh_header = h
        elif h != xsh_header:
            raise SystemExit(f"{d}: shader storage header differs from the first input")
        new_s = sum(1 for k, _ in recs if k not in shaders)
        for k, r in recs:
            shaders.setdefault(k, r)
        h, recs = read_xpso(os.path.join(d, xpso_name), args.pso_record)
        if xpso_header is None:
            xpso_header = h
        elif h != xpso_header:
            raise SystemExit(f"{d}: pipeline storage header (api/version) differs from the first input")
        new_p = sum(1 for k, _ in recs if k not in pipelines)
        for k, r in recs:
            pipelines.setdefault(k, r)
        print(f"{d}: +{new_s} new shaders, +{new_p} new pipelines")
    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, xsh_name), "wb") as f:
        f.write(xsh_header)
        for r in shaders.values():
            f.write(r)
    with open(os.path.join(args.out_dir, xpso_name), "wb") as f:
        f.write(xpso_header)
        for r in pipelines.values():
            f.write(r)
    print(f"merged: {len(shaders)} shaders, {len(pipelines)} pipelines -> {args.out_dir}")


if __name__ == "__main__":
    main()

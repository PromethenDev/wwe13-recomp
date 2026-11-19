#!/usr/bin/env python3
"""compare_pair.py <ref gen dir> <target gen dir> <ref addr> <target addr> [--show]

Compares two matched functions line by line (SDK 0.10 generated code) and lists the constants that differ:
guest addresses (values >= 0x10000 and the lis/addi halves that build them) versus small values (structure
field offsets, shifts). Used to check that the D3D device layout is the same in WWE '12 and WWE '13, and to
read WWE '13 data addresses at the same instruction as a known WWE '12 one.
"""
import re
import sys
from pathlib import Path

FUNC = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{")
NUM = re.compile(r"-?\b(?:0x[0-9A-Fa-f]+|\d+)\b")
NAME = re.compile(r"\b(?:__imp__)?(sub|loc)_[0-9A-F]{8}\b")


def body(gen, addr):
    want = f"DEFINE_REX_FUNC(sub_{addr:08X})"
    for path in sorted(Path(gen).glob("*_recomp.*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        i = text.find(want)
        if i < 0:
            continue
        j = text.find("\nDEFINE_REX_FUNC(", i + 1)
        return [l.strip() for l in text[i:j if j > 0 else None].splitlines()[1:] if l.strip()]
    raise SystemExit(f"sub_{addr:08X} not found in {gen}")


def main():
    ref_gen, tgt_gen, ra, ta = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16)
    r, t = body(ref_gen, ra), body(tgt_gen, ta)
    if len(r) != len(t):
        print(f"different lengths: {len(r)} vs {len(t)} lines")
    diffs = []
    for k, (x, y) in enumerate(zip(r, t)):
        if NAME.sub("_", x) == NAME.sub("_", y):
            continue
        nx, ny = NUM.findall(x), NUM.findall(y)
        if NUM.sub("N", NAME.sub("_", x)) != NUM.sub("N", NAME.sub("_", y)):
            diffs.append((k, "shape", x, y))
            continue
        for a, b in zip(nx, ny):
            if a != b:
                kind = "address" if max(abs(int(a, 0)), abs(int(b, 0))) >= 0x10000 or "lis" in x else "small"
                diffs.append((k, kind, x, y))
                break
    small = [d for d in diffs if d[1] != "address"]
    print(f"sub_{ra:08X} vs sub_{ta:08X}: {len(diffs)} differing lines, {len(small)} not address-like")
    if "--show" in sys.argv:
        for k, kind, x, y in diffs:
            print(f"  [{kind}] {k}: {x}\n  {'':>{len(kind) + 3}}   {y}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""field_offset_map.py <ref gen> <target gen> <map.tsv> [--out offsets.tsv] [--min 0x200]

From matched function pairs (match_functions.py output), collects the memory-access offsets that differ at the
same instruction (REX_LOAD_*/REX_STORE_* "rN.u32 + OFFSET"), and prints the WWE '12 -> WWE '13 translation of
each offset with how many sites agree. Offsets that never differ are not listed (they are unchanged when they
appear in the same instructions). Used for the D3D device object, whose layout grew between the two XDKs.
"""
import argparse
import re
from collections import Counter, defaultdict
from pathlib import Path

FUNC = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{")
ACCESS = re.compile(r"REX_(?:LOAD|STORE)_[A-Z0-9]+\(ctx\.r(\d+)\.u32 \+ (-?\d+)")
NAME = re.compile(r"\b(?:__imp__)?(sub|loc)_[0-9A-F]{8}\b")
NUM = re.compile(r"-?\b(?:0x[0-9A-Fa-f]+|\d+)\b")


def bodies(gen):
    out = {}
    for path in sorted(Path(gen).glob("*_recomp.*.cpp")):
        cur, b = None, []
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = FUNC.match(line)
            if m:
                if cur is not None:
                    out[cur] = b
                cur, b = int(m.group(1), 16), []
            elif cur is not None and line.strip() and not line.strip().startswith("//"):
                b.append(line.strip())
        if cur is not None:
            out[cur] = b
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref_gen")
    ap.add_argument("target_gen")
    ap.add_argument("map")
    ap.add_argument("--out", default="offsets.tsv")
    ap.add_argument("--min", type=lambda s: int(s, 0), default=0x200)
    a = ap.parse_args()
    ref, tgt = bodies(a.ref_gen), bodies(a.target_gen)
    pairs = defaultdict(Counter)
    same = Counter()
    for line in Path(a.map).read_text().splitlines()[1:]:
        r, t, *_ = line.split("\t")
        if t == "-":
            continue
        rb, tb = ref.get(int(r, 16)), tgt.get(int(t, 16))
        if not rb or not tb or len(rb) != len(tb):
            continue
        for x, y in zip(rb, tb):
            if NUM.sub("N", NAME.sub("_", x)) != NUM.sub("N", NAME.sub("_", y)):
                continue
            for (rx, ox), (ry, oy) in zip(ACCESS.findall(x), ACCESS.findall(y)):
                ox, oy = int(ox), int(oy)
                if ox < a.min:
                    continue
                if ox == oy:
                    same[ox] += 1
                else:
                    pairs[ox][oy] += 1
    with open(a.out, "w", newline="\n") as f:
        f.write("wwe12\twwe13\tsites\tdelta\tother\n")
        for ox in sorted(set(pairs) | set(same)):
            c = pairs.get(ox, Counter())
            if same[ox]:
                c = c + Counter({ox: same[ox]})
            (best, n), rest = c.most_common(1)[0], sum(c.values()) - c.most_common(1)[0][1]
            f.write(f"{ox:#x}\t{best:#x}\t{n}\t{best - ox:+d}\t{rest}\n")
    print(f"offsets seen: {len(set(pairs) | set(same))}, changed: {len(pairs)}")


if __name__ == "__main__":
    main()

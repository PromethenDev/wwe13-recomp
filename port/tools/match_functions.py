#!/usr/bin/env python3
"""match_functions.py <ref generated dir> <target generated dir> <ref source dir> [--out map.tsv]

Finds the WWE '13 equivalents of the WWE '12 functions that the WWE '12 PC native renderer uses, by the
structure of their generated code (same SDK 0.10 codegen for both games, same Yuke's D3D library).

Each function body is reduced to a fingerprint: guest addresses (sub_/loc_ names, values above 0x10000)
become placeholders, instruction shapes and small constants (field offsets, shifts) stay. A reference
function whose fingerprint occurs exactly once in the target is matched; ambiguous or missing ones are then
resolved through calls: a matched caller's n-th call site gives the callee.
"""
import argparse
import hashlib
import re
from collections import defaultdict
from pathlib import Path

FUNC = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{")
CALL = re.compile(r"\b(?:__imp__)?sub_([0-9A-F]{8})\(ctx, base\)")
NAME = re.compile(r"\b(?:__imp__)?(sub|loc)_[0-9A-F]{8}\b")
NUM = re.compile(r"-?\b(?:0x[0-9A-Fa-f]+|\d+)\b")


def big_to_token(m):
    text = m.group(0)
    try:
        value = int(text, 0)
    except ValueError:
        return text
    return "K" if abs(value) >= 0x10000 else text


def parse(gen_dir):
    """-> {addr: (fingerprint, [called addrs in order])}"""
    funcs = {}
    for path in sorted(Path(gen_dir).glob("*_recomp.*.cpp")):
        cur, body = None, []
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = FUNC.match(line)
            if m:
                if cur is not None:
                    funcs[cur] = body
                cur, body = int(m.group(1), 16), []
                continue
            if cur is not None:
                body.append(line)
        if cur is not None:
            funcs[cur] = body
    out = {}
    for addr, body in funcs.items():
        calls = [int(c, 16) for line in body for c in CALL.findall(line)]
        lines = [NAME.sub(r"\1", l.strip()) for l in body if l.strip()]
        norm = "\n".join(NUM.sub(big_to_token, l) for l in lines)
        shape = [NUM.sub("N", l) for l in lines]
        out[addr] = (hashlib.sha1(norm.encode()).hexdigest(), calls,
                     hashlib.sha1("\n".join(shape).encode()).hexdigest(), shape)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref_gen")
    ap.add_argument("target_gen")
    ap.add_argument("ref_src", help="WWE '12 PC sources whose sub_XXXXXXXX references are wanted")
    ap.add_argument("--out", default="function_map.tsv")
    ap.add_argument("--window", nargs=2, type=lambda s: int(s, 16), default=(0x82B80000, 0x82BC0000),
                    help="target address window for the similarity pass (WWE '13 D3D library)")
    ap.add_argument("--fuzzy-all", action="store_true",
                    help="similarity pass for every wanted function (not only the WWE '12 D3D window)")
    ap.add_argument("--ref-range", nargs=2, type=lambda s: int(s, 16), default=None,
                    help="also match every reference function in this range (e.g. the WWE '12 D3D library)")
    args = ap.parse_args()

    ref = parse(args.ref_gen)
    tgt = parse(args.target_gen)
    by_hash = defaultdict(list)
    for a, (h, *_) in tgt.items():
        by_hash[h].append(a)

    wanted = set()
    if args.ref_range:
        lo, hi = args.ref_range
        wanted.update(a for a in ref if lo <= a < hi)
    for p in Path(args.ref_src).rglob("*"):
        if p.suffix in (".cpp", ".h", ".inc", ".toml", ".md") and p.is_file():
            for m in re.findall(r"\bsub_([0-9A-F]{8})\b|\b0x(82[0-9A-F]{6})\b", p.read_text(encoding="utf-8", errors="replace")):
                a = int(m[0] or m[1], 16)
                if a in ref:
                    wanted.add(a)

    by_shape = defaultdict(list)
    for a, v in tgt.items():
        by_shape[v[2]].append(a)
    match = {}
    how = {}
    for a in wanted:
        cands = by_hash.get(ref[a][0], [])
        if len(cands) == 1:
            match[a], how[a] = cands[0], "body"
            continue
        cands = by_shape.get(ref[a][2], [])
        if len(cands) == 1:
            match[a], how[a] = cands[0], "shape"
    # Fuzzy: functions of the WWE '12 D3D window against the WWE '13 D3D window, line similarity.
    import difflib
    lo, hi = args.window
    window = [a for a in tgt if lo <= a < hi]
    taken = set(match.values())
    for a in sorted(wanted):
        if a in match or not (args.fuzzy_all or 0x82A90000 <= a < 0x82AD0000):
            continue
        rs = ref[a][3]
        scored = []
        for t in window:
            if t in taken:
                continue
            ts = tgt[t][3]
            if abs(len(ts) - len(rs)) > max(8, len(rs) // 4):
                continue
            sm = difflib.SequenceMatcher(None, rs, ts, autojunk=False)
            if sm.real_quick_ratio() < 0.85 or sm.quick_ratio() < 0.85:
                continue
            scored.append((sm.ratio(), t))
        scored.sort(reverse=True)
        if scored and scored[0][0] >= 0.9 and (len(scored) == 1 or scored[0][0] - scored[1][0] >= 0.03):
            match[a], how[a] = scored[0][1], f"similar {scored[0][0]:.2f}"
            taken.add(scored[0][1])
    # Propagate through call sites: the n-th call of a matched pair gives the n-th callee.
    changed = True
    while changed:
        changed = False
        for ra, ta in list(match.items()):
            rc, tc = ref[ra][1], tgt[ta][1] if ta in tgt else []
            if len(rc) != len(tc):
                continue
            for x, y in zip(rc, tc):
                if x in wanted and x not in match and y in tgt:
                    match[x], how[x] = y, f"call of {ra:08X}"
                    changed = True

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("wwe12\twwe13\thow\tcandidates\n")
        for a in sorted(wanted):
            n = len(by_hash.get(ref[a][0], []))
            f.write(f"{a:08X}\t{match[a]:08X}\t{how[a]}\t{n}\n" if a in match else f"{a:08X}\t-\tunmatched\t{n}\n")
    print(f"wanted {len(wanted)}, matched {len(match)} ({sum(1 for h in how.values() if h == 'body')} by body), "
          f"unmatched {len(wanted) - len(match)}")


if __name__ == "__main__":
    main()

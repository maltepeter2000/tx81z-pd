#!/usr/bin/env python3
"""Sweep every voice parameter on top of a base voice; record which YM2414 registers the
firmware writes differently after loading the voice (bulk dump)."""
import json
import sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, "/home/claude/tx81z-pd/tools")
import probe as P
import tx81z_params as T

base = [int(float(x)) for x in open("basevoice.txt").read().split()[1:]]
V0, A0 = base[:93], base[93:]
LOAD_T, END_T = 8.0, 8.6


def load_image(v, a):
    w = P.probe([(LOAD_T, "syx", P.aced_dump(a)), (LOAD_T + 0.05, "syx", P.vced_dump(v))], END_T)
    seg = [(t, r, x) for t, r, x in w if t >= LOAD_T and not (0x28 <= r <= 0x37 or 0x60 <= r <= 0x7F or r in (0x17, 0x19))]
    img = {}
    for t, r, x in seg:
        img.setdefault(r, []).append(x)
    return {r: xs for r, xs in img.items()}


def run(job):
    name, g, num, val = job
    v, a = list(V0), list(A0)
    (v if g == "V" else a)[num] = val
    return job, load_image(v, a)


if __name__ == "__main__":
    ref = load_image(V0, A0)
    jobs = []
    for nm, g, num, lo, hi, off, lbl, names in T.PARAMS:
        if not nm.endswith(("1",)) and nm[-1].isdigit():
            continue                              # operators: sweep op1 only, others by symmetry later
        vals = sorted(set([lo, hi, (lo + hi) // 2, lo + 1, hi - 1]))
        for x in vals:
            jobs.append((nm, g, num, x))
    with ThreadPoolExecutor(8) as ex:
        res = list(ex.map(run, jobs))
    out = {"ref": {f"{r:02x}": xs for r, xs in ref.items()}, "jobs": []}
    for (nm, g, num, x), img in res:
        diff = {f"{r:02x}": xs for r, xs in img.items() if ref.get(r) != xs}
        out["jobs"].append({"name": nm, "value": x, "diff": diff})
    json.dump(out, open("sweep_static.json", "w"), indent=0)
    for j in out["jobs"]:
        d = j["diff"]
        regs = sorted(d)
        print(f"{j['name']:6s}={j['value']:3d}: " + " ".join(f"{r}:{d[r][-1]:02x}" for r in regs[:10]) + (" ..." if len(regs) > 10 else ""))

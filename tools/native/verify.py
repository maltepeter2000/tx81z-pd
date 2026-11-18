#!/usr/bin/env python3
"""Compare the Python model with the firmware for random voices and notes."""
import random
import study as S
import model as MD
import tx81z_params as T
import os
NEUTRAL = os.environ.get("NEUTRAL", "1") == "1"

def rand_voice(rng):
    v, a = S.voice()
    for nm, g, num, lo, hi, off, lbl, names in T.PARAMS:
        if nm in ("poly", "pmod", "pon", "chor", "lfw"):
            continue
        (v if g == "V" else a)[num] = rng.randint(lo, hi)
    v[59] = rng.randint(0, 2)
    if NEUTRAL:   # controllers and LFO depths neutral (studied separately)
        for nm, val in (("bcpb", 50), ("bceb", 0), ("bcp", 0), ("bca", 0), ("fcp", 0), ("fca", 0), ("mwp", 0),
                        ("mwa", 0), ("pmd", 0), ("amd", 0), ("lfd", 0), ("fcv", 0), ("port", 0)):
            g, num = T.BY_NAME[nm][1], T.BY_NAME[nm][2]
            (v if g == "V" else a)[num] = val
    return v, a

def check(seed):
    rng = random.Random(seed)
    v, a = rand_voice(rng)
    key, vel = rng.randint(24, 108), rng.randint(1, 127)
    w = S.run(v, a, notes=[(8.5, [0x90, key, vel])], end=8.95)
    got = S.load_writes(w, S.LOAD, 8.45)
    exp = MD.by_reg(MD.load_writes(v, a))
    bad = []
    for r in sorted(set(got) | set(exp)):
        e = [x for x in exp.get(r, []) if x is not None]
        if r == 0x18 and None in exp.get(r, []):
            continue
        if got.get(r, []) != e:
            bad.append((hex(r), got.get(r), e))
    ch = [x & 7 for t, r, x in w if t >= 8.5 and r == 0x08]
    ch = ch[0] if ch else 0
    img = S.P.image(w, 8.94)
    tl_bad = []
    for k in (1, 2, 3, 4):
        o = MD.op_fields(v, a, k)
        g = img.get(0x60 + MD.slot(ch, k), 0) & 0x7F
        e = MD.note_tl(o, key + v[62] - 24, vel, v[52], k)
        if g != e:
            tl_bad.append((k, g, e, o["out"], o["kvs"], o["ls"], o["sh"], o["ebs"]))
    s = MD.semitone(key, v[62])
    kc_ok = img.get(0x28 + ch) == MD.keycode(s)
    return seed, bad, tl_bad, kc_ok, (key, vel, v[62], hex(img.get(0x28 + ch, 0)), hex(MD.keycode(s)))

if __name__ == "__main__":
    res = S.pmap(check, range(40))
    nl = sum(1 for r in res if not r[1]); nt = sum(1 for r in res if not r[2]); nk = sum(1 for r in res if r[3])
    print(f"load ok {nl}/40, TL ok {nt}/40, KC ok {nk}/40")
    for seed, bad, tlb, kc, info in res:
        if bad: print("LOAD", seed, bad[:3])
        if tlb: print("TL  ", seed, tlb[:4])
        if not kc: print("KC  ", seed, info)

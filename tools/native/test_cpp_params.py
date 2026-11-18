#!/usr/bin/env python3
"""Engine::set_param against the firmware's handling of a SysEx parameter change: for every voice
parameter, a random voice with notes sounding gets a new value; compared are the register writes caused
by the change (periodic writes left out) and the RAM state afterwards.
usage: test_cpp_params.py [SEED] [NAME ...]"""
import os
import random
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as P
import study as S
import verify as V
import verify_cpp as C
import verify_notes as N
import test_cpp_events as E

T_CHANGE = 8.75


def case(seed, name):
    rng = random.Random(f"{seed}-{name}")
    neu = V.NEUTRAL
    V.NEUTRAL = False
    v, a = V.rand_voice(rng)
    V.NEUTRAL = neu
    v[63] = 0
    nm, g, num, lo, hi, *_ = S.T.BY_NAME[name]
    cur = (v if g == "V" else a)[num]
    val = rng.choice([x for x in range(lo, hi + 1) if x != cur])
    notes = [(8.6, [0x90, 60, 100]), (8.62, [0x90, 67, 90])]
    return v, a, g, num, val, notes


def run(args):
    seed, name = args
    v, a, g, num, val, notes = case(seed, name)
    end = T_CHANGE + 0.25
    msg = [0xF0, 0x43, 0x10, 0x12 if g == "V" else 0x13, num, val, 0xF7]
    with tempfile.TemporaryDirectory() as d:
        ff, nf, vf, lf = (os.path.join(d, x) for x in ("fw.bin", "na.bin", "v.bin", "log.csv"))
        fw = S.run(v, a, notes=notes, end=end + 0.01, ramdump=[(end, ff)], extra=[(T_CHANGE, "syx", msg)])
        open(vf, "wb").write(bytes(v + a))
        args_ = [C.CLI, "--rom", P.ROM, "--voice", vf, "--at", str(S.LOAD), "--seconds", str(end + 0.01),
                 "--opzlog", lf, "--ramdump", f"{end}:{nf}", "--param", f"{T_CHANGE}:{g}:{num}:{val}"]
        for tt, b in notes:
            args_ += ["--midi", f"{tt:.5f}:" + " ".join(f"{x:02X}" for x in b)]
        subprocess.run(args_, check=True, capture_output=True)
        fi, ni = open(ff, "rb").read(), open(nf, "rb").read()
        nat = [(float(x.split(",")[0]), int(x.split(",")[1], 16), int(x.split(",")[2], 16))
               for x in open(lf).read().splitlines()[1:]]
    seq = lambda w: [(e[1], e[2]) for e in w if e[0] >= T_CHANGE and not N.PERIODIC(e[1], e[2])
                     and not (0x28 <= e[1] <= 0x37) and not (0x60 <= e[1] <= 0x7F)]
    gw, ew = seq(fw), seq(nat)
    bad = [(hex(x), E.ram(ni, x), E.ram(fi, x)) for base, n in E.STATE for x in range(base, base + n)
           if E.ram(ni, x) != E.ram(fi, x)]
    ok = gw == ew and not bad
    k = next((i for i, (p, q) in enumerate(zip(gw, ew)) if p != q), min(len(gw), len(ew)))
    return name, g, num, val, ok, (k, gw[k:k + 8]), ew[k:k + 8], bad[:6]


if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    names = sys.argv[2:] or [p[0] for p in S.T.PARAMS]
    res = S.pmap(run, [(seed, n) for n in names])
    print(f"set_param: {sum(1 for r in res if r[4])}/{len(res)} exact")
    for r in res:
        if not r[4]:
            print(r[0], r[1], r[2], "->", r[3], "| fw", r[5], "| native", r[6], "| ram", r[7])

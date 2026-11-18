#!/usr/bin/env python3
"""Compare the C++ native engine (txnative-cli) with the firmware on random voices and note sequences:
1. every non-periodic register write (voice load, note on / off) in order
2. the full register image some time after the last event (includes key code and TL refresh)."""
import os
import random
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import study as S
import verify as V
import verify_notes as N

CLI = os.environ.get("TX_NATIVE", "/tmp/claude-0/-home-claude/0880ee1e-15a1-5b3f-a614-450e24afd090/scratchpad/emubuild/txnative-cli")
SKIP_IMAGE = set(range(0x00, 0x08)) | {0x17, 0x19}   # channel volume (BC EG bias), LFO depths: stage 4


def native(v, a, ev, end):
    with tempfile.TemporaryDirectory() as d:
        vf, lf = os.path.join(d, "v.bin"), os.path.join(d, "log.csv")
        open(vf, "wb").write(bytes(v + a))
        args = [CLI, "--rom", S.P.ROM, "--voice", vf, "--at", str(S.LOAD), "--seconds", str(end), "--opzlog", lf]
        for e in ev:
            args += ["--note", f"{e[0]}:on:{e[2]}:{e[3]}" if e[1] == "on" else f"{e[0]}:off:{e[2]}"]
        subprocess.run(args, check=True, capture_output=True)
        out = []
        for ln in open(lf).read().splitlines()[1:]:
            t, r, x = ln.split(",")
            out.append((float(t), int(r, 16), int(x, 16)))
        return out


def events(rng):
    t, held, ev = 8.6, [], []
    for _ in range(rng.randint(5, 30)):
        if held and (rng.random() < 0.4 or len(held) > 10):
            ev.append((t, "off", held.pop(rng.randrange(len(held)))))
        else:
            k = rng.randint(24, 108)
            if k in held:
                continue
            held.append(k)
            ev.append((t, "on", k, rng.randint(1, 127)))
        t += rng.choice((0.002, 0.03, 0.06))
    return ev, t


def check(seed):
    rng = random.Random(seed)
    v, a = V.rand_voice(rng)
    v[58] = rng.randint(0, 1)
    ev, t = events(rng)
    end = t + 0.2
    notes = [(e[0], [0x90, e[2], e[3]] if e[1] == "on" else [0x80, e[2], 0]) for e in ev]
    fw = S.run(v, a, notes=notes, end=end)
    nat = native(v, a, ev, end)
    seq = lambda w: [(r, x) for tt, r, x in w if tt >= S.LOAD - 0.01 and not N.PERIODIC(r, x)]
    bad = []
    g, e = seq(fw), seq(nat)
    if g != e:
        i = next((i for i, (p, q) in enumerate(zip(g, e)) if p != q), min(len(g), len(e)))
        bad.append(("seq", i, g[max(0, i - 2):i + 3], e[max(0, i - 2):i + 3]))
    gi, ei = S.P.image(fw, end), S.P.image(nat, end)
    diff = [(hex(r), gi.get(r), ei.get(r)) for r in sorted(set(gi) | set(ei))
            if r not in SKIP_IMAGE and gi.get(r) != ei.get(r)]
    if diff:
        bad.append(("image", diff[:6]))
    return seed, bad


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    res = S.pmap(check, range(n))
    print(f"C++ engine ok {sum(1 for r in res if not r[1])}/{n}")
    for seed, bad in res:
        if bad:
            print(seed, bad)

#!/usr/bin/env python3
"""Check fwport.py against the firmware: RAM snapshots right before and after one dispatcher phase
(tx81z-emu --pcdump) and the register writes in between (--opzlog --opzpc)."""
import os
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as P
import study as S
import fwport as F

REC = 2 + 8 + 0x100 + 0x2000
PHASES = {"A": (0x8F4A, 0x8F56), "B": (0x8F58, 0x8F78)}
# $7E0C-$7EE7: MIDI receive ring buffer (filled by the serial interrupt)
STACK = 0x7FC0                                 # $7FC0-$7FFF: CPU stack
IRAM_STATE = [0x86, 0x87, 0x91, 0x92]          # internal RAM bytes that are state (the rest is scratch)


def capture(v, a, notes, t0, t1, extra=()):
    """-> list of (addr, time, (iram, xram)), list of (time, reg, val)"""
    ev = [(S.LOAD, "syx", P.aced_dump(a)), (S.LOAD + 0.05, "syx", P.vced_dump(v))]
    ev += [(t, "midi", b) for t, b in notes] + list(extra)
    with tempfile.TemporaryDirectory() as d:
        nv = os.path.join(d, "t.nvram")
        open(nv, "wb").write(open(P.NVRAM, "rb").read())
        log, dump = os.path.join(d, "log.csv"), os.path.join(d, "pc.bin")
        addrs = ",".join(f"{x:04x}" for p in PHASES.values() for x in p)
        args = [P.EMU, "--rom", P.ROM, "--nvram", nv, "--seconds", str(t1 + 0.01), "--quiet",
                "--opzlog", log, "--pcdump", dump, str(t0), str(t1), addrs]
        for k, (t, kind, data) in enumerate(ev):
            if kind == "midi":
                args += ["--midi", f"{t}:" + " ".join(f"{b:02X}" for b in data)]
            else:
                fn = os.path.join(d, f"e{k}.syx")
                open(fn, "wb").write(bytes(data))
                args += ["--syx", f"{t}:{fn}"]
        subprocess.run(args, check=True, capture_output=True)
        raw = open(dump, "rb").read()
        recs = []
        for i in range(0, len(raw), REC):
            r = raw[i:i + REC]
            recs.append((r[0] << 8 | r[1], struct.unpack("<d", r[2:10])[0], (r[10:0x10A], r[0x10A:])))
        w = []
        for ln in open(log).read().splitlines()[1:]:
            t, rg, x = ln.split(",")
            w.append((float(t), int(rg, 16), int(x, 16)))
        return recs, w


def compare(recs, w, phase, fn, limit=50):
    """run fn on every 'before' snapshot of the phase; -> (checked, list of problems)"""
    e0, e1 = PHASES[phase]
    probs, n = [], 0
    for i, (ad, t, img) in enumerate(recs):
        if ad != e0 or i + 1 >= len(recs) or recs[i + 1][0] != e1:
            continue
        t1, after = recs[i + 1][1], recs[i + 1][2]
        m = F.Ram(img)
        fn(m)
        n += 1
        ref = F.Ram(after)
        bad = [(hex(x), m.r(x), ref.r(x)) for x in list(range(0x6000, 0x7E0C)) + list(range(0x7EE8, STACK)) + IRAM_STATE if m.r(x) != ref.r(x)]
        got = [(r, x) for tt, r, x in w if t - 1e-6 <= tt <= t1 + 1e-6]
        if bad or got != m.writes:
            k = next((k for k, (p, q) in enumerate(zip(got, m.writes)) if p != q), min(len(got), len(m.writes)))
            wd = (k, got[k:k + 4], m.writes[k:k + 4]) if got != m.writes else ""
            probs.append((round(t, 4), bad[:8], wd))
            if len(probs) >= limit:
                break
    return n, probs


if __name__ == "__main__":
    v, a = S.voice()
    recs, w = capture(v, a, [(8.6, [0x90, 60, 100])], 8.55, 8.8)
    n, p = compare(recs, w, "B", F.phase_b)
    print("phase B", n, "checked,", len(p), "problems")
    for x in p[:5]:
        print(x)


def random_case(seed):
    """random voice (incl. LFO, controllers, portamento) and a random stream of notes and controllers"""
    import random
    import verify as V
    rng = random.Random(seed)
    NEU = V.NEUTRAL
    V.NEUTRAL = False
    v, a = V.rand_voice(rng)
    V.NEUTRAL = NEU
    v[63] = 0                     # poly (mono is checked separately)
    ev, t, held = [], 8.6, []
    for _ in range(rng.randint(10, 40)):
        r = rng.random()
        if r < 0.35:
            k = rng.randint(24, 108)
            if k not in held:
                held.append(k)
                ev.append((t, [0x90, k, rng.randint(1, 127)]))
        elif r < 0.55 and held:
            ev.append((t, [0x80, held.pop(rng.randrange(len(held))), 0]))
        elif r < 0.7:
            ev.append((t, [0xE0, rng.randint(0, 127), rng.randint(0, 127)]))
        else:
            cc = rng.choice((1, 2, 4, 7, 5, 64, 65))
            ev.append((t, [0xB0, cc, rng.randint(0, 127)]))
        t += rng.choice((0.003, 0.02, 0.05))
    return v, a, ev, t


def run_random(seed):
    v, a, ev, t = random_case(seed)
    recs, w = capture(v, a, ev, 8.55, t + 0.3)
    out = []
    for ph, fn in (("A", F.phase_a), ("B", F.phase_b)):
        n, p = compare(recs, w, ph, fn, limit=3)
        out.append((ph, n, p))
    return seed, out

#!/usr/bin/env python3
"""The C++ engine's timer routines against the firmware: every 'before' RAM snapshot of a dispatcher
phase (random voices and controller streams, as test_port.py) goes through Engine::test_phase; RAM
after and the register writes must equal the firmware's."""
import os
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as P
import study as S
import test_port as T
import verify_cpp as C

RAMN = 0x100 + 0x2000
SKIP = set(range(0x7E0C, 0x7EE8)) | set(range(T.STACK, 0x8000))
# state the single-mode engine does not keep: software LFO ($7836 phase, $776A, $77B6 output) and the
# LFO delay of LFOs 2..8
SKIP |= set(range(0x7836, 0x7846)) | {0x776A} | set(range(0x77B6, 0x77C6)) | set(range(0x7798, 0x77A6)) | set(range(0x77A8, 0x77B6))


def run(seed):
    v, a, ev, t = T.random_case(seed)
    recs, w = T.capture(v, a, ev, 8.55, t + 0.3)
    pairs = []
    for i, (ad, tt, img) in enumerate(recs[:-1]):
        for ph, (e0, e1) in T.PHASES.items():
            if ad == e0 and recs[i + 1][0] == e1:
                pairs.append((ph, tt, recs[i + 1][1], img, recs[i + 1][2]))
    with tempfile.TemporaryDirectory() as d:
        fin, fout = os.path.join(d, "in.bin"), os.path.join(d, "out.bin")
        with open(fin, "wb") as f:
            for ph, _, _, img, _ in pairs:
                f.write(ph.encode() + img[0] + img[1])
        subprocess.run([C.CLI, "--rom", P.ROM, "--voice", "/dev/null", "--phase-test", fin, fout], check=True)
        out = open(fout, "rb").read()
    probs, o = [], 0
    for ph, t0, t1, img, after in pairs:
        ram = out[o:o + RAMN]
        n = struct.unpack("<H", out[o + RAMN:o + RAMN + 2])[0]
        wr = list(zip(out[o + RAMN + 2:o + RAMN + 2 + 2 * n:2], out[o + RAMN + 3:o + RAMN + 2 + 2 * n:2]))
        o += RAMN + 2 + 2 * n
        ref = after[0] + after[1]
        bad = []
        for k in range(RAMN):
            ad = k if k < 0x100 else 0x6000 + k - 0x100
            if ad < 0x100 and ad not in T.IRAM_STATE:
                continue
            if ad in SKIP:
                continue
            if ram[k] != ref[k]:
                bad.append((hex(ad), ram[k], ref[k]))
        got = [(r, x) for tt, r, x in w if t0 - 1e-6 <= tt <= t1 + 1e-6]
        if bad or got != wr:
            probs.append((ph, round(t0, 4), bad[:5], "writes differ" if got != wr else ""))
    return seed, len(pairs), probs


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    res = S.pmap(run, range(200, 200 + n), n=2)
    tot = sum(r[1] for r in res)
    bad = sum(len(r[2]) for r in res)
    print(f"C++ timer routines: {tot - bad}/{tot} phases exact")
    for seed, k, p in res:
        for x in p[:3]:
            print(seed, x)

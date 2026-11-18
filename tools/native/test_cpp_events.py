#!/usr/bin/env python3
"""The C++ engine's event handling (voice load, notes, sustain, controllers, bend, portamento time)
against the firmware: RAM state that does not depend on the timer phase, 0.3 s after the last event of
a random stream; plus the voice load / note on / note off register writes in order."""
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as P
import study as S
import test_port as T
import verify_cpp as C
import verify_notes as N

STATE = [(0x7D53, 1), (0x7D5B, 1), (0x7D63, 1), (0x7D6B, 1), (0x7D83, 1), (0x41, 1), (0x49, 1),
         (0x77C6, 8), (0x6A67, 110), (0x7CE3, 16), (0x7CF3, 16), (0x7F29, 2), (0x7913, 8), (0x7853, 32),
         (0x7D13, 8), (0x7D1B, 8), (0x7D33, 8), (0x7D3B, 24), (0x7D7B, 8), (0x780E, 16), (0x781E, 16),
         (0x782E, 8), (0x7923, 8), (0x792B, 8), (0x791B, 8), (0x86, 2), (0x7943, 0x74 * 8),
         (0x7F01, 1), (0x7F39, 16), (0x5B, 1), (0x63, 1)]


def ram(img, ad):
    return img[ad] if ad < 0x100 else img[0x100 + ad - 0x6000]


def run(seed):
    v, a, ev, t = T.random_case(seed)
    end = t + 0.3
    with tempfile.TemporaryDirectory() as d:
        ff, nf, vf, lf = (os.path.join(d, x) for x in ("fw.bin", "na.bin", "v.bin", "log.csv"))
        fw = S.run(v, a, notes=ev, end=end + 0.01, ramdump=[(end, ff)])
        open(vf, "wb").write(bytes(v + a))
        args = [C.CLI, "--rom", P.ROM, "--voice", vf, "--at", str(S.LOAD), "--seconds", str(end + 0.01),
                "--opzlog", lf, "--ramdump", f"{end}:{nf}"]
        for tt, b in ev:
            args += ["--midi", f"{tt:.5f}:" + " ".join(f"{x:02X}" for x in b)]
        subprocess.run(args, check=True, capture_output=True)
        fi, ni = open(ff, "rb").read(), open(nf, "rb").read()
        nat = [(float(x.split(",")[0]), int(x.split(",")[1], 16), int(x.split(",")[2], 16))
               for x in open(lf).read().splitlines()[1:]]
    bad = [(hex(x), ram(ni, x), ram(fi, x)) for base, n in STATE for x in range(base, base + n)
           if ram(ni, x) != ram(fi, x)]
    skip = lambda r, x: N.PERIODIC(r, x)
    seq = lambda w: [(e[1], e[2]) for e in w if e[0] >= S.LOAD - 0.01 and not skip(e[1], e[2])
                     and not (0x28 <= e[1] <= 0x37) and not (0x60 <= e[1] <= 0x7F)]
    g, e = seq(fw), seq(nat)
    sd = None
    if g != e:
        k = next((i for i, (p, q) in enumerate(zip(g, e)) if p != q), min(len(g), len(e)))
        sd = (k, g[k:k + 4], e[k:k + 4])
    return seed, bad[:10], sd


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    res = S.pmap(run, range(300, 300 + n), n=2)
    print(f"C++ events: {sum(1 for r in res if not r[1] and not r[2])}/{n} exact")
    for r in res:
        if r[1] or r[2]:
            print(r)

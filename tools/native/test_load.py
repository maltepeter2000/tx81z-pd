#!/usr/bin/env python3
"""Static RAM after a voice load: firmware snapshot vs fwport.load_voice on top of power_on."""
import os
import random
import sys
import tempfile
import study as S
import fwport as F
import verify as V

CHECK = [(0x7913, 8), (0x7D3B, 8), (0x7D43, 8), (0x7D4B, 8), (0x7D7B, 8), (0x780E, 16), (0x781E, 16),
         (0x782E, 8), (0x77C6, 8), (0x7923, 8), (0x792B, 8), (0x7D1B, 8), (0x7D33, 8), (0x791B, 8),
         (0x7853, 32), (0x7D13, 8), (0x7852, 1), (0x7846, 13), (0x793B, 8), (0x7933, 8), (0x7EF1, 1),
         (0x7EF9, 1), (0xF6, 1), (0x7D53, 1), (0x7D5B, 1), (0x7D63, 1), (0x7D6B, 1), (0x7D83, 1),
         (0x41, 1), (0x49, 1), (0x86, 2), (F.VOICE, 110)]


def check(seed):
    rng = random.Random(seed)
    NEU = V.NEUTRAL
    V.NEUTRAL = False
    v, a = V.rand_voice(rng)
    V.NEUTRAL = NEU
    with tempfile.TemporaryDirectory() as d:
        fn = os.path.join(d, "r.bin")
        S.run(v, a, end=8.95, ramdump=[(8.9, fn)])
        img = open(fn, "rb").read()
    fw = F.Ram((img[:0x100], img[0x100:]))
    m = F.Ram()
    F.power_on(m)
    F.load_voice(m, v, a)
    bad = []
    for base, n in CHECK:
        for x in range(base, base + n):
            if m.r(x) != fw.r(x):
                bad.append((hex(x), m.r(x), fw.r(x)))
    return seed, bad


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    res = S.pmap(check, range(n))
    print("load RAM ok", sum(1 for r in res if not r[1]), "/", n)
    for s, b in res:
        if b:
            print(s, b[:8])

#!/usr/bin/env python3
"""Which RAM bytes does a voice parameter or a MIDI message change? (study helper)"""
import sys
import ramstudy as R
import study as S

NOTE = [(8.6, [0x90, 60, 100])]


def snap(kw=None, extra=()):
    rd, _ = R.snap(kw or {}, NOTE + list(extra), at=8.9, end=8.95)
    return rd(0x00, 0x100), rd(0x6000, 0x2000)


def diff(a, b, ignore=()):
    out = []
    for base, x, y in ((0, a[0], b[0]), (0x6000, a[1], b[1])):
        for i, (p, q) in enumerate(zip(x, y)):
            ad = base + i
            if p != q and not any(lo <= ad < hi for lo, hi in ignore):
                out.append((ad, p, q))
    return out


def fmt(d):
    return " ".join(f"{a:04x}:{p:02x}>{q:02x}" for a, p, q in d)


if __name__ == "__main__":
    base = snap()
    base2 = snap()
    noise = {a for a, _, _ in diff(base, base2)}
    print("noise", sorted(hex(a) for a in noise))

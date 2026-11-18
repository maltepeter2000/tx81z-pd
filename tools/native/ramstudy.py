#!/usr/bin/env python3
"""RAM snapshots of the firmware after loading a voice and playing notes (study helper)."""
import os
import sys
import tempfile
sys.path.insert(0, os.path.dirname(__file__))
import study as S


def snap(kw=None, notes=(), at=8.75, end=8.8):
    v, a = S.voice(**(kw or {}))
    with tempfile.TemporaryDirectory() as d:
        fn = os.path.join(d, "ram.bin")
        w = S.run(v, a, notes=notes, end=end, ramdump=[(at, fn)])
        m = open(fn, "rb").read()

    def rd(addr, n=1):
        off = addr if addr < 0x100 else 0x100 + addr - 0x6000
        return list(m[off:off + n])
    return rd, w

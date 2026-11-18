#!/usr/bin/env python3
"""Helpers for the oracle studies: load a voice (bulk dump), optionally play notes, return the
ordered register writes after the load and the register image at a time."""
import sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, "/home/claude/tx81z-pd/tools")
import probe as P
import tx81z_params as T

base = [int(float(x)) for x in open("/home/claude/tx81z-pd/tools/native/basevoice.txt").read().split()[1:]]
V0, A0 = base[:93], base[93:]
LOAD = 8.0


def voice(**kw):
    v, a = list(V0), list(A0)
    for name, val in kw.items():
        nm, g, num = T.BY_NAME[name][:3]
        (v if g == "V" else a)[num] = val
    return v, a


def run(v, a, notes=(), end=9.0, extra=()):
    ev = [(LOAD, "syx", P.aced_dump(a)), (LOAD + 0.05, "syx", P.vced_dump(v))]
    for t, b in notes:
        ev.append((t, "midi", b))
    ev += list(extra)
    return P.probe(ev, end)


def load_writes(w, t0=LOAD, t1=LOAD + 0.45):
    """per register: list of values written while loading (periodic registers excluded)"""
    out = {}
    for t, r, x in w:
        if t0 <= t < t1 and not (0x28 <= r <= 0x37 or 0x60 <= r <= 0x7F or r in (0x17, 0x19)):
            out.setdefault(r, []).append(x)
    return out


def pmap(f, jobs, n=8):
    with ThreadPoolExecutor(n) as ex:
        return list(ex.map(f, jobs))

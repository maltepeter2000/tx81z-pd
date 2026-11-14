#!/usr/bin/env python3
"""Minimal Pd patch writer used by tools/ctlayer.py (same as in the PDoly800 repository)."""

FONT = 10


def esc(t):
    return t.replace(",", " \\,").replace(";", " \\;").replace("$", "\\$")


class P:
    def __init__(s, name=None, w=700, h=500):
        s.o, s.c, s.name, s.w, s.h = [], [], name, w, h

    def add(s, txt, x, y, kind="obj"):
        s.o.append((kind, x, y, txt))
        return len(s.o) - 1

    def msg(s, txt, x, y):
        return s.add(txt, x, y, "msg")

    def note(s, txt, x, y):
        return s.add(txt, x, y, "text")

    def sub(s, p, x, y):
        s.o.append(("sub", x, y, p))
        return len(s.o) - 1

    def con(s, a, o, b, i):
        s.c.append((a, o, b, i))

    def lines(s, top=False):
        L = [f"#N canvas 0 50 {s.w} {s.h} {FONT};" if top else f"#N canvas 0 50 {s.w} {s.h} {s.name} 0;"]
        for k, x, y, t in s.o:
            if k == "sub":
                L += t.lines()
                L.append(f"#X restore {x} {y} pd {t.name};")
            elif k == "text":
                L.append(f"#X text {x} {y} {esc(t)}, f {min(max(len(t), 10), 100)};")
            else:
                L.append(f"#X {k} {x} {y} {esc(t)};")
        for a, o, b, i in s.c:
            L.append(f"#X connect {a} {o} {b} {i};")
        return L


def table_data(rows):
    return "#A set " + " \\; ".join(rows) + " \\;;"

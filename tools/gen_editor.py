#!/usr/bin/env python3
"""Generates pd/txedit.pd: an editor for tx81z~ in rows of 8, row k = CC page k of the
controller layer (pattern of p8edit.pd in the PDoly800 repository).

usage: python3 tools/gen_editor.py [outfile]   (default: pd/txedit.pd)

[txedit <bus>]  e.g. [txedit $0-tx81z] in the help patch
  sends    n <name> <0-127>, pgm <n>, dumpreq, ninfo ...   to [s <bus>]  (the [r <bus>] in front of tx81z~)
  listens  info / voice / lcd                               on [r <bus>-out]  (outlet 3 of tx81z~)
tx81z~ reports every change itself (controller, program change, messages, voice dumps of the
firmware). At load and with the refresh button the editor asks for the edit buffer and all values.
Every cell: name and CC number, label, slider and number box 0-127 (as from a pot), the value
as on the TX81Z. Ranges, labels and rows come from tools/tx81z_params.py, so editor,
controller layer and help always agree.
GUI on top (no wires: send / receive names), the logic in [pd logic].
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ctlayer                                    # noqa: E402
import tx81z_params as T                          # noqa: E402

BS = chr(92)
D, SP, SEMI, COMMA = BS + "$", BS + " ", BS + ";", BS + ","

CC = ctlayer.cc_map(T.CC_ROWS)                    # name -> CC number
ROWS = [(f"PAGE {k + 1}  {t}", list(ns)) for k, (t, ns) in enumerate(T.CC_ROWS)] + list(T.EXTRA_ROWS)
LEFT, RIGHT = ROWS[:7], ROWS[7:]
ALL = [n for _, ns in ROWS for n in ns if n]
assert len(ALL) == len(set(ALL)) == len(T.PARAMS)

CW, ROWH, X0 = 80, 88, 10
BAND_Y = 44
TOP = BAND_Y + 96
X1 = X0 + 8 * CW + 30
BAND = ("#ececec", "#dcdcdc")


def label(nm):
    return T.BY_NAME[nm][6]


def cc_text(ns):
    ccs = [CC[n] for n in ns if n in CC]
    if not ccs:
        return "no CC (mouse only)"
    return f"CC {min(ccs)}-{max(ccs)}" if max(ccs) - min(ccs) + 1 == len(ccs) else "CC " + " ".join(map(str, ccs))


class Canvas:
    def __init__(self):
        self.lines, self.conns = [], []

    def add(self, t):
        self.lines.append(t)
        return len(self.lines) - 1

    def conn(self, a, ao, b, bi):
        self.conns.append(f"#X connect {a} {ao} {b} {bi};")


def bng(c, x, y, send, text):
    c.add(f"#X obj {x} {y} bng 17 250 50 0 {D}0-{send} empty {text.replace(' ', SP)} 21 9 0 10 #fcfcfc #000000 #000000;")


def top_band(c):
    """voice name and LCD of the TX81Z, program change, refresh"""
    y = BAND_Y
    c.add(f"#X obj {X0 - 4} {y} cnv 15 {8 * CW} 86 empty empty VOICE 4 8 0 10 #e4ecf4 #404040 0;")
    c.add(f"#X text {X0} {y + 20} name;")
    c.add(f"#X symbolatom {X0 + 40} {y + 20} 12 0 0 0 - {D}0-voice -;")
    c.add(f"#X text {X0 + 170} {y + 20} bank;")
    c.add(f"#X obj {X0 + 206} {y + 20} hradio 17 1 0 4 {D}0-bank {D}0-bankv empty 0 -8 0 10 #fcfcfc #000000 #000000 0;")
    c.add(f"#X text {X0 + 206} {y + 40} I{SP}{SP}A{SP}{SP}B{SP}{SP}C;")
    c.add(f"#X text {X0 + 286} {y + 20} patch;")
    c.add(f"#X floatatom {X0 + 326} {y + 20} 3 1 32 0 - {D}0-patchv {D}0-patch;")
    c.add(f"#X text {X0 + 366} {y + 20} = program;")
    c.add(f"#X floatatom {X0 + 436} {y + 20} 3 0 0 0 - {D}0-prog -;")
    bng(c, X0 + 8 * CW - 110, y + 20, "refresh", "refresh")
    c.add(f"#X text {X0} {y + 60} bank and patch send a program change (I01-C32 = 0-127) {COMMA} the firmware "
          f"answers with its voice dump and every cell follows., f 104;")
    x1 = X1
    c.add(f"#X obj {x1 - 4} {y} cnv 15 {8 * CW} 86 empty empty TX81Z{SP}LCD 4 8 0 10 #f4ece4 #404040 0;")
    c.add(f"#X symbolatom {x1} {y + 20} 20 0 0 0 - {D}0-lcd0 -;")
    c.add(f"#X symbolatom {x1} {y + 42} 20 0 0 0 - {D}0-lcd1 -;")
    c.add(f"#X text {x1 + 200} {y + 20} the display of the emulated TX81Z. Changes from the editor go to the "
          f"edit buffer: STORE on the TX81Z keeps them., f 60;")


def gui(c):
    c.add(f"#X text {X0} 6 tx81z~ editor: rows of 8 as the pages of the controller (MIDI channel of [txcc]). Slider and number:"
          f" the value as from a pot (0-127) {COMMA} the box right of it: the value as on the TX81Z., f 200;")
    c.add(f"#X text {X0} 22 [txedit <bus>] sends to <bus> and reads outlet 3 of tx81z~ from <bus>-out: every change"
          f" (controller {COMMA} program change {COMMA} messages) shows up here. Page k on the controller = row k., f 200;")
    top_band(c)
    for x0, rows in ((X0, LEFT), (X1, RIGHT)):
        for r, (title, ns) in enumerate(rows):
            y = TOP + r * ROWH
            lbl = f"{title}  {cc_text(ns)}".replace(" ", SP)
            c.add(f"#X obj {x0 - 4} {y} cnv 15 {8 * CW} {ROWH - 6} empty empty {lbl} 4 8 0 10 {BAND[r % 2]} #404040 0;")
            for k, nm in enumerate(ns):
                if not nm:
                    continue
                x, yc = x0 + k * CW, y + 18
                head = f"{nm} {CC[nm]}" if nm in CC else nm
                c.add(f"#X text {x} {yc} {head};")
                c.add(f"#X text {x} {yc + 12} {label(nm).replace('/', '/')};")
                c.add(f"#X obj {x} {yc + 30} hsl 70 12 0 127 0 0 {D}0-e-{nm} {D}0-v-{nm} empty -2 -8 0 10"
                      f" #fcfcfc #000000 #000000 0 1;")
                c.add(f"#X floatatom {x} {yc + 46} 3 0 127 0 - {D}0-v-{nm} {D}0-e-{nm};")
                c.add(f"#X symbolatom {x + 26} {yc + 46} 7 0 0 0 - {D}0-t-{nm} -;")


def logic():
    """the [pd logic] subpatch: cells -> bus, program change, refresh, replies -> cells"""
    c = Canvas()
    c.add("#X text 10 10 cells -> n <name> <value> -> bus (tx81z~ answers with info on <bus>-out), f 60;")
    for i, nm in enumerate(ALL):
        col, row = i % 8, i // 8
        x, y = 10 + col * 150, 40 + row * 100
        r = c.add(f"#X obj {x} {y} r {D}0-e-{nm};")
        m = c.add(f"#X msg {x} {y + 30} n {nm} {D}1;")
        s = c.add(f"#X obj {x} {y + 60} s {D}1;")
        c.conn(r, 0, m, 0); c.conn(m, 0, s, 0)
    y = 40 + ((len(ALL) + 7) // 8) * 100 + 20
    # start and refresh: the edit buffer from the firmware, then all values
    lb = c.add(f"#X obj 400 {y} loadbang;")
    rr = c.add(f"#X obj 500 {y} r {D}0-refresh;")
    tb = c.add(f"#X obj 500 {y + 30} t b b;"); c.conn(rr, 0, tb, 0)
    dq = c.add(f"#X msg 560 {y + 60} dumpreq;"); c.conn(tb, 1, dq, 0)
    sq = c.add(f"#X obj 560 {y + 90} s {D}1;"); c.conn(dq, 0, sq, 0)
    dl = c.add(f"#X obj 400 {y + 60} del 800;")
    c.conn(lb, 0, dl, 0); c.conn(tb, 0, dl, 0)
    allm = c.add(f"#X msg 400 {y + 120} " + f" {COMMA} ".join(f"ninfo {n}" for n in ALL) + ", f 70;")
    c.conn(dl, 0, allm, 0)
    s2 = c.add(f"#X obj 400 {y + 330} s {D}1;")
    c.conn(allm, 0, s2, 0)
    # program change: bank (I A B C) * 32 + patch - 1
    yp = y + 400
    rb = c.add(f"#X obj 10 {yp} r {D}0-bank;")
    rp = c.add(f"#X obj 150 {yp} r {D}0-patch;")
    clp = c.add(f"#X obj 150 {yp + 30} clip 1 32;"); c.conn(rp, 0, clp, 0)
    m1 = c.add(f"#X obj 150 {yp + 60} - 1;"); c.conn(clp, 0, m1, 0)
    tff = c.add(f"#X obj 150 {yp + 90} t f f;"); c.conn(m1, 0, tff, 0)
    st = c.add(f"#X obj 90 {yp + 120} f;"); c.conn(tff, 1, st, 1)
    mul = c.add(f"#X obj 10 {yp + 30} * 32;"); c.conn(rb, 0, mul, 0)
    tbf = c.add(f"#X obj 10 {yp + 60} t b f;"); c.conn(mul, 0, tbf, 0)
    pl = c.add(f"#X obj 150 {yp + 150} +;")
    c.conn(tbf, 1, pl, 1); c.conn(tbf, 0, st, 0); c.conn(st, 0, pl, 0); c.conn(tff, 0, pl, 0)
    tpp = c.add(f"#X obj 150 {yp + 180} t f f;"); c.conn(pl, 0, tpp, 0)
    mp = c.add(f"#X msg 150 {yp + 210} pgm {D}1;"); c.conn(tpp, 0, mp, 0)
    sp = c.add(f"#X obj 150 {yp + 240} s {D}1;"); c.conn(mp, 0, sp, 0)
    ms = c.add(f"#X msg 250 {yp + 210} set {D}1;"); c.conn(tpp, 1, ms, 0)
    sps = c.add(f"#X obj 250 {yp + 240} s {D}0-prog;"); c.conn(ms, 0, sps, 0)
    lbp = c.add(f"#X obj 400 {yp} loadbang;")
    f0 = c.add(f"#X obj 400 {yp + 30} f {D}0;"); c.conn(lbp, 0, f0, 0)
    sb = c.add(f"#X msg 400 {yp + 60} {SEMI} {D}1-bankv set 0 {SEMI} {D}1-patchv set 1;"); c.conn(f0, 0, sb, 0)
    # replies -> cells, voice name, LCD
    y2 = yp + 300
    ro = c.add(f"#X obj 10 {y2} r {D}1-out;")
    ri = c.add(f"#X obj 10 {y2 + 30} route info voice lcd;"); c.conn(ro, 0, ri, 0)
    lp = c.add(f"#X obj 10 {y2 + 60} list prepend {D}0;")   # route info gives the list: name 0-127 text
    stm = c.add(f"#X msg 10 {y2 + 90} {SEMI} {D}1-v-{D}2 set {D}3 {SEMI} {D}1-t-{D}2 set {D}4;")
    c.conn(ri, 0, lp, 0); c.conn(lp, 0, stm, 0)
    lv = c.add(f"#X obj 300 {y2 + 60} list;"); c.conn(ri, 1, lv, 0)
    mv = c.add(f"#X msg 300 {y2 + 90} set {D}1;"); c.conn(lv, 0, mv, 0)
    sv = c.add(f"#X obj 300 {y2 + 120} s {D}0-voice;"); c.conn(mv, 0, sv, 0)
    ll = c.add(f"#X obj 450 {y2 + 60} list prepend {D}0;"); c.conn(ri, 2, ll, 0)
    ml = c.add(f"#X msg 450 {y2 + 90} {SEMI} {D}1-lcd0 set {D}2 {SEMI} {D}1-lcd1 set {D}3;"); c.conn(ll, 0, ml, 0)
    c.add(f"#X text 650 {y2 + 30} info <name> <0-127> <value text> -> slider / number and text of the cell, f 50;")
    return c


def build():
    c = Canvas()
    gui(c)
    lg = logic()
    rows = max(len(LEFT), len(RIGHT))
    sub = ["#N canvas 40 40 1260 900 logic 0;"] + lg.lines + lg.conns + [f"#X restore {X0} {TOP + rows * ROWH + 4} pd logic;"]
    h = TOP + rows * ROWH + 34
    return "\n".join([f"#N canvas 20 20 {X1 + 8 * CW + 10} {h} 10;"] + c.lines + sub + c.conns) + "\n"


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "pd", "txedit.pd")
    open(out, "w").write(build())
    print("wrote", out, f"({len(ALL)} settings)")

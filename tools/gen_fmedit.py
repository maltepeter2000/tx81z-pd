#!/usr/bin/env python3
"""Generates pd/txfm.pd: the FM view of tx81zn~ (pd/txfm.h) as an editor with 7 pages of 8 controls.

usage: python3 tools/gen_fmedit.py [outfile]   (default: pd/txfm.pd)

[txfm <bus>]  e.g. [txfm tx1] next to [r tx1] -> [tx81zn~ <rom> <nvram>] -> outlet 2 -> [s tx1-out]
  sends    fm <control> <0-127>, bank <I|A|B|C|D> <n>, random <kind>, dumpreq   to [s <bus>]
  listens  fm <control> <0-127> <text>, voice <name>                          on [r <bus>-out]
The algorithm gives the operators roles (C carriers, A the modulator of the main carrier, B the
other modulators); every control of a role changes all of its operators. The controls and pages
come from pd/txfm.h, so editor and external always agree. [txedit <bus>] on the same bus shows
every single parameter.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from gen_editor import Canvas, bng, D, SP, SEMI, COMMA   # noqa: E402

RANDOM = ["pluck", "bass", "lead", "brass", "pad", "strings", "total"]
BANKS = "IABCD"
# manual algorithms (operator numbers) and the roles of txfm.h
# (escaped spaces in #A data hang Pd 0.54: one atom per field)
ALGS = [
    ("4>3>2>1", "C:1", "A:2", "B:3+4"),
    ("(4+3)>2>1", "C:1", "A:2", "B:3+4"),
    ("(4+(3>2))>1", "C:1", "A:2", "B:3+4"),
    ("((4>3)+2)>1", "C:1", "A:2", "B:3+4"),
    ("(4>3)+(2>1)", "C:1+3", "A:2", "B:4"),
    ("4>(3+2+1)", "C:1+2+3", "A:4", "B:-"),
    ("(4>3)+2+1", "C:1+2+3", "A:4", "B:-"),
    ("4+3+2+1", "C:1+2+3+4", "A:-", "B:-"),
]


def macros():
    """(name, label, page) in the order of MACROS[] in pd/txfm.h"""
    src = open(os.path.join(ROOT, "pd", "txfm.h")).read()
    env = re.search(r"#define TXFM_ENV\(pg, r, R\)(.*?)\n\n", src, re.S).group(1)
    env = re.findall(r'\{ "(\w+)" R, "([^"]+)"', env)
    body = re.search(r"static const t_macro MACROS\[\] = \{(.*?)\n\};", src, re.S).group(1)
    out = []
    for line in body.splitlines():
        m = re.match(r'\s*\{ "(\w+)", "([^"]+)", (\d+),', line)
        if m:
            out.append((m.group(1), m.group(2), int(m.group(3))))
            continue
        m = re.match(r'\s*TXFM_ENV\((\d+), \w+, "(\w)"\)', line)
        if m:
            out += [(n + m.group(2), lbl, int(m.group(1))) for n, lbl in env]
    pages = re.search(r"PAGES\[\d+\] = \{([^}]*)\}", src).group(1)
    return out, re.findall(r'"([^"]+)"', pages)


MACROS, PAGES = macros()
assert len(MACROS) == len({m[0] for m in MACROS}) == 8 * len(PAGES)

CW, ROWH, X0 = 84, 80, 10
BAND_Y = 40
TOP = BAND_Y + 84
BAND = ("#e8eef6", "#dde5f0")


def top_band(c):
    y = BAND_Y
    c.add(f"#X obj {X0 - 4} {y} cnv 15 {8 * CW} 76 empty empty VOICE 4 8 0 10 #f2ece2 #404040 0;")
    c.add(f"#X text {X0} {y + 18} name;")
    c.add(f"#X symbolatom {X0 + 40} {y + 18} 12 0 0 0 - {D}0-voice -;")
    c.add(f"#X text {X0 + 170} {y + 18} bank;")
    c.add(f"#X obj {X0 + 206} {y + 18} hradio 17 1 0 5 {D}0-bank {D}0-bankv empty 0 -8 0 10 #fcfcfc #000000 #000000 0;")
    for k, b in enumerate(BANKS):
        c.add(f"#X text {X0 + 210 + 17 * k} {y + 2} {b};")
    c.add(f"#X text {X0 + 304} {y + 18} patch;")
    c.add(f"#X floatatom {X0 + 344} {y + 18} 3 1 32 0 - {D}0-patchv {D}0-patch;")
    bng(c, X0 + 8 * CW - 90, y + 18, "refresh", "refresh")
    c.add(f"#X text {X0} {y + 42} algorithm;")
    c.add(f"#X symbolatom {X0 + 70} {y + 42} 14 0 0 0 - {D}0-alg -;")
    for k in range(3):
        c.add(f"#X symbolatom {X0 + 184 + k * 50} {y + 42} 6 0 0 0 - {D}0-role{k} -;")
    c.add(f"#X text {X0 + 340} {y + 42} random;")
    for k, cat in enumerate(RANDOM[:6]):
        bng(c, X0 + 384 + (k % 3) * 100, y + 34 + (k // 3) * 20, f"rnd-{cat}", cat)


README = [
    "tx81zn~ FM editor",
    "[txfm <bus>] sends to [s <bus>] (the [r <bus>] in front of tx81zn~) and reads outlet 2 of tx81zn~ "
    "from [r <bus>-out]. In your own patch: [r tx1] -> [tx81zn~ <rom> <nvram>] -> outlet 2 -> [s tx1-out] "
    "and [txfm tx1]. [txedit tx1] on the same bus shows every single parameter.",
    "The algorithm gives the operators roles: C = the carriers (what you hear), A = the modulator of the "
    "main carrier OP1, B = the other modulators. A control of a role changes all of its operators and "
    "shows the value of the first one. Roles of the 8 algorithms (manual numbering, > = modulates): "
    "1-4 C:1 A:2 B:3+4, 5 C:1+3 A:2 B:4, 6 and 7 C:1+2+3 A:4, 8 C:1+2+3+4. A control without an "
    "operator in its role shows - and does nothing.",
    "SYN 1: algorithm, ratio of C / A / B (frequency ratio, fine on SYN 2), mod A / B = output level of "
    "the modulators (brightness), feedback of OP4, mix = carriers against each other (only with "
    "several carriers: left the main carrier OP1, right the others).",
    "SYN 2: waveform W1-W8 per role, detune (spreads all operators around the centre), fine ratio per "
    "role, level C = output level of the carriers.",
    "ENV C / A / B: envelope of the role (C = the amplitude, A and B = the brightness): attack, decay, "
    "sustain level, fade (decay 2 while the key is held), release, velocity sensitivity, level and rate "
    "scaling along the keyboard.",
    "LFO: speed, wave, delay, pitch and amp depth, pitch and amp sensitivity, key sync. VOICE: poly / "
    "mono, bend range, portamento mode and time, transpose, LFO amp modulation on / off per role.",
    "Changing the algorithm keeps the operator settings: an operator that becomes a carrier sounds with "
    "its output level (level C / mix).",
]


def readme(c):
    body = (" " + SEMI + " " + SEMI + " ").join(t.replace(",", " " + COMMA).replace(";", SEMI) for t in README)
    c.lines.append("#N canvas 60 60 620 520 readme 0;")
    c.lines.append(f"#X text 10 10 {body}, f 92;")
    c.add(f"#X restore {X0} 12 pd readme;")


def gui(c):
    readme(c)
    c.add(f"#X text {X0 + 90} 14 tx81zn~ FM editor - open [pd readme] for how it works;")
    top_band(c)
    for p, title in enumerate(PAGES):
        y = TOP + p * ROWH
        c.add(f"#X obj {X0 - 4} {y} cnv 15 {8 * CW} {ROWH - 6} empty empty {title.replace(' ', SP)} 4 8 0 10 "
              f"{BAND[p % 2]} #404040 0;")
        for k, (nm, lbl, _) in enumerate(m for m in MACROS if m[2] == p):
            x, yc = X0 + k * CW, y + 18
            c.add(f"#X text {x} {yc} {lbl};")
            c.add(f"#X obj {x} {yc + 18} hsl 74 12 0 127 0 0 {D}0-e-{nm} {D}0-v-{nm} empty -2 -8 0 10"
                  f" #fcfcfc #000000 #000000 0 1;")
            c.add(f"#X floatatom {x} {yc + 34} 3 0 127 0 - {D}0-v-{nm} {D}0-e-{nm};")
            c.add(f"#X symbolatom {x + 26} {yc + 34} 8 0 0 0 - {D}0-t-{nm} -;")


def logic():
    c = Canvas()
    c.add("#X text 10 10 controls -> fm <name> <0-127> -> bus (tx81zn~ answers with fm on <bus>-out), f 60;")
    for i, (nm, _, _) in enumerate(MACROS):
        x, y = 10 + (i % 8) * 140, 40 + (i // 8) * 100
        r = c.add(f"#X obj {x} {y} r {D}0-e-{nm};")
        m = c.add(f"#X msg {x} {y + 30} fm {nm} {D}1;")
        s = c.add(f"#X obj {x} {y + 60} s {D}1;")
        c.conn(r, 0, m, 0); c.conn(m, 0, s, 0)
    y = 40 + ((len(MACROS) + 7) // 8) * 100 + 20
    # start and refresh: every value again
    lb = c.add(f"#X obj 10 {y} loadbang;")
    rr = c.add(f"#X obj 100 {y} r {D}0-refresh;")
    dl = c.add(f"#X obj 10 {y + 30} del 300;"); c.conn(lb, 0, dl, 0)
    dq = c.add(f"#X msg 10 {y + 60} dumpreq;"); c.conn(dl, 0, dq, 0); c.conn(rr, 0, dq, 0)
    sq = c.add(f"#X obj 10 {y + 90} s {D}1;"); c.conn(dq, 0, sq, 0)
    # voice selection: bank <I|A|B|C|D> <patch>
    yp = y + 140
    rb = c.add(f"#X obj 10 {yp} r {D}0-bank;")
    sel = c.add(f"#X obj 10 {yp + 30} sel 0 1 2 3 4;"); c.conn(rb, 0, sel, 0)
    sb = c.add(f"#X obj 10 {yp + 90} symbol;")
    for k, b in enumerate(BANKS):
        mb = c.add(f"#X msg {10 + k * 50} {yp + 60} symbol {b};"); c.conn(sel, k, mb, 0); c.conn(mb, 0, sb, 0)
    rp = c.add(f"#X obj 300 {yp} r {D}0-patch;")
    clp = c.add(f"#X obj 300 {yp + 30} clip 1 32;"); c.conn(rp, 0, clp, 0)
    tb = c.add(f"#X obj 300 {yp + 60} t b f;"); c.conn(clp, 0, tb, 0)
    pk = c.add(f"#X obj 10 {yp + 150} pack s 1;"); c.conn(sb, 0, pk, 0); c.conn(tb, 1, pk, 1)
    c.conn(tb, 0, sb, 0)
    mbk = c.add(f"#X msg 10 {yp + 180} bank {D}1 {D}2;"); c.conn(pk, 0, mbk, 0)
    sbk = c.add(f"#X obj 10 {yp + 210} s {D}1;"); c.conn(mbk, 0, sbk, 0)
    # bank and patch boxes start at I 1
    lbs = c.add(f"#X obj 300 {yp + 100} loadbang;")
    sI = c.add(f"#X msg 300 {yp + 130} symbol I;"); c.conn(lbs, 0, sI, 0); c.conn(sI, 0, sb, 1)
    f0 = c.add(f"#X obj 400 {yp + 130} f {D}0;"); c.conn(lbs, 0, f0, 0)
    ini = c.add(f"#X msg 400 {yp + 160} {SEMI} {D}1-bankv set 0 {SEMI} {D}1-patchv set 1;"); c.conn(f0, 0, ini, 0)
    # random voices
    for k, cat in enumerate(RANDOM[:6]):
        r_ = c.add(f"#X obj {450 + k * 110} {yp} r {D}0-rnd-{cat};")
        m_ = c.add(f"#X msg {450 + k * 110} {yp + 30} random {cat};"); c.conn(r_, 0, m_, 0)
        s_ = c.add(f"#X obj {450 + k * 110} {yp + 60} s {D}1;"); c.conn(m_, 0, s_, 0)
    # replies -> controls, voice name, algorithm
    y2 = yp + 260
    ro = c.add(f"#X obj 10 {y2} r {D}1-out;")
    ri = c.add(f"#X obj 10 {y2 + 30} route fm voice;"); c.conn(ro, 0, ri, 0)
    lp = c.add(f"#X obj 10 {y2 + 60} list prepend {D}0;")
    stm = c.add(f"#X msg 10 {y2 + 90} {SEMI} {D}1-v-{D}2 set {D}3 {SEMI} {D}1-t-{D}2 set {D}4;")
    c.conn(ri, 0, lp, 0); c.conn(lp, 0, stm, 0)
    lv = c.add(f"#X obj 300 {y2 + 60} list;"); c.conn(ri, 1, lv, 0)
    mv = c.add(f"#X msg 300 {y2 + 90} set {D}1;"); c.conn(lv, 0, mv, 0)
    sv = c.add(f"#X obj 300 {y2 + 120} s {D}0-voice;"); c.conn(mv, 0, sv, 0)
    # algorithm: 0-127 -> 0-7 -> graph and roles
    ra = c.add(f"#X obj 500 {y2 + 60} route algo;"); c.conn(ri, 0, ra, 0)
    ua = c.add(f"#X obj 500 {y2 + 90} unpack f s;"); c.conn(ra, 0, ua, 0)
    ex = c.add(f"#X obj 500 {y2 + 120} expr int($f1 * 7 / 127 + 0.5);"); c.conn(ua, 0, ex, 0)
    tg = c.add(f"#X obj 500 {y2 + 150} text get {D}0-algs;"); c.conn(ex, 0, tg, 0)
    ul = c.add(f"#X obj 500 {y2 + 180} unpack s s s s;"); c.conn(tg, 0, ul, 0)
    for k, dest in enumerate(("alg", "role0", "role1", "role2")):
        m_ = c.add(f"#X msg {500 + k * 90} {y2 + 210} set {D}1;"); c.conn(ul, k, m_, 0)
        s_ = c.add(f"#X obj {500 + k * 90} {y2 + 240} s {D}0-{dest};"); c.conn(m_, 0, s_, 0)
    c.add(f"#X obj 700 {y2 + 150} text define -k {D}0-algs;")
    c.lines.append("#A set " + " ".join(" ".join(a) + " " + SEMI for a in ALGS) + ";")
    return c


def build():
    c = Canvas()
    gui(c)
    lg = logic()
    sub = ["#N canvas 40 40 1200 900 logic 0;"] + lg.lines + lg.conns + \
        [f"#X restore {X0} {TOP + len(PAGES) * ROWH + 4} pd logic;"]
    h = TOP + len(PAGES) * ROWH + 34
    return "\n".join([f"#N canvas 20 20 {8 * CW + 30} {h} 10;"] + c.lines + sub + c.conns) + "\n"


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "pd", "txfm.pd")
    open(out, "w").write(build())
    print("wrote", out, f"({len(MACROS)} controls on {len(PAGES)} pages)")

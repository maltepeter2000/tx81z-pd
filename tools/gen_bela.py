#!/usr/bin/env python3
"""Generates bela-pd/tx81z-pd/_main.pd: a lean TX81Z for Bela (Pd), no GUI.

usage: python3 tools/gen_bela.py [outfile]   (default: bela-pd/tx81z-pd/_main.pd)

- [tx81z~] on the bus tx / tx-out (the same messages as on the desktop)
- MIDI from Bela: notes, bend, program change, aftertouch, CC 1-19 / 64-69 to the TX81Z
- [txcc 0]: the controller layer (CC pages, pickup, feedback), follows the TX81Z channel
- control channel (CTL_CH, default 16): pads / notes for random voices, store, battery RAM
  save, factory init; program change on it = user slot
- OLED via O2O: the two LCD lines and the last changed parameter
- OSC on UDP 7563: /tx81z/<message> <args> goes to tx81z~ as <message> <args>
  (e.g. /tx81z/random pad, /tx81z/pgm 110, /tx81z/press play), so a Mac can remote it
- the battery RAM is saved every 60 s and when the project stops
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import patchwriter as G                           # noqa: E402

CTL_CH = 16
NSLOTS = 64
RANDOM = ["pluck", "bass", "lead", "brass", "pad", "strings", "total"]
# control channel notes: pad row 1 = 36-43, row 2 = 44-51
PADS = [(36, "factory+"), (37, "user+")] + [(38 + k, c) for k, c in enumerate(RANDOM[:6])] + \
       [(44, "total"), (45, "factory-"), (46, "user-"), (47, "store"), (48, "save"), (49, "finit")]
PAD_TEXT = {"factory+": "next factory patch", "user+": "next user patch", "factory-": "previous factory patch",
            "user-": "previous user patch", "store": "store the voice into the current user slot",
            "save": "save the battery RAM", "finit": "twice within 1 s: factory init"}
SAVE_EVERY_MS = 60000


def fold(p, src, out, x, y):
    """channel 1-32 (two ports) -> 1-16"""
    a = p.add("- 1", x, y); p.con(src, out, a, 0)
    b = p.add("mod 16", x, y + 25); p.con(a, 0, b, 0)
    c = p.add("+ 1", x, y + 50); p.con(b, 0, c, 0)
    return c


def midi_sub():
    p = G.P("midi", 900, 520)
    p.note(f"MIDI from Bela -> tx81z~. Channel {CTL_CH} is the control channel (pd control). "
           "CC 1-19 (not 3) and 64-69 go to the TX81Z (mod wheel, breath, foot, volume, sustain ...), "
           "CC 20-119 are the controller layer [txcc].", 10, 10)
    # notes
    ni = p.add("notein", 10, 60)
    ch = fold(p, ni, 2, 90, 85)
    pk = p.add("pack f f f", 10, 170); p.con(ni, 0, pk, 0); p.con(ni, 1, pk, 1); p.con(ch, 0, pk, 2)
    rot = p.msg("$3 $1 $2", 10, 200); p.con(pk, 0, rot, 0)
    rt = p.add(f"route {CTL_CH}", 10, 230); p.con(rot, 0, rt, 0)
    sc = p.add("s tx-ctl-note", 10, 290); p.con(rt, 0, sc, 0)
    mn = p.msg("note $2 $3 $1", 110, 260); p.con(rt, 1, mn, 0)
    s1 = p.add("s tx", 110, 290); p.con(mn, 0, s1, 0)
    # bend
    bi = p.add("bendin", 250, 60)
    b0 = p.add("- 8192", 250, 85); p.con(bi, 0, b0, 0)
    bc = fold(p, bi, 1, 320, 85)
    bp = p.add("pack f f", 250, 170); p.con(b0, 0, bp, 0); p.con(bc, 0, bp, 1)
    br = p.msg("$2 $1", 250, 200); p.con(bp, 0, br, 0)
    brt = p.add(f"route {CTL_CH}", 250, 230); p.con(br, 0, brt, 0)
    bm = p.msg("bend $2 $1", 250, 260); p.con(brt, 1, bm, 0)
    s2 = p.add("s tx", 250, 290); p.con(bm, 0, s2, 0)
    # program change
    pi = p.add("pgmin", 400, 60)
    p0 = p.add("- 1", 400, 85); p.con(pi, 0, p0, 0)
    pc = fold(p, pi, 1, 460, 85)
    pp = p.add("pack f f", 400, 170); p.con(p0, 0, pp, 0); p.con(pc, 0, pp, 1)
    pr = p.msg("$2 $1", 400, 200); p.con(pp, 0, pr, 0)
    prt = p.add(f"route {CTL_CH}", 400, 230); p.con(pr, 0, prt, 0)
    psl = p.add("s tx-ctl-pgm", 400, 290); p.con(prt, 0, psl, 0)
    pm = p.msg("pgm $2 $1", 490, 260); p.con(prt, 1, pm, 0)
    s3 = p.add("s tx", 490, 290); p.con(pm, 0, s3, 0)
    cur = p.msg("$2", 560, 260); p.con(prt, 1, cur, 0)
    sc2 = p.add("s tx-cur-pgm", 560, 290); p.con(cur, 0, sc2, 0)   # for the factory+ / - pads
    # CC to the firmware: 1-19 without 3, 64-69 (the rest is the controller layer)
    ci = p.add("ctlin", 600, 60)
    cc = fold(p, ci, 2, 680, 85)
    cp = p.add("pack f f f", 600, 170); p.con(ci, 0, cp, 0); p.con(ci, 1, cp, 1); p.con(cc, 0, cp, 2)
    tl = p.add("t l l", 600, 200); p.con(cp, 0, tl, 0)
    un = p.add("unpack f f f", 700, 230); p.con(tl, 1, un, 0)
    ok = p.add(f"expr (($f1 < 20 && $f1 != 3) || ($f1 >= 64 && $f1 <= 69)) && $f2 != {CTL_CH}", 700, 260)
    p.con(un, 1, ok, 0); p.con(un, 2, ok, 1)
    sp = p.add("spigot", 600, 300); p.con(tl, 0, sp, 0); p.con(ok, 0, sp, 1)
    cm = p.msg("cc $2 $1 $3", 600, 330); p.con(sp, 0, cm, 0)
    s4 = p.add("s tx", 600, 360); p.con(cm, 0, s4, 0)
    # aftertouch
    ti = p.add("touchin", 10, 360)
    tc = fold(p, ti, 1, 90, 385)
    t2 = p.add("+ 207", 90, 460); p.con(tc, 0, t2, 0)
    tp = p.add("pack f f", 10, 490); p.con(ti, 0, tp, 0); p.con(t2, 0, tp, 1)
    tm = p.msg("midi $2 $1", 10, 520); p.con(tp, 0, tm, 0)
    s5 = p.add("s tx", 10, 550); p.con(tm, 0, s5, 0)
    return p, (un, ok)


def control_sub():
    p = G.P("control", 1000, 700)
    p.note(f"Control channel {CTL_CH}: " + ", ".join(
        f"{n} {PAD_TEXT.get(a, 'random ' + a)}" for n, a in PADS) +
        f". Program change 1-{NSLOTS} on channel {CTL_CH}: user slot 0-{NSLOTS - 1} "
        "(txuser-bank.txt in the project folder).", 10, 10)
    rn = p.add("r tx-ctl-note", 10, 70)
    un = p.add("unpack f f", 10, 100); p.con(rn, 0, un, 0)
    gt = p.add("> 0", 90, 130); p.con(un, 1, gt, 0)
    sp = p.add("spigot", 10, 160); p.con(un, 0, sp, 0); p.con(gt, 0, sp, 1)
    sel = p.add("sel " + " ".join(str(n) for n, _ in PADS), 10, 190)
    p.con(sp, 0, sel, 0)
    out = {a: k for k, (_, a) in enumerate(PADS)}
    stx = p.add("s tx", 10, 300)
    for k, cat in enumerate(RANDOM):
        m = p.msg(f"random {cat}", 10 + k * 95, 240); p.con(sel, out[cat], m, 0); p.con(m, 0, stx, 0)
    # factory patch + / -: program I01-C32 (0-127), also follows program changes from MIDI
    fp = p.msg("1", 300, 130); fm_ = p.msg("127", 340, 130)
    p.con(sel, out["factory+"], fp, 0); p.con(sel, out["factory-"], fm_, 0)
    add = p.add("+", 300, 160); p.con(fp, 0, add, 0); p.con(fm_, 0, add, 0)
    md = p.add("mod 128", 300, 185); p.con(add, 0, md, 0)
    tff = p.add("t f f", 300, 210); p.con(md, 0, tff, 0); p.con(tff, 1, add, 1)
    rcp = p.add("r tx-cur-pgm", 420, 130); p.con(rcp, 0, add, 1)
    pgm = p.msg("pgm $1", 300, 270); p.con(tff, 0, pgm, 0); p.con(pgm, 0, stx, 0)
    # user patch + / -: slot 0-63, through the recall below
    up_ = p.msg("1", 520, 130); um_ = p.msg(str(NSLOTS - 1), 560, 130)
    p.con(sel, out["user+"], up_, 0); p.con(sel, out["user-"], um_, 0)
    uad = p.add("+", 520, 160); p.con(up_, 0, uad, 0); p.con(um_, 0, uad, 0)
    umd = p.add(f"mod {NSLOTS}", 520, 185); p.con(uad, 0, umd, 0)
    usp = p.add("s tx-ctl-pgm", 520, 210); p.con(umd, 0, usp, 0)
    # user bank
    td = p.add("text define -k tx-bank", 600, 640)
    lb = p.add("loadbang", 600, 520)
    rd = p.msg("read txuser-bank.txt", 600, 550); p.con(lb, 0, rd, 0)
    p.con(rd, 0, td, 0)
    wr = p.msg("write txuser-bank.txt", 760, 600); p.con(wr, 0, td, 0)
    # recall: program change on the control channel
    rp = p.add("r tx-ctl-pgm", 10, 360)
    cl = p.add(f"clip 0 {NSLOTS - 1}", 10, 390); p.con(rp, 0, cl, 0)
    tf = p.add("t f f", 10, 420); p.con(cl, 0, tf, 0); p.con(tf, 1, uad, 1)
    slot = p.add("f", 150, 470); p.con(tf, 1, slot, 1)
    tg = p.add("text get tx-bank", 10, 450); p.con(tf, 0, tg, 0)
    lt = p.add("list trim", 10, 480); p.con(tg, 0, lt, 0)
    rdash = p.add("route -", 10, 510); p.con(lt, 0, rdash, 0)
    fe = p.add("f", 10, 540); p.con(rdash, 0, fe, 0); p.con(tf, 1, fe, 1)
    emp = p.msg("user $1 empty", 10, 570); p.con(fe, 0, emp, 0)
    lp = p.add("list prepend voicedata", 80, 540); p.con(rdash, 1, lp, 0)
    lt2 = p.add("list trim", 80, 570); p.con(lp, 0, lt2, 0)
    s2 = p.add("s tx", 80, 600); p.con(lt2, 0, s2, 0)
    o1 = p.add("s tx-oled-note", 10, 630); p.con(emp, 0, o1, 0)
    # store: getvoice, voicedata into the slot, write the file
    tb = p.add("t b b", 300, 360); p.con(sel, out["store"], tb, 0)
    one = p.msg("1", 360, 390); p.con(tb, 1, one, 0)
    gv = p.msg("getvoice", 300, 420); p.con(tb, 0, gv, 0); p.con(gv, 0, stx, 0)
    ro = p.add("r tx-out", 450, 360)
    rv = p.add("route voicedata", 450, 390); p.con(ro, 0, rv, 0)
    ls = p.add("list", 450, 420); p.con(rv, 0, ls, 0)
    gate = p.add("spigot", 450, 450); p.con(ls, 0, gate, 0); p.con(one, 0, gate, 1)
    tbl = p.add("t b l b", 450, 480); p.con(gate, 0, tbl, 0)    # slot number first, then the voice
    ts = p.add("text set tx-bank", 450, 610); p.con(tbl, 1, ts, 0); p.con(slot, 0, ts, 1)
    p.con(tbl, 2, slot, 0)
    zero = p.msg("0", 600, 450); p.con(tbl, 0, zero, 0); p.con(zero, 0, gate, 1)
    dw = p.add("del 10", 760, 570); p.con(tbl, 0, dw, 0); p.con(dw, 0, wr, 0)
    fs = p.add("f", 860, 480); p.con(tbl, 0, fs, 0); p.con(slot, 0, fs, 1)
    sm = p.msg("stored slot $1", 860, 510); p.con(fs, 0, sm, 0)
    o2 = p.add("s tx-oled-note", 860, 540); p.con(sm, 0, o2, 0)
    # save the battery RAM: now, and every minute
    sv = p.msg("save", 300, 480); p.con(sel, out["save"], sv, 0); p.con(sv, 0, stx, 0)
    svm = p.msg("battery RAM saved", 300, 540); p.con(sel, out["save"], svm, 0)
    o3 = p.add("s tx-oled-note", 300, 570); p.con(svm, 0, o3, 0)
    lb2 = p.add("loadbang", 200, 640)
    dl = p.add(f"del {SAVE_EVERY_MS}", 200, 670); p.con(lb2, 0, dl, 0)
    p.con(dl, 0, sv, 0); p.con(dl, 0, dl, 0)                     # every minute, not at load
    # factory init: twice within 1 s
    tf2 = p.add("t b b", 700, 240); p.con(sel, out["finit"], tf2, 0)
    tm = p.add("timer", 700, 300); p.con(tf2, 1, tm, 1); p.con(tf2, 0, tm, 0)
    lt3 = p.add("< 1000", 700, 330); p.con(tm, 0, lt3, 0)
    s01 = p.add("sel 1 0", 700, 360); p.con(lt3, 0, s01, 0)
    fi = p.msg("factoryinit", 700, 390); p.con(s01, 0, fi, 0)
    s6 = p.add("s tx", 700, 420); p.con(fi, 0, s6, 0)
    fm = p.msg("factory init ...", 800, 390); p.con(s01, 0, fm, 0)
    ag = p.msg("again: factory init", 900, 390); p.con(s01, 1, ag, 0)
    o4 = p.add("s tx-oled-note", 800, 420); p.con(fm, 0, o4, 0); p.con(ag, 0, o4, 0)
    return p


def oled_sub():
    p = G.P("oled", 900, 520)
    p.note("OLED via O2O (127.0.0.1:7562), one line: the last event - a patch change shows the "
           "TX81Z's voice line (C15 LatelyBass), a turned parameter shows out1 = 67, the control "
           "channel shows what it did (stored slot 5). At most every 50 ms.", 10, 10)
    ro = p.add("r tx-out", 10, 60)
    rl = p.add("route lcd info voice status", 10, 90); p.con(ro, 0, rl, 0)
    # LCD line 2 (the voice line in PLAY mode) kept for patch changes
    l1 = p.add("list", 10, 120); p.con(rl, 0, l1, 0)
    up = p.add("unpack s s", 10, 150); p.con(l1, 0, up, 0)
    sy2 = p.add("symbol", 10, 210); p.con(up, 1, sy2, 1)
    # voice change: after 150 ms (the firmware has drawn it) show the voice line; the values of
    # the voice dump stay off the display for 500 ms
    tv = p.add("t b", 300, 120); p.con(rl, 2, tv, 0)
    dv = p.add("del 150", 300, 150); p.con(tv, 0, dv, 0); p.con(dv, 0, sy2, 0)
    # a parameter shows only when it was turned (n <name> ... on the bus), not from voice dumps
    rb = p.add("r tx", 600, 180)
    rnn = p.add("route n", 600, 210); p.con(rb, 0, rnn, 0)
    lnn = p.add("list", 600, 240); p.con(rnn, 0, lnn, 0)
    unn = p.add("unpack s f", 600, 270); p.con(lnn, 0, unn, 0)
    li = p.add("list", 150, 180); p.con(rl, 1, li, 0)
    tll = p.add("t l l", 150, 210); p.con(li, 0, tll, 0)
    nv = p.msg("$1 = $3", 250, 240); p.con(tll, 1, nv, 0)
    ff1 = p.add("fudiformat -u", 250, 270); p.con(nv, 0, ff1, 0)
    ts1 = p.add("list tosymbol", 250, 300); p.con(ff1, 0, ts1, 0)
    cand = p.add("symbol", 250, 330); p.con(ts1, 0, cand, 1)
    uin = p.add("unpack s f s", 150, 240); p.con(tll, 0, uin, 0)
    wsel = p.add("sel -", 150, 300); p.con(uin, 0, wsel, 0); p.con(unn, 0, wsel, 1)
    p.con(wsel, 0, cand, 0)
    rn = p.add("r tx-oled-note", 420, 240)
    ff = p.add("fudiformat -u", 420, 270); p.con(rn, 0, ff, 0)
    ts = p.add("list tosymbol", 420, 300); p.con(ff, 0, ts, 0)
    st = p.add("symbol", 700, 120); p.con(rl, 3, st, 0)
    line = p.add("symbol", 10, 360)
    p.con(ts, 0, line, 1); p.con(sy2, 0, line, 1); p.con(st, 0, line, 1); p.con(cand, 0, line, 1)
    # dirty flag + metro
    one = p.msg("1", 450, 300)
    p.con(ts, 0, one, 0); p.con(sy2, 0, one, 0); p.con(st, 0, one, 0); p.con(cand, 0, one, 0)
    lb = p.add("loadbang", 450, 60)
    mt = p.add("metro 50", 450, 90); p.con(lb, 0, mt, 0)
    gate = p.add("spigot", 450, 330); p.con(mt, 0, gate, 0); p.con(one, 0, gate, 1)
    tb = p.add("t b b", 450, 360); p.con(gate, 0, tb, 0)
    zero = p.msg("0", 600, 390); p.con(tb, 1, zero, 0); p.con(zero, 0, gate, 1)
    p.con(tb, 0, line, 0)
    ls = p.add("list", 10, 390); p.con(line, 0, ls, 0)
    of = p.add("oscformat display-strings-and-numbers", 10, 420); p.con(ls, 0, of, 0)
    ps = p.add("list prepend send", 10, 450); p.con(of, 0, ps, 0)
    lt = p.add("list trim", 10, 480); p.con(ps, 0, lt, 0)
    ns = p.add("netsend -u -b", 10, 510); p.con(lt, 0, ns, 0)
    cm = p.msg("connect 127.0.0.1 7562", 200, 480); p.con(lb, 0, cm, 0); p.con(cm, 0, ns, 0)
    init = p.msg("symbol TX81Z", 600, 300); p.con(lb, 0, init, 0); p.con(init, 0, line, 1); p.con(init, 0, one, 0)
    return p


def remote_sub():
    p = G.P("remote", 700, 300)
    p.note("OSC on UDP 7563: /tx81z/<message> <args> -> tx81z~ (e.g. /tx81z/random pad, "
           "/tx81z/pgm 110, /tx81z/press play, /tx81z/n out1 64).", 10, 10)
    nr = p.add("netreceive -u -b 7563", 10, 60)
    op = p.add("oscparse", 10, 90); p.con(nr, 0, op, 0)
    lt = p.add("list trim", 10, 120); p.con(op, 0, lt, 0)
    rt = p.add("route tx81z", 10, 150); p.con(lt, 0, rt, 0)
    l2 = p.add("list trim", 10, 180); p.con(rt, 0, l2, 0)
    st = p.add("s tx", 10, 210); p.con(l2, 0, st, 0)
    return p


def build():
    top = G.P(None, 900, 420)
    top.note("TX81Z on Bela: tx81z~ (original firmware) - MIDI in, controller layer, OLED, "
             "control channel, OSC remote. Files: tx81z~.pd_linux, txcc.pd, tx81z-v1.6.bin, "
             "tx81z.nvram (made by the factory init), txuser-bank.txt.", 10, 10)
    r = top.add("r tx", 10, 70)
    t = top.add("tx81z~ tx81z-v1.6.bin tx81z.nvram", 10, 100); top.con(r, 0, t, 0)
    d = top.add("dac~ 1 2", 10, 150); top.con(t, 0, d, 0); top.con(t, 1, d, 1)
    so = top.add("s tx-out", 300, 150); top.con(t, 2, so, 0)
    ro = top.add("r tx-out", 10, 210)
    cc = top.add("txcc 0", 10, 240); top.con(ro, 0, cc, 0)
    sc = top.add("s tx", 10, 270); top.con(cc, 0, sc, 0)
    top.note("controller layer: CC pages 20-119, follows the TX81Z receive channel", 80, 240)
    m, _ = midi_sub()
    top.sub(m, 10, 320)
    top.sub(control_sub(), 110, 320)
    top.sub(oled_sub(), 230, 320)
    top.sub(remote_sub(), 330, 320)
    out = []
    for ln in top.lines(top=True):
        out.append(ln)
        if ln.endswith("text define -k tx-bank;"):
            out.append(G.table_data(["-"] * NSLOTS))
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "bela-pd", "tx81z-pd", "_main.pd")
    open(out, "w").write(build())
    print("wrote", out)

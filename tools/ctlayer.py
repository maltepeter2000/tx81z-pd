#!/usr/bin/env python3
"""Controller layer: one MIDI channel per instrument, the same CC pages for every instrument.

The pages (what the Launchkey sends, encoder 1-8 on page 1-11). Left out: 32 (bank LSB),
38 (data LSB), 64-69 (pedals), 84 (portamento control), 88 (high-res velocity),
96-101 (data inc/dec, NRPN, RPN), 120-127 (channel mode). 10 full pages and one of 4.

    page  1: 20 21 22 23 24 25 26 27        page  7: 76 77 78 79 80 81 82 83
    page  2: 28 29 30 31 33 34 35 36        page  8: 85 86 87 89 90 91 92 93
    page  3: 37 39 40 41 42 43 44 45        page  9: 94 95 102 103 104 105 106 107
    page  4: 46 47 48 49 50 51 52 53        page 10: 108 109 110 111 112 113 114 115
    page  5: 54 55 56 57 58 59 60 61        page 11: 116 117 118 119
    page  6: 62 63 70 71 72 73 74 75

An instrument puts its settings in rows (row k = page k); free places stay empty.
build(rows) makes the layer abstraction [<name> <channel>] (channel 0 = all):
  [ctlin] inside, CC on its channel -> outlet: n <name> <0-127>  (to the instrument)
  inlet: the instrument's reports "info <name> <0-127> <text>" (every change, also its own)
         pickup 0/1   (default 1)      sync   (asks for all its settings: ninfo ...)
  CC 3 on its channel (any value) = sync request from the controller (the Launchkey firmware sends
  it before its first CC on a new channel): all values go back to the controller.
  Feedback: a change that did not come from the encoder itself (preset, editor, message)
  goes back to the controller as CC on the same channel and number ([ctlout]), so a
  controller that takes incoming CC knows the new value. Pickup: after such a change the
  encoder acts only when it reaches the value (within 3) or crosses it, so nothing jumps
  with a controller that ignores the feedback. A change from the encoder is not echoed:
  the instrument reports it in the same logical time, which marks it as its own.
The instrument needs: n <name> <0-127>, ninfo <name>, and "info <name> <0-127> <text>" on
an outlet for every change (pdoly8~ does this).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

RESERVED = {32, 38, 64, 65, 66, 67, 68, 69, 84, 88, 96, 97, 98, 99, 100, 101}
FREE = [c for c in range(20, 120) if c not in RESERVED]
PAGES = [FREE[k:k + 8] for k in range(0, len(FREE), 8)]
assert len(PAGES) == 11 and len(PAGES[-1]) == 4


def cc_map(rows):
    """rows: [(title, [name or None, ...]), ...] -> {name: cc}"""
    assert len(rows) <= len(PAGES), "more rows than pages"
    m = {}
    for (title, names), page in zip(rows, PAGES):
        assert len(names) <= len(page), f"row {title}: {len(names)} > {len(page)} places"
        for nm, cc in zip(names, page):
            if nm:
                assert nm not in m, f"{nm} twice"
                m[nm] = cc
    return m


def page_text(k):
    pg = PAGES[k]
    runs, a = [], pg[0]
    for i in range(1, len(pg) + 1):
        if i == len(pg) or pg[i] != pg[i - 1] + 1:
            runs.append(f"{a}-{pg[i - 1]}" if pg[i - 1] != a else f"{a}"); a = pg[i] if i < len(pg) else 0
    return " ".join(runs)


def build(title, rows):
    import patchwriter as G                      # the patch writer P
    m = cc_map(rows)
    p = G.P(None, 900, 900)
    p.note(f"{title}: controller layer - CC pages 20-119 (without 32 38 64-69 84 88 96-101) on channel $1 "
           "(0 = all) -> n <name> <0-127>. Inlet: info <name> <0-127> <text> from the instrument, "
           "pickup 0/1, sync (also CC 3 from the controller). Changes from elsewhere go back to the controller as CC (ctlout), "
           "pickup avoids jumps. Table: line = CC number, the setting or -.", 10, 6)
    # ---- init: two timers (logical time since load), arrays, channel
    lb = p.add("loadbang", 10, 60)
    tb = p.add("t b b b b", 10, 90); p.con(lb, 0, tb, 0)
    c1 = p.msg("const 1", 10, 120); p.con(tb, 3, c1, 0)
    c2 = p.msg("const -1000", 100, 120); p.con(tb, 3, c2, 0)
    apk = p.add("array define $0-pk 128", 10, 150); p.con(c1, 0, apk, 0)
    aow = p.add("array define $0-own 128", 180, 150); p.con(c2, 0, aow, 0)
    p.add("array define $0-cur 128", 360, 150)
    apv = p.add("array define $0-prev 128", 540, 150); p.con(c2, 0, apv, 0)   # -1000: no last position yet
    t1 = p.add("timer", 600, 600); t2 = p.add("timer", 300, 1080)
    p.con(tb, 2, t1, 0); p.con(tb, 2, t2, 0)
    ch = p.add("f $1", 300, 120); p.con(tb, 1, ch, 0)
    chmax = p.add("max 1", 300, 180); p.con(ch, 0, chmax, 0)
    dl = p.add("del 500", 400, 120); p.con(tb, 0, dl, 0)
    on1 = p.msg("1", 480, 120); p.con(tb, 0, on1, 0)
    # ---- CC in: channel filter
    ci = p.add("ctlin", 10, 230)
    pk = p.add("pack f f f", 10, 260)
    p.con(ci, 2, pk, 2); p.con(ci, 1, pk, 1); p.con(ci, 0, pk, 0)
    tl = p.add("t l l", 10, 290); p.con(pk, 0, tl, 0)
    u1 = p.add("unpack f f f", 120, 320); p.con(tl, 1, u1, 0)
    chok = p.add("expr $f1 == $f2 || $f2 == 0", 120, 350); p.con(u1, 2, chok, 0); p.con(ch, 0, chok, 1)
    sp = p.add("spigot", 10, 380); p.con(tl, 0, sp, 0); p.con(chok, 0, sp, 1)
    u2 = p.add("unpack f f f", 10, 410); p.con(sp, 0, u2, 0)
    # cc: name (or -), then the state of this encoder into the pass test
    tc = p.add("t f f f f f", 120, 440); p.con(u2, 1, tc, 0)
    tg = p.add("text get $0-map", 560, 470); p.con(tc, 4, tg, 0)
    ltr = p.add("list trim", 560, 500); p.con(tg, 0, ltr, 0)
    rdash = p.add("route -", 560, 530); p.con(ltr, 0, rdash, 0)
    nm = p.add("list", 640, 560); p.con(rdash, 1, nm, 0)
    tbl = p.add("t b l", 640, 590); p.con(nm, 0, tbl, 0)
    v0 = p.msg("0", 560, 620); v1 = p.msg("1", 640, 620)
    p.con(rdash, 0, v0, 0); p.con(tbl, 0, v1, 0)
    rcur = p.add("tabread $0-cur", 420, 500); p.con(tc, 3, rcur, 0)
    rpk = p.add("tabread $0-pk", 300, 500); p.con(tc, 2, rpk, 0)
    rpv = p.add("tabread $0-prev", 160, 500); p.con(tc, 1, rpv, 0)
    tv = p.add("t f f f", 10, 560); p.con(u2, 0, tv, 0)
    pass_ = p.add("expr if($f6 == 0, 0, if($f5 == 0, 1, $f2 != 0 || abs($f1 - $f3) <= 3 "
                  "|| ($f4 >= 0 && ($f4 - $f3) * ($f1 - $f3) <= 0)))", 10, 660)
    p.con(tv, 0, pass_, 0); p.con(rpk, 0, pass_, 1); p.con(rcur, 0, pass_, 2); p.con(rpv, 0, pass_, 3)
    p.con(on1, 0, pass_, 4); p.con(v0, 0, pass_, 5); p.con(v1, 0, pass_, 5)
    wpv = p.add("tabwrite $0-prev", 160, 600); p.con(tv, 2, wpv, 0)
    sv = p.add("f", 120, 760); p.con(tv, 1, sv, 1)
    # passed: mark as own (now), picked up, n <name> <value>
    sel = p.add("sel 1", 10, 690); p.con(pass_, 0, sel, 0)
    tp = p.add("t b b b", 10, 720); p.con(sel, 0, tp, 0)
    p.con(tp, 2, t1, 1)
    wow = p.add("tabwrite $0-own", 600, 630); p.con(t1, 0, wow, 0)
    one = p.msg("1", 300, 750); p.con(tp, 1, one, 0)
    wpk = p.add("tabwrite $0-pk", 300, 780); p.con(one, 0, wpk, 0)
    for w in (wpv, wpk, wow):
        p.con(tc, 0, w, 1)
    p.con(tp, 0, sv, 0)
    lp = p.add("list prepend", 120, 790); p.con(sv, 0, lp, 0); p.con(tbl, 1, lp, 1)
    lpn = p.add("list prepend n", 120, 820); p.con(lp, 0, lpn, 0)
    ltr2 = p.add("list trim", 120, 850); p.con(lpn, 0, ltr2, 0)
    out = p.add("outlet", 120, 880); p.con(ltr2, 0, out, 0)
    # ---- inlet: info <name> <0-127> <text> / pickup 0/1 / sync
    inl = p.add("inlet", 10, 940)
    ri = p.add("route info pickup sync", 10, 970); p.con(inl, 0, ri, 0)
    p.con(ri, 1, pass_, 4)
    allm = p.msg(", ".join(f"ninfo {n}" for n in m), 560, 1000)
    p.con(ri, 2, allm, 0); p.con(dl, 0, allm, 0); p.con(allm, 0, out, 0)
    s3 = p.add("sel 3", 300, 440); p.con(u2, 1, s3, 0); p.con(s3, 0, allm, 0)   # CC 3: sync request
    lst = p.add("list", 160, 1000); p.con(ri, 0, lst, 0)       # "har1 37 .." (selector har1) -> list
    ui = p.add("unpack s f s", 10, 1030); p.con(lst, 0, ui, 0)
    fv = p.add("f", 300, 1200); p.con(ui, 1, fv, 1)
    li = p.add("list", 10, 1045); p.con(ui, 0, li, 0)
    ts = p.add("text search $0-map", 10, 1060); p.con(li, 0, ts, 0)
    mo = p.add("moses 0", 10, 1090); p.con(ts, 0, mo, 0)
    ti = p.add("t f b f", 10, 1120); p.con(mo, 1, ti, 0)
    wcur = p.add("tabwrite $0-cur", 300, 1260); wpk2 = p.add("tabwrite $0-pk", 420, 1260)
    co = p.add("ctlout", 540, 1260)
    p.con(ti, 2, wcur, 1); p.con(ti, 2, wpk2, 1); p.con(ti, 2, co, 1); p.con(chmax, 0, co, 2)
    p.con(ti, 1, t2, 1)
    rown = p.add("tabread $0-own", 10, 1150); p.con(ti, 0, rown, 0)
    own = p.add("expr $f2 - $f1 < 0.5", 10, 1180)                 # marked in this logical time
    p.con(rown, 0, own, 0); p.con(t2, 0, own, 1)
    tw = p.add("t f b", 10, 1210); p.con(own, 0, tw, 0)
    p.con(tw, 1, fv, 0); p.con(fv, 0, wcur, 0)                    # always: the current value
    sel0 = p.add("sel 0", 10, 1240); p.con(tw, 0, sel0, 0)
    # a change from elsewhere: this encoder must pick up again, the value goes to the controller
    tf = p.add("t b b", 10, 1270); p.con(sel0, 0, tf, 0)
    z = p.msg("0", 420, 1230); p.con(tf, 1, z, 0); p.con(z, 0, wpk2, 0)
    fv2 = p.add("f", 540, 1230); p.con(ui, 1, fv2, 1); p.con(tf, 0, fv2, 0); p.con(fv2, 0, co, 0)
    p.add("text define -k $0-map", 560, 940)
    return p, m


def write(path, title, rows):
    import patchwriter as G
    p, m = build(title, rows)
    by = {cc: nm for nm, cc in m.items()}
    data = [by.get(k, "-") for k in range(128)]
    out = []
    for ln in p.lines(top=True):
        out.append(ln)
        if ln.endswith("text define -k \\$0-map;"):
            out.append(G.table_data(data))
    open(path, "w").write("\n".join(out) + "\n")
    return m

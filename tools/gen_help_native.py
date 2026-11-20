#!/usr/bin/env python3
"""Generate pd/tx81zn~-help.pd (native engine) in the layout of Peter's help patches (pd-help-layout).

usage: python3 tools/gen_help_native.py [outfile]   (default: pd/tx81zn~-help.pd)

Same grid, colours and checks as tools/gen_help.py (tx81z~); the controls are the FM view
(pd/txfm.h), the controller layer is [txfmcc], both editors are live.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ctlayer as CL                             # noqa: E402
import gen_help as H                             # noqa: E402
import gen_fmedit as F                           # noqa: E402
from gen_help import (BS, COMMA, DOLLAR, SEMI, YEL_X, YEL_Y, YEL_W, PINK_X, PINK_TOP, PINK_W,  # noqa: E402
                      BLUE_X, BLUE_TOP, BLUE_W, HX, TX, CX, LINE, TXT_GAP, OBJ_GAP, BOX, STEP, M,
                      YELLOW, PINK, BLUE, box_w, esc, nlines)

OBJ, SHORT = "tx81zn~", "tx81zn"
SUBTITLE = "Yamaha TX81Z sound engine rebuilt natively (no CPU emulation)"
ROM, NVRAM = "tx81z-v1.6.bin", "tx81z.nvram"
CHANNEL = 0             # controller layer [txfmcc 0]: omni until tx81zn~ reports its channel

# example controls of the FM view: name, description (two rows of 4, values 0-127)
CONTROLS = [
    ("algo", "algorithm 1-8, it sets the roles C / A / B."),
    ("ratc", "frequency ratio of the carriers C."),
    ("rata", "frequency ratio of modulator A."),
    ("ratb", "frequency ratio of the modulators B."),
    ("moda", "output level of A = brightness."),
    ("modb", "output level of B."),
    ("fdbk", "feedback of operator 4, 0-7."),
    ("mix", "carriers against each other (only with several: algorithms 5-8)."),
]

ROLES = "Algorithms (> = modulates) and roles: " + ", ".join(
    f"{k + 1} {g} {c} {a} {b}" for k, (g, c, a, b) in enumerate(F.ALGS)) + "."

EXBLOCKS = [
    ("Voices", ["bank <I|A|B|C|D> <1-32> selects any voice, bank D included. Program change "
                "0-127 selects I01-I32 and A01-C32 as on the TX81Z (I 0-31, A 32-63, B 64-95, C 96-127). "
                "Bank I comes from the battery RAM file; without it bank I is empty. random <kind> [seed] "
                "makes a new voice (the same generator as tx81z~)."],
     ["bank A 1", "bank C 15", "bank D 3", "pgm 0", "pgm 40", "random pad", "random bass", "random lead",
      "random total"]),
    ("FM view", ["fm <control> <0-127> sets a control of the FM view, fminfo <control> asks for it. "
                 "Every change comes back as fm <control> <0-127> <text> on outlet 2 (the text is - "
                 "when the role has no operator in this algorithm). All controls: FM view below."],
     ["fm algo 0", "fm algo 73", "fm algo 127", "fm ratc 30", "fm rata 60", "fm moda 110",
      "fm dtun 127", "fm atkc 60", "fm lpmd 80"]),
]


def fm_pages():
    """FM view: one paragraph per page, the controls with their labels"""
    out = []
    for k, title in enumerate(F.PAGES):
        ms = [m for m in F.MACROS if m[2] == k]
        out.append(f"page {k + 1} {title}, CC {CL.page_text(k)}: "
                   + ", ".join(f"{n} {lbl}" for n, lbl, _ in ms))
    return out


def build():
    p = H.Patch()
    bus = DOLLAR + "0-" + SHORT
    out = DOLLAR + "0-" + SHORT + "-out"
    rows = (len(CONTROLS) + 3) // 4
    top_y = YEL_Y + 3 * M + 24 + 28
    util_y = top_y
    demo_y = util_y + STEP + BOX + 24
    ctl_y0 = demo_y + 5 * STEP + BOX + 24
    ROW = 2 * STEP + BOX + 11
    ni_y = ctl_y0 + ROW * (rows - 1) + 2 * STEP + BOX + 24
    cc_y = ni_y + 4 * STEP + 10
    r_y = cc_y + 3 * STEP + 24
    obj_y = r_y + STEP
    mul_y, dac_y = obj_y + STEP, obj_y + 2 * STEP
    y = dac_y + 3 * STEP + BOX + 32

    fm = ["The FM view gives the four operators roles from the algorithm: C = the carriers (what you "
          "hear), A = the modulator of the main carrier OP1, B = the other modulators. A control of a "
          "role changes all of its operators and shows the value of the first one. In the end everything "
          "is an ordinary TX81Z voice parameter: the voice stays a TX81Z voice (txedit shows every "
          "single value).",
          "Ratio shows the real frequency ratio of coarse and fine. Mix: left the main carrier OP1, right "
          "the other carriers (the louder side keeps its level). Detune spreads all operators around the "
          "centre. ENV C is the amplitude, ENV A and ENV B are the brightness. Changing the algorithm "
          "keeps the operator settings: an operator that becomes a carrier sounds with its output level."]
    fm.insert(1, ROLES)
    fm += fm_pages()

    cc = [f"[txfmcc {CHANNEL}] above is the controller layer for the FM view: Launchkey pages 1-7 = the "
          "7 rows of the editor txfm (CC 20-83), transpose is on CC 80 (page 7 VOICE, encoder 5). It follows the "
          "receive channel tx81zn~ reports (0 = omni = all channels).",
          "Changes from elsewhere (voice change, editor, messages) go back to the controller as CC, one "
          "every 2 ms. Pickup: after such a change an encoder acts only when it reaches the value or "
          "crosses it (pickup 0 turns that off). CC 3 or sync sends all values to the controller.",
          "For the single parameters use [txcc] of tx81z~ instead (CC pages 20-119, n <name> <0-127>): "
          "tx81zn~ takes the same messages."]

    sections = [
        ("Inlets and outlets", ["Inlet: messages (see Messages)",
                                "Outlet 0, 1: signal left / right (~55.9 kHz resampled)",
                                "Outlet 2: info, fm, voice, voicedata, channel, status,",
                                "  lcd (the splash screen, once)"]),
        ("Messages", [
            "note <p> <v> [ch], cc <n> <v> [ch], pgm <n> [ch],",
            "  bend <-8192..8191> [ch]",
            "<float> / list, midi <bytes>: raw MIDI bytes ([midiin]),",
            "  SysEx parameter changes and voice dumps included",
            "bank <I|A|B|C|D> <1-32>: select a voice",
            "fm <control> <0-127>, fminfo <control>: FM view",
            "<name> <value>: a voice parameter, TX81Z data value,",
            "  e.g. alg 4, out1 99 (all names: Voice parameters)",
            "n <name> <0-127>: the same from a pot / CC position",
            "ninfo <name>: outlet 2 gets info <name> <0-127> <value>,",
            "  every change is reported this way by itself",
            "vced <p> <d>, aced <p> <d>: raw parameter change",
            "dumpreq: report every value again (info, fm, channel)",
            "getvoice: outlet 2 gets voicedata <name> <93 VCED>",
            "  <23 ACED>, voicedata ...: load that voice",
            "random <kind> [seed]: a new voice,",
            "  kind: pluck bass lead brass pad strings total",
            "channel <0-16>: receive channel, 0 = omni",
            "tune <0-127>: master tune (64 = centre)",
            "rom <file>, nvram <file>: load (the nvram is only read)",
            "factoryinit: all notes off, voice A01",
            "gain <f>: output gain (default 1)"]),
        ("Editors (txfm and txedit)", [
            "[txfm " + bus.replace(BS, "") + "] above: the FM editor, 7 rows of 8 controls = the "
            "Launchkey pages of txfmcc. On top: voice name, bank I A B C D and patch, the algorithm with "
            "its roles, random voices. [txedit " + bus.replace(BS, "") + "]: all 100 voice parameters in "
            "rows of 8, voice name, program change, MIDI channel, user bank (txuser-bank.txt). Both work "
            "on the same bus and stay in sync, click to open them.",
            "In your own patch: [r tx1] -> [tx81zn~ <rom> <nvram>] -> outlet 2 -> [s tx1-out], "
            "[txfm tx1], [txedit tx1], [r tx1-out] -> [txfmcc 0] -> [s tx1]."], True),
        ("FM view", fm, True),
        ("MIDI CC: controller layer (txfmcc)", cc, True),
        ("Controls of the FM view (0-127)", [f"{n}: {t}" for n, t in CONTROLS], True),
        ("Voice parameters", H.param_lines()),
        ("How it works", [
            "The voice, note, modulation and controller routines of the TX81Z firmware v1.6 are rebuilt "
            "in C++ and drive the YM2414 directly, with the same register values as the firmware (tested "
            "against the firmware running in tx81z~). A YM2414 core derived from ymfm, bit-identical to "
            "it, renders the sound. About a third to a sixth of the CPU of tx81z~, and parameter changes "
            "take effect at once (no SysEx round trip).",
            "Single mode only: no performance mode, no micro tuning, no front panel. The display shows "
            "the splash screen once (lcd); the voice name comes as voice <name>."], True),
        ("ROM and battery RAM", [
            f"Arguments: [{OBJ} <firmware> <battery RAM file>], paths relative to the patch. The firmware "
            f"(64 KB, e.g. {ROM} from the MAME set tx81z) is not included: tx81zn~ reads its tables and "
            "the preset banks A-D from it. The battery RAM file of tx81z~ is optional and only read: "
            "bank I, master tune and receive channel. tx81zn~ never writes it, so a channel set here "
            "lasts until the patch closes; without the file tx81zn~ receives omni."], True),
    ]

    placed = []
    for sec in sections:
        title, lines = sec[0], sec[1]
        para = sec[2] if len(sec) > 2 else False
        placed.append((title, lines, y, para))
        y += 28 + LINE * (nlines(lines) if para else len(lines)) + TXT_GAP
    placed_ex = []
    for title, intro, msgs in EXBLOCKS:
        placed_ex.append((title, intro, msgs, y))
        nr = (len(msgs) + 2) // 3
        y += 28 + LINE * nlines(intro) + OBJ_GAP + STEP * nr + BOX + TXT_GAP
    credits = ["Credits: Modelled on the Yamaha TX81Z firmware v1.6 and",
               "service manual, YM2414 after ymfm (Aaron Giles) with help",
               "by Claude"]
    credits_y = y
    yel_bottom = credits_y + 3 * LINE + M
    bar_bottom = yel_bottom + M

    p.cnv(YEL_X, YEL_Y, YEL_W, yel_bottom - YEL_Y, bg=YELLOW)
    p.cnv(PINK_X, PINK_TOP, PINK_W, bar_bottom - PINK_TOP, bg=PINK)
    p.cnv(BLUE_X, BLUE_TOP, BLUE_W, bar_bottom - BLUE_TOP, bg=BLUE)
    p.heading(HX, YEL_Y + M, OBJ, 22)
    p.heading(HX, YEL_Y + 3 * M, SUBTITLE, 16)

    # utility messages
    snd1 = p.add(f"#X obj {CX} {util_y + STEP} s {bus};")
    x = CX
    for text in ("bank A 1", "bank C 15", "bank D 3", "pgm 0", "dumpreq"):
        m = p.add(f"#X msg {x} {util_y} {text};")
        p.conn(m, 0, snd1, 0)
        x += box_w(text) + 16
    p.add(f"#X text {CX + 100} {util_y + STEP} C15 = LatelyBass {COMMA} pgm 0 = I01;")

    # demo: a chord every second
    tg = p.add(f"#X obj {CX} {demo_y} tgl 19 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000 0 1;")
    p.add(f"#X text {CX + 30} {demo_y} demo: a chord every second;")
    p.add(f"#X msg {CX + 300} {demo_y} {SEMI} pd dsp 1;")
    p.add(f"#X msg {CX + 380} {demo_y} {SEMI} pd dsp 0;")
    mt = p.add(f"#X obj {CX} {demo_y + STEP} metro 1000;")
    tb = p.add(f"#X obj {CX} {demo_y + 2 * STEP} t b b;")
    on = p.add(f"#X msg {CX + 160} {demo_y + 3 * STEP} note 43 100 {COMMA} note 50 100 {COMMA} note 58 100;")
    dl = p.add(f"#X obj {CX} {demo_y + 3 * STEP} delay 500;")
    off = p.add(f"#X msg {CX} {demo_y + 4 * STEP} note 43 0 {COMMA} note 50 0 {COMMA} note 58 0;")
    sd = p.add(f"#X obj {CX} {demo_y + 5 * STEP} s {bus};")
    p.conn(tg, 0, mt, 0); p.conn(mt, 0, tb, 0); p.conn(tb, 1, on, 0); p.conn(tb, 0, dl, 0)
    p.conn(dl, 0, off, 0); p.conn(on, 0, sd, 0); p.conn(off, 0, sd, 0)

    # example controls of the FM view
    snds = {}
    for i, (cname, _) in enumerate(CONTROLS):
        r, c = divmod(i, 4)
        cx = CX + c * 112
        ay = ctl_y0 + ROW * r
        if r not in snds:
            snds[r] = p.add(f"#X obj {CX} {ay + 2 * STEP} s {bus};")
        a = p.add(f"#X floatatom {cx} {ay} 4 0 127 0 - - -;")
        m = p.add(f"#X msg {cx} {ay + STEP} fm {cname} {DOLLAR}1;")
        p.conn(a, 0, m, 0)
        p.conn(m, 0, snds[r], 0)

    # MIDI in: controller layer, notes, CC, program change, bend -> bus
    rco = p.add(f"#X obj {CX} {cc_y} r {out};")
    pcc = p.add(f"#X obj {CX} {cc_y + STEP} txfmcc {CHANNEL};")
    scc = p.add(f"#X obj {CX} {cc_y + 2 * STEP} s {bus};")
    p.add(f"#X text {CX + 130} {cc_y + STEP} <- controller layer: the pages of the FM editor;")
    p.conn(rco, 0, pcc, 0); p.conn(pcc, 0, scc, 0)
    cols = [("notein", "pack f f f", f"note {DOLLAR}1 {DOLLAR}2 {DOLLAR}3", 3),
            ("ctlin", "pack f f f", f"cc {DOLLAR}2 {DOLLAR}1 {DOLLAR}3", 3),
            ("pgmin", "- 1", f"pgm {DOLLAR}1", 1),
            ("bendin", "- 8192", f"bend {DOLLAR}1", 1)]
    xs = [CX, CX + 110, CX + 210, CX + 300]
    for xx, (a_, b_, m_, n_) in zip(xs, cols):
        oa = p.add(f"#X obj {xx} {ni_y} {a_};")
        ob = p.add(f"#X obj {xx} {ni_y + STEP} {b_};")
        om = p.add(f"#X msg {xx} {ni_y + 2 * STEP} {m_};")
        os_ = p.add(f"#X obj {xx} {ni_y + 3 * STEP} s {bus};")
        for k in range(n_):
            p.conn(oa, k, ob, k)
        p.conn(ob, 0, om, 0); p.conn(om, 0, os_, 0)

    # the object, its outputs, the editors
    rcv = p.add(f"#X obj {CX} {r_y} r {bus};")
    o = p.add(f"#X obj {CX} {obj_y} {OBJ} {ROM} {NVRAM};")
    p.conn(rcv, 0, o, 0)
    mull = p.add(f"#X obj {CX} {mul_y} *~ 0.5;")
    mulr = p.add(f"#X obj {CX + 70} {mul_y} *~ 0.5;")
    dac = p.add(f"#X obj {CX} {dac_y} dac~;")
    p.conn(o, 0, mull, 0); p.conn(o, 1, mulr, 0); p.conn(mull, 0, dac, 0); p.conn(mulr, 0, dac, 1)
    sev = p.add(f"#X obj {CX + 340} {mul_y} s {out};")
    rl = p.add(f"#X obj {CX + 150} {mul_y} route voice status;")
    p.conn(o, 2, sev, 0); p.conn(o, 2, rl, 0)
    lvn = p.add(f"#X obj {CX + 150} {dac_y} list;")
    p.conn(rl, 0, lvn, 0)
    vn = p.add(f"#X symbolatom {CX + 150} {dac_y + STEP} 10 0 0 0 - - -;")
    p.conn(lvn, 0, vn, 0)
    lst = p.add(f"#X obj {CX + 270} {dac_y} list;")
    p.conn(rl, 1, lst, 0)
    st = p.add(f"#X symbolatom {CX + 270} {dac_y + STEP} 14 0 0 0 - - -;")
    p.conn(lst, 0, st, 0)
    p.add(f"#X text {CX + 150} {dac_y + 2 * STEP} voice name / status;")
    p.add(f"#X obj {CX + 300} {r_y} txfm {bus};")
    p.add(f"#X obj {CX + 300} {obj_y} txedit {bus};")
    p.add(f"#X text {CX + 400} {r_y} <- click: editors;")

    for title, lines, sy, para in placed:
        p.heading(HX, sy, title, 14)
        p.comment(TX, sy + 28, lines, para=para)
    for title, intro, msgs, sy in placed_ex:
        p.heading(HX, sy, title, 14)
        p.comment(TX, sy + 28, intro, para=True)
        ly = sy + 28 + LINE * nlines(intro) + OBJ_GAP
        nr = (len(msgs) + 2) // 3
        sp = p.add(f"#X obj {CX + 380} {ly + STEP * nr} s {bus};")
        for i, txt in enumerate(msgs):
            r, c = divmod(i, 3)
            m = p.add(f"#X msg {CX + c * 130} {ly + STEP * r} {esc(txt)};")
            p.conn(m, 0, sp, 0)
    p.comment(TX, credits_y, credits)

    head = f"#N canvas 348 20 {YEL_X + YEL_W + 2 * M + 20} {bar_bottom + M} 12;"
    txt = head + "\n" + "\n".join(p.lines) + "\n" + "\n".join(p.conns) + "\n"
    H.check(txt)
    return txt


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "pd", OBJ + "-help.pd")
    with open(out, "w") as f:
        f.write(build())
    print("wrote", out)

#!/usr/bin/env python3
"""Generate pd/tx81z~-help.pd in the layout of Peter's help patches (pd-help-layout, as pdoly8~).

usage: python3 tools/gen_help.py [outfile]   (default: pd/tx81z~-help.pd)

Yellow writing surface, pink strip, blue panel, blue heading squares, one [s]/[r] bus,
text sections below. Kept short: a few example parameters, the list of all voice
parameters, and the editor (txedit) for everything else.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ctlayer as CL                             # noqa: E402
import tx81z_params as T                         # noqa: E402

BS = chr(92)
SEMI, COMMA, DOLLAR, SPACE = BS + ";", BS + ",", BS + "$", BS + " "

# ---- grid (pd-help-layout) ----------------------------------------------
M = 20
YEL_X, YEL_Y, YEL_W = 2 * M, M, 700
PINK_X, PINK_W = M, M
PINK_TOP = YEL_Y + 3 * M
BLUE_W = 200
BLUE_X = YEL_X + YEL_W + M - BLUE_W
BLUE_TOP = PINK_TOP + 3 * M
HX = YEL_X + M          # heading squares
LBL = 24
TX = HX + LBL - 2       # text column
CX = TX                 # objects flush with the text
LINE = 16
MAXCH = 62
TXT_GAP = 28
OBJ_GAP = 22
BOX = 21
STEP = 30
RIGHT = BLUE_X - 10     # nothing reaches into the blue panel
PINK, YELLOW, BLUE = "#ffcfd6", "#fafaf2", "#1e3791"

OBJ, SHORT = "tx81z~", "tx81z"
SUBTITLE = "Yamaha TX81Z emulation running the original firmware"
ROM, NVRAM = "tx81z-v1.6.bin", "tx81z.nvram"
CHANNEL = 2             # MIDI channel of the controller layer [txcc 2]

# example controls: name, default, description (two rows of 4)
CONTROLS = [
    ("alg", 0, "algorithm 0-7 (shown as 1-8 on the TX81Z)."),
    ("fb", 0, "feedback of operator 4, 0-7."),
    ("out1", 99, "output level of operator 1, 0-99."),
    ("out2", 0, "output level of operator 2, 0-99."),
    ("out3", 0, "output level of operator 3, 0-99."),
    ("out4", 0, "output level of operator 4, 0-99."),
    ("lfs", 35, "LFO speed, 0-99."),
    ("pmd", 0, "pitch modulation depth of the LFO, 0-99."),
]

# longer descriptions for the parameter list (the label of tools/tx81z_params.py otherwise)
DESC = {
    "alg": "algorithm (1-8 on the TX81Z)", "fb": "feedback of operator 4",
    "lfw": "LFO wave: saw square triangle S/H", "trps": "transpose (24 = middle C)",
    "poly": "poly / mono", "pmod": "portamento mode (full time)", "port": "portamento time",
    "pon": "portamento on / off", "sus": "sustain on / off", "chor": "chorus (set 0)",
    "bcpb": "breath control pitch bias (50 = centre)", "bceb": "breath control EG bias",
    "bcp": "breath control pitch", "bca": "breath control amplitude", "mwp": "mod wheel pitch",
    "mwa": "mod wheel amplitude", "fcv": "foot controller volume", "fcp": "foot controller pitch",
    "fca": "foot controller amplitude", "rev": "reverb rate (0 = off, 7 = fast)",
    "pbr": "pitch bend range in semitones", "pms": "pitch modulation sensitivity",
    "ams": "amplitude modulation sensitivity", "sync": "LFO key sync",
    "pmd": "pitch modulation depth", "amd": "amplitude modulation depth",
}
OP_DESC = {
    "ar": "attack rate", "d1r": "decay 1 rate", "d2r": "decay 2 rate", "rr": "release rate",
    "d1l": "decay 1 level", "ls": "level scaling", "rs": "rate scaling", "ebs": "EG bias sensitivity",
    "ame": "amplitude modulation on / off", "kvs": "key velocity sensitivity", "out": "output level",
    "crs": "frequency coarse", "det": "detune (3 = centre)", "fix": "fixed frequency (0 ratio, 1 fixed)",
    "fixr": "fixed range 250 Hz ... 32 kHz", "fin": "frequency fine", "osw": "waveform W1-W8 (0-7)",
    "sh": "EG shift: 0 96 dB, 1 48, 2 24, 3 12",
}


def param_lines():
    """the list of all voice parameters, one line each (<= 62 characters)"""
    out = ["Voice (data values as stored in the TX81Z):"]
    for g, num, nm, lbl, lo, hi, off, names in T.COMMON:
        out.append(f"{nm} {lo}-{hi}: {DESC.get(nm, lbl.lower())}")
    out.append(" ")
    out.append("Operators: name + operator number 1-4, e.g. ar1, osw4:")
    for _, sh, lbl, lo, hi, _ in T.OP_VCED + T.OP_ACED:
        out.append(f"{sh}1-{sh}4 {lo}-{hi}: {OP_DESC[sh]}")
    return out


def esc(s):
    return s.replace(",", " " + COMMA).replace(";", SEMI).replace("$", DOLLAR)


def label(s):
    return s.replace(" ", SPACE)


class Patch:
    def __init__(self):
        self.lines, self.conns, self.n = [], [], 0

    def add(self, text):
        self.lines.append(text)
        self.n += 1
        return self.n - 1

    def conn(self, a, ao, b, bi):
        self.conns.append(f"#X connect {a} {ao} {b} {bi};")

    def cnv(self, x, y, w, h, lbl="empty", xo=20, yo=12, fs=30, bg=YELLOW, sel=19):
        return self.add(f"#X obj {x} {y} cnv {sel} {w} {h} empty empty {lbl} {xo} {yo} 0 {fs} {bg} #000000 0;")

    def heading(self, x, y, text, fs):
        return self.cnv(x, y, 10, 10, label(text), LBL, 6, fs, BLUE, 10)

    def comment(self, x, y, lines, width=MAXCH + 2, para=False):
        if not para:
            for ln in lines:
                assert len(ln) <= MAXCH, f"line too long ({len(ln)}): {ln}"
        body = (" " + SEMI + " ").join(esc(l) for l in lines)
        return self.add(f"#X text {x} {y} {body}, f {width};")


def wrap(text, width=MAXCH):
    out, cur = [], ""
    for w in text.split():
        if cur and len(cur) + 1 + len(w) > width:
            out.append(cur); cur = w
        else:
            cur = (cur + " " + w).strip()
    return out + [cur]


def nlines(paras, width=MAXCH):
    return sum(len(wrap(t, width)) for t in paras)


def box_w(text):
    return 7 * len(text) + 10


# example message blocks: title, intro paragraphs, messages (3 per row -> [s bus])
EXBLOCKS = [
    ("Program change", ["The TX81Z selects I01-C32 with program changes 0-127 (I 0-31, A 32-63, "
                        "B 64-95, C 96-127). Bank D only via its program change table. Program changes "
                        "need the program change switch on (pcswitch 1, the factory init sets it). After "
                        "every voice change the firmware sends its voice dump: tx81z~ reads it, the editor "
                        "and the controller follow."],
     ["pgm 110", "pgm 32", "pgm 0", "pcswitch 1", "dumpreq"]),
    ("Front panel", ["The buttons of the TX81Z: press <button> taps one (150 ms), button <button> 0/1 "
                     "holds it. The display is above (lcd), the LEDs come as led <1-4 bitmask>. "
                     "Buttons: store utility edit play param- param+ dec inc vol- vol+ cursor."],
     ["press play", "press edit", "press utility", "press param-", "press param+", "press dec",
      "press inc", "press vol-", "press vol+", "press store", "press cursor"]),
]


def build():
    p = Patch()
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
    y = dac_y + 2 * STEP + BOX + 32

    cc = [f"[txcc {CHANNEL}] above is the controller layer of tx81z~ (the same as p8cc of pdoly8~): "
          f"MIDI CC on channel {CHANNEL} (the argument, 0 = all channels) -> n <name> <value> onto the bus. "
          "Every instrument has its own channel and the same CC pages, so the controller keeps one setup: "
          "page k = row k in the editor.",
          "Pages: 20-119 without 32, 38, 64-69, 84, 88, 96-101.",
          "Changes from elsewhere (program change, editor, messages) go back to the controller as CC on "
          "the same channel and number. Pickup: after such a change an encoder acts only when it reaches "
          "the value or crosses it (pickup 0 turns that off). CC 3 or sync sends all values to the controller."]
    for k, (t, ns) in enumerate(T.CC_ROWS):
        cc.append(f"page {k + 1} {t}, CC {CL.page_text(k)}: " + " ".join(ns))
    cc.append("A parameter change is sent to the firmware as SysEx (7 bytes, about 2 ms at 31250 baud). "
              "tx81z~ sends only the newest value of each parameter and only while its MIDI input is idle, "
              "so notes are never delayed by a turning knob. The steps themselves are the firmware's "
              "(e.g. 100 output levels): a fast sweep can sound stepped, as on the real TX81Z.")

    sections = [
        ("Inlets and outlets", ["Inlet: messages (see Messages)",
                                "Outlet 0, 1: signal left / right (~55.9 kHz resampled)",
                                "Outlet 2: lcd, led, midiout, info, voice"]),
        ("Messages", [
            "note <p> <v> [ch], cc <n> <v> [ch], pgm <n> [ch],",
            "  bend <-8192..8191> [ch]: MIDI to the TX81Z",
            "<float> / list, midi <bytes>: raw MIDI bytes ([midiin])",
            "<name> <value>: a voice parameter, TX81Z data value,",
            "  e.g. alg 4, out1 99 (all names: Voice parameters)",
            "n <name> <0-127>: the same from a pot / CC position",
            "ninfo <name>: outlet 2 gets info <name> <0-127> <value>,",
            "  every change is reported this way by itself",
            "vced <p> <d>, aced <p> <d>: raw parameter change",
            "dumpreq: ask the firmware for the edit buffer",
            "press <button>, button <button> 0/1: front panel",
            "rom <file>, nvram <file>, save, reset: firmware and",
            "  battery RAM (saved on save and when the patch closes)",
            "factoryinit: presets, MIDI setup, program change on",
            "  (runs by itself if the nvram file does not exist yet)",
            "pcswitch 0/1/2, sysparam <p> <d>: system setup",
            "gain <f>: output gain (default 1)"]),
        ("Editor (txedit)", [
            "[txedit " + bus.replace(BS, "") + "] above: click it to open the editor. All 100 voice "
            "parameters in rows of 8: rows 1-11 are the CC pages of the controller layer, then two rows "
            "without CC. Each cell: name and CC number, slider and number box 0-127 (n <name> <value>) "
            "and the value as on the TX81Z. On top: voice name, program change (bank + patch), the LCD.",
            "It sends to the bus and reads outlet 2 from the -out bus: tx81z~ reports every change there "
            "(info), so the controller, program changes and messages show up in it. In your own patch: "
            "[txedit <name>] next to [r <name>] -> [tx81z~] -> outlet 2 -> [s <name>-out]."], True),
        ("MIDI CC: controller layer (txcc)", cc, True),
        ("Controls (example parameters)", [f"{n} ({d}): {t}" for n, d, t in CONTROLS], True),
        ("Voice parameters", param_lines()),
        ("Firmware and battery RAM", [
            f"Arguments: [{OBJ} <firmware> <battery RAM file>], paths relative to the patch. The firmware "
            f"(64 KB, e.g. {ROM} from the MAME set tx81z) is not included. The battery RAM file keeps "
            "voices, performances and the system setup. If it does not exist, tx81z~ runs the factory "
            "init (test mode, about 16 s): presets into the internal memory, MIDI omni on, program change "
            "switch on, PLAY mode, then it saves the file."], True),
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
    credits = ["Credits: Modelled on the Yamaha TX81Z service manual,",
               "HD6303X core and machine wiring after MAME (Aaron Giles,",
               "AJR), YM2414 by ymfm (Aaron Giles) with help by Claude"]
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
    for text in ("pgm 110", "pgm 32", "dumpreq", "save", "factoryinit"):
        m = p.add(f"#X msg {x} {util_y} {text};")
        p.conn(m, 0, snd1, 0)
        x += box_w(text) + 16
    p.add(f"#X text {CX + 100} {util_y + STEP} pgm 110 = C15 LatelyBass {COMMA} pgm 32 = A01;")

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

    # example controls
    snds = {}
    for i, (cname, dv, _) in enumerate(CONTROLS):
        r, c = divmod(i, 4)
        cx = CX + c * 112
        ay = ctl_y0 + ROW * r
        if r not in snds:
            snds[r] = p.add(f"#X obj {CX} {ay + 2 * STEP} s {bus};")
        pr = T.BY_NAME[cname]
        a = p.add(f"#X floatatom {cx} {ay} 4 {pr[3]} {pr[4]} 0 - - -;")
        m = p.add(f"#X msg {cx} {ay + STEP} {cname} {DOLLAR}1;")
        p.conn(a, 0, m, 0)
        p.conn(m, 0, snds[r], 0)

    # MIDI in: controller layer, notes, CC, program change, bend -> bus
    rco = p.add(f"#X obj {CX} {cc_y} r {out};")
    pcc = p.add(f"#X obj {CX} {cc_y + STEP} txcc {CHANNEL};")
    scc = p.add(f"#X obj {CX} {cc_y + 2 * STEP} s {bus};")
    p.add(f"#X text {CX + 130} {cc_y + STEP} <- controller layer: CC pages on channel {CHANNEL};")
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

    # the object
    rcv = p.add(f"#X obj {CX} {r_y} r {bus};")
    o = p.add(f"#X obj {CX} {obj_y} {OBJ} {ROM} {NVRAM};")
    p.conn(rcv, 0, o, 0)
    mull = p.add(f"#X obj {CX} {mul_y} *~ 0.5;")
    mulr = p.add(f"#X obj {CX + 70} {mul_y} *~ 0.5;")
    dac = p.add(f"#X obj {CX} {dac_y} dac~;")
    p.conn(o, 0, mull, 0); p.conn(o, 1, mulr, 0); p.conn(mull, 0, dac, 0); p.conn(mulr, 0, dac, 1)
    sev = p.add(f"#X obj {CX + 340} {mul_y} s {out};")
    rl = p.add(f"#X obj {CX + 150} {mul_y} route lcd voice;")
    p.conn(o, 2, sev, 0); p.conn(o, 2, rl, 0)
    un = p.add(f"#X obj {CX + 150} {dac_y} unpack s s;")
    p.conn(rl, 0, un, 0)
    l0 = p.add(f"#X symbolatom {CX + 150} {dac_y + STEP} 16 0 0 0 - - -;")
    l1 = p.add(f"#X symbolatom {CX + 150} {dac_y + 2 * STEP} 16 0 0 0 - - -;")
    p.conn(un, 0, l0, 0); p.conn(un, 1, l1, 0)
    vn = p.add(f"#X symbolatom {CX + 300} {dac_y + STEP} 10 0 0 0 - - -;")
    p.conn(rl, 1, vn, 0)
    p.add(f"#X text {CX + 300} {dac_y + 2 * STEP} LCD / voice name;")
    p.add(f"#X obj {CX + 300} {r_y} txedit {bus};")
    p.add(f"#X text {CX + 300} {obj_y} <- click: editor;")

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
    check(txt)
    return txt


def check(txt):
    """every connection runs down (receiver >= BOX + 8 below the sender) and is not longer than
    200 px; nothing left of the text column, nothing in the blue panel"""
    L = txt.split(";\n")
    objs = [l for l in L[1:] if l.startswith("#X") and not l.startswith("#X connect")]
    pos = [int(l.split()[3]) for l in objs]
    bad = []
    for l in L:
        if l.startswith("#X connect"):
            a, b = int(l.split()[2]), int(l.split()[4]); d = pos[b] - pos[a]
            if d < BOX + 8 or d > 200:
                bad.append((objs[a][:50], objs[b][:50], d))
    for l in objs:
        if " cnv " in l:
            continue
        x = int(l.split()[2])
        if x < TX:
            bad.append(("left of the text column", l[:60]))
        body = re.sub(r"^#X \w+ -?\d+ -?\d+ ", "", l).replace(BS, "")
        w = 7 * len(body.split(", f ")[0]) + 10 if not l.startswith("#X text") else 0
        if l.startswith("#X text"):
            m = re.search(r", f (\d+)$", l)
            w = 7 * min(int(m.group(1)) if m else 60, len(body)) if m else 7 * len(body)
        if l.startswith(("#X floatatom", "#X symbolatom")):
            w = 7 * int(l.split()[4]) + 10
        if x + w > RIGHT and not l.startswith("#X text"):
            bad.append(("reaches into the blue panel", l[:60], x + w))
    assert not bad, bad


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "pd", OBJ + "-help.pd")
    with open(out, "w") as f:
        f.write(build())
    print("wrote", out)

#!/usr/bin/env python3
"""The TX81Z voice parameters: one table for the external, the controller layer, the editor
and the help patch.

Source: TX81Z service manual p. 19-20 (VCED 0-92, ACED 0-22). In the VCED and ACED data the
operators are stored in the order OP4, OP3, OP2, OP1:
    VCED  op k: (4 - k) * 13 + p     (p = 0 AR ... 12 DET),   common 52-76
    ACED  op k: (4 - k) * 5 + p      (p = 0 FIX ... 4 SHFT),  common 20-22
Parameter changes: F0 43 1n 12 pp dd F7 (VCED) and F0 43 1n 13 pp dd F7 (ACED).

Every entry: name, group 'V' / 'A', parameter number, lo, hi, display offset, label,
value names ("a|b|c" for lo, lo+1, ...; "" = number, plus offset).

usage: python3 tools/tx81z_params.py   (writes pd/tx81z_params.h)
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

OP_VCED = [  # offset, short, label, lo, hi, names
    (0, "ar", "Attack rate", 0, 31, ""),
    (1, "d1r", "Decay 1 rate", 0, 31, ""),
    (2, "d2r", "Decay 2 rate", 0, 31, ""),
    (3, "rr", "Release rate", 0, 15, ""),
    (4, "d1l", "D1 level", 0, 15, ""),
    (5, "ls", "Level scale", 0, 99, ""),
    (6, "rs", "Rate scale", 0, 3, ""),
    (7, "ebs", "EG bias sens", 0, 7, ""),
    (8, "ame", "AM enable", 0, 1, "off|on"),
    (9, "kvs", "Vel sens", 0, 7, ""),
    (10, "out", "Output level", 0, 99, ""),
    (11, "crs", "Freq coarse", 0, 63, ""),
    (12, "det", "Detune", 0, 6, "-3|-2|-1|0|+1|+2|+3"),
]
OP_ACED = [
    (0, "fix", "Fixed freq", 0, 1, "ratio|fixed"),
    (1, "fixr", "Fixed range", 0, 7, "250Hz|500Hz|1kHz|2kHz|4kHz|8kHz|16kHz|32kHz"),
    (2, "fin", "Freq fine", 0, 15, ""),
    (3, "osw", "Waveform", 0, 7, "W1|W2|W3|W4|W5|W6|W7|W8"),
    (4, "sh", "EG shift", 0, 3, "96dB|48dB|24dB|12dB"),
]
COMMON = [  # group, number, name, label, lo, hi, offset, names
    ("V", 52, "alg", "Algorithm", 0, 7, 0, "1|2|3|4|5|6|7|8"),
    ("V", 53, "fb", "Feedback", 0, 7, 0, ""),
    ("V", 54, "lfs", "LFO speed", 0, 99, 0, ""),
    ("V", 55, "lfd", "LFO delay", 0, 99, 0, ""),
    ("V", 56, "pmd", "Pitch mod", 0, 99, 0, ""),
    ("V", 57, "amd", "Amp mod", 0, 99, 0, ""),
    ("V", 58, "sync", "LFO sync", 0, 1, 0, "off|on"),
    ("V", 59, "lfw", "LFO wave", 0, 3, 0, "saw|square|triangle|S/H"),
    ("V", 60, "pms", "PM sens", 0, 7, 0, ""),
    ("V", 61, "ams", "AM sens", 0, 3, 0, ""),
    ("V", 62, "trps", "Transpose", 0, 48, -24, ""),
    ("V", 63, "poly", "Poly/mono", 0, 1, 0, "poly|mono"),
    ("V", 64, "pbr", "Bend range", 0, 12, 0, ""),
    ("V", 65, "pmod", "Porta mode", 0, 1, 0, ""),
    ("V", 66, "port", "Porta time", 0, 99, 0, ""),
    ("V", 67, "fcv", "FC volume", 0, 99, 0, ""),
    ("V", 68, "sus", "Sustain", 0, 1, 0, "off|on"),
    ("V", 69, "pon", "Portamento", 0, 1, 0, "off|on"),
    ("V", 70, "chor", "Chorus", 0, 1, 0, "off|on"),
    ("V", 71, "mwp", "MW pitch", 0, 99, 0, ""),
    ("V", 72, "mwa", "MW amp", 0, 99, 0, ""),
    ("V", 73, "bcp", "BC pitch", 0, 99, 0, ""),
    ("V", 74, "bca", "BC amp", 0, 99, 0, ""),
    ("V", 75, "bcpb", "BC p bias", 0, 99, -50, ""),
    ("V", 76, "bceb", "BC EG bias", 0, 99, 0, ""),
    ("A", 20, "rev", "Reverb rate", 0, 7, 0, ""),
    ("A", 21, "fcp", "FC pitch", 0, 99, 0, ""),
    ("A", 22, "fca", "FC amp", 0, 99, 0, ""),
]


def params():
    """[(name, group, number, lo, hi, offset, label, names), ...]"""
    out = []
    for g, num, nm, lbl, lo, hi, off, names in COMMON:
        out.append((nm, g, num, lo, hi, off, lbl, names))
    for k in (1, 2, 3, 4):
        for off_, sh, lbl, lo, hi, names in OP_VCED:
            out.append((f"{sh}{k}", "V", (4 - k) * 13 + off_, lo, hi, 0, lbl, names))
        for off_, sh, lbl, lo, hi, names in OP_ACED:
            out.append((f"{sh}{k}", "A", (4 - k) * 5 + off_, lo, hi, 0, lbl, names))
    return out


PARAMS = params()
BY_NAME = {p[0]: p for p in PARAMS}
assert len(BY_NAME) == len(PARAMS)


def op(k, *shorts):
    return [f"{s}{k}" for s in shorts]


# controller pages (row k = CC page k of tools/ctlayer.py): 10 pages of 8, page 11 of 4
CC_ROWS = [
    ("VOICE", ["alg", "fb", "out1", "out2", "out3", "out4", "trps", "rev"]),
    ("OP1", op(1, "crs", "fin", "osw", "ar", "d1r", "d1l", "d2r", "rr")),
    ("OP2", op(2, "crs", "fin", "osw", "ar", "d1r", "d1l", "d2r", "rr")),
    ("OP3", op(3, "crs", "fin", "osw", "ar", "d1r", "d1l", "d2r", "rr")),
    ("OP4", op(4, "crs", "fin", "osw", "ar", "d1r", "d1l", "d2r", "rr")),
    ("DETUNE / VELOCITY", [f"det{k}" for k in (1, 2, 3, 4)] + [f"kvs{k}" for k in (1, 2, 3, 4)]),
    ("LFO", ["lfw", "lfs", "lfd", "pmd", "amd", "pms", "ams", "sync"]),
    ("SCALING", [f"rs{k}" for k in (1, 2, 3, 4)] + [f"ls{k}" for k in (1, 2, 3, 4)]),
    ("FIXED / AM", [f"fix{k}" for k in (1, 2, 3, 4)] + [f"ame{k}" for k in (1, 2, 3, 4)]),
    ("EG SHIFT / BIAS", [f"sh{k}" for k in (1, 2, 3, 4)] + [f"ebs{k}" for k in (1, 2, 3, 4)]),
    ("CONTROLLERS", ["pbr", "port", "mwp", "mwa"]),
]
# the rest: in the editor only (mouse)
EXTRA_ROWS = [
    ("FIXED RANGE / PLAY", [f"fixr{k}" for k in (1, 2, 3, 4)] + ["poly", "pmod", "pon", "sus"]),
    ("BREATH / FOOT", ["bcp", "bca", "bcpb", "bceb", "fcp", "fca", "fcv", "chor"]),
]
ALL_ROWS = CC_ROWS + EXTRA_ROWS
_listed = [n for _, ns in ALL_ROWS for n in ns]
assert len(_listed) == len(set(_listed)) and set(_listed) == set(BY_NAME), set(BY_NAME) ^ set(_listed)


def write_header(path):
    lines = ["/* generated by tools/tx81z_params.py - do not edit */",
             "/* name, group ('V' VCED / 'A' ACED), parameter number, lo, hi, display offset, label, value names */"]
    lines.append("static const t_txparam TXPARAMS[] = {")
    for nm, g, num, lo, hi, off, lbl, names in PARAMS:
        lines.append(f'    {{"{nm}", \'{g}\', {num}, {lo}, {hi}, {off}, "{lbl}", "{names}"}},')
    lines.append("};")
    lines.append("#define NTXPARAMS ((int)(sizeof TXPARAMS / sizeof TXPARAMS[0]))")
    open(path, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    out = os.path.join(ROOT, "pd", "tx81z_params.h")
    write_header(out)
    print("wrote", out, f"({len(PARAMS)} parameters, {sum(len(n) for _, n in CC_ROWS)} on CC pages)")

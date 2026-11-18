#!/usr/bin/env python3
"""Python model of the firmware's voice -> YM2414 register logic (reference for the C++ engine).
Level, velocity, level scaling and LFO speed follow the firmware routines (addresses in the docstrings)
with tables read from the user's ROM (romtables.py); measured.json holds what is still measured."""
import json
import os

import romtables

M = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "measured.json")))
RT = romtables.load()   # tables from the user's ROM (not stored in the repository)

# coarse 0..63 -> (MUL, DT2): all MUL x DT2 ratios, sorted, duplicates removed
_mul = [0.5] + list(range(1, 16))
_dt2 = [1.0, 1.41, 1.57, 1.73]
_c = []
for r, m, d in sorted((round(_mul[m] * _dt2[d], 2), m, d) for m in range(16) for d in range(4)):
    if not any(abs(r - x[0]) < 1e-9 for x in _c):
        _c.append((r, m, d))
CRS = [(m, d) for _, m, d in _c]
DT1 = [7, 6, 5, 0, 1, 2, 3]
CAR = [0x1, 0x1, 0x1, 0x1, 0x5, 0x7, 0x7, 0xF]   # carriers per algorithm, bit k-1 = OP k


def is_carrier(alg, k):
    return (CAR[alg] >> (k - 1)) & 1


def op_fields(v, a, k):
    """operator k (1..4) of VCED v / ACED a"""
    o = v[(4 - k) * 13:(4 - k) * 13 + 13]
    x = a[(4 - k) * 5:(4 - k) * 5 + 5]
    ar, d1r, d2r, rr, d1l, ls, rs, ebs, ame, kvs, out, crs, det = o
    fix, fixr, fin, osw, sh = x
    return dict(ar=ar, d1r=d1r, d2r=d2r, rr=rr, d1l=d1l, ls=ls, rs=rs, ebs=ebs, ame=ame, kvs=kvs, out=out,
                crs=crs, det=det, fix=fix, fixr=fixr, fin=fin, osw=osw, sh=sh)


def lfo_rate(lfs, lfw):
    """$9B02: LFO speed 0..99 -> register 0x18 (sample and hold uses the scaled speed directly)"""
    d = (lfs * 0xA5 * 4 >> 8) & 0xFF
    return d if lfw == 3 else 255 - RT["mod_att"][d]


def slot(ch, k):
    """chip slot offset of operator k on channel ch (OP4 = +0, OP3 = +8, OP2 = +16, OP1 = +24)"""
    return ch + 8 * (4 - k)


def load_writes(v, a):
    """ordered register writes of a voice load (all 8 channels), as the firmware does it"""
    w = []
    ops = {k: op_fields(v, a, k) for k in (1, 2, 3, 4)}
    alg, fb = v[52], v[53]
    for ch in range(8):
        for k in (4, 3, 2, 1):
            w.append((0xE0 + slot(ch, k), 0xFE))
        for k in (4, 3, 2, 1):
            w.append((0xC0 + slot(ch, k), 0x28))
    for ch in range(8):
        w.append((0x08, ch))
    order = [(k, ch) for k in (4, 3, 2, 1) for ch in range(8)]

    def mulfield(o):
        if o["fix"]:
            return (o["fixr"] << 4) | (o["crs"] >> 2), CRS[o["crs"]][1]
        m, d = CRS[o["crs"]]
        return (DT1[o["det"]] << 4) | m, d
    for k, ch in order:
        o = ops[k]
        w.append((0x80 + slot(ch, k), (o["rs"] << 6) | (o["fix"] << 5) | o["ar"]))
    for k, ch in order:
        o = ops[k]
        w.append((0xA0 + slot(ch, k), (o["ame"] << 7) | o["d1r"]))
    for k, ch in order:
        o = ops[k]
        w.append((0xC0 + slot(ch, k), (mulfield(o)[1] << 6) | o["d2r"]))
    for k, ch in order:
        w.append((0xC0 + slot(ch, k), 0x28))
    for ch in range(8):
        w.append((0x20 + ch, 0x80 | (fb << 3) | alg))
    for k, ch in order:
        w.append((0x40 + slot(ch, k), mulfield(ops[k])[0]))
    for k, ch in order:
        o = ops[k]
        fin = min(o["fin"], 7) if o["crs"] < 4 else o["fin"]
        w.append((0x40 + slot(ch, k), 0x80 | (o["osw"] << 4) | fin))
    rev = a[20]
    for k, ch in order:
        sh = 0 if k == 1 else ops[k]["sh"]                       # no EG shift on OP1 (always a carrier)
        w.append((0xC0 + slot(ch, k), (sh << 6) | 0x28 | rev))
    pms, ams = v[60], v[61]
    for ch in range(8):
        w.append((0x38 + ch, (pms << 4) | ams))
    lfw, lfs = v[59], v[54]
    w.append((0x1B, lfw))
    w.append((0x18, lfo_rate(lfs, lfw)))
    for k, ch in order:
        o = ops[k]
        w.append((0xE0 + slot(ch, k), ((15 - o["d1l"]) << 4) | max(1, o["rr"])))
    bc = M["bceb"][v[76]]
    if bc is not None:
        for ch in range(8):
            w.append((ch, bc))
    return w


def by_reg(writes):
    out = {}
    for r, x in writes:
        out.setdefault(r, []).append(x)
    return out


def semitone(key, trps):
    s = key + trps - 37
    while s < 0:
        s += 12
    while s > 95:
        s -= 12
    return s


CODE = [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14]


def keycode(s):
    return ((s // 12) << 4) | CODE[s % 12]


# ---- exact level path, read from the firmware (tables from the user's ROM, see romtables.py) ----


def sat8(x):
    return 255 if x > 255 else x


def op_base_att(o, alg, k):
    """$9F00: output level -> attenuation, algorithm offset, in 1/2 TL steps"""
    out = o["out"]
    a = RT["out_att"][out] if out < 20 else 99 - out
    oi = 4 - k                     # firmware operator index: 0 = OP4 ... 3 = OP1
    if alg < 4:
        return 2 * a               # (carrier OP1 or modulator: no offset)
    add = {4: 8 if oi & 2 else 0, 5: 13 if oi else 0, 6: 13 if oi else 0, 7: 16}[alg]
    if add:
        a = min(0x7F, a + add)
    return 2 * a


def ls_curve(o, alg, k):
    """$9F70: 29-point level scaling curve per operator (1/2 TL steps)"""
    depth = (o["ls"] * 0xA5 * 4 >> 8) & 0xFF
    base = op_base_att(o, alg, k)
    return [sat8(base + ((RT["ls_curve"][i] * depth) >> 8)) for i in range(29)]


def ls_index(key, trps):
    """$A18C: key -> curve index"""
    b = key - 13 + trps - 24
    while b < 0:
        b += 12
    while b >= 0x60:
        b -= 12
    g = RT["key_group"][b] - 14
    g = min(max(g, 0), 0x70)
    return g >> 2


def note_level(o, key, trps, vel, alg, k):
    """$A155: per-note level (RAM $78A3), 1/2 TL steps"""
    kvs = o["kvs"]
    lo, mul = (15 - 2 * kvs, 32 * kvs) if kvs else (0, 0)
    v = sat8(lo + ((mul * RT["vel"][vel]) >> 8))
    return sat8(v + ls_curve(o, alg, k)[ls_index(key, trps)])


def note_tl(o, key, trps, vel, alg, k, mod=0):
    """TL register value written at note on (mod = RAM $7873, 0 without modulation)"""
    return sat8(note_level(o, key, trps, vel, alg, k) + mod) >> 1

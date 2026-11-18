#!/usr/bin/env python3
"""Python model of the firmware's voice -> YM2414 register logic (reference for the C++ engine).
Uses the measured characteristics in measured.json."""
import json
import os

M = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "measured.json")))

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
    w.append((0x18, M["lfs"][lfs] if lfw != 3 else None))   # S/H: own table (to measure)
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


def vel_att(kvs, vel):
    if kvs == 0:
        return 0
    return 7 - kvs + ((kvs * M["vel_T"][vel - 1] + 8) >> 4)


def fold_key(key):
    while key < 24:
        key += 12
    while key > 108:
        key -= 12
    return key


def ls_att(ls, key):
    if ls == 0:
        return 0
    return M["ls"][ls][fold_key(key) // 3 - 8]


def alg_att(alg, k):
    return M["alg_att"]["att"][alg] if k in M["alg_att"]["ops"][alg] else 0


def note_tl(o, key, vel, alg=0, k=1):
    """key = played key + transpose - 24"""
    return min(127, M["out_tl"][o["out"]] + alg_att(alg, k) + vel_att(o["kvs"], vel) + ls_att(o["ls"], key))

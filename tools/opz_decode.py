#!/usr/bin/env python3
"""
opz_decode.py — turn opz_log.csv (from opz_log.lua) into readable per-note snapshots.

    python3 opz_decode.py opz_log.csv            # one snapshot per key-on
    python3 opz_decode.py opz_log.csv --raw      # every write, annotated
    python3 opz_decode.py opz_log.csv --ch 0     # only channel 0

Register layout follows the OPZ map documented in ymfm_opz.h. Note the OPZ
differs from the OPM here: key on/off is bit 6 of registers 20-27 (for the
channel selected in register 08), not register 08 itself.
"""
import argparse, csv, sys

def op_slot(reg):
    """For regs 0x40-0xFF: return (channel, operator name).
    OPM register slots run M1, M2, C1, C2 (+0, +8, +16, +24),
    while the key-on bits in reg 0x08 run M1, C1, M2, C2."""
    off = reg & 0x1F
    return off & 7, ["M1", "M2", "C1", "C2"][off >> 3]


def describe(reg, val):
    if reg < 0x08:
        return f"ch{reg} volume={val}"
    if reg == 0x08:
        return f"key-on channel select ch{val & 7} (preset load)"
    if reg == 0x0F:
        return f"noise en={val >> 7} freq={val & 0x1F}"
    if reg in (0x10, 0x11, 0x12, 0x14):
        return f"timer reg {reg:02x}={val:02x}"
    if reg in (0x16, 0x18):
        return f"LFO{'2' if reg == 0x16 else '1'} freq={val}"
    if reg in (0x17, 0x19):
        kind = "PMD" if val & 0x80 else "AMD"
        return f"LFO{'2' if reg == 0x17 else '1'} {kind}={val & 0x7f}"
    if reg == 0x1B:
        return f"CT={val >> 6} sync2={(val >> 5) & 1} sync1={(val >> 4) & 1} wave2={(val >> 2) & 3} wave1={val & 3}"
    if 0x20 <= reg <= 0x27:
        return f"ch{reg & 7} panR={val >> 7} KEY={'ON' if val & 0x40 else 'off'} FB={(val >> 3) & 7} ALG={val & 7}"
    if 0x28 <= reg <= 0x2F:
        return f"ch{reg & 7} KC oct={(val >> 4) & 7} note={val & 0xF}"
    if 0x30 <= reg <= 0x37:
        return f"ch{reg & 7} KF={val >> 2} (low bit={val & 1})"
    if 0x38 <= reg <= 0x3F:
        return f"ch{reg & 7} PMS={(val >> 4) & 7} AMS={val & 3} (bit7={val >> 7})"
    if 0x40 <= reg <= 0xFF:
        ch, op = op_slot(reg)
        grp = reg & 0xE0
        if grp == 0x40:
            if val & 0x80:
                return f"ch{ch} {op} WAVE={(val >> 4) & 7} FINE={val & 0xF}"
            return f"ch{ch} {op} DT1/FIXRANGE={(val >> 4) & 7} MUL/FIXFREQ={val & 0xF}"
        if grp == 0x60:
            return f"ch{ch} {op} TL={val & 0x7F}" + (" ext-bit7" if val & 0x80 else "")
        if grp == 0x80:
            return f"ch{ch} {op} KS={val >> 6} FIX={(val >> 5) & 1} AR={val & 0x1F}"
        if grp == 0xA0:
            return f"ch{ch} {op} AMEN={val >> 7} D1R={val & 0x1F}"
        if grp == 0xC0:
            if val & 0x20:
                return f"ch{ch} {op} EGSHIFT={val >> 6} REV={val & 7}"
            return f"ch{ch} {op} DT2={val >> 6} D2R={val & 0x1F}"
        if grp == 0xE0:
            return f"ch{ch} {op} D1L={val >> 4} RR={val & 0xF}"
    return f"ext reg {reg:02x}={val:02x}"


def is_alt(reg, val):
    """OPZ stores two values at 40-5F (bit 7) and C0-DF (bit 5)."""
    return ((reg & 0xE0) == 0x40 and val & 0x80) or ((reg & 0xE0) == 0xC0 and val & 0x20)


def channel_regs(ch):
    regs = [ch, 0x20 + ch, 0x28 + ch, 0x30 + ch, 0x38 + ch]
    for grp in (0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0):
        for slot in range(4):
            regs.append(grp + ch + 8 * slot)
            if grp in (0x40, 0xC0):
                regs.append(0x100 | (grp + ch + 8 * slot))   # alternate value
    return regs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--ch", type=int)
    a = ap.parse_args()

    shadow = {}
    with open(a.log) as fh:
        for row in csv.DictReader(fh):
            t, reg, val = float(row["time_s"]), int(row["reg"], 16), int(row["val"], 16)
            ch = reg & 7 if reg >= 0x20 or reg < 0x08 else None
            key = reg | 0x100 if is_alt(reg, val) else reg
            if a.ch is not None and ch is not None and ch != a.ch:
                shadow[key] = val
                continue
            if a.raw:
                print(f"{t:10.6f}  {reg:02x}={val:02x}  {describe(reg, val)}")
            shadow[key] = val
            keyon = 0x20 <= reg <= 0x27 and (val & 0x40) and (reg & 7) == (shadow.get(0x08, 0) & 7)
            if not a.raw and keyon:
                c = reg & 7
                print(f"\n=== {t:.6f}s  key-on ch{c} ===")
                for r in channel_regs(c):
                    if r in shadow:
                        print(f"  {r & 0xff:02x}={shadow[r]:02x}  {describe(r & 0xff, shadow[r])}")


if __name__ == "__main__":
    sys.exit(main())

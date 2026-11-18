#!/usr/bin/env python3
"""Small HD6303 disassembler for reading the TX81Z firmware (study tool; prints no ROM data by itself).

Opcode names come from the dispatch table of our CPU core (src/cpu/6303tables.h).
usage: dasm6303.py ROM START END [--bank 1]
The ROM is the 64 KB image; addresses $8000-$FFFF map to bank (0|1) * $8000 + (addr & $7FFF).
"""
import re
import sys

TABLES = "/home/claude/tx81z-pd/src/cpu/6303tables.h"


def load_names():
    src = open(TABLES).read()
    body = src[src.index("hd63701_insn[0x100]"):]
    names = re.findall(r"&hd6303x::(\w+)", body)[:256]
    assert len(names) == 256
    return names


NAMES = load_names()
WIDE = {"subd", "cpx", "lds", "ldd", "ldx", "addd"}
BITOPS = {"aim", "oim", "eim", "tim"}
BRANCHES = {"bra", "brn", "bhi", "bls", "bcc", "bcs", "bne", "beq", "bvc", "bvs", "bpl", "bmi",
            "bge", "blt", "bgt", "ble", "bsr"}


def decode(mem, pc):
    """-> (length, text, target or None)"""
    op = mem(pc)
    name = NAMES[op]
    if name == "trap":
        return 1, f"db ${op:02x}", None
    if name in BRANCHES:
        d = mem(pc + 1)
        tgt = (pc + 2 + (d - 256 if d & 0x80 else d)) & 0xFFFF
        return 2, f"{name} ${tgt:04x}", tgt
    if "_" not in name:
        return 1, name, None
    base, mode = name.rsplit("_", 1)
    if base in BITOPS:
        m, a = mem(pc + 1), mem(pc + 2)
        return 3, f"{base} #${m:02x},{'$%02x,x' % a if mode == 'ix' else '$%02x' % a}", None
    if mode == "im":
        if base in WIDE:
            v = mem(pc + 1) << 8 | mem(pc + 2)
            return 3, f"{base} #${v:04x}", None
        return 2, f"{base} #${mem(pc + 1):02x}", None
    if mode == "di":
        a = mem(pc + 1)
        return 2, f"{base} ${a:02x}", a if base == "jsr" else None
    if mode == "ix":
        return 2, f"{base} ${mem(pc + 1):02x},x", None
    if mode == "ex":
        a = mem(pc + 1) << 8 | mem(pc + 2)
        return 3, f"{base} ${a:04x}", a if base in ("jsr", "jmp") else None
    raise ValueError(name)


def main():
    rom = open(sys.argv[1], "rb").read()
    start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
    bank = int(sys.argv[sys.argv.index("--bank") + 1]) if "--bank" in sys.argv else 1

    def mem(a):
        a &= 0xFFFF
        return rom[(bank << 15) | (a & 0x7FFF)] if a >= 0x8000 else 0

    pc = start
    while pc < end:
        n, text, _ = decode(mem, pc)
        raw = " ".join(f"{mem(pc + k):02x}" for k in range(n))
        print(f"{pc:04x}  {raw:<9} {text}")
        pc += n


if __name__ == "__main__":
    main()

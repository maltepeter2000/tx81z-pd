#!/usr/bin/env python3
"""Fetch voice VCED + ACED from the firmware's memory (program change + dump request) - for local
listening tests only; voices from the ROM / NVRAM are not stored in the repository."""
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as P

REQ = [0xF0, 0x43, 0x20, 0x7E] + list(b"LM  8976AE") + [0xF7]


def fetch(program, nvram=P.NVRAM):
    with tempfile.TemporaryDirectory() as d:
        nv = os.path.join(d, "t.nvram")
        open(nv, "wb").write(open(nvram, "rb").read())
        args = [P.EMU, "--rom", P.ROM, "--nvram", nv, "--seconds", "8",
                "--midi", f"6:C0 {program:02X}", "--midi", "6.5:" + " ".join(f"{b:02X}" for b in REQ)]
        out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    b = [int(ln.split()[-1], 16) for ln in out.splitlines() if "MIDI OUT" in ln]
    msgs, cur = [], None
    for x in b:
        if x == 0xF0:
            cur = [x]
        elif cur is not None:
            cur.append(x)
            if x == 0xF7:
                msgs.append(cur)
                cur = None
    aced = next(m for m in msgs if m[3] == 0x7E)[16:16 + 23]
    vced = next(m for m in msgs if m[3] == 0x03)[6:6 + 93]
    name = bytes(vced[77:87]).decode("ascii", "replace")
    return vced, aced, name


if __name__ == "__main__":
    v, a, n = fetch(int(sys.argv[1]))
    print(n)
    if len(sys.argv) > 2:
        open(sys.argv[2], "wb").write(bytes(v + a))

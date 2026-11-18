#!/usr/bin/env python3
"""Run the firmware emulator (tx81z-emu) as an oracle and read back the YM2414 register writes.

probe(events, seconds) -> list of (t, reg, val); events: [(t, 'midi', bytes) | (t, 'syx', bytes)]
image(writes, t) -> dict reg -> last value written before t
"""
import os
import subprocess
import tempfile

EMU = os.environ.get("TX_EMU", "/tmp/claude-0/-home-claude/0880ee1e-15a1-5b3f-a614-450e24afd090/scratchpad/emu")
ROM = os.environ.get("TX_ROM", "/tmp/claude-0/-home-claude/0880ee1e-15a1-5b3f-a614-450e24afd090/scratchpad/nat/rom.bin")
NVRAM = os.environ.get("TX_NVRAM", "/tmp/claude-0/-home-claude/0880ee1e-15a1-5b3f-a614-450e24afd090/scratchpad/nat/base.nvram")


def probe(events, seconds, nvram=NVRAM):
    with tempfile.TemporaryDirectory() as d:
        nv = os.path.join(d, "t.nvram")
        with open(nvram, "rb") as f, open(nv, "wb") as g:
            g.write(f.read())
        log = os.path.join(d, "log.csv")
        args = [EMU, "--rom", ROM, "--nvram", nv, "--seconds", str(seconds), "--quiet", "--opzlog", log]
        for k, (t, kind, data) in enumerate(events):
            if kind == "midi":
                args += ["--midi", f"{t}:" + " ".join(f"{b:02X}" for b in data)]
            else:
                fn = os.path.join(d, f"e{k}.syx")
                open(fn, "wb").write(bytes(data))
                args += ["--syx", f"{t}:{fn}"]
        subprocess.run(args, capture_output=True, check=True)
        out = []
        for ln in open(log).read().splitlines()[1:]:
            t, r, v = ln.split(",")
            out.append((float(t), int(r, 16), int(v, 16)))
        return out


def image(writes, t):
    img = {}
    for tt, r, v in writes:
        if tt >= t:
            break
        img[r] = v
    return img


def between(writes, t0, t1):
    return [(t, r, v) for t, r, v in writes if t0 <= t < t1]


def vced_dump(v, ch=0):
    assert len(v) == 93
    return [0xF0, 0x43, ch, 0x03, 0x00, 0x5D] + list(v) + [(-sum(v)) & 0x7F, 0xF7]


def aced_dump(a, ch=0):
    assert len(a) == 23
    body = list(b"LM  8976AE") + list(a)
    return [0xF0, 0x43, ch, 0x7E, 0x00, 0x21] + body + [(-sum(body)) & 0x7F, 0xF7]

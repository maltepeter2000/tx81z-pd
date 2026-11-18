#!/usr/bin/env python3
"""A/B listening test: the same phrase on the firmware engine and the native engine (local only;
the voices come from the user's NVRAM / ROM and the WAV files stay in the scratchpad).
usage: listen.py OUTDIR PROGRAM [PROGRAM...]"""
import os
import subprocess
import sys
import tempfile
import wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as P
import fetch_voice as F
import verify_cpp as C

PHRASE = [(0.0, 60, 100, 1.0), (0.0, 64, 90, 1.0), (0.0, 67, 80, 1.0), (1.2, 72, 110, 0.22), (1.45, 71, 70, 0.22),
          (1.7, 69, 90, 0.22), (1.95, 67, 120, 0.6), (1.95, 48, 100, 0.9), (2.9, 36, 127, 0.4), (3.0, 84, 60, 0.4)]
LEN = 4.2


CTRL = [(3.3, [0xB0, 1, x]) for x in ()] + [(3.4 + 0.05 * i, [0xE0, 0, 64 + 4 * i]) for i in range(8)] + \
       [(3.8, [0xE0, 0, 64])]


def events(t0):
    """(time, midi bytes): the phrase plus a pitch bend gesture on the last notes"""
    ev = []
    for t, k, v, d in PHRASE:
        ev += [(t0 + t, [0x90, k, v]), (t0 + t + d, [0x80, k, 0])]
    ev += [(t0 + t, b) for t, b in CTRL]
    return sorted(ev, key=lambda e: e[0])


def read(fn, start):
    w = wave.open(fn)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(np.float32)
    s = int(start * 48000)
    return x[s:s + int(LEN * 48000)]


def firmware(prog, d):
    nv = os.path.join(d, "fw.nvram")
    open(nv, "wb").write(open(P.NVRAM, "rb").read())
    fn = os.path.join(d, "fw.wav")
    args = [P.EMU, "--rom", P.ROM, "--nvram", nv, "--seconds", str(6.5 + LEN), "--quiet", "--wav", fn,
            "--midi", f"6:C0 {prog:02X}"]
    for t, b in events(6.5):
        args += ["--midi", f"{t:.4f}:" + " ".join(f"{x:02X}" for x in b)]
    subprocess.run(args, check=True, capture_output=True)
    return read(fn, 6.5)


def native(v, a, d):
    vf, fn = os.path.join(d, "v.bin"), os.path.join(d, "nat.wav")
    open(vf, "wb").write(bytes(v + a))
    args = [C.CLI, "--rom", P.ROM, "--voice", vf, "--at", "0", "--seconds", str(0.5 + LEN), "--wav", fn]
    for t, b in events(0.5):
        args += ["--midi", f"{t:.4f}:" + " ".join(f"{x:02X}" for x in b)]
    subprocess.run(args, check=True, capture_output=True)
    return read(fn, 0.5)


def save(fn, x):
    w = wave.open(fn, "wb")
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
    w.writeframes(np.clip(x, -32768, 32767).astype(np.int16).tobytes())


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    ab = []
    for p in map(int, sys.argv[2:]):
        v, a, name = F.fetch(p)
        with tempfile.TemporaryDirectory() as d:
            f, n = firmware(p, d), native(v, a, d)
        m = min(len(f), len(n))
        f, n = f[:m], n[:m]
        # waveforms are not phase aligned (note timing differs by the MIDI and tick phase), so compare
        # the level envelope (10 ms frames) and the long-term spectrum
        def env(x):
            k = len(x) // 480 * 480
            return 20 * np.log10(np.sqrt(np.mean(x[:k, 0].reshape(-1, 480) ** 2, axis=1)) + 1)
        def spec(x):
            return 20 * np.log10(np.abs(np.fft.rfft(x[:, 0] * np.hanning(len(x))))[:len(x) // 2 // 64 * 64].reshape(-1, 64).mean(axis=1) + 1)
        ef, en = env(f), env(n)
        loud = ef > ef.max() - 40
        print(f"{p:2d} {name.strip():<11} envelope {np.median(np.abs(ef - en)[loud]):4.2f} dB (median), "
              f"spectrum {np.median(np.abs(spec(f) - spec(n))):4.2f} dB (median)")
        gap = np.zeros((24000, 2), np.float32)
        ab += [f, gap, n, gap, gap]
    save(os.path.join(out, "ab_firmware_then_native.wav"), np.concatenate(ab))

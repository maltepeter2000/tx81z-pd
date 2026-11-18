#!/usr/bin/env python3
"""The optimised ymfm fork (src/native/opz) against the unmodified ymfm: random voices and streams of
notes and controllers through the native engine, every register write goes to both chips, every
output sample must be equal (txnative-cli --check-ymfm)."""
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as P
import study as S
import test_port as T
import verify_cpp as C


def run(seed):
    v, a, ev, t = T.random_case(seed)
    with tempfile.TemporaryDirectory() as d:
        vf = os.path.join(d, "v.bin")
        open(vf, "wb").write(bytes(v + a))
        args = [C.CLI, "--rom", P.ROM, "--voice", vf, "--at", "0.05", "--seconds", str(t - 8.5 + 1.5), "--check-ymfm"]
        for tt, b in ev:
            args += ["--midi", f"{tt - 8.5:.5f}:" + " ".join(f"{x:02X}" for x in b)]
        r = subprocess.run(args, capture_output=True, text=True)
    return seed, r.returncode, r.stdout.strip()


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    res = S.pmap(run, range(500, 500 + n))
    ok = sum(1 for r in res if r[1] == 0)
    print(f"ymfm fork bit-identical: {ok}/{n}")
    for r in res:
        if r[1]:
            print(r)

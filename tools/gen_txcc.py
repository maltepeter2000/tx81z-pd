#!/usr/bin/env python3
"""Generates pd/txcc.pd, the controller layer of tx81z~ (tools/ctlayer.py with CC_ROWS of
tools/tx81z_params.py).

[txcc <channel>]  (0 = all channels)
  [ctlin] inside: CC pages 20-119 (without 32 38 64-69 84 88 96-101) -> outlet: n <name> <0-127>
  inlet: outlet 3 of tx81z~ (info ...), pickup 0/1, sync
  changes from elsewhere (program change, editor, messages) go back to the controller as CC
  ([ctlout]), pickup avoids jumps.
usage: python3 tools/gen_txcc.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ctlayer                                   # noqa: E402
import tx81z_params as T                         # noqa: E402

if __name__ == "__main__":
    out = os.path.join(ROOT, "pd", "txcc.pd")
    m = ctlayer.write(out, "txcc (tx81z~)", T.CC_ROWS)
    print("wrote", out, f"({len(m)} settings on CC)")

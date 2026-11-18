#!/usr/bin/env python3
"""Tables the native engine reads from the user's TX81Z ROM at start-up (v1.6 image, CPU bank 1).

Nothing here is copied into the repository: the engine opens the same ROM file the firmware engine
needs and takes these byte ranges from it. Addresses are CPU addresses in bank 1 (= file offset).
"""
import os

ROM = os.environ.get("TX_ROM", "/tmp/claude-0/-home-claude/0880ee1e-15a1-5b3f-a614-450e24afd090/scratchpad/nat/rom.bin")

# name: (address, length, what it is, routine that reads it)
TABLES = {
    "out_att":   (0x9FA8, 20, "output level 0..19 -> attenuation (level >= 20: 99 - level)", "$9FD9"),
    "ls_curve":  (0x9FBC, 29, "level scaling curve shape, scaled by depth", "$9F84"),
    "key_group": (0xA2F0, 96, "folded key 0..95 -> scaling position", "$A1A1"),
    "vel":       (0xDF94, 128, "MIDI velocity -> velocity factor", "$CC86"),
    "mod_att":   (0xA82B, 256, "modulation amount -> attenuation (TL refresh)", "$A7A2"),
}


def load(path=ROM):
    rom = open(path, "rb").read()
    assert len(rom) == 0x10000
    return {k: list(rom[a:a + n]) for k, (a, n, _, _) in TABLES.items()}

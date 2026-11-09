#!/usr/bin/env python3
"""
set_message.py - change the TX81Z power-on message (2nd LCD line at boot)
in a battery RAM image (tx81z.nvram).

The message is the 16-character "ID" field of the system setup data
(service manual p. 26, SYS bytes 11-26, ASCII 32-127). It lives in the
battery-backed RAM, so the emulator's tx81z.nvram holds it.

    python3 set_message.py tx81z.nvram                 # show where the message is
    python3 set_message.py tx81z.nvram "Hallo Peter!"  # set a new message

A backup (tx81z.nvram.bak) is written before changing anything.
Only run this while the emulator / Pd patch / Bela project is stopped,
otherwise the running emulator overwrites the file again when it saves.
"""
import shutil
import sys

DEFAULT = b"<Good morning!!>"


def find_message(ram, current):
    hits = []
    start = 0
    while True:
        i = ram.find(current, start)
        if i < 0:
            return hits
        hits.append(i)
        start = i + 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    ram = bytearray(open(path, "rb").read())
    if len(ram) != 0x2000:
        print(f"{path}: expected 8192 bytes, got {len(ram)}")
        return 1

    # find the current message: the default text, or a previously set one
    hits = find_message(ram, DEFAULT)
    if not hits:
        # fall back to the last message written by this tool
        try:
            prev = open(path + ".msg", "rb").read()
            hits = find_message(ram, prev)
        except OSError:
            pass
    if not hits:
        print("message not found. Hex dump around 'Good' / 'morning' for diagnosis:")
        for key in (b"Good", b"morning", b"YAMAHA"):
            i = ram.find(key)
            if i >= 0:
                lo = max(0, i - 16)
                print(f"  {key.decode()} at 0x{i:04x} (CPU address 0x{0x6000 + i:04x}):",
                      ram[lo:lo + 48].hex(" "))
        return 1
    if len(hits) > 1:
        print("found more than once at", ", ".join(f"0x{h:04x}" for h in hits), "- refusing to guess")
        return 1

    off = hits[0]
    print(f"power-on message at NVRAM offset 0x{off:04x} (CPU address 0x{0x6000 + off:04x}):",
          ram[off:off + 16].decode("ascii", "replace"))

    if len(sys.argv) < 3:
        return 0

    text = sys.argv[2]
    if any(ord(c) < 32 or ord(c) > 127 for c in text):
        print("only ASCII characters 32-127 are allowed")
        return 1
    if len(text) > 16:
        print("max. 16 characters")
        return 1
    new = text.ljust(16).encode("ascii")

    shutil.copyfile(path, path + ".bak")
    ram[off:off + 16] = new
    open(path, "wb").write(ram)
    open(path + ".msg", "wb").write(new)   # remember it for the next change
    print(f"new message: '{new.decode()}'  (backup: {path}.bak)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

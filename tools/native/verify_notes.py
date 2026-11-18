#!/usr/bin/env python3
"""Random note sequences (chords, more than 8 notes, overlaps): compare the note on / off register writes
of the firmware with model.Voices (periodic writes - key code, TL refresh, LFO depth, channel volume -
are left out)."""
import random
import study as S
import model as MD
import verify as V

PERIODIC = lambda r, x: 0x28 <= r <= 0x37 or r in (0x17, 0x19) or r < 8 and r != 0x08 or (0x60 <= r <= 0x7F and x & 0x80)


def check(seed):
    rng = random.Random(seed)
    v, a = V.rand_voice(rng)
    v[58] = rng.randint(0, 1)
    t, held, ev = 8.6, [], []
    for _ in range(rng.randint(5, 30)):
        if held and (rng.random() < 0.4 or len(held) > 10):
            k = held.pop(rng.randrange(len(held)))
            ev.append((t, "off", k))
        else:
            k = rng.randint(24, 108)
            if k in held:
                continue
            held.append(k)
            ev.append((t, "on", k, rng.randint(1, 127)))
        t += rng.choice((0.002, 0.03, 0.06))
    notes = [(e[0], [0x90, e[2], e[3]] if e[1] == "on" else [0x80, e[2], 0]) for e in ev]
    w = S.run(v, a, notes=notes, end=t + 0.1)
    got = [(r, x) for tt, r, x in w if tt >= 8.55 and not PERIODIC(r, x)]
    m = MD.Voices(v, a)
    exp = []
    for e in ev:
        exp += m.note_on(e[2], e[3]) if e[1] == "on" else m.note_off(e[2])
    if got == exp:
        return seed, None
    i = next((i for i, (g, x) in enumerate(zip(got, exp)) if g != x), min(len(got), len(exp)))
    return seed, (i, got[max(0, i - 3):i + 4], exp[max(0, i - 3):i + 4])


if __name__ == "__main__":
    import sys
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    res = S.pmap(check, range(n))
    print(f"note sequences ok {sum(1 for r in res if r[1] is None)}/{n}")
    for seed, bad in res:
        if bad:
            print(seed, bad)

# Native engine: notes from the firmware oracle

Goal: drive the YM2414 (ymfm) directly, without emulating the HD6303X, writing the same register
values as the v1.6 firmware for the same input (single mode only). The firmware in the emulator is the
oracle: `tools/native/probe.py` runs `tx81z-emu --opzlog` with a script of MIDI / SysEx events and
returns every register write with its time; `tools/native/sweep.py` varies one voice parameter at a time
on top of a base voice (`tools/native/basevoice.txt`, made up - no factory voice data in the repository) and records the registers written when the voice is loaded.

## Timing

- No OPZ timers are used (only `14 = 70` at boot). The firmware runs a periodic task from the CPU's own
  timer: **one tick = 584 samples** at the OPZ rate (18 688 CPU cycles, 10.44 ms).
- Every tick: key code / key fraction of all 8 channels (`28-2F`, `30-37`) and the two LFO depth
  registers (`17`, `19`, alternating AM (bit 7 = 0) and PM (bit 7 = 1) writes).
- Every second tick: total level of all 32 operators (`60-7F`, bit 7 set on these refresh writes).
- So pitch (bend, portamento, transpose, LFO delay / depth) and levels (controllers, velocity) are
  computed in software; the chip LFOs run at the rate in `18`.

## Voice load (all 8 channels get the same values, one copy per voice)

Order seen on a program change / bulk dump:

1. damp: `E0-FF = FE`, `C0-DF = 28` (alt write, bit 5 = EG shift / rev) per channel
2. `08 = 0..7` (load preset per channel)
3. `80-9F` (KS | fix | AR), `A0-BF` (AME | D1R), `C0-DF` (DT2 | D2R), `C0-DF` alt (EG shift | rev)
4. `20-27` (pan | FB | ALG)
5. `40-5F` (DT1 | MUL or fix range / freq), `40-5F` alt (bit 7: wave | fine)
6. `C0-DF` alt again, `38-3F` (PMS | AMS), `1B` (LFO wave / sync), `18` (LFO speed)
7. `E0-FF` (D1L | RR) - these become the chip's "preset" values

At note on the firmware damps the channel (`E0 = FE`, `C0 alt = 28`) and writes `08 = ch` so the chip
restores the preset D1L / RR and EG shift / rev (see the comment in ymfm_opz.cpp `write`).

## Parameter -> register (first sweep, operator 1 = slots 18-1F)

| Parameter | Register | Mapping |
|---|---|---|
| ALG | 20-27 bits 0-2 | ALG |
| FB | 20-27 bits 3-5 | FB (bit 7 always set) |
| AR | 80-9F bits 0-4 | AR |
| RS | 80-9F bits 6-7 | RS |
| fixed | 80-9F bit 5 | FIX |
| D1R | A0-BF bits 0-4 | D1R, AME in bit 7 |
| D1L | E0-FF bits 4-7 | 15 - D1L |
| RR | E0-FF bits 0-3 | min(15, 2 RR + 1) |
| PMS / AMS | 38-3F | PMS << 4 / AMS |
| wave / fine | 40-5F alt | 0x80 | wave << 4 | fine |
| LFO wave | 1B bits 0-1 | (S/H shows 18 = 5A: table) |
| LFO speed | 18 | table (0 -> 00, 1 -> 1F, 49 -> DF, 98/99 -> FF) |
| reverb rate | C0-DF alt bits 0-2 | rev |
| BC EG bias | 00-07 | channel volume (table) |
| CRS / DET | 40-5F, C0-DF | MUL / DT1 / DT2 (table, next sweep) |

Not in the load at all (computed per tick): LFO delay, PMD, AMD, sync, transpose, poly, pitch bend
range, portamento, sustain, controllers, KVS, LS, OUT (levels go through the TL refresh).

## Next

- full value sweeps for the table parameters (CRS x DET, LFO speed, fixed frequency, BC EG bias)
- note on: TL from OUT / LS / KVS / velocity / key, KC / KF from note / transpose / fine
- per tick: LFO delay ramp, pitch bend, portamento, controllers

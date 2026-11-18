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

## Reading the firmware

`tx81z-emu --opzpc` adds the CPU address (`bank:pc`) and six stack words to every `--opzlog` line, so each
register write points at the routine that made it; `--trace FILE T0 T1` logs every instruction in a time
window, `--ramdump S:FILE` writes the internal and external RAM at a time. `tools/native/dasm6303.py`
disassembles the ROM (opcode names from our CPU core's dispatch table). All sound code runs in ROM bank 1,
where CPU address = file offset.

| Routine | What it does |
|---|---|
| `$9F00` | per operator at voice load: OUT -> attenuation (`$9FD9`), algorithm offset, then the 29-point level scaling curve (RAM `$7943`, 29 bytes per operator, `$74` per channel) |
| `$9FEC` | per operator at voice load: velocity offset `15 - 2 KVS` (`$78D3`) and factor `32 KVS` (`$78F3`) |
| `$A155` | note on: per-note level (RAM `$78A3`) = velocity term + curve[key position], TL write with bit 7 clear |
| `$A769` | every tick, 4 channels x 4 operators (alternating halves): TL = (`$78A3` + modulation `$7873`) / 2, bit 7 set |
| `$A68F` | every tick: channel volume registers `00-07` (BC EG bias, controllers) |
| `$9B02` | LFO speed -> `18` (`16` for the second LFO) |
| `$A53F` | every tick: key code / fraction `28-37` |
| `$CC86` | MIDI velocity -> velocity factor (RAM `$F1`) |

Levels are kept in **half TL steps** (8 bit, saturating at 255) and halved only when written; this is
where the +1 differences of the earlier measured model came from.

## Tables from the user's ROM

The native engine needs the same ROM file as the firmware engine anyway, so it reads its few tables from
that file at start-up instead of keeping copies in the repository (`tools/native/romtables.py`):
OUT curve for levels 0..19 (`$9FA8`), level scaling curve shape (`$9FBC`, 29), key -> scaling position
(`$A2F0`, 96), velocity curve (`$DF94`, 128) and the amount -> attenuation table (`$A82B`, 256, also used
for the LFO speed). The engine checks the ROM version before using them.

## Model status (tools/native/model.py, checked by tools/native/verify.py)

`verify.py` loads random voices into the firmware and the model and compares register by register
(controller parameters, LFO depths and portamento neutral; they are stage 4).

| Part | Result |
|---|---|
| voice load, every register write in order (all LFO waves) | 600 / 600 exact |
| key code on a note (with transpose) | 600 / 600 exact |
| total level on a note (velocity, level scaling, algorithm) | 600 / 600 exact |

Rules:

- operators on chip slots: OP4 +0, OP3 +8, OP2 +16, OP1 +24
- coarse: MUL / DT2 = all MUL x {1, 1.41, 1.57, 1.73} ratios sorted (computed, no table);
  fixed mode: low nibble = CRS >> 2, bits 4-6 = range, DT2 as in ratio mode
- detune 0..6 -> DT1 7 6 5 0 1 2 3; fine limited to 7 for CRS 0..3 (ratio < 1)
- D1L -> 15 - D1L, RR -> max(1, RR); EG shift in the last C0 alt write, not for OP1
- key code: s = key + transpose - 37, folded by octaves into 0..95, OPM note codes; KF bit 0 set
- LFO speed: d = (speed * 660) >> 8; register = 255 - att[d], sample and hold: d
- level (half TL steps, all sums saturate at 255):
  - base = 2 * min(127, out_att + algorithm offset); out_att = table for OUT < 20, else 99 - OUT;
    offset for alg 5: OP1, OP2 +8; alg 6, 7: OP1-3 +13; alg 8: all +16
  - curve[i] = base + (shape[i] * depth) >> 8, depth = (LS * 660) >> 8, i = 0..28
  - position: k = key - 13 + transpose - 24 folded into 0..95; i = clamp(pos[k] - 14, 0, 112) >> 2
  - velocity term (KVS > 0) = 15 - 2 KVS + (32 KVS * vel[velocity]) >> 8
  - note level = velocity term + curve[i]; TL = (note level + modulation) >> 1
- BC EG bias -> `00-07` (channel volume) is still a measured table in `tools/native/measured.json`;
  ymfm does not use these registers, so they do not change the sound

## Next

- note on / off sequence and voice allocation (8 voices), key on bit in `20-27`
- per tick: TL modulation (`$7873`: AMS / breath / EG bias), LFO delay and depths (`17`, `19`),
  pitch bend, portamento, mono, controllers, sustain

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
| `$A53F` | key code / fraction `28-37` (every second tick) |
| `$8F1F` | tick: always `$EC17`, `$E8D0`, `$ABC7`; then alternating `$A390`, `$AAF7`, `$AA1A`, `$A538` (key code) and `$A64D`, `$A68F` (channel volume, TL refresh), `$A94B` |
| `$82BA` / `$8596` | note on / note off, voice allocation |
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

## Note on / off (single mode, poly; `tools/native/verify_notes.py`: 340 / 340 random sequences exact)

- voices are taken round robin, starting after the voice used last; a held voice is skipped; when all 8
  are held, the oldest held note is stolen (queue in note-on order, note off removes the voice from it);
  a voice load resets the voices and starts again at voice 0
- note on, voice c: `E0+c+8i = FE` and `C0+c+8i` alt = shadow & `E8` (keeps EG shift) for i = 0..3,
  `08 = c`, TL `60+c+8i` (bit 7 clear), `1B = 10 | wave` if LFO sync is on, `20+c = 40 | 80 | FB | ALG`
- note off (also note on with velocity 0): `08 = c`, `20+c` without the key on bit
- key code is not written at note on; the next periodic update writes it

## C++ engine (`src/native`, library `txnative`, test runner `txnative-cli`)

- `txn::Engine`: `init(rom)` (checks for the v1.6 ROM by CRC and takes the tables from it),
  `load_voice(vced, aced)`, `note_on`, `note_off`, `render`; drives ymfm's YM2414 directly
- periodic task every 584 samples: TL refresh of one half of the channels (bit 7 set), LFO depth
  register (`19` / `17` alternating, neutral for now), key code / fraction of all 8 channels
- `tools/native/verify_cpp.py`: random voices and note sequences through the firmware and the C++
  engine; every voice load / note on / note off write in order plus the full register image afterwards
  (except `00-07` and the LFO depths): 220 / 220 exact
- `tools/native/listen.py OUTDIR PROGRAM...`: A/B WAV of a phrase (firmware, then native) with voices
  from the internal bank (local only). Waveforms are not sample-aligned (MIDI and tick phase), so it
  compares level envelopes and spectra: within about 1 dB for voices without LFO modulation
- speed: 60 s of audio take 0.31 s with the native engine and 1.71 s with the firmware engine
  (desktop, one core), i.e. about 5.6 x faster; ymfm is now most of the cost

## Stage 4: modulation and controllers (ported routine by routine)

The C++ engine keeps the firmware's RAM variables at their CPU addresses and every routine is a direct
port (`tools/native/fwport.py` is the Python reference with the same structure):

| Routine | Port | What |
|---|---|---|
| `$A390` | `bend_all`, `bc_bias` | pitch bend (MSB, curve `$A4B8`, range `$A0E4`) and breath pitch bias (`$A485`) |
| `$AAF7` | `soft_lfo` | software LFO (only for the performance 'vib' setting; state kept anyway) |
| `$AA1A` | `pitch_depth` | controller pitch depth (wheel, breath, foot x sensitivities) and PM depth of LFO 1 / 2 |
| `$A538`, `$A210` | `keycode`, `note_keycode` | portamento glide, key code / fraction (also right at note on) |
| `$A64D` | `lfo_delay` | two-stage LFO delay (wait, then ramp), restarted by every note on |
| `$A68F`, `$A769` | `channel_volume`, `tl_refresh` | EG bias (breath), foot volume, MIDI volume; TL with modulation |
| `$A94B` | `am_depth` | AM depth from AMD x delay plus wheel / breath / foot |
| `$CE0C` | `control_change` | CC 1, 2, 4, 5 (portamento time), 7, 64 (sustain), 65 (portamento on / off) |
| `$8596`, `$885F` | `note_off`, sustain | note off with sustain, release of sustained voices |
| `$8406`, `$86C7`, `$8787` | `mono_note_on`, `mono_note_off` | mono mode, see below |
| `$CF2C`, `$CF52`, `$CF84` | `all_notes_off`, `set_mono` | CC 123, CC 126 (value 1) / 127 |

The timer task runs in two halves 292 samples apart (`$8F1F`): phase A = `$A390`, `$AAF7`, `$AA1A`,
`$A538`; phase B = `$A64D`, `$A68F` (with `$A769`), `$A94B`.

Tests (firmware in the emulator as reference):

- `tools/native/test_port.py`: Python ports vs RAM snapshots of the firmware right before and after each
  timer phase (`tx81z-emu --pcdump`): 10 702 / 10 702 phases exact (random voices with LFO, controllers,
  portamento; random notes, wheel, breath, foot, volume, bend, sustain, portamento switch / time)
- `tools/native/test_cpp_port.py`: the same snapshots through the C++ engine (`txnative-cli --phase-test`):
  5 452 / 5 452 phases exact, RAM and register writes
- `tools/native/test_cpp_events.py`: C++ event handling vs the firmware (RAM state after random event
  streams, plus every voice load / note on / note off write in order): 30 / 30 exact
- `tools/native/verify_cpp.py`: register image and write order: 220 / 220 exact
- an end-to-end comparison of the whole register stream is not exact by construction: the firmware
  handles a MIDI message over about a millisecond and its timer can interrupt in the middle; the native
  engine handles a message at once

Mono mode (`$8406`): one voice (0) and a stack of up to 8 held keys (RAM `$7F39`, count `$7F01`).
The first key starts the voice (damp, levels, key on; with fingered portamento it starts at its own
pitch, with full time portamento it glides from where the voice is). A second key sets the direction
(`$5B`: higher or lower than the first) and takes over legato; from the third key on only keys further
in that direction take over. When the stack is full, the key at the other end is replaced. Releasing a
key returns to the highest (direction up) or lowest remaining key; the last key off releases the voice
(or keeps it with sustain). CC 126 / 127 switch the mode (damp all voices, voices reset); a CC 126 with
a value other than 1, or a switch to the mode already set, acts as all notes off.

Tests with mono voices, mode switches and all notes off: `MONO=1 CHMODE=1 test_cpp_events.py` 30 / 30,
`CHMODE=1 test_cpp_events.py` 30 / 30, `MONO=1 CHMODE=1 test_cpp_port.py` 1 566 / 1 566 phases exact.

Not ported: micro tuning (`$7F99`), performance mode. CC 6 (data entry) and aftertouch are ignored by
the firmware in single mode as well.

## Parameter changes (`Engine::set_param`)

As the firmware's SysEx parameter change: the value is stored in the voice copy, then the register
blocks that depend on it are rewritten for all 4 operators x 8 channels and the derived RAM is
recomputed; sounding notes go on. Operator parameters: AR, RS -> `80`; D1R, AME -> `A0`; D2R -> `C0`;
RR, D1L -> `E0`; CRS, FIX, FIXRG, FINE -> `80`, `40`, `40` alt, `C0`; OSW -> `40`, `40` alt, `C0`;
DET -> `40`; SHFT, reverb rate -> `C0` alt; LS, OUT, KVS, EBS -> RAM only (levels apply from the next
note). Algorithm and poly / mono damp all voices and reset them (as the firmware); feedback -> `20`;
LFO and wheel / breath / foot sensitivities -> `1B`, `18`; PMS, AMS -> `38`. The firmware takes no
change of portamento mode, the DX21 switches (sustain, portamento, chorus) or the OP1 EG shift this way.
Fine is clamped to 7 below ratio 1 only by a voice load, not by a parameter change.
`tools/native/test_cpp_params.py`: every parameter against the firmware, 13 seeds x 100 parameters exact.

## Single mode specialisation

All channels belong to instrument 0 on LFO 1, LFO 2 is unused: the timer task uses these as constants,
the software LFO (`$AAF7`, only for the performance 'vib' setting) and the LFO delay of LFOs 2..8 are
left out, LFO 2 writes its constant depths (`17` = 00 / 80). The main RAM variables have names
(`RAM_...` in `txnative.cpp`). Register output unchanged (`test_cpp_port.py`, RAM compared without the
dropped state).

## CPU on the Bela Gem (Cortex-A53, 64 bit)

60 s of audio rendered without output (`txnative-cli` / `tx81z-emu`, same voices; firmware loads them by
SysEx). Share of one core:

| | firmware engine | native engine (OpzCore) |
|---|---|---|
| no notes | 13.9 % | - |
| voice without LFO, 1 note | 14.7 % | 2.5 % |
| voice without LFO, 8 notes | 18.2 % | 8.1 % |
| voice with LFO, 1 note | 20.5 % | 2.6 % |
| voice with LFO, 8 notes | 24.3 % | 9.2 % |

The unmodified ymfm recomputes the phase step of all 32 operators every sample while LFO pitch
modulation is on; OpzCore does it only when the PM value changes and skips silent operators and channels.

## Next

- Pd object `tx81zn~` (`pd/tx81zn~.cpp`): done – ROM banks A-D, internal bank from the battery RAM
  (`src/native/txvoice.h`, records checked against the firmware's dumps of all 128 programs with
  `tools/native/vmem.py`), splash text; editor parameters go through `set_param(..., strict = false)`
- editor in the style of a 4-operator FM synth (roles carrier / modulators from the algorithm)
- Bela Gem build of the Pd project
- ymfm is now most of the cost; voices with the LFO running cost about 2.5 x more in ymfm than without
  (desktop: 60 s of a voice with LFO 0.9 s native vs 1.4 s firmware engine; without LFO 0.35 s vs 1.7 s)

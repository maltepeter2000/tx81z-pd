# tx81z-pd

Yamaha TX81Z emulator (HD6303X CPU + YM2414 via ymfm + HD44780 LCD) running the
original firmware: a command-line tool, the Pd external `tx81z~` (macOS, Linux,
Bela), a native Bela C++ project and a Bela Pd project.

| Folder | Contents |
|---|---|
| `src/` | emulator core (CPU, machine, LCD) and command-line tool |
| `pd/` | Pd external `tx81z~` with help patch |
| `bela/` | native Bela C++ project (auto play, OLED via O2O) – see `bela/README.md` |
| `bela-pd/` | Bela Pd project with `tx81z~` and OSC remote – see `bela-pd/README.md` |
| `tools/` | MAME logger, OPZ register decoder, power-on message editor |
| `tests/` | self-written 6303 test ROM |
| `third_party/` | ymfm, `m_pd.h` |

The firmware (`tx81z-v1.6.bin`) and battery RAM (`tx81z.nvram`) are not part of
this repository; add them locally.

## Building

```
cmake -S . -B build && cmake --build build -j
```

Without cmake (macOS):

```
mkdir -p build && clang++ -std=c++17 -O2 -Isrc -Ithird_party/ymfm/src \
  src/main.cpp src/tx81z.cpp src/cpu/hd6303x.cpp \
  third_party/ymfm/src/ymfm_opz.cpp third_party/ymfm/src/ymfm_misc.cpp \
  third_party/ymfm/src/ymfm_adpcm.cpp third_party/ymfm/src/ymfm_ssg.cpp \
  -o build/tx81z-emu
```

## Usage

```
# start normally and follow the LCD output
./build/tx81z-emu --rom tx81z-v1.6.bin --nvram tx81z.nvram --seconds 5

# test mode (service manual p. 28): hold MASTER VOLUME + CURSOR at power-on,
# then INC -> presets into RAM, OMNI ON, MIDI parameters set
./build/tx81z-emu --rom tx81z-v1.6.bin --nvram tx81z.nvram --hold vol-,cursor --release-at 3 --press 5:inc --seconds 10

# play a note, write a WAV file, log YM2414 register writes
./build/tx81z-emu --rom tx81z-v1.6.bin --nvram tx81z.nvram \
    --midi "6:90 30 64" --midi "7:80 30 00" --wav out.wav --opzlog opz.csv --seconds 8
python3 tools/opz_decode.py opz.csv
```

Options: `--syx S:FILE` (send a SysEx file), `--press S:BUTTON`, `--quiet`.
Buttons: `store utility edit play param- param+ dec inc vol- vol+ cursor`.

## Pd external `tx81z~`

```
cd pd && make          # macOS: tx81z~.pd_darwin, Linux/Bela: tx81z~.pd_linux
```

Open `tx81z~-help.pd`. The ROM (`tx81z-v1.6.bin`) and `tx81z~.pd_darwin` go into
the same folder as the patch (with `txcc.pd` and `txedit.pd`); the battery RAM file (`tx81z.nvram`) is created there.

- Arguments: `[tx81z~ <rom> <nvram>]`, paths relative to the patch
- Inlet: raw MIDI bytes (straight from `[midiin]`), `note`, `cc`, `pgm`, `bend`,
  `press <button>`, `button <button> 0|1`, `rom`, `nvram`, `save`, `reset`,
  `gain`, `factoryinit`, `pcswitch <0|1|2>`, `sysparam <p> <d>`
- Voice parameters by name: `<name> <value>` (TX81Z data value, e.g. `alg 4`,
  `out1 99`), `n <name> <0-127>` (from a pot or CC), `ninfo <name>`,
  `vced <p> <d>` / `aced <p> <d>` (raw), `dumpreq` (also sent by itself after
  every program change: the firmware does not send its voice on its own).
- `channel <0-16>`: receive channel of the TX81Z (0 = omni). `note`, `cc`, `pgm`,
  `bend` without a channel and all SysEx follow it (the firmware only takes SysEx
  on its receive channel). Reported as `channel <n>` on outlet 2.
- `getvoice` -> `voicedata <name> <93 VCED> <23 ACED>`; `voicedata ...` loads such
  a voice into the edit buffer (ACED + VCED bulk dump).
- `random <pluck|bass|lead|brass|pad|strings|total> [seed]`: a new voice of that
  kind into the edit buffer (category ranges for algorithm, envelopes, levels,
  frequency ratios and LFO; checked by measuring attack, decay and level of each kind).
- The firmware needs about 35 ms per parameter change (it redraws its display);
  faster changes end in "Midi Buffer Full", as on the real TX81Z. `tx81z~` sends one
  change per 45 ms (the newest value of each parameter) and a whole voice with a
  200 ms gap.
- Dump requests wait until no key is down: the firmware stops taking notes when it
  is asked for a dump while a key is held.
- A program change in EDIT or UTILITY mode first presses PLAY (the firmware ignores
  program changes there). All 100 names are listed in
  the help patch and in `tools/tx81z_params.py`.
- Outlets: audio L/R (resampled from ~55.93 kHz to Pd's rate, Hermite),
  messages `lcd <line1> <line2>`, `led <mask>`, `midiout <byte>`,
  `info <name> <0-127> <value>` (for every parameter change, also after a program
  change: the firmware sends its voice dump, `tx81z~` reads it), `voice <name>`
- Parameter changes go to the firmware as SysEx; only the newest value per
  parameter is sent, and only while the MIDI input is idle, so notes keep their timing.
- `[txcc <channel>]`: controller layer (MIDI CC pages 20-119, feedback, pickup,
  CC 3 = sync), the same layout as `p8cc` of pdoly8~. `[txedit <bus>]`: editor
  with all parameters in rows of 8 (rows 1-11 = CC pages), voice name, program
  change, LCD, MIDI channel and a user bank (64 slots in `txuser-bank.txt` next to
  the editor: recall a slot, store to a slot, save / load the bank). `txcc` follows
  the channel `tx81z~` reports. Both are wired up in the help patch.
- If the nvram file given as argument does not exist yet, `tx81z~` runs the
  factory init by itself (~26 s, only while DSP is on): presets, then MIDI
  Control via the front panel (receive omni, program change, control change,
  pitch bend and **Exclusive on**), and saves the file. The test-mode presets
  leave Exclusive off, and with it off the firmware ignores all SysEx
  (parameter changes, `pcswitch`, `sysparam`, dump requests).
- Program changes only work if the TX81Z's **program change switch** (system
  setup byte 3) is not *off*: `pcswitch 1` sets it to *com* (the factory init
  does this). `sysparam <p> <d>` sets any system setup byte via SysEx
  (`F0 43 10 10 7B pp dd F7`, verified with the firmware: 0 master tune,
  1 receive channel, 2 transmit channel, 3 program change, 4 control change,
  5 pitch bend, 6 note on/off, 7 Exclusive, 8 memory protect, 9 combine).
- Battery RAM is written on `save` and when the patch is closed. Quitting Pd
  directly does not always close all patches – press `save` first.

## Structure

| File | Contents |
|---|---|
| `src/cpu/hd6303x.*` | HD6303X core, ported from MAME `m6800/m6801` (timer 1/2, SCI, ports 2/5/6, IRQs) |
| `src/cpu/6800ops.hxx`, `6800macros.h`, `6303tables.h` | opcodes, macros and tables taken unchanged from MAME |
| `src/tx81z.*` | machine: memory map, ports, buttons, ROM bank, OPZ timers, MIDI serial, timing |
| `src/hd44780.h` | minimal LCD controller |
| `src/main.cpp` | command-line tool |
| `pd/tx81z~.cpp`, `pd/Makefile`, `pd/tx81z~-help.pd` | Pd external and help patch |
| `pd/txcc.pd`, `pd/txedit.pd`, `pd/tx81z_params.h` | controller layer, editor, parameter table (generated) |
| `tools/tx81z_params.py` | the parameter table (source for the external, txcc, txedit and the help) |
| `tools/gen_txcc.py`, `tools/gen_editor.py`, `tools/gen_help.py` | generators for `txcc.pd`, `txedit.pd`, `tx81z~-help.pd` |
| `tests/make_test_rom.py` | self-written 6303 test ROM (no Yamaha code) |
| `tools/opz_log.lua`, `tools/opz_decode.py` | MAME logger and decoder for comparisons |
| `tools/set_message.py` | changes the power-on message in a battery RAM file |

Timing: crystal 7.15909 MHz → CPU E clock /4, OPZ /2 = 3.579545 MHz, sample rate
OPZ/64 ≈ 55.93 kHz. One sample = exactly 32 CPU cycles. MIDI: 500 kHz / 16 = 31,250 baud
at the SCI (external clock), RX on P23.

## Status

Tested with the original firmware v1.6: boot animation, test mode (presets → RAM),
play mode, sound via MIDI. Pd external tested in Pd (current git version, Linux):
audio, LCD/LED, MIDI out, OSC loopback, battery RAM saved on close.

Tested with the test ROM: reset/vectors, LCD, ROM bank switching via P63,
OPZ timer A IRQ on IRQ1, MIDI receive via the SCI (bit level, 31,250 baud),
MIDI out, OPZ sound (key code A4 → 440 Hz).

Open / to be checked with the real firmware:
- which ROM bank is active after reset (currently bank 0, as in MAME)
- LCD special characters (Yamaha character set) are only shown roughly as ASCII
- comparison of the OPZ register streams against MAME (`tools/opz_log.lua`)

## Licenses

- `src/cpu/*`: derived from MAME, BSD-3-Clause, copyright Aaron Giles
- memory map / port wiring after MAME `ymtx81z.cpp`, BSD-3-Clause, copyright AJR
- `third_party/ymfm`: BSD-3-Clause, copyright Aaron Giles
- `third_party/pd/m_pd.h`: BSD-3-Clause, copyright Miller Puckette and others
- The TX81Z firmware is copyrighted by Yamaha and does not belong in this repository.

# tx81z-pd

TX81Z-Emulator (HD6303X + YM2414 über ymfm + LCD) mit der Original-Firmware:
Kommandozeilen-Tool, Pd-External `tx81z~` (Mac/Linux/Bela), natives Bela-C++-Projekt
und Bela-Pd-Projekt.

| Ordner | Inhalt |
|---|---|
| `src/` | Emulator-Kern (CPU, Maschine, LCD) und Kommandozeilen-Tool |
| `pd/` | Pd-External `tx81z~` mit Hilfe-Patch |
| `bela/` | natives Bela-C++-Projekt (Auto-Play, OLED über O2O) – siehe `bela/README.md` |
| `bela-pd/` | Bela-Pd-Projekt mit `tx81z~`, OSC-Fernbedienung – siehe `bela-pd/README.md` |
| `tools/` | MAME-Logger, OPZ-Decoder, Startgruß-Editor |
| `tests/` | eigenes 6303-Test-ROM |
| `third_party/` | ymfm, `m_pd.h` |

Firmware (`tx81z-v1.6.bin`) und Batterie-RAM (`tx81z.nvram`) gehören nicht ins
Repo und werden lokal ergänzt.


Eigenständiger TX81Z-Emulator in C++ ohne MAME-Framework:
HD6303X-CPU + YM2414 (ymfm) + HD44780-LCD + 8 KB Batterie-RAM.
Die Original-Firmware läuft unverändert; das ROM liefert man selbst.

## Bauen

```
cmake -S . -B build && cmake --build build -j
```

Ohne cmake (macOS):

```
mkdir -p build && clang++ -std=c++17 -O2 -Isrc -Ithird_party/ymfm/src \
  src/main.cpp src/tx81z.cpp src/cpu/hd6303x.cpp \
  third_party/ymfm/src/ymfm_opz.cpp third_party/ymfm/src/ymfm_misc.cpp \
  third_party/ymfm/src/ymfm_adpcm.cpp third_party/ymfm/src/ymfm_ssg.cpp \
  -o build/tx81z-emu
```

## Benutzen

```
# normal starten, LCD-Ausgabe verfolgen
./build/tx81z-emu --rom tx81z-v1.6.ic15 --nvram tx81z.nvram --seconds 5

# Testmodus (Service Manual S. 28): MASTER VOLUME + CURSOR beim Einschalten,
# dann INC -> Presets ins RAM, OMNI ON, MIDI-Parameter gesetzt
./build/tx81z-emu --rom rom.ic15 --nvram tx81z.nvram --hold vol-,cursor --release-at 2 --press 3:inc --seconds 8

# Note spielen, WAV schreiben, OPZ-Registerschreibzugriffe loggen
./build/tx81z-emu --rom rom.ic15 --nvram tx81z.nvram \
    --midi "2:90 30 64" --midi "3:80 30 00" --wav out.wav --opzlog opz.csv
python3 tools/opz_decode.py opz.csv
```

Optionen: `--syx S:FILE` (SysEx-Datei senden), `--press S:BUTTON`, `--quiet`.
Tasten: `store utility edit play param- param+ dec inc vol- vol+ cursor`.

## Pd-External `tx81z~`

```
cd pd && make          # macOS: tx81z~.pd_darwin, Linux/Bela: tx81z~.pd_linux
```

`tx81z~-help.pd` öffnen. ROM (`tx81z-v1.6.bin`) und `tx81z~.pd_darwin` liegen im
selben Ordner wie das Patch; das NVRAM (`tx81z.nvram`) wird dort angelegt.

- Argumente: `[tx81z~ <rom> <nvram>]`, Pfade relativ zum Patch
- Eingang: rohe MIDI-Bytes (direkt von `[midiin]`), `note`, `cc`, `pgm`, `bend`,
  `press <taste>`, `button <taste> 0|1`, `rom`, `nvram`, `save`, `reset`, `gain`
- Ausgänge: Audio L/R (aus ~55,93 kHz per Hermite auf Pd-Samplerate),
  Nachrichten `lcd <zeile1> <zeile2>`, `led <maske>`, `midiout <byte>`
- OSC: das Hilfe-Patch schickt `/tx81z/lcd s s` und `/tx81z/led i` per UDP an
  `localhost:9000` (`[oscformat]` + `[netsend -u -b]`, Port im Patch änderbar)
- Batterie-RAM wird bei `save` und beim Schließen des Patches geschrieben.
  Pd direkt zu beenden schließt nicht immer alle Patches – vorher `save` drücken.

## Aufbau

| Datei | Inhalt |
|---|---|
| `src/cpu/hd6303x.*` | HD6303X-Core, portiert aus MAME `m6800/m6801` (Timer 1/2, SCI, Ports 2/5/6, IRQs) |
| `src/cpu/6800ops.hxx`, `6800macros.h`, `6303tables.h` | Opcodes, Makros und Tabellen unverändert aus MAME |
| `src/tx81z.*` | Maschine: Memory Map, Ports, Tasten, ROM-Bank, OPZ-Timer, MIDI-Seriell, Timing |
| `src/hd44780.h` | minimales LCD |
| `src/main.cpp` | Kommandozeilen-Tool |
| `pd/tx81z~.cpp`, `pd/Makefile`, `pd/tx81z~-help.pd` | Pd-External und Hilfe-Patch |
| `tests/make_test_rom.py` | eigenes 6303-Test-ROM (kein Yamaha-Code) |
| `tools/opz_log.lua`, `tools/opz_decode.py` | MAME-Logger und Decoder für den Vergleich |

Timing: Quarz 7,15909 MHz → CPU-E-Takt /4, OPZ /2 = 3,579545 MHz, Samplerate
OPZ/64 ≈ 55,93 kHz. Ein Sample = genau 32 CPU-Zyklen. MIDI: 500 kHz / 16 = 31.250 Baud
an der SCI (externer Takt), RX auf P23.

## Stand

Mit Originalfirmware v1.6 geprüft: Boot-Animation, Testmodus (Presets → RAM),
Play-Modus, Klang per MIDI. Pd-External in Pd (aktuelle Git-Version, Linux) getestet (Audio, LCD/LED,
MIDI-Out, OSC-Loopback, NVRAM beim Schließen).

Mit dem Test-ROM geprüft: Reset/Vektoren, LCD, ROM-Bankumschaltung über P63,
OPZ-Timer-A-IRQ auf IRQ1, MIDI-Empfang über die SCI (bitweise, 31.250 Baud),
MIDI-Ausgang, OPZ-Klang (Keycode A4 → 440 Hz).

Offen / zu prüfen mit echter Firmware:
- welche ROM-Bank nach Reset aktiv ist (derzeit Bank 0, wie MAME)
- LCD-Sonderzeichen (Yamaha-Zeichensatz) werden nur grob als ASCII angezeigt
- Vergleich der OPZ-Registerströme mit MAME (`tools/opz_log.lua`)

## Lizenzen

- `src/cpu/*`: abgeleitet aus MAME, BSD-3-Clause, Copyright Aaron Giles
- Memory Map / Portbelegung nach MAME `ymtx81z.cpp`, BSD-3-Clause, Copyright AJR
- `third_party/ymfm`: BSD-3-Clause, Copyright Aaron Giles
- `third_party/pd/m_pd.h`: BSD-3-Clause, Copyright Miller Puckette and others
- Die TX81Z-Firmware ist urheberrechtlich geschützt (Yamaha) und gehört nicht ins Repo.

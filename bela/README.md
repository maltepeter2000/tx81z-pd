# TX81Z on Bela (C++ project)

The folder `tx81z/` is a ready-to-use Bela project. All files are in one flat
folder (no subfolders), and it builds with Bela's default settings – no make
parameters needed.

## Installation (Bela via USB, 192.168.7.2)

From the Mac, inside the folder `bela`:

```
scp -r tx81z root@192.168.7.2:~/Bela/projects/
scp PATH/TO/tx81z-v1.6.bin root@192.168.7.2:~/Bela/projects/tx81z/tx81z-v1.6.bin
scp PATH/TO/tx81z.nvram    root@192.168.7.2:~/Bela/projects/tx81z/      # optional
```

On the Bela the ROM file must be named exactly `tx81z-v1.6.bin` (lower case).

## Running

**In the IDE:** open the project `tx81z` → Build & Run. The first build takes a few minutes.

**Or via SSH:**
```
ssh root@192.168.7.2
make -C ~/Bela PROJECT=tx81z run
```

## What happens

- Without `tx81z.nvram` the factory init runs first (test mode, presets → RAM),
  then the emulated TX81Z restarts (~7 s).
- After booting, **auto play** selects C15 "LatelyBass" (program change 110) and
  plays a 4-bar loop Gm – Bb – Dm – F in a gallop rhythm (long-short-short).
  As soon as MIDI arrives from a keyboard it stops; the voice stays selected.
- The LCD is sent via OSC to O2O (`/display-strings-and-numbers`, 127.0.0.1:7562),
  i.e. to the OLED.
- The battery RAM is saved when the project is stopped (`tx81z.nvram` in the project).

## OSC control (UDP port 7563)

| Address | Arguments |
|---|---|
| `/tx81z/press` | button (s) |
| `/tx81z/button` | button (s), 0/1 (i) |
| `/tx81z/autoplay` | 0/1 (i) |
| `/tx81z/save` | – |

Buttons: `store utility edit play param- param+ dec inc vol- vol+ cursor`

## Settings

At the top of `render.cpp`: ROM / nvram file names, MIDI ports (`hw:1,0,0`,
`hw:1,0,1`, `hw:2,0,0`), OLED host/port, gain, auto play on/off, and for the loop:
`kAutoProgram` (voice), `kAutoBpm` (tempo), `kGallop` (true = gallop,
false = straight eighths), `kAutoRoots` (root notes of the 4 bars).

After changes to `src/` in the main project: `./update_project.sh` (copies the
files flat and adapts the ymfm copy to C++11).

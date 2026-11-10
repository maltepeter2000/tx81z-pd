# TX81Z as a Pd project on Bela

`tx81z-pd/_main.pd` is a Bela Pd project using `[tx81z~]`. The external is built
once on the Bela itself (`build_on_bela.sh`).

## Setup (Bela via USB, 192.168.7.2)

From the Mac, in the folder that contains this repository (`tx81z-pd`):

```
# 1. sources and Pd project to the Bela
scp -r tx81z-pd root@192.168.7.2:~/tx81z-emu
scp -r tx81z-pd/bela-pd/tx81z-pd root@192.168.7.2:~/Bela/projects/

# 2. firmware into the project (lower-case file name!)
scp PATH/TO/TX81Z-v1.6.bin root@192.168.7.2:~/Bela/projects/tx81z-pd/tx81z-v1.6.bin

# 3. build the external on the Bela (takes a few minutes)
ssh root@192.168.7.2 "sh ~/tx81z-emu/bela-pd/build_on_bela.sh"
```

Then open the project `tx81z-pd` in the IDE and click **Run**.

## What the patch does

- MIDI from the Bela (`[notein]`, `[ctlin]`, `[pgmin]`, `[bendin]`, `[touchin]`)
  is reassembled into MIDI for `tx81z~`. Bela passes MIDI to Pd only in parsed
  form, `[midiin]` gets no raw bytes; SysEx therefore does not arrive.
- At load the patch opens `hw:0,0,0` (`; bela_setMidi hw 0 0 0`): the Bela as a
  USB MIDI device of the computer (Ableton etc.). `hw:1,0,0` (keyboard on the
  Bela's USB host port) is opened by Bela by default. Channels of a second port
  (17–32) are folded to 1–16.
- Audio on outputs 1/2.
- LCD → O2O / OLED: `/display-strings-and-numbers <line 1> \n <line 2>` to
  127.0.0.1:7562.
- After booting the patch presses PLAY, so the TX81Z never stays in a menu,
  and sends `pcswitch 1` so program changes (MIDI or OSC) are accepted.
- No `tx81z.nvram` yet? `tx81z~` runs the factory init by itself (presets,
  MIDI setup, ~16 s) and saves the file.
- The battery RAM is saved when the project stops (Bela closes the patch). Only
  replace the nvram file while the project is stopped, otherwise it gets
  overwritten again.

## Remote control

`tx81z-remote.pd` runs on the Mac: click "connect", then the buttons (play, edit,
utility, vol+/vol-, inc/dec …) and program changes are sent via OSC to the Bela
(UDP 7563):

| Address | Arguments |
|---|---|
| `/tx81z/press` | button (s) |
| `/tx81z/button` | button (s), 0/1 (i) |
| `/tx81z/pgm` | 0–127 (i) |

## After changes to the external

Copy the new sources to `~/tx81z-emu` and run `build_on_bela.sh` again.

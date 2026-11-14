# TX81Z as a Pd project on Bela

A lean TX81Z for Bela: `[tx81z~]` (original firmware), MIDI in, the controller
layer, OLED, a control channel and an OSC remote. No GUI. `_main.pd` is generated
by `tools/gen_bela.py`.

Files in `tx81z-pd/` (the Bela project folder):

| File | |
|---|---|
| `_main.pd` | the patch |
| `txcc.pd` | controller layer (CC pages, same as on the desktop) |
| `txuser-bank.txt` | user bank, 64 slots (same format as the desktop editor's) |
| `tx81z~.pd_linux` | the external for Bela (prebuilt, or `build_on_bela.sh`) |
| `tx81z-v1.6.bin` | the firmware (yours, not included) |
| `tx81z.nvram` | battery RAM, made by the factory init on the first run |

## Setup

```
scp -r tx81z-pd root@192.168.7.2:~/Bela/projects/
scp PATH/TO/tx81z-v1.6.bin root@192.168.7.2:~/Bela/projects/tx81z-pd/
```

Open the project in the IDE, Run. Without `tx81z.nvram` the factory init runs first
(about 26 s): factory voices, MIDI omni, program change and SysEx on. Bela opens
its MIDI ports itself (the patch does not set a port).

## Playing

- Notes, pitch bend, program change (I01-C32 = 1-128), aftertouch, CC 1-19 and
  64-69 (mod wheel, breath, foot, volume, sustain ...) go to the TX81Z.
- CC 20-119: the controller layer, 11 pages of 8 as in the desktop editor, with
  pickup and feedback (`ctlout`). It follows the TX81Z receive channel (omni = all).
  CC 3 = send all values back to the controller.
- Parameter changes reach the firmware at its pace (one per 45 ms, newest value).

## Control channel 16

| Note | |
|---|---|
| 36-42 | random voice: pluck, bass, lead, brass, pad, strings, total |
| 43 | store the current voice into the current user slot (writes txuser-bank.txt) |
| 44 | save the battery RAM now |
| 45 | twice within 1 s: factory init |
| program change 1-64 | load user slot 0-63 (and make it the current slot) |

Notes on channel 16 do not play the TX81Z. The battery RAM is also saved every
60 s and when the project stops.

## OLED (O2O)

One line (`/display-strings-and-numbers` to 127.0.0.1:7562), the last event: a
patch change shows the voice line of the TX81Z (`C15  LatelyBass`), a turned
parameter `out1 = 67`, the control channel what it did (`stored slot 5`).

## OSC remote (UDP 7563)

`/tx81z/<message> <args>` goes to `tx81z~` as `<message> <args>`, e.g.
`/tx81z/random pad`, `/tx81z/pgm 110`, `/tx81z/press play`, `/tx81z/n out1 64`,
`/tx81z/channel 2`. `tx81z-remote.pd` (front panel buttons) runs on the Mac.

## Building the external on the Bela

Instead of the prebuilt `tx81z~.pd_linux`: copy the repository to `~/tx81z-emu`
and run `sh ~/tx81z-emu/bela-pd/build_on_bela.sh`.

# TX81Z als Pd-Projekt auf Bela

`tx81z-pd/_main.pd` ist ein Bela-Pd-Projekt mit `[tx81z~]`. Das External wird
einmal auf dem Bela selbst gebaut (`build_on_bela.sh`).

## Einrichten (Bela per USB, 192.168.7.2)

Vom Mac aus, im Ordner, in dem `tx81z-emu` liegt:

```
# 1. Quellen und Pd-Projekt auf den Bela
scp -r tx81z-emu root@192.168.7.2:~/
scp -r tx81z-emu/bela-pd/tx81z-pd root@192.168.7.2:~/Bela/projects/

# 2. Firmware und Batterie-RAM ins Projekt (Name klein schreiben!)
scp PFAD/ZUM/TX81Z-v1.6.bin root@192.168.7.2:~/Bela/projects/tx81z-pd/tx81z-v1.6.bin
scp PFAD/ZUM/tx81z.nvram    root@192.168.7.2:~/Bela/projects/tx81z-pd/

# 3. External auf dem Bela bauen (dauert einige Minuten)
ssh root@192.168.7.2 "sh ~/tx81z-emu/bela-pd/build_on_bela.sh"
```

Dann im IDE das Projekt `tx81z-pd` öffnen und **Run**.

## Was das Patch macht

- MIDI vom Bela (`[notein]`, `[ctlin]`, `[pgmin]`, `[bendin]`, `[touchin]`)
  wird wieder zu MIDI für `tx81z~` zusammengesetzt. Bela gibt MIDI an Pd nur
  ausgewertet weiter, `[midiin]` bekommt keine Rohbytes; SysEx kommt deshalb
  nicht an.
- Audio auf Ausgang 1/2.
- LCD → O2O/OLED: `/display-strings-and-numbers <Zeile 1> \n <Zeile 2>` an
  127.0.0.1:7562.
- Batterie-RAM wird beim Stoppen gespeichert (Bela schließt das Patch).

Standardmäßig öffnet Bela nur den MIDI-Port `hw:1,0,0`. Für den zweiten
Launchkey-Port gilt dasselbe wie in deinen anderen Pd-Projekten
(`libpd_render.cpp`); Kanäle von Port 2 (17–32) faltet das Patch auf 1–16.

## Nach Änderungen am External

Neue Quellen nach `~/tx81z-emu` kopieren und `build_on_bela.sh` erneut ausführen.

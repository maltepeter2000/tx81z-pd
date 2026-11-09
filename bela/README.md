# TX81Z auf Bela (C++-Projekt)

Der Ordner `tx81z/` ist ein fertiges Bela-Projekt. Alle Dateien liegen flach in
einem Ordner (keine Unterordner), und es baut mit Belas Standard-Einstellungen –
keine Make-Parameter nötig.

## Installation (Bela per USB, 192.168.7.2)

Vom Mac aus, im Ordner `bela`:

```
scp -r tx81z root@192.168.7.2:~/Bela/projects/
scp PFAD/ZUM/tx81z-v1.6.bin root@192.168.7.2:~/Bela/projects/tx81z/tx81z-v1.6.bin
scp PFAD/ZUM/tx81z.nvram    root@192.168.7.2:~/Bela/projects/tx81z/      # optional
```

Die ROM-Datei muss auf dem Bela genau `tx81z-v1.6.bin` heißen (Kleinschreibung).

## Starten

**Im IDE:** Projekt `tx81z` öffnen → Build & Run. Der erste Build dauert einige Minuten.
Falls du vorher schon `CPPFLAGS=-std=c++14` eingetragen hast, kannst du es stehen lassen.

**Oder per SSH:**
```
ssh root@192.168.7.2
make -C ~/Bela PROJECT=tx81z run
```

## Was passiert

- Ohne `tx81z.nvram` läuft zuerst die Werks-Initialisierung (Testmodus,
  Presets → RAM), danach startet der emulierte TX81Z neu (~7 s).
- Nach dem Boot wählt **Auto-Play** C15 „LatelyBass“ (Program Change 110) und
  spielt einen 4-Takt-Loop Gm – Bb – Dm – F im Galopp-Rhythmus (lang-kurz-kurz).
  Sobald MIDI von einem Keyboard ankommt, stoppt es; die Voice bleibt gewählt.
- Das LCD geht per OSC an O2O (`/display-strings-and-numbers`, 127.0.0.1:7562),
  also aufs OLED.
- Das Batterie-RAM wird beim Stoppen gespeichert (`tx81z.nvram` im Projekt).

## OSC-Steuerung (UDP-Port 7563)

| Adresse | Argumente |
|---|---|
| `/tx81z/press` | Taste (s) |
| `/tx81z/button` | Taste (s), 0/1 (i) |
| `/tx81z/autoplay` | 0/1 (i) |
| `/tx81z/save` | – |

Tasten: `store utility edit play param- param+ dec inc vol- vol+ cursor`

## Einstellungen

Oben in `render.cpp`: ROM-/NVRAM-Dateiname, MIDI-Ports (`hw:1,0,0`, `hw:1,0,1`,
`hw:2,0,0`), OLED-Host/Port, Gain, Auto-Play an/aus, und für den Loop:
`kAutoProgram` (Voice), `kAutoBpm` (Tempo), `kGallop` (true = Galopp,
false = durchgehende Achtel), `kAutoRoots` (Grundtöne der 4 Takte).

Nach Änderungen an `src/` im Hauptprojekt: `./update_project.sh` (kopiert flach
und passt die ymfm-Kopie an C++11 an).

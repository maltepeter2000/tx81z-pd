#!/bin/sh
# Run ON THE BELA (ssh root@bela.local, Bela Gem: root@bela-multi.local) after copying the
# repository to ~/tx81z-emu and the Pd projects to ~/Bela/projects/tx81z-pd and / or tx81zn-pd.
# Builds tx81z~ (firmware emulation) and tx81zn~ (native engine) with the board's Pd headers
# (Cortex-A8 on Bela, Cortex-A53 on the Bela Gem) and copies them into the projects that exist.
set -e
cd ~/tx81z-emu/pd
make clean
make
for p in tx81z-pd:tx81z~ tx81zn-pd:tx81zn~; do
  dir=~/Bela/projects/${p%%:*}; obj=${p##*:}
  if [ -d "$dir" ]; then cp "$obj.pd_linux" "$dir/" && echo "$obj.pd_linux installed in $dir"; fi
done

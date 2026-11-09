#!/bin/sh
# Run ON THE BELA (ssh root@192.168.7.2) after copying tx81z-emu to ~/tx81z-emu
# and the Pd project to ~/Bela/projects/tx81z-pd.
# Builds the tx81z~ external for the Bela (Cortex-A8, Bela's Pd headers)
# and copies it into the Pd project.
set -e
cd ~/tx81z-emu/pd
make clean
make
cp "tx81z~.pd_linux" ~/Bela/projects/tx81z-pd/
echo "tx81z~.pd_linux installed in ~/Bela/projects/tx81z-pd"

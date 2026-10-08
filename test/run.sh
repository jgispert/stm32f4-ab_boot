#!/usr/bin/env bash
# Pruebas en PC: estado A/B y protocolo #FW con una flash en RAM; herramientas.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
FLAGS="-std=c++17 -Wall -Wextra -Werror -Isrc"
g++ $FLAGS test/test_bootstate.cpp src/BootState.cpp -lz -o "$OUT/bootstate"
g++ $FLAGS -DAB_RUN_SLOT=0 test/test_fwupdate.cpp src/FwUpdate.cpp src/BootState.cpp \
    src/FlashF4.cpp src/AbPlatformF4.cpp src/AbBootloader.cpp -o "$OUT/fwupdate"
"$OUT/bootstate"
"$OUT/fwupdate"
python3 test/test_tools.py
echo "PRUEBAS EN PC: TODO OK"

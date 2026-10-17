#!/bin/bash
# presto/build.sh [--curious] [--autostart=FRAMES]: the Presto firmware with the player built in
# (rp2350-bbc-micro's firmware, from the submodule, plus presto/elite_presto.c and cplayer/).
# Needs the Pico SDK and the Arm toolchain: rp2350-bbc-micro/scripts/setup.sh fetches both (its
# deps/env.sh is used if there), or set PICO_SDK_PATH and put arm-none-eabi-gcc on the PATH.
# presto/elite_splash.c is the loading screen: tools/make_splash.py <Comfortaa.ttf> --screen "elite agent"
# presto/elite_splash.c elite_splash
# Writes presto/build/presto_master.uf2: hold BOOT, tap RESET, copy it to the RP2350 drive.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; TOP="$(dirname "$HERE")"; EMU="$TOP/rp2350-bbc-micro"
CURIOUS=0; AUTO=2500
for a in "$@"; do case $a in --curious) CURIOUS=1;; --autostart=*) AUTO=${a#*=};; *) echo "unknown: $a"; exit 1;; esac; done
[ -f "$EMU/deps/env.sh" ] && . "$EMU/deps/env.sh"
[ -n "$PICO_SDK_PATH" ] || { echo "PICO_SDK_PATH not set (run rp2350-bbc-micro/scripts/setup.sh)"; exit 1; }
SRC="$HERE/elite_presto.c;$HERE/elite_splash.c"
for f in keyq agent combat docked drv flight galaxy pilot ships; do SRC="$SRC;$TOP/cplayer/$f.c"; done
cmake -S "$EMU" -B "$HERE/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH" \
      -DMACHINE_EXTRA_SOURCES="$SRC" -DMACHINE_EXTRA_INCLUDES="$TOP/cplayer" \
      -DMACHINE_EXTRA_DEFINES="PLAYER_CURIOUS=$CURIOUS;ELITE_AUTOSTART=$AUTO" > "$HERE/build.log" 2>&1 || { tail "$HERE/build.log"; exit 1; }
ninja -C "$HERE/build" >> "$HERE/build.log" 2>&1 || { grep -E "error" "$HERE/build.log" | head; exit 1; }
ls -la "$HERE/build/presto_master.uf2"

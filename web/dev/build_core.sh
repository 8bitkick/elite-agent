#!/bin/bash
# web/dev/build_core.sh: the emulator (Kieran Connell's Master core) and the player in one binary, written to core.dev.wasm.
# Needs clang's wasm32-wasi target and the emulator's source (scripts/fetch-emulator.sh). The page and its verified careers
# use the pinned core.wasm, not this. To pin a new one: cp core.dev.wasm core.wasm; sha256sum core.wasm > core.wasm.sha256;
# and the references in main.js and the careers must be flown again.
cd "$(dirname "$0")"
CORE=../../rp2350-bbc-micro/src/cores/kc-beeb
SRC="keyq agent cfly combat docked drv elite_ver fair flight galaxy pilot ships show wsp_pilot wsp_dock"
FILES=""; for f in $SRC; do FILES="$FILES ../../cplayer/$f.c"; done
clang --target=wasm32-wasi --sysroot=/usr -isystem /usr/include/wasm32-wasi -L/usr/lib/wasm32-wasi \
  -O2 -fwrapv -mexec-model=reactor -I../../cplayer -I$CORE -DPLAYER_CURIOUS=0 -Wno-everything \
  -Wl,--allow-undefined-file=../imports.txt -Wl,--gc-sections -Wl,-z,stack-size=1048576 \
  -o core.dev.wasm core_wasm.c ../player_wasm.c $FILES -lm 2>&1 | grep -E "error|undefined" | head -30
ls -l core.dev.wasm && sha256sum core.dev.wasm

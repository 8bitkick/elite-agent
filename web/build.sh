#!/bin/bash
# web/build.sh: the C player as WebAssembly (clang's wasm32-wasi target; wasi-libc), written to agent.dev.wasm. The page and
# the results in STARTS.md use the pinned agent.wasm, not this. To pin a new one: cp agent.dev.wasm agent.wasm;
# sha256sum agent.wasm > agent.wasm.sha256; and the careers in STARTS.md must be flown again
cd "$(dirname "$0")"
SRC="keyq agent cfly combat docked drv elite_ver fair flight galaxy pilot ships show wsp_pilot wsp_dock"
FILES=""; for f in $SRC; do FILES="$FILES ../cplayer/$f.c"; done
clang --target=wasm32-wasi --sysroot=/usr -isystem /usr/include/wasm32-wasi -L/usr/lib/wasm32-wasi \
  -O2 -fwrapv -mexec-model=reactor -I../cplayer -DPLAYER_CURIOUS=0 \
  -Wno-unused-function -Wno-unused-variable -Wno-parentheses \
  -Wl,--allow-undefined-file=imports.txt -Wl,--export-dynamic -Wl,-z,stack-size=1048576 \
  -o agent.dev.wasm player_wasm.c $FILES -lm 2>&1 | grep -E "error|undefined" | head -30
ls -l agent.dev.wasm

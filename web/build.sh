#!/bin/bash
# web/build.sh: the emulator (Kieran Connell's Master core, with a 6502 Second Processor on its Tube) and the player in one
# binary, written to core.dev.wasm. Needs a clang with the wasm32-wasi target and the emulator's source
# (scripts/fetch-emulator.sh): the system's clang and wasi-libc, or the WASI SDK (WASI_SDK=<its directory>; the pinned
# core.wasm was built with Debian 13's clang 19). The core is copied to tube/build/kc-beeb and patched (tube/kc-beeb-tube.patch: the
# Tube in SHEILA, its IRQ, the two processors run in slices); tube/tube.c is the ULA and the parasite. With no parasite
# attached (every game but ?game=tube) the machine runs as the unpatched core did.
# The page and its verified careers use the pinned core.wasm, not this. To pin a new one: cp core.dev.wasm core.wasm;
# sha256sum core.wasm > core.wasm.sha256; and the references in src/main.js and the careers must be flown again.
set -e
cd "$(dirname "$0")"
rm -rf tube/build && mkdir -p tube/build
cp -r ../rp2350-bbc-micro/src/cores/kc-beeb tube/build/kc-beeb
(cd tube/build/kc-beeb && patch -s -p1 < ../../kc-beeb-tube.patch)
CORE=tube/build/kc-beeb
SRC="keyq agent cfly combat docked drv elite_ver fair flight galaxy pilot ships show wsp_pilot wsp_dock"
FILES=""; for f in $SRC; do FILES="$FILES ../cplayer/$f.c"; done
if [ -n "$WASI_SDK" ]; then
    CC=("$WASI_SDK/bin/clang" --target=wasm32-wasip1 --sysroot="$WASI_SDK/share/wasi-sysroot")
else
    CC=(clang --target=wasm32-wasi --sysroot=/usr -isystem /usr/include/wasm32-wasi -L/usr/lib/wasm32-wasi)
fi
"${CC[@]}" -O2 -fwrapv -mexec-model=reactor -I../cplayer -I$CORE -I. -DPLAYER_CURIOUS=0 -Wno-everything \
  -Wl,--allow-undefined-file=imports.txt -Wl,--gc-sections -Wl,-z,stack-size=1048576 \
  -o core.dev.wasm core_wasm.c player_wasm.c tube/tube.c $FILES -lm
ls -l core.dev.wasm && { command -v sha256sum >/dev/null && sha256sum core.dev.wasm || shasum -a 256 core.dev.wasm; }

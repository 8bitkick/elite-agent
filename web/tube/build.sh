#!/bin/bash
# web/tube/build.sh: the machine with a 6502 Second Processor on its Tube, and the player, in one binary: web/core.wasm
# for this fork's page. Kieran Connell's Master core (scripts/fetch-emulator.sh) is copied to build/kc-beeb and patched
# (kc-beeb-tube.patch: the Tube in SHEILA, its IRQ, the two processors run in slices); tube.c is the ULA and the parasite.
# Needs the WASI SDK (github.com/WebAssembly/wasi-sdk/releases: its clang, sysroot and builtins): WASI_SDK=<its directory>.
set -e
cd "$(dirname "$0")"
WASI_SDK=${WASI_SDK:-$(ls -d ../../../tools/wasi-sdk-* 2>/dev/null | tail -1)}
[ -x "$WASI_SDK/bin/clang" ] || { echo "set WASI_SDK to the WASI SDK's directory"; exit 1; }
CC=$WASI_SDK/bin/clang
SYSROOT=$WASI_SDK/share/wasi-sysroot
rm -rf build && mkdir -p build
cp -r ../../rp2350-bbc-micro/src/cores/kc-beeb build/kc-beeb
(cd build/kc-beeb && patch -s -p1 < ../../kc-beeb-tube.patch)
FILES=(); for f in keyq agent cfly combat docked drv elite_ver fair flight galaxy pilot ships show wsp_pilot wsp_dock; do FILES+=(../../cplayer/$f.c); done
$CC --target=wasm32-wasip1 --sysroot=$SYSROOT \
  -O2 -fwrapv -mexec-model=reactor -I../../cplayer -Ibuild/kc-beeb -I.. -DPLAYER_CURIOUS=0 -Wno-everything \
  -Wl,--allow-undefined-file=../imports.txt -Wl,--gc-sections -Wl,-z,stack-size=1048576 \
  -o ../core.wasm ../core_wasm.c ../player_wasm.c tube.c "${FILES[@]}" -lm
ls -l ../core.wasm

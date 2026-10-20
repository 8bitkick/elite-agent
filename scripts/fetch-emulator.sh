#!/bin/sh
# Fetches what the native sims (cplayer/run_sim, run_sim_b, lab) and presto/build.sh need into
# rp2350-bbc-micro/ (not part of this repository): the emulator, rp2350-bbc-micro at a pinned
# commit (Kieran Connell's Master core, beebjit and its embedded layer, the Model B's ROMs), the
# Master's MOS 3.20 ROMs, and Mark Moxon's flicker-free Elite for both machines. Elite is (c) Ian
# Bell and David Braben, so the discs aren't part of either repository. Re-running is safe: what's
# there is kept (another EMU_COMMIT checks that one out).
#   EMU_COMMIT=<sha> scripts/fetch-emulator.sh
set -e
EMU_COMMIT=${EMU_COMMIT:-b8232984d0e02350c6f1434374e1d9a0fad09dbf}
TOP=$(cd "$(dirname "$0")/.." && pwd); EMU=$TOP/rp2350-bbc-micro
if [ ! -d "$EMU/.git" ]; then
    git init -q "$EMU" && git -C "$EMU" remote add origin https://github.com/8bitkick/rp2350-bbc-micro.git
fi
if [ "$(git -C "$EMU" rev-parse HEAD 2>/dev/null)" != "$EMU_COMMIT" ]; then
    echo "fetching rp2350-bbc-micro $EMU_COMMIT"
    git -C "$EMU" fetch -q --depth 1 origin "$EMU_COMMIT" && git -C "$EMU" checkout -q FETCH_HEAD
fi
mkdir -p "$EMU/roms/mos3.20" "$EMU/discs"
for r in mos dfs viewsht edit basic4 adfs view terminal; do
    [ -f "$EMU/roms/mos3.20/$r.rom" ] || curl -fsSL -o "$EMU/roms/mos3.20/$r.rom" \
        "https://raw.githubusercontent.com/scarybeasts/beebjit/master/roms/mos3.20/$r.rom"
done
[ -f "$EMU/discs/elite-master-flicker-free-sng47.ssd" ] || curl -fsSL -o "$EMU/discs/elite-master-flicker-free-sng47.ssd" \
    https://elite.bbcelite.com/versions/flicker_free_elite/elite-master-flicker-free-sng47.ssd
[ -f "$EMU/discs/elite-disc-flicker-free-sth.ssd" ] || sh "$EMU/scripts/fetch-elite.sh" >/dev/null
echo "rp2350-bbc-micro $(git -C "$EMU" rev-parse --short HEAD), its ROMs and both Elites: ready"

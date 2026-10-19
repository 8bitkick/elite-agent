#!/bin/bash
# cplayer/build.sh: run_sim, the C player on the native emulator
cd "$(dirname "$0")"
CORE=../rp2350-bbc-micro/src/cores/kc-beeb   # the emulator: Kieran Connell's Master core (git submodule)
[ -d $CORE ] || CORE=../rp2350-bbc-micro/core     # (the submodule before the merge of bbc-master)
SRC="keyq.c agent.c combat.c docked.c drv.c fair.c flight.c galaxy.c pilot.c ships.c show.c"
gcc -O2 -Wall -fwrapv -DPLAYER_TIMING -o run_sim run_sim.c $SRC ../sim/libbeeb.c -I$CORE -lm 2>&1 | grep -E "error|agent|combat|docked|drv|fair|flight|galaxy|pilot|ships|run_sim" | grep -v "^In file" | head -30
ls -la run_sim

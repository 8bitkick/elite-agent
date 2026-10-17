#!/bin/bash
# cplayer/build.sh: run_sim, the C player on the native emulator
cd "$(dirname "$0")"
CORE=../rp2350-bbc-micro/core      # the emulator (git submodule)
SRC="keyq.c agent.c combat.c docked.c drv.c flight.c galaxy.c pilot.c ships.c"
gcc -O2 -Wall -fwrapv -o run_sim run_sim.c $SRC ../sim/libbeeb.c -I$CORE -lm 2>&1 | grep -E "error|agent|combat|docked|drv|flight|galaxy|pilot|ships|run_sim" | grep -v "^In file" | head -30
ls -la run_sim

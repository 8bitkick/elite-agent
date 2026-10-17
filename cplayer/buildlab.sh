#!/bin/bash
cd "$(dirname "$0")"
CORE=../rp2350-bbc-micro/core      # the emulator (git submodule)
gcc -O2 -Wall -fwrapv -o lab lab.c keyq.c agent.c combat.c docked.c drv.c flight.c galaxy.c pilot.c ships.c ../sim/libbeeb.c -I$CORE -lm 2>&1 | grep -E "error|warning: .*(lab|agent|combat)"

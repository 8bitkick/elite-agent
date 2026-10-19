#!/bin/bash
cd "$(dirname "$0")"
CORE=../rp2350-bbc-micro/src/cores/kc-beeb   # the emulator: Kieran Connell's Master core (git submodule)
[ -d $CORE ] || CORE=../rp2350-bbc-micro/core     # (the submodule before the merge of bbc-master)
gcc -O2 -Wall -fwrapv -o lab lab.c keyq.c agent.c combat.c docked.c drv.c fair.c flight.c galaxy.c pilot.c ships.c show.c showmeter.c ../sim/libbeeb.c -I$CORE -lm 2>&1 | grep -E "error|warning: .*(lab|agent|combat|fair|drv)"

#!/bin/bash
cd "$(dirname "$0")"
CORE=../rp2350-bbc-micro/src/cores/kc-beeb   # the emulator: Kieran Connell's Master core (scripts/fetch-emulator.sh)
gcc -O2 -Wall -fwrapv -o lab lab.c keyq.c agent.c cfly.c combat.c docked.c drv.c elite_ver.c fair.c flight.c galaxy.c pilot.c ships.c show.c showmeter.c ../sim/libbeeb.c -I$CORE -lm 2>&1 | grep -E "error|warning: .*(lab|agent|combat|fair|drv)"
ls -la lab

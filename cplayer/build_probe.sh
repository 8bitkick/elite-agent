#!/bin/bash
# cplayer/build_probe.sh: run_probe, the C player on a real BBC Micro Model B through the Pi1MHz
# RAM probe (docs/PROBE.md).  No emulator: the BBC is the machine.
cd "$(dirname "$0")"
SRC="keyq.c agent.c cfly.c combat.c docked.c drv.c elite_ver.c fair.c flight.c galaxy.c pilot.c ships.c show.c wsp_pilot.c wsp_dock.c"
gcc -O2 -Wall -fwrapv -o run_probe run_probe.c $SRC -lm 2>&1 | grep -E "error|warning|run_probe" | grep -v "^In file" | head -30
ls -la run_probe

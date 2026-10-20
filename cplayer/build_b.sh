#!/bin/bash
# cplayer/build_b.sh: run_sim_b, the C player on the native BBC Model B (beebjit, as the Presto's Model B
# runs it: src/cores/beebjit and the embedded layer in src/machines/beeb), for Model B disc Elite.
# The same player as run_sim (build.sh, the Master); only the emulator differs.
#   cplayer/build_b.sh        (beebjit's objects are kept in cplayer/.beebjit-obj)
cd "$(dirname "$0")"
EMU=${ELITE_EMU:-../rp2350-bbc-micro}
BJ=$EMU/src/cores/beebjit
MB=$EMU/src/machines/beeb
OBJ=.beebjit-obj
mkdir -p $OBJ
BJFLAGS="-O2 -std=gnu11 -w -DBEEBJIT_NO_MMU=1 -DBEEBJIT_MALLOC=beebjit_embedded_malloc \
  -DBEEBJIT_ALLOC_CALLER=beebjit_embedded_alloc_caller -DBEEBJIT_REALLOC=beebjit_embedded_realloc \
  -DBEEBJIT_FREE=beebjit_embedded_free -DBEEBJIT_HEADLESS=1 -DBEEBJIT_RENDER_PACKED4=1 \
  -DBEEBJIT_INTERP_COUNTDOWN32=1 -DBEEBJIT_VIDEO_FAST_RUNS=1 -DBEEBJIT_RENDER_WIRE=0 -I$BJ -I$MB"
SOURCES=$(grep -v '^#' $MB/beebjit_sources.txt | sed -e 's#^asm/asm_common.c#asm/null/asm_common_null.c#' \
    -e 's#^asm/asm_jit.c#asm/null/asm_jit_null.c#' -e 's#^asm/asm_inturbo.c#asm/null/asm_inturbo_null.c#')
OBJS=""
for f in $SOURCES; do
    o=$OBJ/$(echo $f | tr / _ | sed 's/\.c$/.o/')
    [ $o -nt $BJ/$f ] || gcc $BJFLAGS -c $BJ/$f -o $o &
    OBJS="$OBJS $o"
done
for f in os_embedded.c beebjit_embedded.c; do
    o=$OBJ/$(basename $f .c).o
    [ $o -nt $MB/$f ] || gcc $BJFLAGS -c $MB/$f -o $o &
    OBJS="$OBJS $o"
done
wait
SRC="keyq.c agent.c combat.c docked.c drv.c elite_ver.c fair.c flight.c galaxy.c pilot.c ships.c show.c"
gcc -O2 -Wall -fwrapv -DPLAYER_TIMING -o run_sim_b run_sim.c $SRC ../sim/libbeebjit.c $OBJS -I$BJ -I$MB -lm 2>&1 \
    | grep -E "error|agent|combat|docked|drv|elite_ver|fair|flight|galaxy|pilot|ships|run_sim|libbeebjit" | grep -v "^In file" | head -30
ls -la run_sim_b

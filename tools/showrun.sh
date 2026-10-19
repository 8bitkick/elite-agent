#!/bin/bash
# tools/showrun.sh <fight list> <name> [VAR=value ...]: every fight in its own lab process (static state in the
# player carries over within one), NPROC (3) at a time, undamped as careers fly. The lab's lines for each fight
# (result, SCN, SHOW, DCAUSE) go to runs/<name>.txt, in list order, for tools/show.py
list=$1; name=$2; shift 2
cd "$(dirname "$0")/.."
T=/tmp/showrun_$name; rm -rf $T; mkdir -p runs $T
LAB=${LAB:-$PWD/cplayer/lab}
nl -nln -w1 -s' ' "$list" | xargs -P ${NPROC:-3} -L1 sh -c "echo \$1 > $T/\$0.lst; env UNDAMPED=1 DCAUSE=1 COLLOG=1 $* $LAB $T/\$0.lst 2>&1 | grep -E '^(/|SCN|SHOW|DCAUSE|COL)' > $T/\$0.out"
n=$(wc -l < "$list")
for i in $(seq 1 $n); do cat $T/$i.out; done > runs/$name.txt
rm -rf $T

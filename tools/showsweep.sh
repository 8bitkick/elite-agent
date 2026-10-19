#!/bin/bash
# tools/showsweep.sh <list> <prefix> "name SHOWP [VAR=value ...]" ...: one showrun per configuration (curious
# mode; SHOWP "-" for show.c's defaults), then the summaries, show off first, into runs/<prefix>_sweep.txt
list=$1; pre=$2; shift 2
cd "$(dirname "$0")/.."
cfgs=("$@")
[ -f runs/${pre}_base.txt ] || tools/showrun.sh $list ${pre}_base CURIOUS=1 SHOW=0
outs=runs/${pre}_base.txt
for c in "${cfgs[@]}"; do
    set -- $c
    name=$1 p=$2; shift 2
    [ "$p" = "-" ] && p=""
    tools/showrun.sh $list ${pre}_$name CURIOUS=1 SHOWP=$p "$@"
    outs="$outs runs/${pre}_$name.txt"
done
python3 tools/show.py $outs > runs/${pre}_sweep.txt 2>&1
touch runs/${pre}_sweep.done

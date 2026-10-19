#!/bin/bash
# tools/showcar.sh <name> <skew> [VAR=value ...]: one curious career (run_sim) for up to 100 game hours, its
# journal and log in careers/<name>/ (tools/career_summary.py's layout), then the summary line
name=$1; skew=$2; shift 2
cd "$(dirname "$0")/.."
mkdir -p careers/$name
env CURIOUS=1 SKEW=$skew "$@" ${RUNSIM:-cplayer/run_sim} 6000 "" careers/$name/journal.jsonl > careers/$name/run.log 2>&1
python3 tools/career_summary.py careers/$name > careers/$name/summary.txt 2>&1
touch careers/$name/done

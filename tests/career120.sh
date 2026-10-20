#!/bin/sh
# tests/career120.sh: the player's behaviour, pinned. A 120-game-minute efficient career on the Master
# (seed 13, from Jameson) must write the journal tests/career120.sha256 has the hash of: any change in
# what the player does changes it. A change meant to play differently updates the hash (and gets an
# EXPERIMENTS.md row); a cleanup must leave it as it is. Needs cplayer/run_sim built
# (scripts/fetch-emulator.sh, cplayer/build.sh). ~45 s.
#   tests/career120.sh [--update]
set -e
TOP=$(cd "$(dirname "$0")/.." && pwd); OUT=${TMPDIR:-/tmp}/career120-$$; mkdir -p "$OUT"
cd "$TOP" && UNDAMPED=1 SKEW=13 cplayer/run_sim 120 "" "$OUT/career120.jsonl" > "$OUT/run.log" 2>&1 || { tail "$OUT/run.log"; exit 1; }
cd "$OUT"
if [ "$1" = --update ]; then sha256sum career120.jsonl > "$TOP/tests/career120.sha256"; echo "updated: $(cut -c1-12 "$TOP/tests/career120.sha256")"; exit 0; fi
if sha256sum -c --quiet "$TOP/tests/career120.sha256"; then
    echo "career120: identical ($(grep -c . career120.jsonl) journal lines)"
else
    echo "career120: the journal differs (the player plays differently); kept in $OUT/career120.jsonl"; exit 1
fi

#!/bin/bash
# Fly a career on the page's own core.wasm, headless, and print its row for the results table in web/STARTS.md.
#   web/tools/fly.sh --game flicker|solid --skew N [--out dir]
# Needs only Node 22 and curl: it fetches the ROMs and the disc at their pinned commits and checks their hashes. It stops at
# Elite or after MINUTES of game time (default 2400). Environment: VERIFIED_BY (default your git user.name), MACHINE, MINUTES.
set -e
WEB=$(cd "$(dirname "$0")/.." && pwd)
OUT=runs GAME= SKEW=
while [ $# -gt 0 ]; do
    case "$1" in
        --game) GAME=$2; shift 2 ;;
        --skew) SKEW=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
if [ -z "$GAME" ] || [ -z "$SKEW" ]; then echo "usage: fly.sh --game flicker|solid --skew N [--out dir]" >&2; exit 2; fi
case "$GAME" in
    flicker) DISC_URL=https://raw.githubusercontent.com/markmoxon/elite-source-code-bbc-master/e6581b586b1c984142a0fee0c8009c7466a38d52/5-compiled-game-discs/elite-master-flicker-free-sng47.ssd ;;
    solid) DISC_URL=https://raw.githubusercontent.com/ebenupton/solid-elite-bbc-master/262bfa61b69c3fb75ce2c004d4944bdc47c1abbd/5-compiled-game-discs/elite-master-sng47.ssd ;;
    *) echo "unknown game $GAME" >&2; exit 2 ;;
esac
ROM_BASE=https://raw.githubusercontent.com/scarybeasts/beebjit/db16aaf09df1fa0fd7ea686bc8d09b13f5674a42/roms/mos3.20
MINUTES=${MINUTES:-2400}
mkdir -p "$OUT/assets/roms"; OUT=$(cd "$OUT" && pwd)
for r in mos dfs viewsht edit basic4 adfs view terminal; do
    [ -s "$OUT/assets/roms/$r.rom" ] || curl -fsSL -o "$OUT/assets/roms/$r.rom" "$ROM_BASE/$r.rom"
done
DISC=$OUT/assets/$GAME.ssd
[ -s "$DISC" ] || curl -fsSL -o "$DISC" "$DISC_URL"
T=$OUT/${GAME}_$SKEW
(cd "$WEB" && sha256sum -c core.wasm.sha256 >/dev/null)
node "$WEB/tools/career_meta.mjs" --game "$GAME" --skew "$SKEW" --disc "$DISC" --wasm "$WEB/core.wasm" --minutes "$MINUTES" > "$T.meta.json"
t0=$(date +%s)
ROMS=$OUT/assets/roms DISC=$DISC SKEW=$SKEW NAME=BITKICK node --no-warnings "$WEB/run_core.mjs" "$MINUTES" "$T.journal.jsonl" > "$T.out"
WALL=$(awk -v s=$(( $(date +%s) - t0 )) 'BEGIN { printf "%.1f", s / 3600 }')
tail -n 2 "$T.out"
node "$WEB/tools/career_row.mjs" --game "$GAME" --skew "$SKEW" --out "$T.out" --journal "$T.journal.jsonl" --wasm "$WEB/core.wasm" \
    --by "${VERIFIED_BY:-$(git -C "$WEB" config user.name)}" --machine "${MACHINE:-$(uname -sm), $(node -p 'os.cpus()[0].model')}" --wall-hours "$WALL"

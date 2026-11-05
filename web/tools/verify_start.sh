#!/bin/bash
# Verify a page start: its snapshot from a real browser visit, the determinism gate, then its career to Elite in Node.
# Prints one row for the results table in web/STARTS.md. See that file for the prerequisites.
#   web/tools/verify_start.sh --game flicker --start 31 --disc flicker.ssd [--out dir] [--gate-only] [--throttled]
#     --throttled  also the gate's second visit, the CPU throttled 4x (asked of one start per game)
#     --gate-only  stop after the gate
# Environment: JSBEEB (jsbeeb's source, at c6ac7200; default web/.jsbeeb), SITE (default web/_site), CHROMIUM (a browser
# for Playwright, if not its own), VERIFIED_BY (default your git user.name), MACHINE, MINUTES (game minutes allowed; default 2400).
# Rerun the same command after an interruption: the visit and the gate aren't repeated once passed, and the career
# resumes from its checkpoint.
set -e
WEB=$(cd "$(dirname "$0")/.." && pwd)
OUT=. GATE_ONLY= THROTTLED= GAME= START= DISC=
while [ $# -gt 0 ]; do
    case "$1" in
        --game) GAME=$2; shift 2 ;;
        --start) START=$2; shift 2 ;;
        --disc) DISC=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        --gate-only) GATE_ONLY=1; shift ;;
        --throttled) THROTTLED=1; shift ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
if [ -z "$GAME" ] || [ -z "$START" ] || [ -z "$DISC" ]; then
    echo "usage: verify_start.sh --game flicker|solid --start N --disc file.ssd [--out dir] [--gate-only] [--throttled]" >&2; exit 2
fi
SITE=${SITE:-$WEB/_site}
JSBEEB=$(cd "${JSBEEB:-$WEB/.jsbeeb}" && pwd)
DISC=$(cd "$(dirname "$DISC")" && pwd)/$(basename "$DISC")
MINUTES=${MINUTES:-2400}
WASM=$SITE/agent.wasm
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
T=$OUT/${GAME}_$START
run_node() { JSBEEB=$JSBEEB WASM=$WASM DISC=$DISC START=$T.json node "$WEB/run_node.mjs" "$@"; }
visit() { node "$WEB/tools/start_snapshot.mjs" --game "$GAME" --start "$START" --disc "$DISC" --site "$SITE" --out "$OUT" "$@"; }
compare() { node "$WEB/tools/gate_compare.mjs" "$@"; }

# ---- the gate. Anything different stops here
if [ ! -e "$T.gate" ]; then
    [ -e "$T.json" ] && [ -e "$T.log" ] || visit
    run_node 10 > "$T.gate.out"
    compare node "$T.log" "$T.gate.out"
    touch "$T.gate"
fi
if [ -n "$THROTTLED" ] && [ ! -e "$T.throttled" ]; then
    visit --throttle 4 --tag throttled
    compare snapshot "$T.json" "${T}_throttled.json"
    compare browser "$T.log" "${T}_throttled.log"
    touch "$T.throttled"
fi
echo "gate passed: $GAME $START"
[ -n "$GATE_ONLY" ] && exit 0

# ---- the career (a resumed run adds to its output)
[ -e "$T.meta.json" ] || node "$WEB/tools/career_meta.mjs" --game "$GAME" --start "$START" --snapshot "$T.json" --disc "$DISC" \
    --wasm "$WASM" --jsbeeb "$JSBEEB" --minutes "$MINUTES" ${WASM_SOURCE:+--wasm-source "$WASM_SOURCE"} > "$T.meta.json"
export CKPT=$T.ckpt
t0=$(date +%s)
run_node "$MINUTES" "$T.journal.jsonl" >> "$T.out"
echo $(( $(date +%s) - t0 )) >> "$T.wall"
unset CKPT

WALL=$(awk '{ s += $1 } END { printf "%.1f", s / 3600 }' "$T.wall")
tail -n 1 "$T.out"
node "$WEB/tools/career_row.mjs" --game "$GAME" --start "$START" --out "$T.out" --journal "$T.journal.jsonl" --wasm "$WASM" \
    --by "${VERIFIED_BY:-$(git -C "$WEB" config user.name)}" --machine "${MACHINE:-$(uname -sm), $(node -p 'os.cpus()[0].model')}" --wall-hours "$WALL"

# Career journals

Each `.jsonl` is one career, a journal event per line (`tools/career_stats.py` reads them).

## Native careers (`run_sim`)

These were committed before careers recorded what flew them. The mode and seed come from the filename, and the hours from
the journal. The commit is the one that added or last replaced the file, so the career was flown at or before it; the exact
build, disc and other parameters weren't recorded.

| journal | mode | seed | game hours to Elite | committed in | disc, other parameters |
|---|---|---|---|---|---|
| `efficient_s13.jsonl` | efficient | 13 | 14.54 | `0d687c9` | not recorded |
| `efficient_s101.jsonl` | efficient | 101 | 14.35 | `0d687c9` | not recorded |
| `fair_s13.jsonl` | Fair | 13 | 16.71 | `be09872` | not recorded |
| `fair_s101.jsonl` | Fair | 101 | 16.99 | `be09872` | not recorded |

## Page careers (`starts/`)

`starts/<game>_<start>.journal.jsonl`, flown by `web/run_node.mjs` from a start the page makes, with
`starts/<game>_<start>.meta.json` beside it: this repo and commit, the start, the parameters, the disc's repo, commit and
hash, the `player.wasm` hash and source, the jsbeeb commit, and the machine. See [web/STARTS.md](../../web/STARTS.md).

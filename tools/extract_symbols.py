#!/usr/bin/env python3
"""Pull label addresses out of Mark Moxon's assembled listing (3-assembled-output/compile.txt of
elite-source-code-bbc-master, flicker-free branch) into elite/symbols.json.

  git clone -b flicker-free https://github.com/markmoxon/elite-source-code-bbc-master vendor-src
  tools/extract_symbols.py vendor-src/3-assembled-output/compile.txt
"""
import json, re, sys
lines = open(sys.argv[1], errors='replace').read().split('\n')
# the loader (M128Elt) comes first and reuses names for its own zero page: only take labels from
# after it's saved (the game code BCODE, then the data BDATA)
start = next(i for i, l in enumerate(lines) if l.startswith("Saving file") and "M128Elt" in l)
syms = {}
for i, l in enumerate(lines[:-1]):
    if i < start: continue
    m = re.match(r'^\.(\S+)\s*$', l)
    m2 = re.match(r'^\s+([0-9A-F]{4})', lines[i + 1])
    if m and m2 and m.group(1) not in syms:
        syms[m.group(1)] = int(m2.group(1), 16)
json.dump(syms, open('elite/symbols.json', 'w'), indent=0, sort_keys=True)
print(len(syms), 'symbols')

#!/usr/bin/env python3
"""showkills.py runs/X.txt ...: where the kills happen (the SHOW lines' kd lists), by scenario: distance
quartiles of laser kills, the share under 1000 and 2000, and how (f front, r rear, m missile, b bomb)."""
import sys, collections
for p in sys.argv[1:]:
    ws, by = {}, collections.defaultdict(list)
    for line in open(p):
        v = line.split()
        if v and v[0] == 'SCN':
            d = dict(zip(v[2::2], v[3::2])); n = int(d['n'])
            ws[v[1]] = 'witchspace' if d['ws'] == '1' else '1' if n <= 1 else '2-3' if n <= 3 else '4+'
        elif v and v[0] == 'SHOW':
            d = dict(zip(v[2::2], v[3::2]))
            if d.get('kd', '-') != '-':
                by[ws.get(v[1], '?')] += [(int(x[:-1]), x[-1]) for x in d['kd'].split(',')]
    print('==', p)
    for k in sorted(by):
        ks = by[k]; ds = sorted(x for x, h in ks if h in 'fr')
        if not ds: continue
        print('  %-10s kills %5d  laser kill distance quartiles %5d %5d %5d  under 1000 %3.0f%%  under 2000 %3.0f%%  %s' % (
            k, len(ks), ds[len(ds) // 4], ds[len(ds) // 2], ds[3 * len(ds) // 4], 100 * sum(x < 1000 for x in ds) / len(ds),
            100 * sum(x < 2000 for x in ds) / len(ds), dict(collections.Counter(h for _, h in ks))))

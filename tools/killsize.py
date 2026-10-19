#!/usr/bin/env python3
"""killsize.py runs/X.txt ...: where the kills happen and how big they're drawn, from the SHOW lines' kd lists
(distance, how: f front laser, r rear laser, m missile, b bomb; size on screen, px; the mode then: S show, B the
budget spent, M the margin had handed over to efficient, O no show). Per mode and weapon: kills, the share drawn
20 px or more, the median distance; and the same for witchspace alone."""
import sys, re, collections
ENTRY = re.compile(r'^(\d+)([frmbe])(\d+)([SBMO])$')
for p in sys.argv[1:]:
    ws, kills = {}, []
    for line in open(p):
        v = line.split()
        if v and v[0] == 'SCN':
            d = dict(zip(v[2::2], v[3::2]))
            ws[v[1]] = d.get('ws') == '1'
        elif v and v[0] == 'SHOW':
            d = dict(zip(v[2::2], v[3::2]))
            for x in d.get('kd', '-').split(','):
                m = ENTRY.match(x)
                if m: kills.append((ws.get(v[1], False), int(m.group(1)), m.group(2), int(m.group(3)), m.group(4)))
    print('==', p, len(kills), 'kills; drawn 20 px+: %.0f%%' % (100 * sum(k[3] >= 20 for k in kills) / max(1, len(kills))))
    for label, sel in (('all', lambda k: True), ('pirates', lambda k: not k[0]), ('witchspace', lambda k: k[0])):
        K = [k for k in kills if sel(k)]
        if not K: continue
        print('  %s: %d kills, 20 px+ %.0f%%' % (label, len(K), 100 * sum(k[3] >= 20 for k in K) / len(K)))
        g = collections.defaultdict(list)
        for k in K: g[(k[4], k[2])].append(k)
        for (mode, how), G in sorted(g.items(), key=lambda kv: -len(kv[1])):
            ds = sorted(k[1] for k in G)
            print('    mode %s how %s: %5d kills (%4.1f%%)  20 px+ %3.0f%%  median distance %5d  median px %3d' % (
                mode, how, len(G), 100 * len(G) / len(K), 100 * sum(k[3] >= 20 for k in G) / len(G), ds[len(ds) // 2],
                sorted(k[3] for k in G)[len(G) // 2]))

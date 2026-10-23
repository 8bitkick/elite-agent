#!/usr/bin/env python3
"""docklab.py <lab stdout> <lab stderr>: the docking lab (lab LABDOCK=1 LOG=3) measured, a docking a start.

From the DOCKLAB lines (stdout): docked / died / timeout and the time from dock()'s start (fights left out). From the DK
lines (stderr, a frame each): bumps, off the axis and the slot's roll at entry (as tools/dockstats.py); stick reversals a
second (the roll and pitch sticks changing side, |J| 8 or more each way); and the turn-in, from the last time the station
came within 30 deg of the nose to its centre within 2 deg in the final: its time, the time the station sat stalled
off-centre in view (over 4 deg off, closing under 2 deg a second over the next half second), and the number of separate
moves bringing it in (runs of it closing faster than that, apart by 0.3 s or more). One move is one smooth curve."""
import math, re, statistics, sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from dockstats import DK, measure

SX = re.compile(r'DK [FL] fr \d+ S (-?\d+) (-?\d+) (-?\d+) .*? jx (-?\d+) jy (-?\d+)')


def turnin(d):
    ang = [math.degrees(math.atan2(math.hypot(x['Sx'], x['Sy']), x['Sz'])) for x in d]
    end = next((i for i, x in enumerate(d) if x['ph'] == 'F' and ang[i] < 2), None)
    if end is None:
        return None
    start = end
    while start > 0 and ang[start - 1] < 30:
        start -= 1
    W = 25
    closing = []
    for i in range(start, end):
        j = min(end, i + W)
        rate = (ang[i] - ang[j]) / max(1, j - i) * 50      # deg a second
        closing.append(rate >= 2)
    stall = sum(1 for k, i in enumerate(range(start, end)) if ang[i] > 4 and not closing[k]) / 50
    moves, gap, inmove = 0, 99, False
    for c in closing:
        if c:
            if not inmove and gap >= 15:
                moves += 1
            inmove, gap = True, 0
        else:
            inmove = False
            gap += 1
    return dict(tt=(end - start) / 50, stall=stall, moves=max(1, moves))


def reversals(d):
    out = []
    for k in ('jx', 'jy'):
        last, n = 0, 0
        for x in d:
            s = (x[k] >= 8) - (x[k] <= -8)
            if s and last and s != last:
                n += 1
            if s:
                last = s
        out.append(n)
    t = max(1, len(d)) / 50
    return out[0] / t, out[1] / t


def parse(outp, errp):
    res = [l.split() for l in open(outp) if l.startswith('DOCKLAB')]
    docks, cur = [], []
    for line in open(errp, errors='replace'):
        m = DK.search(line)
        if m:
            g = m.groups()
            s = SX.search(line)
            cur.append(dict(ph=g[0], fr=int(g[1]), sx=float(g[2]), sy=float(g[3]), dist=int(g[4]), along=int(g[5]), lat=int(g[6]),
                            off=float(g[7]), roll=float(g[8]), spd=int(g[9]), E=int(g[10]), F=int(g[11]), A=int(g[12]),
                            Sx=int(s.group(1)), Sy=int(s.group(2)), Sz=int(s.group(3)), jx=int(s.group(4)), jy=int(s.group(5))))
        elif line.startswith('STATUS Docked') or line.startswith('died: back') or line.startswith('timeout'):
            docks.append(cur)
            cur = []
    rows = []
    for r, d in zip(res, docks):
        row = dict(name=r[1].rsplit('/', 1)[-1], how=r[2], t=(int(r[4]) - int(r[12])) / 50)
        if r[2] == 'docked' and len(d) > 10:
            m = measure(d)
            row.update(bumps=m['bumps'], late=m['late'], rolle=m['rolle'], tf=m['tf'])
            row['rrev'], row['prev'] = reversals(d)
            ti = turnin(d)
            if ti:
                row.update(ti)
        rows.append(row)
    return rows


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p * len(xs)))] if xs else 0


def summary(rows, label=''):
    ok = [r for r in rows if r['how'] == 'docked']
    print('%s%d starts: docked %d, died %d, timeout %d' % (label, len(rows), len(ok), sum(r['how'] == 'died' for r in rows),
                                                         sum(r['how'] == 'timeout' for r in rows)))
    for k, name in (('t', 'docking time (s)'), ('tf', 'final (s)'), ('late', 'off the axis at entry'), ('rolle', 'slot roll at entry (deg)'),
                    ('rrev', 'roll reversals a second'), ('prev', 'pitch reversals a second'), ('tt', 'turn-in: 30 deg to centred (s)'),
                    ('stall', 'turn-in: station stalled off-centre (s)'), ('moves', 'turn-in: separate moves')):
        xs = [r[k] for r in ok if k in r]
        if xs:
            print('  %-40s mean %6.2f  median %6.2f  p90 %6.2f  max %6.1f' % (name, statistics.mean(xs), statistics.median(xs), pct(xs, 0.9), max(xs)))
    mv = [r['moves'] for r in ok if 'moves' in r]
    print('  bumps %d (in %d dockings); turn-ins in one move %d of %d' % (sum(r.get('bumps', 0) for r in ok), sum(1 for r in ok if r.get('bumps')),
                                                                         sum(1 for m in mv if m == 1), len(mv)))


if __name__ == '__main__':
    rows = parse(sys.argv[1], sys.argv[2])
    if len(sys.argv) > 3 and sys.argv[3] == '-v':
        for r in rows:
            print(r)
    summary(rows)

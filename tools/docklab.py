#!/usr/bin/env python3
"""docklab.py <lab stdout> <lab stderr, or - for stdin> [-v]: the docking lab (lab LABDOCK=1 LOG=3) measured, a docking a start.

From the DOCKLAB lines (stdout): docked / died / timeout and the time from dock()'s start (fights left out). From the DK
lines (stderr, a frame each): bumps, off the axis and the slot's roll at entry (as tools/dockstats.py); stick reversals a
second (the roll and pitch sticks changing side, |J| 8 or more each way); and the turn-in, from the last time the station
came within 40 deg of the nose (at most 10 s before) to within 8 deg of it for good: its time, the time the station sat
stalled in view (closing under 2 deg a second over the next half second), and the number of separate moves bringing
it in (runs of it closing faster than that, apart by 0.3 s or more). One move is one smooth curve. And at the
hand-over to the final: how far off the nose the station was, and its slot off level. Overshoot: the most the station
strays off the nose in the 3 s after it first comes within 1 deg; and the pitch stick's reversals in the 2 s before
the hand-over."""
import math, re, statistics, sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from dockstats import DK, measure

SX = re.compile(r'DK [FL] fr \d+ S (-?\d+) (-?\d+) (-?\d+) .*? jx (-?\d+) jy (-?\d+)')


def turnin(d):
    ang = [math.degrees(math.atan2(math.hypot(x['Sx'], x['Sy']), x['Sz'])) for x in d]
    # (the end: the station within 8 deg of the nose (the final holds it up to 6 off, closing a sideways offset) from then on)
    end = len(d)
    while end > 0 and ang[end - 1] < 8:
        end -= 1
    if end >= len(d) - 1 or end == 0:
        return None
    start = end
    while start > 0 and ang[start - 1] < 40 and end - start < 500:
        start -= 1
    h = next((i for i, x in enumerate(d) if x['ph'] == 'F'), end)
    W = 25
    closing = []
    for i in range(start, end):
        j = min(len(d) - 1, i + W)
        closing.append((ang[i] - ang[j]) / max(1, j - i) * 50 >= 2)      # (deg a second)
    st = [i for k, i in enumerate(range(start, end)) if not closing[k]]
    moves, gap, inmove = 0, 99, False
    for c in closing:
        if c:
            if not inmove and gap >= 15:
                moves += 1
            inmove, gap = True, 0
        else:
            inmove = False
            gap += 1
    # (overshoot: the most the station strays off the nose in the 3 s after it first comes within 1 deg (from the
    # turn-in's start); and the pitch stick's reversals in the last 2 s before the hand-over)
    i0 = next((i for i in range(start, len(d)) if ang[i] < 1), None)
    over = max(ang[i0:i0 + 150]) if i0 is not None else 0
    last, prev2 = 0, 0
    for x in d[max(0, h - 100):h]:
        sg = (x['jy'] >= 8) - (x['jy'] <= -8)
        if sg and last and sg != last:
            prev2 += 1
        if sg:
            last = sg
    return dict(over=over, prev2=prev2, tt=(end - start) / 50, stall=len(st) / 50, moves=max(1, moves), hang=ang[min(h, len(d) - 1)], hroll=abs(d[min(h, len(d) - 1)]['roll']),
                stallL=sum(1 for i in st if i < h) / 50)


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


def measures(errf):
    """the measures of each docking in the lab's stderr (a file, or - for stdin), in order"""
    cur = []
    for line in (sys.stdin if errf == '-' else open(errf, errors='replace')):
        m = DK.search(line)
        if m:
            g = m.groups()
            s = SX.search(line)
            cur.append(dict(ph=g[0], fr=int(g[1]), sx=float(g[2]), sy=float(g[3]), dist=int(g[4]), along=int(g[5]), lat=int(g[6]),
                            off=float(g[7]), roll=float(g[8]), spd=int(g[9]), E=int(g[10]), F=int(g[11]), A=int(g[12]),
                            Sx=int(s.group(1)), Sy=int(s.group(2)), Sz=int(s.group(3)), jx=int(s.group(4)), jy=int(s.group(5))))
        elif line.startswith('STATUS Docked') or line.startswith('died: back') or line.startswith('timeout'):
            row = {}
            if line.startswith('STATUS') and len(cur) > 10:
                mm = measure(cur)
                row.update(bumps=mm['bumps'], late=mm['late'], rolle=mm['rolle'], tf=mm['tf'])
                row['rrev'], row['prev'] = reversals(cur)
                ti = turnin(cur)
                if ti:
                    row.update(ti)
            yield row
            cur = []


def parse(outp, errf):
    ms = list(measures(errf))
    res = [l.split() for l in open(outp) if l.startswith('DOCKLAB')]
    rows = []
    for r, m in zip(res, ms):
        row = dict(name=r[1].rsplit('/', 1)[-1], how=r[2], t=(int(r[4]) - int(r[12])) / 50)
        if r[2] == 'docked':
            row.update(m)
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
                    ('rrev', 'roll reversals a second'), ('prev', 'pitch reversals a second'), ('tt', 'turn-in: 40 to 8 deg (s)'),
                    ('stall', 'turn-in: station stalled off-centre (s)'), ('stallL', '... of it before the hand-over (s)'), ('moves', 'turn-in: separate moves'),
                    ('over', 'overshoot past the nose (deg)'), ('prev2', 'pitch reversals, last 2 s before the final'),
                    ('hang', 'hand-over: station off the nose (deg)'), ('hroll', 'hand-over: slot off level (deg)')):
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

#!/usr/bin/env python3
"""dockstats.py <run.log> ...: the dockings in run_sim logs made with DOCKLOG=1, measured.

Per docking (the DK lines from the start of dock()'s approach to the next "Docked" or the end): time (s, the first DK
line to the last), time in the final; the largest offset from the slot's axis inside 2000 out (lat, game units) and
at entry (the last DK line); the slot's roll off level at entry (deg); how far off the sights the slot's centre was
drawn, at most, inside 2000 (deg); bumps (a shield drop between DK lines inside 800); re-line-ups (final -> line up).
Failures: "dock" failures (the deadline) in the log."""
import re, statistics, sys

DK = re.compile(r'DK ([FL]) fr (\d+) .*? slot (-?[\d.]+) (-?[\d.]+) dist (\d+) along (-?\d+) lat (\d+) off ([\d.]+) roll (-?[\d.]+) .*? spd (\d+) E (\d+) F (\d+) A (\d+)')


def dockings(path):
    cur, out, fails = [], [], 0
    for line in open(path, errors='replace'):
        m = DK.search(line)
        if m:
            g = m.groups()
            cur.append(dict(ph=g[0], fr=int(g[1]), sx=float(g[2]), sy=float(g[3]), dist=int(g[4]), along=int(g[5]), lat=int(g[6]),
                            off=float(g[7]), roll=float(g[8]), spd=int(g[9]), E=int(g[10]), F=int(g[11]), A=int(g[12])))
            continue
        if 'Docked at' in line or 'docked' in line.lower() and 'STATUS' in line:
            if cur:
                out.append(cur)
            cur = []
        elif re.search(r'\bfail\w*\b.*\bdock\b|\bdock\b.*\bfail', line, re.I):
            fails += 1
            cur = []
    return out, fails


def measure(d):
    fin = [x for x in d if x['ph'] == 'F']
    in2k = [x for x in d if x['dist'] < 2000 and x['ph'] == 'F']
    bumps = sum(1 for a, b in zip(d, d[1:]) if b['dist'] < 800 and (b['F'] + b['A'] < a['F'] + a['A'] - 2))
    relines = sum(1 for a, b in zip(d, d[1:]) if a['ph'] == 'F' and b['ph'] == 'L')
    return dict(t=(d[-1]['fr'] - d[0]['fr']) / 50, tf=(fin[-1]['fr'] - fin[0]['fr']) / 50 if fin else 0,
                lat2k=max((x['lat'] for x in in2k), default=0), late=d[-1]['lat'], rolle=abs(d[-1]['roll']),
                scr2k=max((max(abs(x['sx']), abs(x['sy'])) for x in in2k if x['dist'] > 300), default=0), bumps=bumps, relines=relines)


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p * len(xs)))] if xs else 0


def main():
    ds, fails = [], 0
    for p in sys.argv[1:]:
        o, f = dockings(p)
        ds += [measure(d) for d in o if len(d) > 10]
        fails += f
    n = len(ds)
    print('%d dockings, %d failures' % (n, fails))
    for k, name in (('t', 'time (s)'), ('tf', 'final (s)'), ('lat2k', 'max off-axis inside 2000 (final)'), ('late', 'off-axis at entry'),
                    ('rolle', 'slot roll at entry (deg)'), ('scr2k', 'slot centre off the sights 300-2000 (deg)')):
        xs = [d[k] for d in ds]
        print('  %-44s median %7.1f  p90 %7.1f  max %7.1f' % (name, statistics.median(xs) if xs else 0, pct(xs, 0.9), max(xs, default=0)))
    print('  bumps %d (in %d dockings), re-line-ups %d' % (sum(d['bumps'] for d in ds), sum(1 for d in ds if d['bumps']), sum(d['relines'] for d in ds)))


if __name__ == '__main__':
    main()

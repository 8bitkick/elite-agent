#!/usr/bin/env python3
"""Career statistics per mode, from run_sim journals (up to the rank of Elite), as a Markdown table.

  career_stats.py MODE=journal.jsonl[,journal.jsonl...] ...

Each figure is the mean over that mode's careers. Light years are summed between consecutive docks in
the same galaxy (4 x sqrt(dx^2 + (dy/2)^2) tenths, the game's distance), so a jump through witchspace
or a hop flown through without docking counts as the straight line between the docks.
"""
import json, math, sys


def career(path):
    c = dict(hours=0, systems=0, galaxies=1, ly=0.0, docks=0, fights=0, kills=0, ws=0, deaths=0, pods=0, cash=0.0)
    last = None
    for line in open(path):
        try:
            d = json.loads(line)
        except ValueError:
            continue
        e, ev = d['e'], d['e'].get('event')
        c['hours'] = d['frame'] / 180000
        if ev == 'rank' and e.get('rank') == 'Elite':
            break
        if ev == 'new_system':
            c['systems'] += 1
            c['galaxies'] = max(c['galaxies'], e.get('galaxy', 1))
        elif ev == 'docked':
            c['docks'] += 1
            c['cash'] = max(c['cash'], e.get('cash', 0))
            if 'x' in e:
                here = (e.get('g'), e['x'], e['y'])
                if last and last[0] == here[0]:
                    c['ly'] += 0.4 * math.hypot(here[1] - last[1], (here[2] - last[2]) / 2)
                last = here
        elif ev == 'fight_end':
            c['fights'] += 1
            c['kills'] += e.get('kills', 0)
        elif ev == 'session' and e.get('kind') == 'witchspace':
            c['ws'] += 1
        elif ev == 'died':
            c['deaths'] += 1
        elif ev == 'escape_pod':
            c['pods'] += 1
    return c


ROWS = [('hours', 'Game hours to Elite', '%.1f'), ('systems', 'Star systems visited', '%.0f'),
        ('galaxies', 'Galaxies', '%.0f'), ('ly', 'Light years flown (dock to dock)', '{:,.0f}'),
        ('docks', 'Docks', '{:,.0f}'), ('fights', 'Fights', '{:,.0f}'), ('kills', 'Kills', '{:,.0f}'),
        ('ws', 'Witchspace sessions (Thargoids)', '%.0f'), ('deaths', 'Deaths (reloaded)', '%.0f'),
        ('pods', 'Escape pods used', '%.0f'), ('cash', 'Most credits held', '{:,.0f} Cr')]


def main():
    modes = []
    for a in sys.argv[1:]:
        name, paths = a.split('=', 1)
        cs = [career(p) for p in paths.split(',')]
        modes.append((name, {k: sum(c[k] for c in cs) / len(cs) for k in cs[0]}, len(cs)))
    print('| | ' + ' | '.join('%s (%d)' % (n, k) for n, _, k in modes) + ' |')
    print('|---|' + '---|' * len(modes))
    for k, label, fmt in ROWS:
        print('| %s | %s |' % (label, ' | '.join((fmt % m[k]) if '%' in fmt else fmt.format(m[k]) for _, m, _ in modes)))


if __name__ == '__main__':
    main()

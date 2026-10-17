#!/usr/bin/env python3
"""The Elite career in numbers, for the poster: logs/sim-journal.jsonl + logs/sim.log -> logs/poster/data.json"""
import json, re, sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from elite import galaxy as G

J = [json.loads(l) for l in open('logs/sim-journal.jsonl')]
LOG = open('logs/sim.log', errors='replace').read().split('\n')
ITEMS = ['Food', 'Textiles', 'Radioactives', 'Slaves', 'Liquor/Wines', 'Luxuries', 'Narcotics', 'Computers',
         'Machinery', 'Alloys', 'Firearms', 'Furs', 'Minerals', 'Gold', 'Platinum', 'Gem-Stones', 'Alien Items']
RANKS = [(0, 'Harmless'), (8, 'Mostly Harmless'), (16, 'Poor'), (32, 'Average'), (64, 'Above Average'),
         (128, 'Competent'), (512, 'Dangerous'), (2560, 'Deadly'), (6400, 'Elite')]

# game time: hunting sessions are measured (frames); a hop (launch, jump, fly in, dock) ~3.5 game minutes
HOP_MIN = 3.5
t = 0.0
galaxy = 0
series, docks, ranks_at, equip, events = [], [], {}, [], []
last_tally = 0.0
for e in J:
    ev = e['event']
    if ev == 'farmed':
        t += e['minutes']           # (a session's minutes run from launch to docking again)
    elif ev == 'depart':
        t += HOP_MIN                # a trading or travelling hop
    if ev == 'galaxy':
        galaxy = e['galaxy']
        events.append(dict(h=t / 60, kind='galaxy', label=f'Galaxy {galaxy + 1}'))
    if 'tally' in e and ev in ('save', 'docked', 'farmed', 'died', 'escape_pod'):
        tally = e['tally']
        if ev == 'save':
            series.append(dict(h=round(t / 60, 3), tally=round(tally, 2), cash=e.get('cash')))
        for thr, name in RANKS:
            if last_tally < thr <= tally and name not in ranks_at:
                ranks_at[name] = round(t / 60, 2)
        last_tally = max(last_tally, tally)
    if ev == 'docked' and e.get('at') and not e['at'].startswith('('):
        sysm = G.find_system(galaxy, e['at'])
        if sysm:
            docks.append(dict(g=galaxy, x=sysm.x, y=sysm.y, name=sysm.name, gov=sysm.government,
                              econ=sysm.economy, h=round(t / 60, 2)))
    if ev == 'equip':
        equip.append(dict(h=round(t / 60, 2), item=e['item']))
    if ev in ('died', 'escape_pod', 'bomb'):
        events.append(dict(h=round(t / 60, 2), kind=ev))
ranks_at.setdefault('Harmless', 0.0)

# trading: tonnes bought per commodity, and money in and out
bought = [0] * 17
spent = income = 0.0
for line in LOG:
    m = re.search(r'docked buy (\d+) (.+?) at ([\d.]+)', line)
    if m and m.group(2) in ITEMS:
        bought[ITEMS.index(m.group(2))] += int(m.group(1))
    m = re.search(r'bought \{.*\}; cash ([\d.]+) -> ([\d.]+)', line)
    if m:
        spent += float(m.group(1)) - float(m.group(2))
    m = re.search(r'sold: \+([\d.]+) Cr', line)
    if m:
        income += float(m.group(1))

# fights: who we engaged (per targeting), how fights went
engaged = {}
for line in LOG:
    m = re.search(r'combat target (.+?) \(slot', line)
    if m:
        engaged[m.group(1)] = engaged.get(m.group(1), 0) + 1

# systems visited (unique per galaxy) and every system of those galaxies for the map
visited = {}
for d in docks:
    k = (d['g'], d['name'])
    visited[k] = visited.get(k, 0) + 1
galaxies = sorted({d['g'] for d in docks})
maps = {g: [dict(x=s.x, y=s.y, name=s.name, gov=s.government) for s in G.galaxy_systems(g)] for g in galaxies}

out = dict(
    series=series, ranks_at=ranks_at, ranks=RANKS, equip=equip, events=events,
    docks=docks, visited=[dict(g=g, name=n, n=c) for (g, n), c in visited.items()], maps=maps,
    bought=dict(zip(ITEMS, bought)), trade=dict(spent=round(spent), income=round(income)),
    engaged=dict(sorted(engaged.items(), key=lambda kv: -kv[1])),
    totals=dict(game_hours=round(t / 60, 1), hunting_hours=round(sum(e['minutes'] for e in J if e['event'] == 'farmed') / 60, 1),
                sessions=sum(e['event'] == 'farmed' for e in J), docks=sum(e['event'] == 'docked' for e in J),
                deaths=sum(e['event'] == 'died' for e in J), pods=sum(e['event'] == 'escape_pod' for e in J),
                systems=len(visited), final_tally=round(max(s['tally'] for s in series), 2),
                peak_cash=max(s['cash'] or 0 for s in series)),
)
json.dump(out, open('logs/poster/data.json', 'w'))
print(json.dumps(out['totals']), '\nranks', out['ranks_at'], '\ntrade', out['trade'], '\nengaged', list(out['engaged'].items())[:12],
      '\nbought', {k: v for k, v in out['bought'].items() if v}, '\nsystems', len(visited), 'docks', len(docks))

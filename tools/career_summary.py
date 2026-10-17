"""One line per career: time to each rank, deaths, pods, stranded reloads, systems and galaxies seen,
missions, and how the time went (witchspace, hunting, trading).
  career_summary.py logs/c/<run> [...]"""
import json, os, sys, collections

FPS = 50 * 60        # journal frames to game minutes


def summary(d):
    ev = []
    for l in open(os.path.join(d, 'journal.jsonl')):
        try:
            x = json.loads(l)
        except ValueError:
            continue
        e = x['e']; e['fr'] = x['frame']; ev.append(e)
    c = collections.Counter(e['event'] for e in ev)
    end = ev[-1]['fr'] / FPS if ev else 0
    ranks = {e['rank']: e['fr'] / FPS / 60 for e in ev if e['event'] == 'rank'}
    seen = {(e['galaxy'], e['name']) for e in ev if e['event'] == 'new_system'}
    gal = sorted({g for g, _ in seen})
    sess = collections.defaultdict(lambda: [0, 0.0, 0.0])
    for e in ev:
        if e['event'] == 'session':
            s = sess[e['kind']]; s[0] += 1; s[1] += e['minutes']; s[2] += e['points']
    stuck = collections.Counter(e['why'] for e in ev if e['event'] == 'stuck')
    log = open(os.path.join(d, 'run.log')).read()
    m1 = 'Constrictor destroyed' in log
    m2 = 'Docked at Birera' in log
    elite = ranks.get('Elite')
    print(f"{os.path.basename(d.rstrip('/')):10s} Elite {'%.1f h' % elite if elite else 'not yet (%.1f h)' % (end / 60)}"
          f" | Deadly {ranks.get('Deadly', 0):.1f} h | deaths {c['died']} pods {c['escape_pod']} stuck {sum(stuck.values())}"
          f" | systems {len(seen)} galaxies {len(gal)} | missions {'C' if m1 else '-'}{'T' if m2 else '-'}"
          f" | witch {sess['witchspace'][0]} ({sess['witchspace'][1] / 60:.1f} h, {sess['witchspace'][2]:.0f} pts)"
          f" hunt {sess['hunt'][0]} ({sess['hunt'][1] / 60:.1f} h, {sess['hunt'][2]:.0f} pts) relocations {c['relocate']}")


for d in sys.argv[1:]:
    summary(d)

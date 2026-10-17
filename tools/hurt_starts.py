"""tools/hurt_starts.py run.log ...: Fight starts in run_sim logs (FSTART lines): how many start hurt (energy < 200 or front shield < 100),
and what the career was doing (the latest activity before the fight)."""
import re, sys, collections

KEYS = (('Hunting bounties', 'hunt'), ('Into witchspace', 'witch'), ('leaving witchspace', 'witch exit'),
        ('bailing out', 'witch exit'), ('Chaining', 'chain'), ('Docked at', 'trade'), ('Galactic jump', 'galactic'))
tot = collections.Counter(); hurt = collections.Counter(); deaths = 0; minutes = 0
for path in sys.argv[1:]:
    act = 'start'
    for line in open(path, errors='replace'):
        m = re.match(r'\s*([\d.]+) (.*)', line)
        if not m:
            continue
        t, s = float(m.group(1)), m.group(2)
        minutes = max(minutes, t)
        if s.startswith('FSTART'):
            f = dict(zip(s.split()[1::2], map(int, s.split()[2::2])))
            a = 'witchspace' if f.get('ws') else act
            tot[a] += 1
            if f['E'] < 200 or f['F'] < 100:
                hurt[a] += 1
            continue
        if 'died: back' in s:
            deaths += 1
        for k, lab in KEYS:
            if k in s:
                act = lab
print('fights %d, hurt %d (%.0f%%), deaths %d' % (sum(tot.values()), sum(hurt.values()), 100 * sum(hurt.values()) / max(1, sum(tot.values())), deaths))
for a, n in tot.most_common():
    print('  %-12s fights %4d  hurt %4d' % (a, n, hurt[a]))

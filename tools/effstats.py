#!/usr/bin/env python3
"""Efficient-mode fight measures from suite runs (the lab's FX lines, kept in the run's json by suite.py).

  effstats.py <run.json>              per scenario: time to clear, time per kill, first kill, damage, deaths, pts/min,
                                      and where the engaged time goes
  effstats.py <a.json> <b.json>       the same for B, with paired differences against A (95% bootstrap intervals)

Per fight (lab frames, 50 a second, from the fight's start):
  clear     first frame with no hostile ship left about (any range); fights that never clear are left out of its mean
  engaged   frames in engage(): attacking (slewing > 10 deg off, tracking within 10, in the sights, no target),
            breaking off, turning a shield to a missile, dodging a ram, evading
  per kill  engaged seconds over kills (the scenario's sums)
  first     seconds to the first kill
  far       engaged frames with the target beyond 6000 (closing in)
  wait      frames with hostiles about but not in engage() (beyond engage range, regenerating, choosing the way out)
"""
import json, sys, random, collections

CATS = ['slew', 'track', 'in', 'notgt', 'break', 'msl', 'ram', 'evade', 'count', 'wait', 'idle']


def load(p):
    d = json.load(open(p))
    return d, {k: v for k, v in d['fights'].items() if v.get('fx')}


def ci(xs, n=1000):
    if not xs:
        return 0, 0, 0
    random.seed(1)
    m = sum(xs) / len(xs)
    bs = sorted(sum(random.choice(xs) for _ in xs) / len(xs) for _ in range(n))
    return m, bs[int(0.025 * n)], bs[int(0.975 * n) - 1]


def agg(F, keys):
    a = collections.Counter()
    clr = []
    first = []
    for k in keys:
        f = F[k]; x = f['fx']
        a['n'] += 1; a['pts'] += f['pts']; a['min'] += f['min']; a['died'] += f['died']; a['taken'] += f['taken']
        a['kills'] += len(x['kt'])
        for i, c in enumerate(CATS):
            a[c] += x['cat'][i]
        a['far'] += x['far']
        a['eng'] += sum(x['cat'][:8])
        if x['clear'] >= 0:
            clr.append(x['clear'] / 50)
        if x['kt']:
            first.append(x['kt'][0] / 50)
    return a, clr, first


def show(name, F, keys):
    a, clr, first = agg(F, keys)
    eng = max(a['eng'], 1)
    print('%-18s n %4d deaths %3d pts/min %6.2f | clear %5.1f s (%3.0f%% cleared) | per kill %5.1f s | first kill %5.1f s | taken %5.0f | '
          % (name, a['n'], a['died'], a['pts'] / max(a['min'], 1e-9), sum(clr) / max(len(clr), 1), 100 * len(clr) / max(a['n'], 1),
             a['eng'] / 50 / max(a['kills'], 1), sum(first) / max(len(first), 1), a['taken'] / max(a['n'], 1))
          + ' '.join('%s %.0f%%' % (c, 100 * a[c] / eng) for c in CATS[:8] if a[c]) + ' | far %.0f%% wait %.1f s count %.1f s'
          % (100 * a['far'] / eng, a['wait'] / 50 / max(a['n'], 1), a['count'] / 50 / max(a['n'], 1)))


def paired(name, A, B, keys):
    both = [k for k in keys if A[k]['fx']['clear'] >= 0 and B[k]['fx']['clear'] >= 0]
    d = [(B[k]['fx']['clear'] - A[k]['fx']['clear']) / 50 for k in both]
    m, lo, hi = ci(d)
    base = sum(A[k]['fx']['clear'] for k in both) / 50 / max(len(both), 1)
    dt = [B[k]['taken'] - A[k]['taken'] for k in keys]
    tm, tlo, thi = ci(dt)
    ea, eb = agg(A, keys)[0], agg(B, keys)[0]
    pk_a, pk_b = ea['eng'] / max(ea['kills'], 1), eb['eng'] / max(eb['kills'], 1)
    print('%-18s clear %+5.2f s [%+.2f, %+.2f] (%+.1f%%, n %d) | per kill %+.1f%% | kills %d -> %d | taken %+.1f [%+.1f, %+.1f] | deaths %d -> %d'
          % (name, m, lo, hi, 100 * m / max(base, 1e-9), len(both), 100 * (pk_b / max(pk_a, 1e-9) - 1), ea['kills'], eb['kills'], tm, tlo, thi, ea['died'], eb['died']))


def main():
    runs = [load(p) for p in sys.argv[1:3]]
    if len(runs) == 1:
        d, F = runs[0]
        by = collections.defaultdict(list)
        for k, f in F.items():
            by[f['bucket']].append(k)
        for b in sorted(by):
            show(b, F, by[b])
        show('ALL', F, list(F))
        return
    (da, A), (db, B) = runs
    common = [k for k in A if k in B]
    by = collections.defaultdict(list)
    for k in common:
        by[A[k]['bucket']].append(k)
    print('A = %s %s\nB = %s %s' % (da['name'], ' '.join(da['kv']), db['name'], ' '.join(db['kv'])))
    for b in sorted(by):
        show(b + ' A', A, by[b])
        show(b + ' B', B, by[b])
    show('ALL A', A, common)
    show('ALL B', B, common)
    print()
    for b in sorted(by):
        paired(b, A, B, by[b])
    paired('ALL', A, B, common)


if __name__ == '__main__':
    main()

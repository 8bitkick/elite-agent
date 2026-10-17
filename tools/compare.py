#!/usr/bin/env python3
"""Paired comparison of two suite runs on the same fights: mean difference per fight with a 95% interval
(bootstrap), overall and per scenario; deaths compared as counts with the fights where only one died.
  compare.py ~/work/runs/<set>_<a>.json ~/work/runs/<set>_<b>.json"""
import json, sys, random, collections
A, B = (json.load(open(p)) for p in sys.argv[1:3])
common = [k for k in A['fights'] if k in B['fights']]
random.seed(1)
def ci(d, n=2000):
    if not d: return (0, 0, 0)
    m = sum(d) / len(d)
    bs = sorted(sum(random.choice(d) for _ in d) / len(d) for _ in range(n))
    return m, bs[int(0.025 * n)], bs[int(0.975 * n)]
def line(name, keys):
    dp = [B['fights'][k]['pts'] - A['fights'][k]['pts'] for k in keys]
    m, lo, hi = ci(dp)
    da = sum(A['fights'][k]['died'] for k in keys); db = sum(B['fights'][k]['died'] for k in keys)
    only_a = sum(1 for k in keys if A['fights'][k]['died'] and not B['fights'][k]['died'])
    only_b = sum(1 for k in keys if B['fights'][k]['died'] and not A['fights'][k]['died'])
    sig = 'BETTER' if lo > 0 else 'WORSE' if hi < 0 else 'no clear difference'
    tot = sum(A['fights'][k]['pts'] for k in keys)
    # the rate (what time to Elite depends on): points over minutes, paired bootstrap of the ratio
    def rate(F, ks): return sum(F[k]['pts'] for k in ks) / max(sum(F[k].get('min', 0) for k in ks), 1e-9)
    ra, rb = rate(A['fights'], keys), rate(B['fights'], keys)
    rs = sorted((lambda ks: rate(B['fights'], ks) / max(rate(A['fights'], ks), 1e-9))([random.choice(keys) for _ in keys]) for _ in range(500)) if all('min' in A['fights'][k] for k in keys) else []
    rtxt = ' | pts/min %.2f -> %.2f (%+.1f%% [%+.1f, %+.1f])' % (ra, rb, 100 * (rb / max(ra, 1e-9) - 1), 100 * (rs[12] - 1), 100 * (rs[487] - 1)) if rs else ''
    print('%-20s n %4d  pts %+7.3f/fight [%+.3f, %+.3f] (%+5.1f%%)  deaths %d -> %d (only A %d, only B %d)  %s' % (
        name, len(keys), m, lo, hi, 100 * m * len(keys) / max(tot, 1e-9), da, db, only_a, only_b, sig) + rtxt)
print('A = %s %s\nB = %s %s' % (A['name'], ' '.join(A['kv']), B['name'], ' '.join(B['kv'])))
by = collections.defaultdict(list)
for k in common: by[A['fights'][k]['bucket']].append(k)
for b in sorted(by): line(b, by[b])
line('ALL', common)

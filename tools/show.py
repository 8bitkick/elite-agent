#!/usr/bin/env python3
"""The show score (curious mode's fights; docs/fighting-showcase.md), from tools/showrun.sh output.

  show.py runs/A.txt [runs/B.txt ...]      a summary per run (score, its parts, deaths, length, techniques)
  show.py -c runs/A.txt runs/B.txt         B against A, fight by fight (paired, bootstrap 95% CI)
  show.py -f runs/A.txt                    one line per fight, best first

A fight's score (0-100) = 100 x completion x the weighted mean of its parts; our death = 0 (and counts
against the death budget: up to ~5% of showcase fights).
"""
import sys, random, collections, math

# (score 2, 2026-10-18 round 2: a static screen (seen but nothing moving for 2 s+) is as dull as an empty one, so the
# air part counts both; motion is its own part (the share of seen frames with something moving: a ship drifting
# across or growing by 2 px a game loop, our laser, an explosion), rewarding motion over a ship held dead centre;
# weights moved to make room: ckill, variety 0.15 -> 0.10, sup, pace 0.10 -> 0.05, screen 0.20 -> 0.15, air 0.10 -> 0.15.
# SCORE=1 in the environment gives the first score)
W = dict(screen=0.15, close=0.15, air=0.15, ckill=0.10, motion=0.10, passes=0.05, variety=0.10, sup=0.05, pace=0.05, novel=0.10)
if __import__('os').environ.get('SCORE') == '1':
    W = dict(screen=0.20, close=0.15, air=0.10, ckill=0.15, passes=0.05, variety=0.15, sup=0.10, pace=0.10, novel=0.10)
# (novel: only in fights with something new: a rare type (Thargoid, Thargon, Constrictor, Cougar) or a type the
# career hadn't met before (the corpus's career order, NOVEL_SCN); the share of them seen close up (CLOSEUP px)
# at least once. Fights without one leave it out and the other weights share its place)
RARE = {29, 30, 31, 32}
CLOSEUP = 32
NOVEL_SCN = __import__('os').path.expanduser('~/work/show/all_eq.scn')
_first = None


def first_meetings():
    # each career's fights in order: the types met at each fight's start that the career hadn't met before
    global _first
    if _first is None:
        _first = {}
        try:
            rows = sorted(l.split() for l in open(NOVEL_SCN))
        except OSError:
            rows = []
        seen = collections.defaultdict(set)
        for v in rows:
            run = v[1].split('/')[-3]
            d = dict(zip(v[2::2], v[3::2]))
            ts = {int(x) for x in d.get('types', '').split(',') if x}
            _first[v[1]] = {t for t in ts if t > 8} - seen[run]
            seen[run] |= ts
    return _first
SHOWY = ('rear', 'missile', 'ecm', 'bomb', 'shootdown', 'pass', 'loop', 'side')      # beyond the front laser
# ('rear': a rear-laser kill or a fight watched in the rear view, one technique either way: occasional rear
# work is good, a rear-only fight no better than a front one)
FLICK = 8           # view changes a minute above this cost variety in proportion (constant flicking disorients)


def load(path):
    fights, scn, res = [], {}, {}
    cols, cdeaths, cur = collections.Counter(), set(), None
    for line in open(path):
        v = line.split()
        if not v:
            continue
        # (collisions, round 5: the lab's COLLOG lines, a sudden loss and the nearest ship; a ram costs 128 + its
        # energy / 2, so a loss of 100+ with a ship inside 500. Collision deaths from DCAUSE)
        if v[0] == 'COL':
            d = dict(zip(v[1::2], v[2::2]))
            if cur and int(d.get('drop', 0)) >= 100 and float(d.get('dist', 1e9)) < 500: cols[cur] += 1
            continue
        if v[0] == 'DCAUSE' and len(v) > 2 and v[2].startswith('collision'):
            cdeaths.add(v[1])
            continue
        if v[0] == 'SCN':
            cur = v[1]
            scn[v[1]] = {v[i]: v[i + 1] for i in range(2, len(v) - 1, 2)}
        elif v[0] == 'SHOW':
            d = {v[i]: v[i + 1] for i in range(2, len(v) - 1, 2)}
            d['path'] = v[1]
            fights.append(d)
        elif v[0].startswith('/') and len(v) >= 17:
            res[v[0]] = v
    out = []
    for d in fights:
        r = res.get(d['path'])
        f = dict(path=d['path'], run=d['path'].split('/')[-3], frames=int(d['frames']), scr=float(d['scr']), close=float(d['close']),
                 seen=float(d['seen']), air=float(d['air']), kills=int(d['kills']), ckills=int(d['ckills']), passes=int(d['passes']),
                 start_n=max(1, int(d['start_n'])), left=int(d['left']), dmg=int(d['dmg']), lowest=int(d['lowest']), died=int(d['died']),
                 pts=float(r[1]) if r else 0.0, pod=int(r[6]) if r else 0, air0=float(d.get('air0', 0)))
        f['cols'] = cols.get(d['path'], 0); f['cdied'] = d['path'] in cdeaths
        f['tech'] = {} if d['tech'] == '-' else {k: int(n) for k, n in (x.split(':') for x in d['tech'].split(','))}
        if f['tech'].pop('rearview', 0):
            f['tech']['rear'] = f['tech'].get('rear', 0) or 1
        f['views'] = [int(x) for x in d.get('views', '0,0,0,0').split(',')]
        f['switches'] = int(d.get('switches', 0))
        f['tmax'] = {} if d.get('tmax', '-') == '-' else {int(k): float(x) for k, x in (y.split(':') for y in d['tmax'].split(','))}
        s_ = scn.get(d['path'], {})
        ts = {int(x) for x in s_.get('types', '').split(',') if x}
        f['novel'] = (ts & RARE) | first_meetings().get(d['path'], set())
        f['airsplit'] = [int(x) for x in d.get('airsplit', '0,0,0').split(',')]
        import re as _re
        f['ckills8'] = sum(1 for x in d.get('kd', '-').split(',') if _re.match(r'^\d+[frmbe](\d+)[SBMO]$', x) and int(_re.match(r'^\d+[frmbe](\d+)[SBMO]$', x).group(1)) >= 8)
        f['ckills20'] = sum(1 for x in d.get('kd', '-').split(',') if _re.match(r'^\d+[frmbe](\d+)[SBMO]$', x) and int(_re.match(r'^\d+[frmbe](\d+)[SBMO]$', x).group(1)) >= 20)
        f['strafe'] = d.get('strafe', '0,0')
        f['thg'] = [int(x) for x in d.get('thg', '0,0,0').split(',')]
        f['ev'] = [int(x) for x in d.get('ev', '0,0').split(',')]
        f['fought'] = [int(x) for x in d.get('fought', '0,0').split(',')]
        f['sfh'] = [int(x) for x in d.get('sfh', ','.join(['0'] * 16)).split(',')]
        f['s20'] = int(d.get('s20', 0)); f['s40'] = int(d.get('s40', 0)); f['sint'] = float(d.get('sint', 0)); f['bigv'] = float(d.get('bigv', 0)); f['spdrev'] = int(d.get('spdrev', 0)); f['spdsum'] = int(d.get('spdsum', 0)); f['spdfast'] = int(d.get('spdfast', 0)); f['stkrev'] = int(d.get('stkrev', 0))
        f['lively'] = int(d.get('lively', 0)); f['static'] = int(d.get('static', 0)); f['staticair'] = int(d.get('staticair', 0))
        f['showfr'] = int(d.get('showfr', 0)); f['modesw'] = int(d.get('modesw', 0)); f['mode'] = int(d.get('mode', -1)); f['spent'] = int(d.get('spent', 0))
        s = scn.get(d['path'], {})
        n = int(s.get('n', 1))
        f['scn'] = 'witchspace' if s.get('ws') == '1' else '1' if n <= 1 else '2-3' if n <= 3 else '4+'
        out.append(f)
    # repetition: against the career's previous fight in the list (fights are in career order)
    prev = {}
    for f in out:
        t = set(k for k in f['tech'] if k in SHOWY) | ({'front'} if f['tech'].get('front') else set())
        p = prev.get(f['run'])
        f['same'] = (len(t & p) / len(t | p) if (t | p) else 1.0) if p is not None else None
        prev[f['run']] = t
    for f in out:
        score(f)
    return out


def pace(t, n):
    n = min(n, 6)
    lo, hi = 10 + 5 * n, 30 + 15 * n
    if t < lo:
        return 0.5 + 0.5 * t / lo
    if t <= hi:
        return 1.0
    return max(0.0, 1 - (t - hi) / (2 * hi))


def flick(f):
    spm = f['switches'] / max(f['frames'] / 3000, 1 / 6)
    return min(1.0, FLICK / spm) if spm else 1.0


def score(f):
    k = sum(1 for x in SHOWY if f['tech'].get(x))
    own = min(1.0, k / 3)
    nov = 1 - f['same'] if f['same'] is not None else 0.5
    p = dict(screen=f['scr'], close=f['close'],
             air=1 - f['air'] - (f['staticair'] / f['frames'] if 'motion' in W and f['frames'] else 0),
             motion=f['lively'] / (f['lively'] + f['static']) if f['lively'] + f['static'] else 0.0, ckill=f['ckills'] / f['kills'] if f['kills'] else 0.0,
             passes=min(1.0, f['passes'] / f['start_n']), variety=(0.5 * own + 0.5 * nov) * flick(f), sup=1 - min(1.0, f['dmg'] / 384),
             pace=pace(f['frames'] / 50, f['start_n']))
    if f['novel']:
        p['novel'] = sum(1 for t in f['novel'] if f['tmax'].get(t, 0) >= CLOSEUP) / len(f['novel'])
    p = {k: v for k, v in p.items() if k in W}
    f['parts'] = p
    f['complete'] = min(1.0, f['kills'] / f['start_n'])
    f['score'] = 0.0 if f['died'] else 100 * f['complete'] * sum(W[x] * p[x] for x in p) / sum(W[x] for x in p)
    return f['score']


def summary(name, F):
    n = len(F)
    m = lambda xs: sum(xs) / max(1, len(xs))
    print('== %s: %d fights' % (name, n))
    # the primary measure (round 3): how big and how long. Per fight: seconds with an enemy drawn 20 px or more,
    # 40 px or more; and the size-time integral (the largest enemy's drawn px, capped at 100, summed over frames) per
    # minute of fight, in px-seconds
    mins = max(1e-9, sum(f['frames'] for f in F) / 3000)
    print('  COLLISIONS: %d in %d fights (%.1f per 1000), collision deaths %d (%.1f per 1000); kills drawn 20 px+ %.0f%%, 8 px+ %.0f%%' % (
        sum(f['cols'] for f in F), n, 1000 * sum(f['cols'] for f in F) / max(1, n), sum(f['cdied'] for f in F), 1000 * sum(f['cdied'] for f in F) / max(1, n),
        100 * sum(f['ckills20'] for f in F) / max(1, sum(f['kills'] for f in F)), 100 * sum(f['ckills8'] for f in F) / max(1, sum(f['kills'] for f in F))))
    print('  FOUGHT: %.2f s a fight minute (a big target, 24 px+, %.1f s a minute; centred or under fire %.0f%% of it)' % (
        sum(f['fought'][1] for f in F) / 50 / mins, sum(f['fought'][0] for f in F) / 50 / mins, 100 * sum(f['fought'][1] for f in F) / max(1, sum(f['fought'][0] for f in F))))
    h = [sum(f['sfh'][i] for f in F) for i in range(16)]
    if sum(h):
        lab = ['hit'] + ['%.1f-%.1f' % (i * 0.2, i * 0.2 + 0.2) for i in range(14)] + ['2.8+']
        print('  NEAR MISSES (gap from the sights to its outline, x its drawn radius): %d shots; ' % sum(h) + ', '.join('%s %.0f%%' % (lab[i], 100 * h[i] / sum(h)) for i in range(16) if h[i]))
    if any(f['ev'][0] for f in F):
        print('  EVASIONS: %.2f a fight (in %.0f%% of fights), %.1f s each' % (sum(f['ev'][0] for f in F) / max(1, n),
              100 * sum(1 for f in F if f['ev'][0]) / max(1, n), sum(f['ev'][1] for f in F) / 50 / max(1, sum(f['ev'][0] for f in F))))
    tf = [f for f in F if f['thg'][0] or f['thg'][2]]
    if tf:
        print('  THARGONS: in %d fights, %.2f killed a fight, the last at %.1f s (median), a Thargoid drawn 20 px+ %.1f s a fight' % (
            len(tf), sum(f['thg'][0] for f in tf) / len(tf), sorted(f['thg'][1] for f in tf)[len(tf) // 2] / 50, sum(f['thg'][2] for f in tf) / 50 / len(tf)))
    sf = [tuple(int(x) for x in str(f.get('strafe', '0,0')).split(',')) for f in F]
    if any(b for b, s in sf):
        print('  STRAFE: bursts in %.0f%% of fights, %.2f a fight, %.1f frames fired a burst' % (
            100 * sum(1 for b, s in sf if b) / max(1, n), sum(b for b, s in sf) / max(1, n), sum(s for b, s in sf) / max(1, sum(b for b, s in sf))))
    print('  CAPPED: %d fights of %d ran 239 s or more' % (sum(1 for f in F if f['frames'] >= 239 * 50), n))
    # (round 8, how big for how long: showmeter.c's big_value, 0 under 16 px, 1 at 64-160 px, 0 at 256)
    print('  BIG SHIPS: %.1f s a fight minute (a ship drawn 64-160 px a whole second, less below or above), %.1f s a fight' % (
        sum(f['bigv'] for f in F) / 50 / mins, sum(f['bigv'] for f in F) / 50 / max(1, n)))
    fr = max(1, sum(f['frames'] for f in F))
    print('  PULSING: our speed changed direction %.1f times a fight minute | speed %.1f mean, 30+ %.0f%% of fight time | stick reversals %.1f a minute (judder)' % (
        sum(f['spdrev'] for f in F) / mins, sum(f['spdsum'] for f in F) / fr, 100 * sum(f['spdfast'] for f in F) / fr, sum(f['stkrev'] for f in F) / mins))
    print('  SIZE x TIME: >=20 px %.1f s a fight (%.1f s a minute), >=40 px %.1f s a fight (%.1f s a minute), size-time %.0f px.s a minute' % (
        sum(f['s20'] for f in F) / 50 / max(1, n), sum(f['s20'] for f in F) / 50 / mins, sum(f['s40'] for f in F) / 50 / max(1, n),
        sum(f['s40'] for f in F) / 50 / mins, sum(f['sint'] for f in F) / 50 / mins))
    print('  SHOW %.1f | deaths %d (%.1f per 1000) pods %d | complete %.2f | fight %.1f s | kills %.2f, pts %.2f | dmg %.0f, lowest E %.0f' % (
        m([f['score'] for f in F]), sum(f['died'] for f in F), 1000 * sum(f['died'] for f in F) / max(1, n), sum(f['pod'] for f in F),
        m([f['complete'] for f in F]), m([f['frames'] / 50 for f in F]), m([f['kills'] for f in F]), m([f['pts'] for f in F]), m([f['dmg'] for f in F]), m([f['lowest'] for f in F])))
    print('  parts: ' + '  '.join('%s %.3f' % (x, m([f['parts'][x] for f in F if x in f['parts']])) for x in W) + ' (novel: %d fights)' % sum('novel' in f['parts'] for f in F))
    print('  raw: seen %.2f air %.2f (before the first sight %.2f) close kills %.2f passes %.2f/fight, same-as-last %.2f' % (
        m([f['seen'] for f in F]), m([f['air'] for f in F]), m([f['air0'] for f in F]), sum(f['ckills'] for f in F) / max(1, sum(f['kills'] for f in F)),
        m([f['passes'] for f in F]), m([f['same'] for f in F if f['same'] is not None])))
    L = sorted(f['frames'] / 50 for f in F)
    q = lambda p_: L[min(len(L) - 1, int(p_ * len(L)))] if L else 0
    dm = collections.Counter({1: 'show', 0: 'efficient', -1: 'off'}[f['mode']] for f in F if f['died'])
    print('  mode: in the show %.0f%% of fight time, %.0f%% of fights ever, show/efficient changes %.2f a fight, budget spent in %.0f%% | deaths by mode then: %s' % (
        100 * sum(f['showfr'] for f in F) / max(1, sum(f['frames'] for f in F)), 100 * m([f['showfr'] > 0 for f in F]), m([f['modesw'] for f in F]),
        100 * m([f['spent'] for f in F]), dict(dm) or '-'))
    a_ = [sum(f['airsplit'][i] for f in F) for i in range(3)]; fr_ = max(1, sum(f['frames'] for f in F))
    print('  nothing seen (any gap): a dot in the picture %.0f%%, near (<4000) but out of the picture %.0f%%, all far %.0f%% of fight frames' % tuple(100 * x / fr_ for x in a_))
    print('  static (seen, nothing moving): %.0f%% of fight frames, in runs over 2 s %.0f%%; motion %.2f of seen frames' % (
        100 * sum(f['static'] for f in F) / fr_, 100 * sum(f['staticair'] for f in F) / fr_,
        sum(f['lively'] for f in F) / max(1, sum(f['lively'] + f['static'] for f in F))))
    print('  fight length: median %.1f s, p90 %.1f, max %.1f' % (q(0.5), q(0.9), L[-1] if L else 0))
    vs = [sum(f['views'][i] for f in F) for i in range(4)]
    print('  views (frames with an enemy seen): front %.0f%%, rear %.0f%%, left %.0f%%, right %.0f%% | switches %.1f a fight, flick factor %.2f' % (
        tuple(100 * x / max(1, sum(vs)) for x in vs) + (m([f['switches'] for f in F]), m([flick(f) for f in F]))))
    tc = collections.Counter(k for f in F for k in f['tech'])
    print('  techniques (share of fights): ' + ', '.join('%s %.0f%%' % (k, 100 * v / n) for k, v in tc.most_common()))
    by = collections.defaultdict(list)
    for f in F:
        by[f['scn']].append(f)
    for k in sorted(by):
        G = by[k]
        print('  %-10s n %4d  SHOW %5.1f  deaths %2d  fight %5.1f s  complete %.2f  seen %.2f  close kills %.2f' % (
            k, len(G), m([f['score'] for f in G]), sum(f['died'] for f in G), m([f['frames'] / 50 for f in G]), m([f['complete'] for f in G]),
            m([f['seen'] for f in G]), sum(f['ckills'] for f in G) / max(1, sum(f['kills'] for f in G))))


def compare(A, B):
    a = {f['path']: f for f in A}
    pairs = [(a[f['path']], f) for f in B if f['path'] in a]
    d = [y['score'] - x['score'] for x, y in pairs]
    random.seed(1)
    bs = sorted(sum(random.choice(d) for _ in d) / len(d) for _ in range(1000))
    print('paired %d: SHOW %+.2f [%+.2f, %+.2f] | deaths %d -> %d (only A %d, only B %d)' % (
        len(d), sum(d) / len(d), bs[25], bs[975], sum(x['died'] for x, _ in pairs), sum(y['died'] for _, y in pairs),
        sum(1 for x, y in pairs if x['died'] and not y['died']), sum(1 for x, y in pairs if y['died'] and not x['died'])))


if __name__ == '__main__':
    args = sys.argv[1:]
    if args[0] == '-c':
        A, B = load(args[1]), load(args[2])
        summary(args[1], A); summary(args[2], B); compare(A, B)
    elif args[0] == '-f':
        for f in sorted(load(args[1]), key=lambda f: -f['score']):
            print('%5.1f %s %s kills %d/%d %.0fs dmg %d %s' % (f['score'], f['path'], f['scn'], f['kills'], f['start_n'], f['frames'] / 50, f['dmg'],
                                                       ','.join('%s:%d' % kv for kv in f['tech'].items())))
    else:
        for p in args:
            summary(p, load(p))

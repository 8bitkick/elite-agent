#!/usr/bin/env python3
"""The test suite: fights and session replays from recorded careers, in parallel, grouped by scenario.

  suite.py build                      make the sets from ~/work/corpus: smoke (a few per scenario + every
                                      failure), full (everything)
  suite.py run <set> <name> [VAR=v ...]   run a set with a configuration (env for lab/run_sim); prints a
                                      per-scenario table, appends a summary to ~/work/suite_results.txt

Fights: cplayer/lab on each fight's starting snapshot (its own window), scored on points, deaths, damage,
kills and the aim measures. Sessions: cplayer/run_sim from each launch snapshot until the first dock,
death or stuck (STOPDOCK), up to 30 game minutes.
"""
import json, os, random, re, subprocess, sys, collections
from concurrent.futures import ThreadPoolExecutor

HOME = os.path.expanduser('~')
AGENT = os.environ.get('SUITE_AGENT', os.path.join(HOME, 'work/elite-agent'))
CORPUS = os.path.join(HOME, 'work/corpus')
SETS = os.path.join(HOME, 'work/sets')
NPROC = int(os.environ.get('NPROC', '6'))

SHIPS = {3: 'pod', 5: 'canister', 17: 'Sidewinder', 18: 'Mamba', 19: 'Krait', 20: 'Adder', 21: 'Gecko', 22: 'Cobra I',
         23: 'Worm', 24: 'Cobra III', 25: 'Asp', 26: 'Python', 27: 'Fer-de-lance', 28: 'Moray', 29: 'Thargoid', 30: 'Thargon',
         16: 'Viper', 11: 'Cobra III(t)', 12: 'Python(t)', 13: 'Boa', 14: 'Anaconda'}
BIG = {'Python', 'Boa', 'Anaconda', 'Fer-de-lance', 'Asp', 'Cobra III', 'Python(t)'}


def fights_and_sessions():
    fights, sessions = [], []
    for run in sorted(os.listdir(CORPUS)):
        for kind, out in (('fights', fights), ('sessions', sessions)):
            idx = os.path.join(CORPUS, run, kind, 'index.jsonl')
            if not os.path.exists(idx):
                continue
            for line in open(idx):
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                e['path'] = os.path.join(CORPUS, run, kind, '%05d.fight' % e['id'])
                e['run'] = run
                if os.path.exists(e['path']):
                    out.append(e)
    return fights, sessions


def fight_bucket(e):
    names = [n.split(' (')[0] for n in e.get('enemies') or []]
    if e.get('witchspace'):
        return 'witchspace'
    n = len(names)
    big = any(x in BIG for x in names)
    return 'pirates %s %s' % ('1' if n <= 1 else '2-3' if n <= 3 else '4+', 'big' if big else 'small')


def build():
    random.seed(1)
    fights, sessions = fights_and_sessions()
    os.makedirs(SETS, exist_ok=True)
    by = collections.defaultdict(list)
    for e in fights:
        if e['end'] in ('stuck', 'restarted'):
            continue
        by[fight_bucket(e)].append(e)
    smoke = []
    for b, v in sorted(by.items()):
        bad = [e for e in v if e['end'] in ('died', 'escape pod') or (e.get('energy_end') or 255) < 80]
        random.shuffle(v)
        smoke += bad[:6] + [e for e in v if e not in bad][:6]
    fail_sess = [e for e in sessions if e['end'] in ('stuck', 'died')]
    ok_sess = [e for e in sessions if e['end'] == 'docked']
    random.shuffle(ok_sess)
    # aim1: one enemy only, so hold-on-target and acquire time measure aiming alone (no one else shoots)
    one = [e for b, v in by.items() if b.startswith('pirates 1') for e in v]
    random.shuffle(one)
    hard1 = [e for e in one if e['end'] in ('died', 'escape pod') or (e.get('energy_end') or 255) < 120]
    aim1 = hard1[:30] + [e for e in one if e not in hard1][:90]
    # mid: a tenth of everything, stratified (every scenario keeps its share), for screening before the full set
    mid = []
    for b, v in sorted(by.items()):
        w = list(v); random.shuffle(w); mid += w[:max(12, len(w) // 10)]
    sets = {'smoke': (smoke, fail_sess[:20] + ok_sess[:6]), 'full': ([e for v in by.values() for e in v], fail_sess + ok_sess[:60]),
            'aim1': (aim1, []), 'mid': (mid, fail_sess + ok_sess[:10])}
    # per-scenario tuning sets (~200 fights each, half of them the hard ones), excluding the mid set so the
    # mid set stays a fair check afterwards
    midset = set(e['path'] for e in mid)
    groups = {'b1': ('pirates 1 big', 'pirates 1 small'), 'b23': ('pirates 2-3 big', 'pirates 2-3 small'),
              'b4': ('pirates 4+ big', 'pirates 4+ small'), 'bw': ('witchspace',)}
    # tune: 300 fights, stratified, none from mid (mid stays the check)
    pool = collections.defaultdict(list)
    for b, v in by.items():
        pool[b] = [e for e in v if e['path'] not in midset]
    tune = []
    for b, v in sorted(pool.items()):
        w = list(v); random.shuffle(w)
        hard = [e for e in w if e['end'] in ('died', 'escape pod') or (e.get('energy_end') or 255) < 120]
        k = max(10, 300 * len(v) // max(1, sum(len(x) for x in pool.values())))
        tune += (hard[:k // 3] + [e for e in w if e not in hard])[:k]
    sets['tune'] = (tune, [])
    for name, bs in groups.items():
        v = [e for b in bs for e in by.get(b, []) if e['path'] not in midset]
        random.shuffle(v)
        hard = [e for e in v if e['end'] in ('died', 'escape pod') or (e.get('energy_end') or 255) < 120]
        sets[name] = ((hard[:100] + [e for e in v if e not in hard])[:200], [])
    for name, (f, s) in sets.items():
        json.dump({'fights': f, 'sessions': s}, open(os.path.join(SETS, name + '.json'), 'w'))
        print('%s: %d fights (%s), %d sessions (%d failures)' % (name, len(f), ', '.join('%s %d' % (b, sum(1 for e in f if fight_bucket(e) == b)) for b in sorted(by)),
                                                                len(s), sum(1 for e in s if e['end'] != 'docked')))


def run_lab(chunk, env):
    lst = '/tmp/suite_%d.txt' % os.getpid() + str(id(chunk))
    open(lst, 'w').write('\n'.join(e['path'] for e in chunk) + '\n')
    p = subprocess.run([os.path.join(AGENT, os.environ.get('SUITE_LAB', 'cplayer/lab')), lst], cwd=AGENT, env=env, capture_output=True, text=True)
    os.unlink(lst)
    return p.stdout, p.stderr


def run_session(e, env):
    env = dict(env, START=e['path'], STOPDOCK='1')
    p = subprocess.run([os.path.join(AGENT, 'cplayer/run_sim'), '30', '', '/dev/null'], cwd=AGENT, env=env, capture_output=True, text=True)
    m = re.search(r'SESSION (\w+)', p.stdout)
    return m.group(1) if m else 'timeout'


def run(setname, name, kv):
    s = json.load(open(os.path.join(SETS, setname + '.json')))
    env = dict(os.environ)
    for x in kv:
        k, v = x.split('=', 1)
        env[k] = v
    # (the lab flies the player's own numbers: a LABP default here once tested witchspace with a turn_off
    # the player never had)
    fights, sessions = s['fights'], s['sessions']
    # FRESH=1: every fight in its own lab process (static state in the player carries over between fights
    # in one process: witchspace moved 2.5% between runs that changed nothing there)
    chunks = [[e] for e in fights] if os.environ.get('FRESH', '1') != '0' else [fights[i::NPROC] for i in range(NPROC)]
    rows, scn = {}, {}
    aims = []
    tgts = []
    dcause = []
    expo = []
    ttk = []
    epi = []
    env.setdefault('DCAUSE', '1')
    with ThreadPoolExecutor(NPROC) as ex:
        fut_f = [ex.submit(run_lab, c, env) for c in chunks if c]
        fut_s = [(e, ex.submit(run_session, e, env)) for e in sessions]
        for f in fut_f:
            out, err = f.result()
            for line in out.splitlines():
                v = line.split()
                if len(v) >= 17:
                    rows[v[0]] = v
            for line in err.splitlines():
                if line.startswith('SCN '):
                    scn[line.split()[1]] = line
                elif line.startswith('AIM '):
                    aims.append(line)
                elif line.startswith('TGT '):
                    tgts.append(line)
                elif line.startswith('DCAUSE '):
                    dcause.append(line)
                elif line.startswith('EPI '):
                    v = line.split()
                    m = re.search(r'off (\d+) side (\d+)', line)
                    epi.append((v[1] == 'hit', int(v[2]), int(m.group(1)) if m else 0, int(m.group(2)) if m else 0))
                elif line.startswith('TTK '):
                    ttk.append([int(x) for x in line.split()[1:]])
                elif line.startswith('EXPO '):
                    expo.append([int(x) for x in line.split()[1:]])
        sess = [(e, f.result()) for e, f in fut_s]
    by = collections.defaultdict(lambda: [0, 0.0, 0.0, 0, 0, 0, 0])      # n pts minutes deaths kills dmg dealt
    for e in fights:
        v = rows.get(e['path'])
        if not v:
            continue
        b = by[fight_bucket(e)]
        b[0] += 1; b[1] += float(v[1]); b[2] += float(v[2]); b[3] += int(v[5]); b[4] += int(v[7]); b[5] += int(v[4]); b[6] += int(v[16])
    lines = ['%-22s %4s %8s %7s %6s %6s %8s' % ('scenario', 'n', 'pts', 'pts/min', 'deaths', 'kills', 'taken')]
    tot = [0, 0.0, 0.0, 0, 0, 0, 0]
    for k, b in sorted(by.items()):
        lines.append('%-22s %4d %8.1f %7.2f %6d %6d %8d' % (k, b[0], b[1], b[1] / max(b[2], 1e-9), b[3], b[4], b[5]))
        tot = [x + y for x, y in zip(tot, b)]
    lines.append('%-22s %4d %8.1f %7.2f %6d %6d %8d' % ('ALL FIGHTS', tot[0], tot[1], tot[1] / max(tot[2], 1e-9), tot[3], tot[4], tot[5]))
    oc = collections.Counter(r for _, r in sess)
    if aims:   # the aim measures, summed over the chunks' AIM lines
        acc = collections.Counter()
        for a in aims:
            m = re.search(r'acquire ([\d.]+) s avg over (\d+) \(never (\d+)\) \| hold ([\d.]+)% of (\d+) locked frames \| off ([\d.]+) deg avg \| stick at a stop ([\d.]+)%', a)
            if not m:
                continue
            acq, n, never, hold, locked, off, stop = map(float, m.groups())
            acc['acq'] += acq * n; acc['n'] += n; acc['never'] += never; acc['in'] += hold / 100 * locked; acc['locked'] += locked
            acc['off'] += off * locked; acc['stop'] += stop / 100 * locked
        if acc['locked']:
            lines.append('AIM acquire %.2f s (never %d of %d) | hold %.1f%% | off %.2f deg | stick at a stop %.1f%%' % (
                acc['acq'] / max(acc['n'], 1), acc['never'], acc['n'] + acc['never'], 100 * acc['in'] / acc['locked'], acc['off'] / acc['locked'], 100 * acc['stop'] / acc['locked']))
    if tgts:
        t = collections.Counter()
        for x in tgts:
            for k, v in re.findall(r'(\w+) (\d+)', x[4:]):
                t[k] += int(v)
        lines.append('TGT %d targets: gone (killed) %.0f%%, switched before ever aimed %.0f%%, switched after aiming %.0f%% | lock lost %d times (%.1f per target)' % (
            t['episodes'], 100 * t['gone'] / max(t['episodes'], 1), 100 * t['switched_unacquired'] / max(t['episodes'], 1),
            100 * t['switched_acquired'] / max(t['episodes'], 1), t['lock_losses'], t['lock_losses'] / max(t['episodes'], 1)))
    lines.append('sessions %d: %s' % (len(sess), ', '.join('%s %d' % kv for kv in sorted(oc.items()))))
    print('== %s on %s (%s)' % (name, setname, ' '.join(kv)))
    print('\n'.join(lines))
    os.makedirs(os.path.join(HOME, 'work/runs'), exist_ok=True)
    per = {e['path']: {'bucket': fight_bucket(e), 'pts': float(rows[e['path']][1]), 'min': float(rows[e['path']][2]), 'died': int(rows[e['path']][5]), 'kills': int(rows[e['path']][7]),
                       'taken': int(rows[e['path']][4])} for e in fights if e['path'] in rows}
    if expo:        # how much of the fight an enemy's guns bore on us, and how
        t = [sum(x[i] for x in expo) for i in range(5)]
        print('EXPO guns on us %.2f per frame | jousting %.0f%%, ahead %.0f%%, behind %.0f%%' % (t[0] / max(1, t[4]), 100 * t[1] / max(1, t[0]),
              100 * t[2] / max(1, t[0]), 100 * t[3] / max(1, t[0])))
    if epi:         # each target taken: frames until it was in the sights, or dropped before
        hits = [f for h, f, *_ in epi if h]
        print('TARGETS %d: in the sights %.0f%% (median %d frames, mean %.0f), dropped before %.0f%%' % (len(epi), 100 * len(hits) / len(epi),
              sorted(hits)[len(hits) // 2] if hits else 0, sum(hits) / max(1, len(hits)), 100 * (len(epi) - len(hits)) / len(epi)))
        for sname, test in (('big turn', lambda o, s_: o >= 30), ('mid', lambda o, s_: 10 <= o < 30), ('small, above/below', lambda o, s_: o < 10 and s_ < 20),
                           ('small, angled', lambda o, s_: o < 10 and 20 <= s_ < 50), ('small, level', lambda o, s_: o < 10 and s_ >= 50)):
            g = [(h, f) for h, f, o, s_ in epi if test(o, s_)]
            gh = sorted(f for h, f in g if h)
            if g: print('  %-20s n %4d  in the sights %3.0f%%  median %4d frames' % (sname, len(g), 100 * len(gh) / len(g), gh[len(gh) // 2] if gh else 0))
    if ttk:         # where fight time goes
        t = [sum(x[i] for x in ttk) for i in range(4)]
        a = max(1, sum(t))
        print('TIME slewing %.0f%%, tracking %.0f%%, in the sights %.0f%%, no target %.0f%%' % (100 * t[0] / a, 100 * t[1] / a, 100 * t[2] / a, 100 * t[3] / a))
    if dcause:      # why each death: collision (rammed), shot down, planet
        open(os.path.join(HOME, 'work/runs/%s_%s_dcause.txt' % (setname, name)), 'w').write('\n'.join(dcause) + '\n')
        print('DEATHS by cause: ' + ', '.join('%s %d' % kv_ for kv_ in collections.Counter(l.split()[2].rstrip(':') for l in dcause).most_common()))
    json.dump({'set': setname, 'name': name, 'kv': kv, 'fights': per}, open(os.path.join(HOME, 'work/runs/%s_%s.json' % (setname, name)), 'w'))
    with open(os.path.join(HOME, 'work/suite_results.txt'), 'a') as f:
        f.write('%s %s %s | fights %d pts %.1f deaths %d kills %d taken %d | sessions %s\n' % (setname, name, ' '.join(kv), tot[0], tot[1], tot[3], tot[4], tot[5],
                                                                                         ', '.join('%s %d' % kv for kv in sorted(oc.items()))))


if __name__ == '__main__':
    if sys.argv[1] == 'build':
        build()
    else:
        run(sys.argv[2], sys.argv[3], sys.argv[4:])

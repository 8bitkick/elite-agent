#!/usr/bin/env python3
"""rrstats.py <run.log> ...: running away and rolling in fights, from run_sim logs made with RRLOG=1 (cplayer/combat.c).

Running away: every enemy behind us (b1), our speed 30 or more, the nearest's range opening (over the last 25 frames).
Rolling: the stick's roll each game loop as Elite's roll rate (alpha, 1/256 rad a loop), the angle turned, and the
reversals (the roll changing side at alpha 2 or more each way). Both split by what was flying then."""
import collections, re, sys

RR = re.compile(r'RR (\d+) (\d+) b(\d) d(\d+) rr-?\d+ s(\d+) jx(-?\d+) jy(-?\d+) v(\d) sh(\d) ws(\d) m(\d) p(\d) w(\d) r(\d) fb(\d) ty(\d) off(\d+) tt(\d+)(?: td(\d+) px(\d+) tz(\d))?(?: f(\d))?(?: ta([\d.]+))?')


def alpha(jx):          # pilot.c's jx_for_alpha, inverted: 0..3 by 8 then 4 a step (8..31)
    m = abs(jx)
    a = 0 if m < 4 else (m - 4) // 8 if m < 34 else (m - 2) // 4
    a = min(a, 31)
    return a if jx >= 0 else -a


def state(f):
    if f['m'] == 1:
        return 'break-off run'
    if f['m'] == 3:
        return 'ram dodge'
    if f['m'] == 4:
        return 'evade'
    if f['m'] == 2:
        return 'missile side'
    if f['sh'] and f['fb'] == 1:
        return 'show: flyby'
    if f['sh'] and f['fb'] == 2:
        return 'show: after flyby'
    if f['sh'] and f['w']:
        return 'show: weave'
    if f['sh'] and f['ty']:
        return 'show: toy follow'
    if f['sh']:
        return 'show: other'
    return 'efficient' + (' (planner)' if f['p'] else '')


def main():
    run = collections.Counter(); frames = collections.Counter()
    rollang = collections.Counter(); rev = collections.Counter(); rollfr = collections.Counter(); loops = collections.Counter()
    total = 0
    aim_last = [0, 0]
    aim_on = aimless = 0   # (the target within 3 deg of the sights, attacking: frames, and of those rotating)
    flipst = collections.Counter()
    flips = []        # (pitch stick flips: +-40 to -+40 within 25 frames)
    segs = []         # (roll segments: state at its start, degrees, sign, start and end frame)
    big = [0, 0, 0, 0]   # (frames with the target drawn 24 px+: all, within 4.5 deg of the sights (~20 px), fired)
    tgt = []          # (every fight frame with a target: its drawn size, and closing (under 8 px, range falling))
    pur = []          # (the show on, the target ahead: its range and drawn size)
    for p in sys.argv[1:]:
        hist, last_fr, last_mc, last_sign = [], -10, -1, 0
        seg = None
        jys = []
        for line in open(p, errors='replace'):
            m = RR.search(line)
            if not m:
                continue
            g = [int(x) if x is not None and '.' not in x else float(x) if x is not None else -1 for x in m.groups()]
            f = dict(fr=g[0], mc=g[1], b=g[2], d=g[3], s=g[4], jx=g[5], jy=g[6], v=g[7], sh=g[8], ws=g[9], m=g[10], p=g[11], w=g[12], r=g[13], fb=g[14], ty=g[15], off=g[16], td=g[18], px=g[19], tz=g[20], fire=g[21], ta=g[22])
            if f['fr'] - last_fr > 5:
                hist, last_sign = [], 0           # (a new fight)
            last_fr = f['fr']
            hist.append(f)
            st = state(f)
            jys.append((f['fr'], f['jy']))
            if len(jys) > 25: jys.pop(0)
            if f['jy'] <= -40 and any(y >= 40 for fr, y in jys) or f['jy'] >= 40 and any(y <= -40 for fr, y in jys):
                if not flips or flips[-1] < f['fr'] - 25: flips.append(f['fr']); flipst[st] += 1
            if len(hist) > 26:
                hist.pop(0)
            if f['px'] >= 24:
                big[0] += 1; big[1] += 0 <= f['ta'] < 4.5; big[2] += f['fire'] == 1; big[3] += (0 <= f['ta'] < 4.5) or f['fire'] == 1
            if f['px'] >= 0:
                tgt.append((f['px'], f['px'] < 8 and len(hist) > 25 and f['td'] < hist[0]['td'] - 5))
            if f['sh'] and f['tz'] == 1 and f['m'] == 0:
                pur.append((f['td'], f['px']))
            frames[st] += 1
            total += 1
            if f['m'] == 0 and f['off'] < 3 and f['tz'] == 1:
                aim_on += 1
                a_ = alpha(f['jx']); sa = 1 if a_ >= 2 else -1 if a_ <= -2 else 0; sp = 1 if f['jy'] >= 36 else -1 if f['jy'] <= -36 else 0
                if (sa and aim_last[0] and sa != aim_last[0]) or (sp and aim_last[1] and sp != aim_last[1]): aimless += 1
                if sa: aim_last[0] = sa
                if sp: aim_last[1] = sp
            else:
                aim_last = [0, 0]
            opening = len(hist) > 25 and f['d'] > hist[0]['d'] + 5
            if f['b'] and f['s'] >= 30 and opening:
                run[st] += 1
            if f['mc'] != last_mc:               # a game loop: the roll it turns
                last_mc = f['mc']
                a = alpha(f['jx'])
                loops[st] += 1
                rollang[st] += abs(a) / 256 * 57.3
                if a:
                    rollfr[st] += 1
                sgn = 1 if a >= 2 else -1 if a <= -2 else 0
                if sgn and last_sign and sgn != last_sign:
                    rev[st] += 1
                s1 = 1 if a > 0 else -1 if a < 0 else 0
                if seg and (s1 != seg[2] or f['fr'] - seg[4] > 5):
                    segs.append(seg); seg = None
                if s1:
                    if not seg: seg = [st, 0.0, s1, f['fr'], f['fr']]
                    seg[1] += abs(a) / 256 * 57.3; seg[4] = f['fr']
                if sgn:
                    last_sign = sgn
    mins = total / 3000
    print('FOUGHT (24 px+ and centred or under fire): %.2f s a fight minute' % (big[3] / 50 / max(1e-9, total / 3000)))
    if big[0]:
        print('BIG TARGET (24 px+): %.1f s a fight minute; centred (within ~20 px of the sights) %.0f%% of it; firing %.0f%% of it (%.1f s a fight minute)' % (big[0] / 50 / (total / 3000), 100 * big[1] / big[0], 100 * big[2] / big[0], big[2] / 50 / (total / 3000)))
    if tgt:
        allp = sorted(p for p, c in tgt); fp = sorted(p for p, c in tgt if not c)
        print('TARGET SIZE (all fight time): median %d px, 24 px+ %.0f%% | closing (under 8 px, range falling) %.0f%% of fight time; the rest: median %d px, 24 px+ %.0f%%' % (
            allp[len(allp) // 2], 100 * sum(1 for p in allp if p >= 24) / len(allp), 100 * (len(allp) - len(fp)) / len(allp),
            fp[len(fp) // 2] if fp else 0, 100 * sum(1 for p in fp if p >= 24) / max(1, len(fp))))
    print('aimless motion (hunting): stick reversals with the target within 3 deg of the sights, %.1f a minute of it (%.1f min on target)' % (aimless / max(1e-9, aim_on / 3000), aim_on / 3000))
    print('pitch flips (stick +-40 to -+40 inside half a second): %.1f a minute; by state: %s' % (len(flips) / (total / 3000), ', '.join('%s %.1f' % (k, v / (total / 3000)) for k, v in flipst.most_common(5))))
    if segs:
        segs.sort(key=lambda s: s[3])
        quick = sum(1 for x, y in zip(segs, segs[1:]) if y[2] != x[2] and y[3] - x[4] <= 50)
        small = sum(1 for s in segs if s[1] < 30)
        print('roll segments: %.1f a minute, small (<30 deg) %.1f, large %.1f, reversed within 1 s %.1f a minute; mean large %.0f deg' % (
            len(segs) / (total / 3000), small / (total / 3000), (len(segs) - small) / (total / 3000), quick / (total / 3000),
            sum(s[1] for s in segs if s[1] >= 30) / max(1, len(segs) - small)))
        bys = collections.Counter(s[0] for s in segs if s[1] < 30)
        print('  small rolls by state (a minute of fight): ' + ', '.join('%s %.1f' % (k, v / (total / 3000)) for k, v in bys.most_common(6)))
    if pur:
        ds = sorted(d for d, p in pur); ps = sorted(p for d, p in pur)
        print('show on, target ahead: %.1f min; range median %d (p90 %d), drawn median %d px; under 8 px %.0f%% of it, 24 px+ %.0f%%' % (
            len(pur) / 3000, ds[len(ds) // 2], ds[int(len(ds) * .9)], ps[len(ps) // 2], 100 * sum(1 for p in ps if p < 8) / len(ps), 100 * sum(1 for p in ps if p >= 24) / len(ps)))
    print('%.1f fight minutes; running away (all enemies behind, speed 30+, range opening) %.1f%% of fight time' % (mins, 100 * sum(run.values()) / max(1, total)))
    print('  %-24s %7s %8s %9s %10s %10s %9s' % ('flying', 'time%', 'run%', 'run s/min', 'roll deg/s', 'rolling%', 'rev/min'))
    for st in sorted(frames, key=lambda k: -frames[k]):
        fm = frames[st] / 3000
        print('  %-24s %6.1f%% %7.1f%% %9.1f %10.1f %9.0f%% %9.1f' % (st, 100 * frames[st] / total, 100 * run[st] / frames[st], run[st] / 50 / max(1e-9, mins),
                                                               rollang[st] / max(1e-9, frames[st] / 50), 100 * rollfr[st] / max(1, loops[st]), rev[st] / max(1e-9, fm)))
    fm = total / 3000
    print('  %-24s %6.1f%% %7.1f%% %9.1f %10.1f %9.0f%% %9.1f' % ('ALL', 100, 100 * sum(run.values()) / total, sum(run.values()) / 50 / mins,
                                                           sum(rollang.values()) / (total / 50), 100 * sum(rollfr.values()) / max(1, sum(loops.values())), sum(rev.values()) / fm))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Where a career's time and points go, from a run_sim journal (up to the rank of Elite): fights and sessions.

  fightstats.py journal.jsonl ...

A fight runs from its fight_start to the next fight_end, death, restart, dock, escape pod or fight_start,
so a fight left without a fight_end (the Master's journal has none for a bail-out by hyperspace or a dock)
still counts its time. Points: the tally's rise over the fight (fight_start's tally to the next event that
carries one). Kills: the journal's fight_end kills, which on the Model B (the disc version, 1 point a kill)
are the tally's rise, ours only, and on the Master the hostile ships seen blowing up during the fight (ours
or not: its tally is points by type, so there's no exact count of ours).
"""
import json, sys

END = ('fight_end', 'died', 'stuck', 'docked', 'escape_pod', 'fight_start')


def stats(path):
    ev = []
    for line in open(path):
        try:
            d = json.loads(line)
        except ValueError:
            continue
        ev.append(d)
        if d['e'].get('event') == 'rank' and d['e'].get('rank') == 'Elite':
            break
    hours = ev[-1]['frame'] / 180000 if ev else 0
    n = fmin = pts = kills = 0
    open_ = None            # (frame, tally) of the fight under way
    tally = 0.0
    sess = {}
    for d in ev:
        e, k = d['e'], d['e'].get('event')
        if open_ and k in END:
            fmin += (d['frame'] - open_[0]) / 3000
            t = e.get('tally')
            if k == 'fight_end':
                pts += e.get('gained', 0)
                kills += e.get('kills', 0)
            elif t is not None:
                pts += max(0.0, t - open_[1])
            open_ = None
        if k == 'fight_start':
            n += 1
            open_ = (d['frame'], e.get('tally', tally))
        if k == 'session':
            s = sess.setdefault(e['kind'], [0, 0.0, 0.0])
            s[0] += 1
            s[1] += e['points']
            s[2] += e['minutes']
        if e.get('tally') is not None:
            tally = e['tally']
    print('%s: %.1f game hours, tally %.0f' % (path, hours, tally))
    print('  fights %d, %.0f fight minutes (%.1f%% of the time), %.0f points in fights (%.1f a fight minute), '
          'journal kills %d (%.1f a fight minute)' % (n, fmin, fmin / 60 / max(hours, 1e-9) * 100, pts,
                                                       pts / max(fmin, 1e-9), kills, kills / max(fmin, 1e-9)))
    for kind, (c, p, m) in sorted(sess.items()):
        print('  %s sessions %d: %.0f points in %.0f minutes (%.2f a minute), %.0f%% of the time'
              % (kind, c, p, m, p / max(m, 1e-9), m / 60 / max(hours, 1e-9) * 100))


if __name__ == '__main__':
    for p in sys.argv[1:]:
        stats(p)

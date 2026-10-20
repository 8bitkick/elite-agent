#!/usr/bin/env python3
"""smooth.py <lab stderr files (LOG=2 RRLOG=1)>: how smooth the steering looks, from Elite's own rates (the frame
trace's be, Elite's pitch BETA, and al, its roll ALP1): the nose's pitch reversing (BETA changing sign), a minute;
rolling with the target already within 5 deg of the nose (ALP1 > 0), and rolling at all, as shares of frames."""
import re, sys
frames = rev = near = rnear = rolling = steps = onoff = 0
for fn in sys.argv[1:]:
    last = 0; lastbe = None
    for l in open(fn):
        if ' RR ' not in l: continue
        d = dict(re.findall(r' ([a-z]+)(-?[0-9.]+)', ' ' + l.split(' RR ', 1)[1]))
        if d.get('tt', '0') == '0': last = 0; continue
        frames += 1
        be, al, ta = int(d['be']), int(d['al']), float(d.get('ta', 180))
        s = (be > 0) - (be < 0)
        if lastbe is not None and be != lastbe:
            steps += 1
            if (be == 0) != (lastbe == 0): onoff += 1
        lastbe = be
        if s and last and s != last: rev += 1
        if s: last = s
        rolling += al > 0
        if ta < 5: near += 1; rnear += al > 0
m = max(1, frames) / 3000
print('pitch reversals %.0f a minute, changes %.0f (on/off %.0f) | rolling %.0f%% of frames, %.0f%% with the target within 5 deg | %d frames' % (
    rev / m, steps / m, onoff / m, 100 * rolling / max(1, frames), 100 * rnear / max(1, near), frames))

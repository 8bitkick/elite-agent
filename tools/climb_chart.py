#!/usr/bin/env python3
"""The README's climb chart (docs/img/climb.svg): kill tally against game hours, one line per career.

  climb_chart.py out.svg GROUP=journal.jsonl[,journal.jsonl...] ... [keyboard=docs/img/climb-keyboard.csv]

GROUP is efficient, fair or curious; each career's journal (run_sim's) is read up to its rank of Elite.
The keyboard line is the first joystick-less career (hours,tally rows), kept as data since its build is gone.
"""
import json, sys

W, H = 820, 420
X0, X1, Y0, Y1 = 64, 640, 352, 24          # plot area
HMAX, TMAX = 120, 7000
STYLE = {   # colour, label (validated: violet / aqua / orange, every line direct-labelled)
    'efficient': ('#6250d6', 'Efficient'),
    'fair': ('#1baf7a', 'Fair (information)'),
    'curious': ('#eb6834', 'Curious (show fights)'),
}


def X(h): return X0 + h * (X1 - X0) / HMAX
def Y(t): return Y0 - min(t, TMAX) * (Y0 - Y1) / TMAX


def career(path):
    pts, last = [], -1.0
    for line in open(path):
        try:
            d = json.loads(line)
        except ValueError:
            continue
        e, h = d['e'], d['frame'] / 180000
        if e.get('event') == 'rank' and e.get('rank') == 'Elite':
            pts.append((h, 6400))
            return pts
        t = e.get('tally')
        if t is not None and h - last >= 0.1:
            pts.append((h, min(t, 6400)))
            last = h
    return pts


def poly(pts, colour, width=1.8, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ''
    return '<polyline points="%s" fill="none" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' % (
        ' '.join('%.1f,%.1f' % (X(h), Y(t)) for h, t in pts), colour, width, d)


def main():
    out = sys.argv[1]
    groups, keyboard = {}, None
    for a in sys.argv[2:]:
        k, v = a.split('=', 1)
        if k == 'keyboard':
            keyboard = [tuple(map(float, l.split(','))) for l in open(v) if l.strip()]
        else:
            groups[k] = [career(p) for p in v.split(',')]
    s = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" '
         'font-family="Helvetica,Arial,sans-serif" font-size="12">' % (W, H, W, H),
         '<rect width="%d" height="%d" fill="#fcfcfb"/>' % (W, H)]
    for t in range(0, TMAX + 1, 1000):
        s.append('<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" stroke="#e6e6ea"/><text x="%d" y="%.1f" text-anchor="end" fill="#52514e">%s</text>'
                 % (X0, X1, Y(t), Y(t), X0 - 6, Y(t) + 4, format(t, ',')))
    for h in range(0, HMAX + 1, 20):
        s.append('<line x1="%.1f" x2="%.1f" y1="%d" y2="%d" stroke="#eeeef2"/><text x="%.1f" y="%d" text-anchor="middle" fill="#52514e">%d</text>'
                 % (X(h), X(h), Y1, Y0, X(h), Y0 + 16, h))
    s.append('<text x="%.1f" y="%d" text-anchor="middle" fill="#0b0b0b">game hours</text>' % ((X0 + X1) / 2, Y0 + 38))
    s.append('<text transform="translate(16,%.1f) rotate(-90)" text-anchor="middle" fill="#0b0b0b">kill tally</text>' % ((Y0 + Y1) / 2))
    for t, name in ((2560, 'Deadly (2,560)'), (6400, 'Elite (6,400)')):
        s.append('<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" stroke="#999" stroke-dasharray="4 4"/><text x="%d" y="%.1f" fill="#52514e" text-anchor="middle">%s</text>'
                 % (X0, X1, Y(t), Y(t), X(96), Y(t) - 4, name) if t != 6400 else
                 '<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" stroke="#999" stroke-dasharray="4 4"/><text x="%d" y="%.1f" fill="#0b0b0b" text-anchor="end" font-weight="bold">Elite</text>'
                 % (X0, X1, Y(t), Y(t), X0 - 6, Y(t) + 4))
    legend = []
    if keyboard:
        s.append(poly(keyboard, '#8a8a96', 1.8, '5 3'))
        legend.append(('#8a8a96', '5 3', 'Where it started: keyboard aim, %.1f h' % keyboard[-1][0]))
        s.append('<text x="%.1f" y="%.1f" fill="#52514e" text-anchor="start">Keyboard %.1f h</text>' % (X(keyboard[-1][0]) + 4, Y(6400) - 8, keyboard[-1][0]))
    for g in ('curious', 'fair', 'efficient'):
        if g not in groups:
            continue
        colour, label = STYLE[g]
        for pts in groups[g]:
            s.append(poly(pts, colour))
        hs = [p[-1][0] for p in groups[g] if p and p[-1][1] >= 6400]
        if hs:
            txt = '%s: %.1f h' % (label, hs[0]) if len(hs) == 1 else '%s: %.1f-%.1f h (avg %.1f)' % (label, min(hs), max(hs), sum(hs) / len(hs))
            legend.append((colour, None, txt))
            # direct label at the end of the group's earliest line
            s.append('<text x="%.1f" y="%.1f" fill="#0b0b0b" text-anchor="%s">%s</text>'
                     % (X(min(hs) if g == 'efficient' else max(hs)), Y(6400) - 8, 'end' if g == 'efficient' else 'start' if g == 'fair' else 'middle', '%s %.1f h' % (label.split(' ')[0], sum(hs) / len(hs))))
    y = 40
    for colour, dash, txt in reversed(legend):
        d = ' stroke-dasharray="%s"' % dash if dash else ''
        # (bottom right, under the curves: the lines are all up and left by then)
        s.append('<line x1="%.1f" x2="%.1f" y1="%d" y2="%d" stroke="%s" stroke-width="3"%s/><text x="%.1f" y="%d" fill="#0b0b0b">%s</text>'
                 % (X(62), X(62) + 22, Y0 - y, Y0 - y, colour, d, X(62) + 28, Y0 - y + 4, txt))
        y += 20
    s.append('</svg>')
    open(out, 'w').write('\n'.join(s) + '\n')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Careers, the best of each group bold and its others faint.

  climb_bands.py out.svg GROUP=a.jsonl,b.jsonl,... [GROUP=...]   [HMAX=32]

GROUP is a climb_chart.py style (efficient, fair, curious, wsp). A group of one career is drawn as a plain line.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import climb_chart as cc

W, H, X0, X1, Y0, Y1 = 820, 432, 64, 640, 352, 24
HMAX, TMAX = float(os.environ.get("HMAX", 32)), 7000
LEVELS = list(range(0, 6401, 50))


def X(h): return X0 + h * (X1 - X0) / HMAX
def Y(t): return Y0 - min(t, TMAX) * (Y0 - Y1) / TMAX


def hours_at(pts, t):
    """The first time the career's tally reaches t, interpolated between its samples."""
    ph, pt = 0.0, 0.0
    for h, tt in pts:
        if tt >= t:
            return ph + (h - ph) * ((t - pt) / (tt - pt) if tt > pt else 1.0)
        ph, pt = h, tt
    return None


def main():
    out = sys.argv[1]
    groups = [a.split("=", 1) for a in sys.argv[2:]]
    s = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="Helvetica,Arial,sans-serif" font-size="12">' % (W, H, W, H),
         '<rect width="%d" height="%d" fill="#fcfcfb"/>' % (W, H)]
    for t in range(0, TMAX + 1, 1000):
        s.append('<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" stroke="#e6e6ea"/><text x="%d" y="%.1f" text-anchor="end" fill="#52514e">%s</text>' % (X0, X1, Y(t), Y(t), X0 - 6, Y(t) + 4, format(t, ',')))
    for h in range(0, int(HMAX) + 1, 5 if HMAX <= 40 else 20):
        s.append('<line x1="%.1f" x2="%.1f" y1="%d" y2="%d" stroke="#eeeef2"/><text x="%.1f" y="%d" text-anchor="middle" fill="#52514e">%d</text>' % (X(h), X(h), Y1, Y0, X(h), Y0 + 16, h))
    s.append('<text x="%.1f" y="%d" text-anchor="middle" fill="#0b0b0b">game hours</text>' % ((X0 + X1) / 2, Y0 + 38))
    s.append('<text transform="translate(16,%.1f) rotate(-90)" text-anchor="middle" fill="#0b0b0b">kill tally</text>' % ((Y0 + Y1) / 2))
    for t, name in ((2560, 'Deadly'), (6400, 'Elite')):
        s.append('<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" stroke="#999" stroke-dasharray="4 4"/><text x="%d" y="%.1f" fill="#0b0b0b" text-anchor="end"%s>%s</text>'
                 % (X0, X1, Y(t), Y(t), X0 - 6, Y(t) + 4, ' font-weight="bold"' if t == 6400 else '', name))
    legend = []
    for g, files in groups:
        colour, label = cc.STYLE[g]
        careers = sorted((cc.career(p) for p in files.split(",")), key=lambda c: c[-1][0])
        for c in careers[-1:] if len(careers) > 1 else []:       # the slowest, faint (the middle ones left out)
            s.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="1.6" stroke-opacity="0.35" stroke-linejoin="round"/>' % (' '.join('%.1f,%.1f' % (X(h), Y(t)) for h, t in c), colour))
        best = careers[0]           # the fastest, bold
        s.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="2.8" stroke-linejoin="round"/>' % (' '.join('%.1f,%.1f' % (X(h), Y(t)) for h, t in best), colour))
        fin = [c[-1][0] for c in careers]
        legend.append((colour, len(careers) > 1, '%s: best %.1f h' % (label.split(' (')[0], fin[0]) + (', slowest %.1f, mean of %d %.1f' % (fin[-1], len(fin), sum(fin) / len(fin)) if len(fin) > 1 else '')))
    y = Y(2200)             # (bottom right: empty, the careers are past here before they climb this high)
    lx = X(HMAX * 0.52)
    for colour, band, txt in legend:
        s.append('<line x1="%.1f" x2="%.1f" y1="%d" y2="%d" stroke="%s" stroke-width="2.8"/><text x="%.1f" y="%d" fill="#0b0b0b">%s</text>' % (lx, lx + 22, y, y, colour, lx + 28, y + 4, txt))
        y += 20
    s.append('<text x="%d" y="%d" fill="#52514e" text-anchor="end">bold: each pilot&#8217;s fastest career; faint: its slowest (the same three seeds for both fair pilots)</text>' % (X1, Y0 + 58))
    s.append('</svg>')
    open(out, 'w').write('\n'.join(s) + '\n')


if __name__ == '__main__':
    main()

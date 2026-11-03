#!/usr/bin/env python3
"""The README's climb chart: kill tally against game hours, each mode's fastest career bold and its slowest faint.

  climb_best.py out.svg GROUP=journal.jsonl,... [GROUP=csv:file.csv] ... [keyboard=docs/img/climb-keyboard.csv]
                [HMAX=160]

GROUP is a climb_chart.py style (efficient, fair, wsp, curious, ...). Journals are run_sim's, read up to the rank of
Elite; csv:file reads careers as career,hours,tally rows (curious's, recovered from the old chart, whose journals
aren't kept). The keyboard line is the first joystick-less career (hours,tally), dashed.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import climb_chart as cc

W, X0, X1, Y0, Y1 = 820, 64, 640, 352, 24
HMAX, TMAX = float(os.environ.get("HMAX", 160)), 7000


def X(h): return X0 + h * (X1 - X0) / HMAX
def Y(t): return Y0 - min(t, TMAX) * (Y0 - Y1) / TMAX


def read_csv(path):
    careers = {}
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        k, h, t = line.strip().split(",")
        careers.setdefault(k, []).append((float(h), float(t)))
    return list(careers.values())


def poly(pts, colour, width, opacity=1.0, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ''
    o = ' stroke-opacity="%.2f"' % opacity if opacity < 1 else ''
    return '<polyline points="%s" fill="none" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s%s/>' % (
        ' '.join('%.1f,%.1f' % (X(h), Y(t)) for h, t in pts), colour, width, o, d)


def main():
    out = sys.argv[1]
    groups, keyboard = [], None
    for a in sys.argv[2:]:
        k, v = a.split("=", 1)
        if k == "keyboard":
            keyboard = [tuple(map(float, l.split(","))) for l in open(v) if l.strip() and not l.startswith("#")]
        elif v.startswith("csv:"):
            groups.append((k, read_csv(v[4:])))
        else:
            groups.append((k, [cc.career(p) for p in v.split(",")]))
    rows = len(groups) + (1 if keyboard else 0)
    H = Y0 + 56 + 18 * (rows + 1) + 8
    s = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="Helvetica,Arial,sans-serif" font-size="12">' % (W, H, W, H),
         '<rect width="%d" height="%d" fill="#fcfcfb"/>' % (W, H)]
    for t in range(0, TMAX + 1, 1000):
        s.append('<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" stroke="#e6e6ea"/><text x="%d" y="%.1f" text-anchor="end" fill="#52514e">%s</text>' % (X0, X1, Y(t), Y(t), X0 - 6, Y(t) + 4, format(t, ',')))
    step = 20 if HMAX > 60 else 5
    for h in range(0, int(HMAX) + 1, step):
        s.append('<line x1="%.1f" x2="%.1f" y1="%d" y2="%d" stroke="#eeeef2"/><text x="%.1f" y="%d" text-anchor="middle" fill="#52514e">%d</text>' % (X(h), X(h), Y1, Y0, X(h), Y0 + 16, h))
    s.append('<text x="%.1f" y="%d" text-anchor="middle" fill="#0b0b0b">game hours</text>' % ((X0 + X1) / 2, Y0 + 36))
    s.append('<text transform="translate(16,%.1f) rotate(-90)" text-anchor="middle" fill="#0b0b0b">kill tally</text>' % ((Y0 + Y1) / 2))
    for t, name in ((2560, 'Deadly'), (6400, 'Elite')):
        s.append('<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" stroke="#999" stroke-dasharray="4 4"/><text x="%d" y="%.1f" fill="#0b0b0b" text-anchor="end"%s>%s</text>'
                 % (X0, X1, Y(t), Y(t), X0 - 6, Y(t) + 4, ' font-weight="bold"' if t == 6400 else '', name))
    legend = []
    if keyboard:
        s.append(poly(keyboard, '#8a8a96', 1.8, dash='5 3'))
        legend.append(('#8a8a96', '5 3', 'Where it started: keyboard aim, %.1f h' % keyboard[-1][0]))
    labels = []
    for g, careers in groups:
        colour, label = cc.STYLE[g]
        careers = sorted((c for c in careers if c), key=lambda c: c[-1][0])
        if len(careers) > 1:
            s.append(poly(careers[-1], colour, 1.6, 0.35))
        s.append(poly(careers[0], colour, 2.8))
        fin = [c[-1][0] for c in careers]
        name = label.split(' (')[0]
        legend.append((colour, None, '%s: best %.1f h' % (label, fin[0]) +
                       (', slowest %.1f, mean of %d %.1f' % (fin[-1], len(fin), sum(fin) / len(fin)) if len(fin) > 1 else '')))
        labels.append((fin[0], colour, '%s %.1f h' % (name, fin[0])))
    # direct labels above the Elite line: the ones bunched at the left in one row, fastest first (the colour says
    # which line); the others at their own line
    labels.sort()
    xr = X0 + 6
    for h, colour, txt in labels:
        w = 6.6 * len(txt)
        if X(h) < X0 + (X1 - X0) * 0.3:
            s.append('<text x="%.1f" y="%.1f" fill="%s" font-weight="bold">%s</text>' % (xr, Y(6400) - 8, colour, txt))
            xr += w + 14
        else:
            s.append('<text x="%.1f" y="%.1f" fill="%s" font-weight="bold" text-anchor="middle">%s</text>' % (X(h), Y(6400) - 8, colour, txt))
    y = Y0 + 58
    for colour, dash, txt in legend:
        d = ' stroke-dasharray="%s"' % dash if dash else ''
        s.append('<line x1="%d" x2="%d" y1="%d" y2="%d" stroke="%s" stroke-width="2.8"%s/><text x="%d" y="%d" fill="#0b0b0b">%s</text>' % (X0, X0 + 22, y, y, colour, d, X0 + 28, y + 4, txt))
        y += 18
    s.append('<text x="%d" y="%d" fill="#52514e">bold: each mode&#8217;s fastest career; faint: its slowest</text>' % (X0, y + 4))
    s.append('</svg>')
    open(out, 'w').write('\n'.join(s) + '\n')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Add a group of careers to an existing climb chart (climb_chart.py's SVG), on its own axes.

  climb_add.py chart.svg GROUP=journal.jsonl[,journal.jsonl...] [HMAX=160]

The chart's own journals need not be here: the new careers are drawn over it, a legend line added under the
others and a label at the Elite line. GROUP is a climb_chart.py style (wsp: the weight-space network).
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import climb_chart as cc


def main():
    path = sys.argv[1]
    args = dict(a.split("=", 1) for a in sys.argv[2:])
    cc.HMAX = int(args.pop("HMAX", 160))
    svg = open(path).read()
    # (the new entries go under the chart's own legend, nearest the axis, clear of the rank labels above it)
    y = 20
    add = []
    for g, files in args.items():
        colour, label = cc.STYLE[g]
        careers = [cc.career(p) for p in files.split(",")]
        for pts in careers:
            add.append(cc.poly(pts, colour, 2.2))
        hs = [p[-1][0] for p in careers if p and p[-1][1] >= 6400]
        if hs:
            avg = sum(hs) / len(hs)
            txt = "%s: %.1f h" % (label, hs[0]) if len(hs) == 1 else "%s: %.1f-%.1f h (avg %.1f)" % (label, min(hs), max(hs), avg)
            lx = cc.X(cc.HMAX * 62 / 140)
            add.append('<line x1="%.1f" x2="%.1f" y1="%d" y2="%d" stroke="%s" stroke-width="3"/><text x="%.1f" y="%d" fill="#0b0b0b">%s</text>'
                       % (lx, lx + 22, cc.Y0 - y, cc.Y0 - y, colour, lx + 28, cc.Y0 - y + 4, txt))
            add.append('<text x="%.1f" y="%.1f" fill="%s" text-anchor="start" font-weight="bold">%s %.1f h</text>'
                       % (cc.X(max(hs)) + 4, cc.Y(6400) - 22, colour, label.split(" (")[0], avg))
            y -= 14
    svg = svg.replace("</svg>", "\n".join(add) + "\n</svg>")
    open(path, "w").write(svg)


if __name__ == "__main__":
    main()

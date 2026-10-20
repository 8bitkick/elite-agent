#!/usr/bin/env python3
"""tools/make_splash.py <Comfortaa.ttf> --screen "<title>" <out.c> <symbol> [--preview <png>]   ("" for no title)

The loading screen, baked: the 8bitkick logo (presto/splash/8bitkick-logo.png, its left two-thirds so
its traces read at the panel's size; the rainbow in MODE 2's eight colours, Bayer dithered; the traces
crisp white), the 8bitkick mark over it (Comfortaa Bold, white, blue for half-covered edge pixels), and the
title on the logo (Comfortaa Bold, yellow, red edges, in a thick black
outline). 480 x 256: the framebuffer's rows, which the panel stretches to 480.

Writes <out.c>, a machine_boot_image <symbol> for the agent to give the firmware
(rp2350-bbc-micro/core/machine_hook.h): presto/elite_splash.c.
Comfortaa: https://fonts.google.com/specimen/Comfortaa (OFL).
"""
import sys, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'presto', 'splash', '8bitkick-logo.png')
W, H, FB_ROWS = 480, 480, 256
TRACE = (159, 211, 220)
ART_Y = 87                                   # the logo's top (of 480): centred between the mark and the firmware's loading line
TITLE_Y = 210                                # a title's middle (of 480): over the logo's middle
PAL = [(r * 255, g * 255, b * 255) for r in (0, 1) for g in (0, 1) for b in (0, 1)]
NIB = [(1 if b else 0) | (2 if g else 0) | (4 if r else 0) for (r, g, b) in PAL]   # bit 0 blue, 1 green, 2 red
WHITE, BLUE, BLACK, YELLOW, RED = (PAL.index(c) for c in ((255, 255, 255), (0, 0, 255), (0, 0, 0), (255, 255, 0), (255, 0, 0)))

def comfortaa(path, size):
    f = ImageFont.truetype(path, size)
    f.set_variation_by_name('Bold')
    return f

def text_mask(font, text, cx, cy):
    m = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(m)
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=font)
    d.text((cx - (x1 - x0) // 2 - x0, cy - (y1 - y0) // 2 - y0), text, font=font, fill=255)
    return m, (cx - (x1 - x0) // 2, cx + (x1 - x0) // 2)

def screen(font_path, title):
    # the logo: the traces found and kept aside; under them the colour of the band they cross,
    # carried along each row from the left, so the rainbow dithers clean
    logo = Image.open(SRC).convert('RGB').crop((6, 0, 1000, 580))
    px = logo.load()
    trace = Image.new('L', logo.size, 0)
    tp = trace.load()
    for y in range(logo.height):
        last = (0, 0, 0)
        for x in range(logo.width):
            r, g, b = px[x, y]
            if abs(r - TRACE[0]) < 40 and abs(g - TRACE[1]) < 40 and abs(b - TRACE[2]) < 40:
                tp[x, y] = 255
                px[x, y] = last
            else:
                last = px[x, y]
    art_h = round(logo.height * W / logo.width)
    pic = Image.new('RGB', (W, H), (0, 0, 0))
    pic.paste(logo.resize((W, art_h), Image.LANCZOS), (0, ART_Y))
    traces = Image.new('L', (W, H), 0)
    traces.paste(trace.resize((W, art_h), Image.LANCZOS), (0, ART_Y))
    # MODE 2: 160 x 256, eight colours, Bayer dithered; each pixel three of the panel's across
    bw = 160
    small = pic.resize((bw, FB_ROWS), Image.LANCZOS).load()
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    out = [[0] * W for _ in range(FB_ROWS)]
    for y in range(FB_ROWS):
        row = []
        for x in range(bw):
            t = (bayer[y & 3][x & 3] + 0.5) / 16 - 0.5
            r, g, b = (min(255, max(0, c + t * 230)) for c in small[x, y])
            row.append(min(range(8), key=lambda i: (PAL[i][0] - r) ** 2 + (PAL[i][1] - g) ** 2 + (PAL[i][2] - b) ** 2))
        for x in range(W):
            out[y][x] = row[x * bw // W]
    def over(mask, colour, at):
        mm = mask.resize((W, FB_ROWS), Image.LANCZOS).load()
        for y in range(FB_ROWS):
            for x in range(W):
                if mm[x, y] >= at: out[y][x] = colour
    over(traces, WHITE, 128)
    # the maker's mark, at the top
    mark, _ = text_mask(comfortaa(font_path, 48), '8bitkick', W // 2, ART_Y // 2)
    over(mark, BLUE, 64)
    over(mark, WHITE, 153)
    bar = (120, 240)
    if title:
        tm, (tx0, tx1) = text_mask(comfortaa(font_path, 72), title, W // 2, TITLE_Y)
        over(tm.filter(ImageFilter.MaxFilter(17)), BLACK, 64)        # the thick black outline
        over(tm, RED, 64)
        over(tm, YELLOW, 153)
        bar = (tx0 - 10, tx1 - tx0 + 20)
    return out, bar

def write(out, path, header, symbol, bar):
    with open(path, 'w') as f:
        f.write('// %s (tools/make_splash.py): 480 x 256 panel pixels, two a byte (the left one in the low\n' % header)
        f.write('// nibble), each bit 0 blue, 1 green, 2 red%s.\n#include <stdint.h>\n' % (', bit 3 set: drawn' if symbol else ''))
        if symbol: f.write('#include "machine_hook.h"\n')
        f.write('%sconst uint8_t %s[%d * %d / 2] = {\n' % ('static ' if symbol else '', 'px' if symbol else 'splash_logo', W, FB_ROWS))
        for row in out:
            n = [NIB[c] | (8 if symbol else 0) for c in row]
            f.write(''.join('0x%02x,' % (n[x] | n[x + 1] << 4) for x in range(0, W, 2)) + '\n')
        f.write('};\n')
        if symbol:
            f.write('const struct machine_boot_image %s = {%d, %d, px};\n' % (symbol, W, FB_ROWS))
    print('wrote', path)

def main():
    a = sys.argv[1:]
    font = a.pop(0)
    title = out = symbol = preview = None
    while a:
        k = a.pop(0)
        if k == '--screen': title, out, symbol = a.pop(0), a.pop(0), a.pop(0)
        elif k == '--preview': preview = a.pop(0)
    pic, bar = screen(font, title)
    if preview:
        im = Image.new('RGB', (W, FB_ROWS))
        im.putdata([PAL[c] for row in pic for c in row])
        im.resize((W, H), Image.NEAREST).save(preview)
    if out is None: sys.exit('--screen "<title>" <out.c> <symbol>')
    write(pic, out, 'the loading screen with "%s" on it' % title if title else 'the loading screen, the logo alone', symbol, bar)

main()

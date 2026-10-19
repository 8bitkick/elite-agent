#!/usr/bin/env python3
"""A show reel of a recorded career (run_sim FILM=dir FIGHTS=dir): its best battles, a witchspace fight, the
missions and the rank of Elite, replayed exactly from the film (sim/film) and cut together with title cards.

  tools/reel.py RUNDIR OUT.mp4 [--plan]       RUNDIR: journal.jsonl, fights/index.jsonl, film/ (snapshots, inputs.bin)

--plan prints the clips it would use (and their frames) without rendering. Needs ffmpeg (libx264, drawtext).
The emulator's ROMs and disc: $ELITE_EMU or the submodule; DISC=... for another disc (default the solid one).
"""
import json, os, subprocess, sys

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMU = os.environ.get('ELITE_EMU', os.path.join(TOP, 'rp2350-bbc-micro'))
DISC = os.environ.get('DISC', os.path.join(EMU, 'discs', 'elite-master-solid.ssd'))
FILM = os.environ.get('FILMBIN', os.path.join(TOP, 'sim', 'film'))
FONT = os.environ.get('FONT', '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')
W, H = 768, 640                 # the renderer's frame
CROP = os.environ.get('CROP', '528:512:120:72')     # the Elite screen inside it
SCALE = int(os.environ.get('SCALE', '2'))
FPS = 50
RANKS = [(0, 'Harmless'), (8, 'Mostly Harmless'), (16, 'Poor'), (32, 'Average'), (64, 'Above Average'),
         (128, 'Competent'), (512, 'Dangerous'), (2560, 'Deadly'), (6400, 'Elite')]


def rank(t): return [n for k, n in RANKS if t >= k][-1]
def hours(fr): return fr / FPS / 3600


def load(run):
    J = [json.loads(l) for l in open(os.path.join(run, 'journal.jsonl'))]
    F = [json.loads(l) for l in open(os.path.join(run, 'fights', 'index.jsonl'))]
    return J, F


def tally_at(J, frame):
    t = 0.0
    for e in J:
        if e['frame'] > frame: break
        v = e['e'].get('tally')
        if isinstance(v, (int, float)): t = v
    return t


NCAND = int(os.environ.get('NCAND', '25'))     # fights scanned for each kind of battle


def best_window(run, a, b, length):
    """The stretch of `length` frames in [a, b) with the most ships on screen, the closest (SCAN=1)."""
    out = subprocess.run([FILM, os.path.join(EMU, 'roms', 'mos3.20'), DISC, os.path.join(run, 'film'), str(a), str(b)],
                         env=dict(os.environ, SCAN='1'), capture_output=True, text=True).stdout.split('\n')
    rows = [l.split() for l in out if l]
    fr = [int(r[0]) for r in rows]
    sc = [float(r[3]) for r in rows]
    if len(sc) <= length:
        return a, sum(sc) / max(1, length)
    s = sum(sc[:length])
    bs, bi = s, 0
    for i in range(length, len(sc)):
        s += sc[i] - sc[i - length]
        if s > bs: bs, bi = s, i - length + 1
    return fr[bi], bs / length


def plan(run, J, F):
    clips = []
    fights = [f for f in F if f['end'] == 'over' and f.get('result')]
    def kills(f): return len(f['killed'])
    # the missions, from the mission byte (TP): 0 -> 1 briefing, 1 -> 3 the Constrictor dead, 3 -> 2 rewarded;
    # bits 2-3: 4 the plans briefing (... -> 6 then the plans delivered -> 10)
    # (NOMISSIONS=1: a reel of battles only)
    tp = [] if os.environ.get('NOMISSIONS') else [e for e in J if e['e'].get('event') == 'tp' and e['frame'] > 100]
    for e in tp:
        a, b = e['e']['from'], e['e']['to']
        if a & 3 == 0 and b & 3 == 1:
            clips.append(('Mission 1: the briefing', 'a stolen Constrictor, somewhere in galaxy 2', e['frame'] - 25, 450))
        if a & 3 == 1 and b & 3 == 3:
            f = max((f for f in fights if 'Constrictor' in f['killed'] and f['start'] < e['frame'] <= f['start'] + f['frames'] + 200),
                    key=lambda f: f['start'], default=None)
            end = f['start'] + f['frames'] if f else e['frame']
            clips.append(('Mission 1: the Constrictor', 'hunted down and destroyed', end - 700, 760))
        if a & 3 == 3 and b & 3 == 2:
            clips.append(('Mission 1: the reward', 'docked: the Navy pays up', e['frame'] - 25, 350))
        # (mission 2, bits 2-3 of TP: 1 briefed, 2 the plans collected at Ceerdi, 3 delivered to Birera)
        if a & 12 == 0 and b & 12 == 4:
            clips.append(('Mission 2: the briefing', 'Thargoid plans, galaxy 3', e['frame'] - 25, 350))
        if a & 12 == 4 and b & 12 == 8:
            clips.append(('Mission 2: the plans', 'collected at Ceerdi: take them to Birera', e['frame'] - 25, 400))
        if a & 12 == 8 and b & 12 == 12:
            clips.append(('Mission 2: the plans delivered', 'Birera: a naval energy unit for it', e['frame'] - 25, 400))
    # the best battles: of the fights with the most kills, the ones whose best stretch has the enemies
    # closest and most on screen (sim/film SCAN=1); that stretch is the clip
    def best(cands, n, length, title):
        scored = []
        for f in cands:
            s, sc = best_window(run, f['start'], f['start'] + f['frames'] + 100, length)
            scored.append((sc * (1 + 0.1 * kills(f)), s, f))
        scored.sort(key=lambda x: -x[0])
        out = []
        for sc, s, f in scored:
            if len(out) == n: break
            if any(abs(f['start'] - p['start']) < 100000 for _, p in out): continue
            out.append((s, f))
        for s, f in out:
            names = ', '.join(sorted(set(f['killed'])))
            clips.append((title(f), names, s, length))
    normal = sorted((f for f in fights if not f['witchspace'] and kills(f) >= 3 and f['frames'] < 4000),
                    key=lambda f: (-kills(f), f['frames']))[:NCAND]
    best(normal, int(os.environ.get('NBATTLE', '3')), int(os.environ.get('BLEN', '600')), lambda f: f'Battle: {kills(f)} kills')     # (NBATTLE, BLEN: a longer reel)
    ws = sorted((f for f in fights if f['witchspace'] and kills(f) >= 3 and f['frames'] < 4000), key=lambda f: (-kills(f), f['frames']))[:NCAND]
    best(ws, int(os.environ.get('NWS', '1')), 800, lambda f: f'Witchspace: {kills(f)} kills')
    # Elite: the fight in which the tally passed 6400
    e = next((e for e in J if e['e'].get('event') == 'rank' and e['e'].get('rank') == 'Elite'), None)
    if e:
        f = max((f for f in fights if f['start'] <= e['frame']), key=lambda f: f['start'], default=None)
        s = f['start'] + f['frames'] - 500 if f and e['frame'] - f['start'] < 20000 else e['frame'] - 500
        clips.append(('ELITE', 'the kill that made the rank: Right On, Commander!', s, 750))
        # (n < 0: a still of the status screen from the career's last snapshot, -n frames)
        if os.path.exists(PROBE):     # (sim/probe, from the solid-elite branch, renders it)
            clips.append(('Rating: Elite', 'the status screen at the end of the career', max(e['frame'], s + 751), -200))
    clips.sort(key=lambda c: c[2])
    return [(t, s, fr, n, tally_at(J, fr)) for t, s, fr, n in clips]


def card(path, lines, secs=1.8):
    w, h = [int(x) for x in CROP.split(':')[:2]]
    w, h = w * SCALE, h * SCALE
    vf = []
    for i, (txt, size, col) in enumerate(lines):
        txt = txt.replace(':', r'\:').replace("'", '')
        y = f'(h/2)+{int((i - (len(lines) - 1) / 2) * size * 1.6)}-th/2'
        vf.append(f"drawtext=fontfile={FONT}:text='{txt}':fontsize={size}:fontcolor={col}:x=(w-tw)/2:y={y}")
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i', f'color=c=black:s={w}x{h}:r={FPS}:d={secs}',
                    '-vf', ','.join(vf), '-c:v', 'libx264', '-preset', 'slow', '-crf', '24', '-pix_fmt', 'yuv420p', path], check=True)


PROBE = os.environ.get('PROBEBIN', os.path.join(TOP, 'sim', 'probe'))


def status_still(run, path, n):
    """The status screen (f8) from the career's last snapshot: out of any prompt first (DELETE twice,
    RETURN), then f8, through sim/probe; held for n frames. The only keys in the reel the career didn't press."""
    snaps = sorted(p for p in os.listdir(os.path.join(run, 'film')) if p.endswith('.snap'))
    tmp = path + '.state'
    with open(os.path.join(run, 'film', snaps[-1]), 'rb') as f, open(tmp, 'wb') as g:
        g.write(f.read()[24:])          # (FILM header: magic, size, the four joystick values)
    ppm = path + '.ppm'
    script = f'l {tmp}\nf 20\nk 0x59\nk 0x59\nk 0x49\nf 60\nk 0x76\nf 100\ns {ppm}\ns {ppm}\n'
    subprocess.run([PROBE, os.path.join(EMU, 'roms', 'mos3.20'), DISC], input=script, text=True, check=True, capture_output=True)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-loop', '1', '-r', str(FPS), '-i', ppm, '-frames:v', str(n),
                    '-vf', f'crop={CROP},scale=iw*{SCALE}:ih*{SCALE}:flags=neighbor', '-c:v', 'libx264', '-preset', 'slow',
                    '-crf', os.environ.get('CRF', '24'), '-pix_fmt', 'yuv420p', path], check=True)
    os.remove(tmp)
    os.remove(ppm)


def clip(run, path, frame, n):
    if n < 0:
        return status_still(run, path, -n)
    film = subprocess.Popen([FILM, os.path.join(EMU, 'roms', 'mos3.20'), DISC, os.path.join(run, 'film'), str(frame), str(frame + n - 1)],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS), '-i', '-',
                    '-vf', f'crop={CROP},scale=iw*{SCALE}:ih*{SCALE}:flags=neighbor', '-c:v', 'libx264', '-preset', 'slow',
                    '-crf', os.environ.get('CRF', '24'), '-pix_fmt', 'yuv420p', path], stdin=film.stdout, check=True)
    film.wait()


def main():
    run, out = sys.argv[1], sys.argv[2]
    J, F = load(run)
    clips = plan(run, J, F)
    for t, s, fr, n, ta in clips:
        print(f'{hours(fr):6.2f} h  frame {fr:9d}  {n / FPS:5.1f} s  {t}: {s}  (tally {ta:.0f}, {rank(ta)})')
    if '--plan' in sys.argv: return
    tmp = out + '.parts'
    os.makedirs(tmp, exist_ok=True)
    parts = []
    end = max(e['frame'] for e in J)
    card(f'{tmp}/00.mp4', [('ELITE', 72, 'yellow'), (os.environ.get('TITLE1', 'solid-fill BBC Master Elite'), 30, 'white'),
                           (os.environ.get('TITLE2', 'played by a C agent, curious mode, from Jameson to Elite'), 22, 'cyan'),
                           (f'{hours(end):.1f} game hours', 22, 'cyan')], 3.0)
    parts.append(f'{tmp}/00.mp4')
    for i, (t, s, fr, n, ta) in enumerate(clips, 1):
        card(f'{tmp}/{i:02d}a.mp4', [(t, 44, 'yellow'), (s, 22, 'white'), (f'game hour {hours(fr):.1f}  -  {rank(ta)}  -  tally {ta:.0f}', 22, 'cyan')])
        clip(run, f'{tmp}/{i:02d}b.mp4', fr, n)
        parts += [f'{tmp}/{i:02d}a.mp4', f'{tmp}/{i:02d}b.mp4']
    with open(f'{tmp}/list.txt', 'w') as f:
        f.writelines(f"file '{os.path.abspath(p)}'\n" for p in parts)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', f'{tmp}/list.txt', '-c', 'copy',
                    '-movflags', '+faststart', out], check=True)
    print(out, os.path.getsize(out) // 1024, 'KB')
    # where each clip starts in the reel
    def dur(p):
        return float(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', p],
                                    capture_output=True, text=True).stdout)
    at = dur(parts[0])
    with open(out.rsplit('.', 1)[0] + '.txt', 'w') as f:
        for i, (t, s, fr, n, ta) in enumerate(clips, 1):
            line = f'{int(at // 60)}:{at % 60:04.1f}  {t}' + (f' ({s})' if s else '') + f'  game hour {hours(fr):.1f}, {rank(ta)}'
            print(line)
            f.write(line + '\n')
            at += dur(parts[2 * i - 1]) + dur(parts[2 * i])
        f.write(f'{int(at // 60)}:{at % 60:04.1f}  end\n')


if __name__ == '__main__':
    main()

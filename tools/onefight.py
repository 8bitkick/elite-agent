#!/usr/bin/env python3
"""One whole fight from a filmed career (run_sim FILM=dir FIGHTS=dir), uncut, at real speed: rank the fights by size
x time over the whole fight with every kill on screen and close, then render one.

  onefight.py RUNDIR rank [N]            the best N candidates (default 10), from sim/film SCAN=2 over each fight with
                                         4+ kills: from first contact (a witchspace fight: from the jump in) to 2 s
                                         after the last kill
  onefight.py RUNDIR sheet ID OUT.png    a contact sheet of fight ID (a frame every 2 s)
  onefight.py RUNDIR render ID OUT.mp4   fight ID as an MP4 (H.264), captioned only with what's on screen

$ELITE_EMU for the ROMs, DISC for the disc (default the flicker-free one), FILMBIN for sim/film.
"""
import json, os, subprocess, sys

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMU = os.environ.get('ELITE_EMU', os.path.join(TOP, 'rp2350-bbc-micro'))
DISC = os.environ.get('DISC', os.path.join(EMU, 'discs', 'elite-master-flicker-free-sng47.ssd'))
FILM = os.environ.get('FILMBIN', os.path.join(TOP, 'sim', 'film'))
FONT = os.environ.get('FONT', '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')
CROP = os.environ.get('CROP', '528:512:120:72')
W, H, FPS = 768, 640, 50


def fights(run):
    return [json.loads(l) for l in open(os.path.join(run, 'fights', 'index.jsonl'))]


def scan(run, a, b):
    out = subprocess.run([FILM, os.path.join(EMU, 'roms', 'mos3.20'), DISC, os.path.join(run, 'film'), str(a), str(b)],
                         env=dict(os.environ, SCAN='2'), capture_output=True, text=True).stdout.split('\n')
    rows = []
    for l in out:
        v = l.split()
        if len(v) < 4: continue
        rows.append((int(v[0]), int(v[1]), float(v[2]), int(v[3]), [float(k[1:]) for k in v[4:] if k.startswith('K')]))
    return rows


def window(f):
    # (a witchspace fight starts as we come out of the jump: 5 s before, the jump itself)
    return f['start'] - (250 if f['witchspace'] else 50), f['start'] + f['frames'] + 100


def measure(run, f):
    a, b = window(f)
    rows = scan(run, a, b)
    kills = [(r[0], k) for r in rows for k in r[4]]
    if not kills: return None
    end = kills[-1][0] + 100                     # (2 s after the last kill)
    rows = [r for r in rows if r[0] <= end]
    n = len(rows)
    s20 = sum(1 for r in rows if r[2] >= 20) / FPS
    s40 = sum(1 for r in rows if r[2] >= 40) / FPS
    sint = sum(min(r[2], 100) for r in rows) / FPS
    # dead air: runs over 2 s with nothing 4 px or more drawn
    air, run_ = 0, 0
    for r in rows:
        if r[2] < 4: run_ += 1
        else:
            if run_ > 100: air += run_
            run_ = 0
    if run_ > 100: air += run_
    ks = [k for _, k in kills]
    return dict(id=f['id'], start=a, end=end, secs=n / FPS, ws=f['witchspace'], kills=len(ks), seen=sum(1 for k in ks if k > 0),
                close=sum(1 for k in ks if k >= 20), s20=s20, s40=s40, sint_min=sint / max(1e-9, n / FPS / 60), air=air / max(1, n),
                types=sorted(set(f.get('killed') or [])))


def rank(run, nbest):
    F = [f for f in fights(run) if f.get('end') == 'over' and len(f.get('killed') or []) >= 4]
    from multiprocessing import Pool
    with Pool(int(os.environ.get('NPROC', '2'))) as pool:
        M = [m for m in pool.starmap(measure, [(run, f) for f in F]) if m]
    # every kill close, most kills, big for long, little dead air
    M.sort(key=lambda m: -(m['close'] * 2 + m['seen'] + m['s20'] / 5 - 10 * m['air'] - 5 * (m['kills'] - m['seen'])))
    for m in M[:nbest]:
        print('id %(id)5d  %(secs)5.1f s  %(kills)2d kills, %(seen)2d on screen, %(close)2d at 20 px+  >=20 px %(s20)5.1f s  >=40 px %(s40)4.1f s  '
              'size-time %(sint_min)4.0f px.s/min  dead air %(air)3.0f%%  %(ws_)s  %(types)s  frames %(start)d-%(end)d' %
              dict(m, air=100 * m['air'], ws_='witchspace' if m['ws'] else 'normal space', types=', '.join(m['types'])))
    json.dump(M, open(os.path.join(run, 'onefight.json'), 'w'))


def frames_to(run, a, b, step, cmd):
    film = subprocess.Popen([FILM, os.path.join(EMU, 'roms', 'mos3.20'), DISC, os.path.join(run, 'film'), str(a), str(b), str(step)],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(max(1, FPS // step)),
                    '-i', '-'] + cmd, stdin=film.stdout, check=True)
    film.wait()


def get(run, fid):
    M = json.load(open(os.path.join(run, 'onefight.json')))
    return next(m for m in M if m['id'] == fid)


if __name__ == '__main__':
    run, what = sys.argv[1], sys.argv[2]
    if what == 'rank':
        rank(run, int(sys.argv[3]) if len(sys.argv) > 3 else 10)
    elif what == 'sheet':
        m = get(run, int(sys.argv[3]))
        cols = 8
        rows_ = -(-int(m['secs'] / 2 + 1) // cols)
        frames_to(run, m['start'], m['end'], 100, ['-vf', f'crop={CROP},scale=176:-1,tile={cols}x{rows_}', '-frames:v', '1', sys.argv[4]])
    elif what == 'render':
        m = get(run, int(sys.argv[3]))
        place = 'witchspace' if m['ws'] else 'a pirate pack'
        cap = f"{m['seen']} kills on screen, {place}, uncut, real speed"
        cap = cap.replace(':', r'\:')
        vf = (f"crop={CROP},scale=iw*2:ih*2:flags=neighbor,"
              f"drawtext=fontfile={FONT}:text='{cap}':fontsize=22:fontcolor=white:x=(w-tw)/2:y=h-th-8:enable='lt(t,4)'")
        frames_to(run, m['start'], m['end'], 1, ['-vf', vf, '-c:v', 'libx264', '-preset', 'slow', '-crf', os.environ.get('CRF', '23'),
                                                 '-pix_fmt', 'yuv420p', sys.argv[4]])

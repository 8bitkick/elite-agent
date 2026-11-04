#!/usr/bin/env python3
"""A chapter for the page: the commander's save (Master Elite's 256-byte commander block, from the mission byte on) as
it stood just after the player saved at a docking in a recorded career, from the film's replay of that moment
(sim/film RAMAT=). The block holds no name (the save's file name is the name) and no code of the game.

  chapter.py FILMDIR JOURNAL RANK out.cmdr        (the first save at or after reaching RANK)
"""
import json, os, subprocess, sys, tempfile
TOP = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
A_TP = 0x1234        # Master Elite SNG47 (cplayer/elite_ver.c): the commander block's first byte
film_dir, journal, rank, out = sys.argv[1:5]
J = [json.loads(l) for l in open(journal)]
at = next(j['frame'] for j in J if j['e'].get('event') == 'rank' and j['e'].get('rank') == rank)
save = next(j for j in J if j['e'].get('event') == 'save' and j['frame'] >= at)
ram = tempfile.mktemp(suffix='.bin')
subprocess.run([os.environ.get('FILM', f'{TOP}/sim/film'), f'{TOP}/rp2350-bbc-micro/roms/mos3.20',
                f'{TOP}/rp2350-bbc-micro/discs/elite-master-flicker-free-sng47.ssd', film_dir, str(save['frame']), str(save['frame'] + 200)],
               env=dict(os.environ, RAMAT=f"{save['frame'] + 100}:{ram}"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
r = open(ram, 'rb').read(); os.unlink(ram)
open(out, 'wb').write(r[A_TP:A_TP + 256])
print(json.dumps(dict(rank=rank, frame=save['frame'], hours=round(save['frame'] / 180000, 2), tally=save['e']['tally'], cash=save['e']['cash'],
                      galaxy=r[0x1234 + 0] and None, x=r[A_TP + 1], y=r[A_TP + 2])))

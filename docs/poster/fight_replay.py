#!/usr/bin/env python3
"""Replay one recorded encounter with the final rules, frame by frame, for the poster:
  tools/fight_replay.py logs/encounters/01044.snap [shots]  -> logs/poster/fight.json (+ PNGs)"""
import asyncio, json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import logging
logging.disable(logging.CRITICAL)
from sim.beeb import Beeb
from sim.simlink import SimLink
from elite.driver import Driver, Died
from elite import combat, pilot, ships as SH, keys as K

SNAP = sys.argv[1]
SHOTS = [int(x) for x in sys.argv[2].split(',')] if len(sys.argv) > 2 else []


async def main():
    b = Beeb()
    b.load(open(SNAP, 'rb').read())
    link = SimLink(beeb=b)
    for f in SHOTS:
        link.shoot_at[f] = f'logs/poster/fight-{f:04d}.png'
    d = Driver(link=link)
    p = pilot.Docker(d)
    f = combat.Fighter(d, p)
    f.record = False
    p.fighter = f
    d.in_flight = True
    frames = []
    orig_frame = d.frame

    async def frame():
        s = await orig_frame()
        ships = []
        for o in SH.ships(s.m):
            if o.slot >= 2 and o.is_ship:
                ships.append(dict(slot=o.slot, type=o.type, name=SH.type_name(o.type), x=o.x, y=o.y, z=o.z,
                                  nose=[round(v, 3) for v in o.nosev], e=o.energy, dead=bool(o.dead),
                                  hostile=bool(o.newb & 4)))
        frames.append(dict(f=link.frame, E=s.energy, fs=s.shields[0], as_=s.shields[1], spd=s.speed, heat=s.laser_temp,
                           fire=K.FIRE in link._down, view=s.space_view, tally=round(s.tally, 2), ships=ships))
        return s
    d.frame = frame
    t0 = d.snap().tally
    try:
        await f.engage(timeout=240)
    except Died:
        frames.append(dict(died=True))
    json.dump(dict(snap=SNAP, frames=frames, tally0=t0), open('logs/poster/fight.json', 'w'))
    s = d.s
    print('frames', len(frames), 'tally %.2f -> %.2f' % (t0, s.tally), 'energy', s.energy, 'shields', s.shields)
    kills = sorted({(sh['slot'], sh['name']) for fr in frames if 'ships' in fr for sh in fr['ships'] if sh['dead']})
    print('killed', kills)
    first_dead = next((fr['f'] for fr in frames if 'ships' in fr and any(sh['type'] == 31 and sh['dead'] for sh in fr['ships'])), None)
    print('constrictor dead at frame', first_dead, 'start frame', frames[0]['f'])


asyncio.run(main())

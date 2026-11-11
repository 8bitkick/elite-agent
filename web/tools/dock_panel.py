"""The docking network as a curated graph for the panels (the page's, the film's): the units shown in each column, and the
edges between adjacent columns (a path of non-zero weights whose intermediate units are not shown). load() builds it."""
import sys, collections, os
import numpy as np
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'wsp'))
from elite_wsp import dock_author as DA, dock_features as DF

_z = np.load(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'wsp', 'elite_dock.npz'))
L = DA.build(dict(zip(_z['param_names'], _z['param_values'])))      # (the weights as shipped: evolution's parameters)
W = [l.W() for l in L]


def load():
    """(built on import)"""
NAMES = [list(DF.NAMES)] + [l.names for l in L]       # column 0: the senses

INPUTS = [('pz', 'planet z'), ('alt', 'altitude'), ('st', 'stn seen'), ('sx', 'slot x'), ('sy', 'slot y'), ('sz', 'slot ahead'),
          ('ax', 'mark x'), ('ay', 'mark y'), ('bx', 'round x'), ('by', 'round y'), ('ex', 'escape x'), ('col', 'collision'),
          ('along', 'along axis'), ('lat', 'off axis'), ('dist', 'range'), ('dx', 'slot x drawn'), ('dy', 'slot y drawn'),
          ('lvl', 'slot tilt'), ('spin', 'stn spin'), ('nvo', 'axis off nose'), ('cen', 'stn off nose'), ('ths', 'slot off nose'),
          ('clear', 'slot clear'), ('jy', 'stick y'), ('up', 'was above'), ('final', 'was final'), ('commit', 'was commit')]
TESTS = [('side+', 'go round'), ('le2+', 'nearly lined'), ('le+', 'lined up'), ('ls+', 'still lined'), ('alf+', 'past 1200'),
         ('al0+', 'in front'), ('low+', 'low'), ('pjump+', 'planet ahead'), ('off10+', 'slot off 10°'), ('off8+', 'slot off 8°'),
         ('d400+', 'past 400'), ('d1000+', 'past 1000'), ('d2500+', 'past 2500'), ('d600+', 'inside 600'), ('d1800+', 'inside 1800'),
         ('fade+', 'hold on'), ('sq+', 'square'), ('cen+', 'centred'), ('fsh+', 'shield up'), ('trim+', 'trim +'), ('trim-', 'trim -'),
         ('sdx_a', 'spin·x'), ('turn', 'turn needed'), ('dstep+', 'past 1500')]
AIMS = [('cruise', 'aim: planet'), ('away', 'aim: away'), ('swerve', 'aim: escape'), ('slot', 'aim: slot'), ('appr', 'aim: mark'), ('side', 'aim: round'), ('step', 'aim: step')]
REGIME = [(a + 'z+o', lab) for a, lab in AIMS] + [('stp', 'step aside'), ('fin_on', 'final law'), ('lvl7+', 'level'), ('lvl20+', 'far off lvl'), ('enter', 'enter final'),
          ('stay', 'stay final'), ('okp', 'commit tests'), ('keep', 'keep commit'), ('jump', 'jump'), ('v_cruise', 'v cruise'), ('v_swerve', 'v swerve'),
          ('v_side', 'v round'), ('turn', 'turn room'), ('v_bands', 'v by range'), ('v_commit', 'v commit'), ('hold+', 'hold +'), ('hold-', 'hold -'),
          ('le2+', 'nearly lined')]
AIM = [('rx+', 'aim right'), ('rx-', 'aim left'), ('over+', 'past cap r'), ('over-', 'past cap l'), ('cy+', 'clock up'), ('cy-', 'clock down'),
       ('abscx', '|clock x|'), ('p_up', 'pitch up'), ('p_dn', 'pitch down'), ('up+', 'above'), ('back+', 'behind us'), ('live+', 'off the nose'),
       ('rf+', 'roll match +'), ('rf-', 'roll match -'), ('pf+', 'final pitch +'), ('pf-', 'final pitch -'), ('crawl', 'crawl'),
       ('v_appr0', 'v approach'), ('v_final', 'v final'), ('ok', 'commit ok'), ('v_cruise', 'v cruise'), ('v_side', 'v round'), ('fin_on', 'final law'), ('v_stepo', 'v step')]
RATES = [('roll_up+', 'roll r above'), ('roll_up-', 'roll l above'), ('roll_dn+', 'roll r below'), ('roll_dn-', 'roll l below'),
         ('roll_up+c', 'roll capped'), ('p_up', 'pitch up'), ('p_dn', 'pitch down'), ('b_up', 'pull up hard'), ('b_dn', 'push dn hard'),
         ('rf+', 'roll match +'), ('rf-', 'roll match -'), ('pf+', 'final pitch +'), ('pf-', 'final pitch -'), ('final_s', 'final'),
         ('commit_s', 'commit'), ('v_final', 'v final'), ('v_appr', 'v approach'), ('v_cruise', 'v cruise'), ('v_swerve', 'v swerve'),
         ('v_side', 'v round'), ('jump', 'jump'), ('up+', 'above')]
OUT = [('alpha', 'roll'), ('beta', 'pitch'), ('speed', 'throttle'), ('jump', 'jump'), ('final', 'final'), ('commit', 'commit')]
COLS = [('INPUTS', 0, INPUTS), ('TESTS', 1, TESTS), ('REGIME', 2, REGIME), ('THE AIM', 3, AIM), ('RATES', 4, RATES), ('OUTPUT', 5, OUT)]
for title, li, rows in COLS:
    for n, _ in rows: assert n in NAMES[li], (title, n)
IDX = [{n: i for i, n in enumerate(names)} for names in NAMES]
WHERE = {}
for c, (_, li, rows) in enumerate(COLS):
    for r, (n, _) in enumerate(rows): WHERE[(li, IDX[li][n])] = (c, r)
# the edges: from each shown unit, forward through non-shown units to the next column's shown units
LINKS = set()
for c, (_, li, rows) in enumerate(COLS[:-1]):
    for r, (n, _) in enumerate(rows):
        front = {(li, IDX[li][n])}
        seen = set(front)
        while front:
            nxt = set()
            for (l, u) in front:
                if l >= len(W): continue
                Wl = W[l][0]
                for o in np.nonzero(Wl[:, u])[0]:
                    key = (l + 1, int(o))
                    if key in WHERE:
                        if WHERE[key][0] == c + 1: LINKS.add((c, r, WHERE[key][1]))
                        continue
                    if key not in seen: seen.add(key); nxt.add(key)
            front = nxt
LINKS = sorted(LINKS)
if __name__ == '__main__':
    print(len(LINKS), collections.Counter(c for c, _, _ in LINKS))

"""The viewer's meter: what a fight is like to watch, from the lab's per-frame log (RRLOG), in seconds.

The show meter (tools/show.py) scores parts a fight should have. This one counts what the viewer sat through:
  closeup    the target drawn 24 px or more in the view shown
  action     the target 10-24 px, or the laser on a target of 10 px or more
  dot        the target under 10 px and nothing else: a dot, or a turn with the target behind
  dotfire    the laser on a target under 10 px: "firing at dots"
  empty      no target
  longest    the longest run of dot + empty, seconds
  passes     the target going by inside 600 (the range rate turning positive, the target leaving the front)
  look       seconds in a side or rear view with the target in it

  fun = 100 x (closeup + 0.5 action + 2 passes_s - 0.5 dotfire - 0.5 dot - empty) / seconds - 15 x died
     - 10 x max(0, longest - 10) / 10

so a fight of close-ups scores towards 100, a fight of dots towards -50, and a stretch of nothing beyond 10 s costs a
point a second. Deaths cost 15 (the viewer sees the death; the career the time).
"""


def measure(rows, died=0, kills_close=0):
    n = len(rows)
    if not n: return dict(fun=0.0, secs=0.0)
    closeup = action = dot = dotfire = empty = look = 0
    longest = run = 0
    passes = 0
    last = None
    for r in rows:
        px, fire, tt, tz = r["px"], r["fire"], r["tt"], r["tz"]
        if not tt: empty += 1; run += 1
        elif px >= 24: closeup += 1; run = 0
        elif px >= 10 or (fire and px >= 10): action += 1; run = 0
        else:
            dot += 1; run += 1
            if fire: dotfire += 1
        if r["view"] and tt and px >= 4: look += 1
        if last and last["td"] and r["td"] and last["td"] < 600 and r["td"] > last["td"] and last["tz"] and not tz: passes += 1
        longest = max(longest, run)
        last = r
    s = n / 50.0
    f = 100.0 * (closeup + 0.5 * action - 0.5 * dotfire - 0.5 * dot - empty) / n + 2.0 * passes * 100.0 / max(s, 1) / 10 - 15 * died - max(0.0, longest / 50.0 - 10) * 1.0
    return dict(fun=round(f, 1), secs=round(s, 1), closeup=round(closeup / 50, 1), action=round(action / 50, 1), dot=round(dot / 50, 1), dotfire=round(dotfire / 50, 1),
                empty=round(empty / 50, 1), longest=round(longest / 50, 1), passes=passes, look=round(look / 50, 1), died=died)


def line(m):
    return "fun %5.1f | %5.1f s: close-up %.1f  action %.1f  dot %.1f (firing at dots %.1f)  empty %.1f  longest nothing %.1f s  passes %d  looks %.1f s  died %d" % (
        m["fun"], m["secs"], m["closeup"], m["action"], m["dot"], m["dotfire"], m["empty"], m["longest"], m["passes"], m["look"], m["died"])

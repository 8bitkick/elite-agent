#!/usr/bin/env python3
"""The director's fight lab: watch a fight the camera flies, frame by frame, and judge it by eye.

Runs the fight lab (cplayer/lab) on a recorded fight start with the camera pilot (Curious mode as the page flies it),
dumps the screen every N frames, and writes a contact sheet of captioned frames (time, the target, its distance and
drawn size, the view, the laser), a timeline (view switches, bursts, target changes, passes, kills), the show meter's
verdict for the fight, and optionally a GIF.

  director.py FIGHT... [--every 100] [--cols 6] [--out DIR] [--weights written|C|file.wsp] [--gif] [--secs S]
  director.py --worst N SET [...]          the N lowest-scoring fights of a set (val|test), scored first

FIGHT is a .fight path or "cur13:00617" (WORK/rec/cur13/fights/00617.fight). --every 100 is a frame every 2 s.
Output: DIR/<run>_<id>/sheet.png, timeline.txt, fight.gif. Read the sheet; change the unit behind what you see.
"""
import json, os, re, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import es_modes as E

SHIPS = {1: "missile", 2: "Coriolis", 3: "escape pod", 4: "alloys", 5: "canister", 6: "boulder", 7: "asteroid", 8: "splinter", 9: "Shuttle",
         10: "Transporter", 11: "Cobra III", 12: "Python", 13: "Boa", 14: "Anaconda", 15: "hermit", 16: "Viper", 17: "Sidewinder", 18: "Mamba",
         19: "Krait", 20: "Adder", 21: "Gecko", 22: "Cobra I", 23: "Worm", 24: "Cobra III", 25: "Asp II", 26: "Python", 27: "Fer-de-lance",
         28: "Moray", 29: "Thargoid", 30: "Thargon", 31: "Constrictor", 32: "Cougar", 33: "Dodo"}
VIEWS = {0: "front", 1: "REAR", 2: "LEFT", 3: "RIGHT"}
RR = re.compile(r"RR (\d+) (\d+) b(\d) d(-?\d+) rr(-?\d+) s(\d+) be(-?\d+) al(-?\d+) cm(-?\d+) cb(-?\d+) jx(-?\d+) jy(-?\d+) v(\d) sh(-?\d) ws(\d) m(-?\d+) p(\d) w(\d) r(\d) fb(\d) ty(\d+) off(\d+) tt(\d+) td(\d+) px(\d+) tz(\d) f(\d) ta(\S+)")


def fight_path(f):
    if f.endswith(".fight"): return f
    run, n = f.split(":")
    return os.path.join(E.WORK, "rec", run, "fights", "%05d.fight" % int(n))


def run_fight(path, w, every, raw):
    env = E.cam_env(w)
    env.update(SHOTRAW=raw, SHOTEVERY=str(every), RRLOG="1", LOG="2", SHOTTAIL="0")
    fd, lst = tempfile.mkstemp(suffix=".lst"); os.close(fd); open(lst, "w").write(path + "\n")
    r = subprocess.run([E.LAB, lst], cwd=E.ROOT, env=env, capture_output=True, text=True, timeout=1200)
    os.unlink(lst)
    return (r.stdout + "\n" + r.stderr).splitlines()


def meter(lines):
    import show
    keep = [l for l in lines if l[:3] in ("SCN", "SHO", "DCA", "COL") or l.endswith(".fight") or ".fight " in l[:120]]
    fd, t = tempfile.mkstemp(suffix=".txt"); os.close(fd); open(t, "w").write("\n".join(keep) + "\n")
    F = show.load(t); os.unlink(t)
    return F[0] if F else None


def parse_rr(lines):
    rows = []
    for l in lines:
        m = RR.search(l)          # (the lab prefixes its lines with the machine's frame)
        if not m: continue
        g = m.groups()
        rows.append(dict(now=int(g[0]), behind=int(g[2]), nd=int(g[3]), speed=int(g[5]), view=int(g[12]), show=int(g[13]), ws=int(g[14]),
                         mode=int(g[15]), fb=int(g[19]), ty=int(g[20]), off=int(g[21]), tt=int(g[22]), td=int(g[23]), px=int(g[24]), tz=int(g[25]), fire=int(g[26])))
    return rows


def timeline(rows, frame0):
    """Events from the per-frame log: a line each for view switches, bursts, target changes, passes (the target going by
    at under 600 with the range rate turning), and the closest approach."""
    out, last = [], None
    burst = None
    tsec = lambda r: (r["now"] - frame0) / 50
    for r in rows:
        if last is None or r["view"] != last["view"]: out.append("%6.1fs  view %s" % (tsec(r), VIEWS.get(r["view"], r["view"])))
        if last is None or r["tt"] != last["tt"]: out.append("%6.1fs  target %s at %d" % (tsec(r), SHIPS.get(r["tt"], r["tt"]), r["td"]))
        if r["fire"] and burst is None: burst = r
        if not r["fire"] and burst is not None:
            out.append("%6.1fs  laser %.1f s at %s %d, %d px" % (tsec(burst), tsec(r) - tsec(burst), SHIPS.get(burst["tt"], burst["tt"]), burst["td"], burst["px"])); burst = None
        if last and last["td"] and r["td"] and last["td"] < 600 and r["td"] > last["td"] and last["tz"] and not r["tz"]:
            out.append("%6.1fs  the target goes by, %d" % (tsec(r), last["td"]))
        if last is not None and r["mode"] != last["mode"]: out.append("%6.1fs  mode %d (0 attack, 1 break off, 3 ram dodge, 4 evade)" % (tsec(r), r["mode"]))
        last = r
    if rows:
        c = min(rows, key=lambda r: r["td"] if r["td"] else 1e9)
        out.append("closest %d at %.1fs; %d frames logged, %.0f%% target ahead, %.0f%% laser on, %.0f%% in a side or rear view" % (
            c["td"], tsec(c), len(rows), 100 * sum(r["tz"] for r in rows) / len(rows), 100 * sum(r["fire"] for r in rows) / len(rows),
            100 * sum(r["view"] != 0 for r in rows) / len(rows)))
    return out


def sheet(frames, rows, frame0, every, cols, path, title):
    n = len(frames)
    if not n: return
    H, W = frames[0].shape[:2]
    sw, sh = W // 2, H // 2
    cap = 30
    rowsn = (n + cols - 1) // cols
    img = Image.new("RGB", (cols * (sw + 6), 28 + rowsn * (sh + cap + 6)), (24, 24, 24))
    d = ImageDraw.Draw(img)
    try: font = ImageFont.load_default(size=13); big = ImageFont.load_default(size=16)
    except TypeError: font = big = ImageFont.load_default()
    d.text((6, 6), title, fill=(230, 230, 230), font=big)
    by = {}
    for r in rows: by[r["now"] - frame0] = r
    for k, fr in enumerate(frames):
        x, y = (k % cols) * (sw + 6) + 3, 28 + (k // cols) * (sh + cap + 6)
        f = k * every
        r = by.get(f) or by.get(f - 1) or by.get(f + 1)
        im = Image.fromarray(fr).resize((sw, sh), Image.NEAREST)
        img.paste(im, (x, y))
        col = (240, 60, 60) if r and r["fire"] else (70, 160, 240) if r and r["view"] else (90, 90, 90)
        d.rectangle([x - 2, y - 2, x + sw + 1, y + sh + 1], outline=col, width=2)
        t = "%5.1fs" % (f / 50)
        c = t if not r else "%s  %s %d  %dpx  off %d  %s%s%s" % (t, SHIPS.get(r["tt"], r["tt"]) if r["tt"] else "no target", r["td"], r["px"], r["off"],
                                                              VIEWS[r["view"]] if r["view"] else "", "  LASER" if r["fire"] else "", "  behind" if not r["tz"] and r["tt"] else "")
        d.text((x, y + sh + 4), c, fill=(220, 220, 220), font=font)
    img.save(path)


def gif(frames, every, path, secs):
    keep = frames[: int(secs * 50 / every)] if secs else frames
    if not keep: return
    ims = [Image.fromarray(f).resize((f.shape[1] // 2, f.shape[0] // 2), Image.NEAREST).quantize(16) for f in keep]
    ims[0].save(path, save_all=True, append_images=ims[1:], duration=max(20, int(1000 * every / 50)), loop=0)


def direct(f, w, every, cols, out, want_gif, secs):
    path = fight_path(f)
    run, base = path.split("/")[-3], os.path.basename(path).split(".")[0]
    d = os.path.join(out, "%s_%s" % (run, base)); os.makedirs(d, exist_ok=True)
    raw = os.path.join(d, "frames.rgb")
    lines = run_fight(path, w, every, raw)
    m = re.search(r"SHOT (\d+)x(\d+)", "\n".join(lines))
    W, H = (int(m.group(1)), int(m.group(2))) if m else (768, 640)
    data = np.fromfile(raw, dtype=np.uint8)
    n = data.size // (W * H * 3)
    frames = list(data[: n * W * H * 3].reshape(n, H, W, 3))
    os.unlink(raw)
    rows = parse_rr(lines)
    frame0 = 0          # (the per-frame log counts the fight's frames from 1, as the shots do)
    res = next((l for l in lines if l.startswith(path)), "")
    F = meter(lines)
    verdict = "show %.1f  parts %s  died %d  kills %d/%d  %.0f s" % (F["score"], json.dumps({k: round(v, 2) for k, v in F["parts"].items()}), F["died"], F["kills"], F["start_n"], F["frames"] / 50) if F else "(no meter line)"
    title = "%s:%s  %s" % (run, base, verdict)
    sheet(frames, rows, frame0, every, cols, os.path.join(d, "sheet.png"), title)
    tl = [res, verdict] + timeline(rows, frame0)
    open(os.path.join(d, "timeline.txt"), "w").write("\n".join(tl) + "\n")
    if want_gif: gif(frames, every, os.path.join(d, "fight.gif"), secs)
    print(d); print("\n".join(tl))
    return d


if __name__ == "__main__":
    a = sys.argv[1:]
    opt = dict(every=100, cols=6, out=os.path.join(E.WORK, "director"), weights="written", gif=False, secs=0)
    fights, i = [], 0
    while i < len(a):
        if a[i] == "--worst":
            n, name = int(a[i + 1]), a[i + 2]; i += 3
            w = E.wfile("cam", E.DEF["cam"], tempfile.mkdtemp())
            f, info, F = E.evaluate("cam", w, E.read_set("cam", name), int(os.environ.get("NPROC", 4)))
            fights += [x["path"] for x in sorted(F, key=lambda x: x["score"])[:n]]
        elif a[i] in ("--every", "--cols", "--secs"): opt[a[i][2:]] = int(a[i + 1]); i += 2
        elif a[i] in ("--out", "--weights"): opt[a[i][2:]] = a[i + 1]; i += 2
        elif a[i] == "--gif": opt["gif"] = True; i += 1
        else: fights.append(a[i]); i += 1
    w = opt["weights"]
    if w == "written": w = E.wfile("cam", E.DEF["cam"], tempfile.mkdtemp())
    elif w.endswith(".json"): w = E.wfile("cam", {**E.DEF["cam"], **json.load(open(w))}, tempfile.mkdtemp())
    for f in fights:
        direct(f, w, opt["every"], opt["cols"], opt["out"], opt["gif"], opt["secs"])

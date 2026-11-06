#!/usr/bin/env python3
"""The viewer's meter (fun.py) over a set of fights: each fight in its own lab process with the per-frame log on.

  fun_set.py SET [written|C|file.wsp|params.json]     SET: val|test (the camera's sets), or a .lst
"""
import json, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import es_modes as E, director as D, fun


def one(path, env):
    fd, lst = tempfile.mkstemp(suffix=".lst"); os.close(fd); open(lst, "w").write(path + "\n")
    r = subprocess.run([E.LAB, lst], cwd=E.ROOT, env=env, capture_output=True, text=True, timeout=1200)
    os.unlink(lst)
    lines = (r.stdout + "\n" + r.stderr).splitlines()
    rows = D.parse_rr(lines)
    F = D.meter(lines)
    m = fun.measure(rows, F["died"] if F else 0)
    m["show"] = F["score"] if F else 0.0
    m["path"] = path
    return m


if __name__ == "__main__":
    name, w = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "written"
    paths = [l.strip() for l in open(name) if l.strip()] if name.endswith(".lst") else E.read_set("cam", name)
    if w == "written": w = E.wfile("cam", E.DEF["cam"], tempfile.mkdtemp())
    elif w.endswith(".json"): w = E.wfile("cam", {**E.DEF["cam"], **json.load(open(w))}, tempfile.mkdtemp())
    env = E.cam_env(None if w == "C" else w); env.update(RRLOG="1", LOG="2")
    with ThreadPoolExecutor(int(os.environ.get("NPROC", 4))) as ex:
        M = list(ex.map(lambda p: one(p, env), paths))
    n = len(M)
    mean = lambda k: sum(m[k] for m in M) / n
    print("%s %s: n %d  FUN %.1f  show %.1f | per fight: %.0f s, close-up %.1f s, action %.1f, dot %.1f (firing at dots %.1f), empty %.1f, longest nothing %.1f s, passes %.2f, looks %.1f s | died %d" % (
        name, sys.argv[2] if len(sys.argv) > 2 else "C", n, mean("fun"), mean("show"), mean("secs"), mean("closeup"), mean("action"), mean("dot"), mean("dotfire"), mean("empty"), mean("longest"), mean("passes"), mean("look"), sum(m["died"] for m in M)))
    if os.environ.get("LIST"):
        for m in sorted(M, key=lambda m: m["fun"]): print("%6.1f %s" % (m["fun"], fun.line(m)), m["path"].split("/rec/")[-1])

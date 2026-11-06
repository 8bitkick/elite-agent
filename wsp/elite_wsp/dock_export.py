"""Write the docking network: .npz (every unit named), .wsp (run time, WSPAPPW=file, sparse) and cplayer/dock_weights.h.
With evolve/es_dock_best.json present its parameters are used (DOCK_WRITTEN=1: the written defaults)."""
import os
import numpy as np
from .dock_author import build, PARAMS, OUTPUTS, SENSE_KEYS
from .dock_features import NAMES
from .export import ROOT, WSP


def arrays(layers): return [L.W() for L in layers]


def save_npz(layers, path, params):
    d = {}
    for i, L in enumerate(layers):
        W, b = L.W(); d[f"W{i}"], d[f"b{i}"] = W, b
        d[f"names{i}"], d[f"notes{i}"] = np.array(L.names), np.array(L.notes)
    d["param_names"] = np.array(list(params)); d["param_values"] = np.array([float(v) for v in params.values()], dtype=np.float64)
    np.savez(path, **d, features=np.array(NAMES), outputs=np.array(OUTPUTS))


def save_wsp(Wb, path, G):
    with open(path, "w") as f:
        f.write("WSPD %d\n" % len(Wb))
        f.write("SENSES " + " ".join("%.9g" % G[k] for k in SENSE_KEYS) + "\n")
        for W, b in Wb:
            nz = np.nonzero(W.ravel())[0]
            f.write(f"{W.shape[0]} {W.shape[1]} {len(nz)}\n")
            f.write(" ".join("%d %.9g" % (i, W.ravel()[i]) for i in nz) + "\n")
            f.write(" ".join("%.9g" % x for x in b) + "\n")


def fl(x):
    t = "%.9g" % x
    return t + ("" if ("." in t or "e" in t or "n" in t) else ".0") + "f"


def save_header(Wb, path, names, G, prefix="DOCK"):
    with open(path, "w") as f:
        f.write("// the docking network, compiled in (sparse): wsp/elite_wsp/dock_export.py writes this\n")
        f.write("// the senses' geometry: " + ", ".join(SENSE_KEYS) + "\n")
        f.write("static const double " + prefix + "_SENSES[] = {" + ", ".join("%.9g" % G[k] for k in SENSE_KEYS) + "};\n")
        f.write("#define " + prefix + "_LAYERS %d\n" % len(Wb))
        f.write("static const int " + prefix + "_SHAPE[] = {%s};\n" % ", ".join([str(Wb[0][0].shape[1])] + [str(W.shape[0]) for W, b in Wb]))
        for i, (W, b) in enumerate(Wb):
            nz = np.nonzero(W.ravel())[0]
            f.write(f"// layer {i}: " + ", ".join(names[i]) + "\n")
            f.write(f"static const int {prefix}_N{i} = {len(nz)};\n")
            f.write(f"static const int {prefix}_I{i}[] = {{" + (", ".join(str(int(j)) for j in nz) or "0") + "};\n")
            f.write(f"static const float {prefix}_V{i}[] = {{" + (", ".join(fl(W.ravel()[j]) for j in nz) or "0.0f") + "};\n")
            f.write(f"static const float {prefix}_B{i}[] = {{" + ", ".join(fl(x) for x in b) + "};\n")
        f.write("static const int *const " + prefix + "_I[] = {%s};\n" % ", ".join(f"{prefix}_I{i}" for i in range(len(Wb))))
        f.write("static const float *const " + prefix + "_V[] = {%s};\n" % ", ".join(f"{prefix}_V{i}" for i in range(len(Wb))))
        f.write("static const float *const " + prefix + "_B[] = {%s};\n" % ", ".join(f"{prefix}_B{i}" for i in range(len(Wb))))
        f.write("static const int " + prefix + "_N[] = {%s};\n" % ", ".join(f"{prefix}_N{i}" for i in range(len(Wb))))


EVOLVED = os.path.join(WSP, "evolve", "es_dock_best.json")


def main(params=None, tag=""):
    """The weights from the written defaults, under evolution's parameters (evolve/es_dock_best.json) when that exists."""
    if params is None and os.path.exists(EVOLVED) and not os.environ.get("DOCK_WRITTEN"):
        import json
        params = json.load(open(EVOLVED))
    P = dict(PARAMS, **(params or {}))
    layers = build(P); Wb = arrays(layers)
    save_npz(layers, os.path.join(WSP, f"elite_dock{tag}.npz"), P)
    G = {k: P[k] for k in SENSE_KEYS}
    save_wsp(Wb, os.path.join(WSP, f"elite_dock{tag}.wsp"), G)
    if not tag: save_header(Wb, os.path.join(ROOT, "cplayer", "dock_weights.h"), [L.names for L in layers], G)
    elif tag in MODES: save_header(Wb, os.path.join(ROOT, "cplayer", f"dock{tag}_weights.h"), [L.names for L in layers], G, MODES[tag])
    nz = sum(int((np.abs(W) > 0).sum()) for W, b in Wb)
    nb = sum(int((np.abs(b) > 0).sum()) for W, b in Wb)
    return f"shape {[Wb[0][0].shape[1]] + [W.shape[0] for W, b in Wb]}, {nz} non-zero weights, {nb} biases"


MODES = {"_opt": "DOCKO", "_cur": "DOCKC"}      # the other modes' sets (wsp/MODES.md): elite_dock_opt.*, cplayer/dock_opt_weights.h


if __name__ == "__main__":
    import sys, json
    if len(sys.argv) > 2 and sys.argv[1] in ("opt", "cur"):    # dock_export.py opt|cur params.json
        print(main(json.load(open(sys.argv[2])), "_" + sys.argv[1]))
    else:
        print(main())

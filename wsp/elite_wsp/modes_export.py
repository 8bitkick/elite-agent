"""Write the mode networks: elite_interceptor.* and elite_camera.* (.npz with names, .wsp for WSPINTW= / WSPWS=) and the
compiled-in headers cplayer/interceptor_weights.h and camera_weights.h (INT_* and CAM_*).
  python3 -m elite_wsp.modes_export [interceptor_params.json] [camera_params.json]
"""
import json, os, sys
from . import author_modes as M
from .export import arrays, save_npz, save_wsp, save_header, WSP, ROOT


def save_mode(stem, prefix, layers, params):
    save_npz(layers, os.path.join(WSP, stem + ".npz"), params)
    Wb = arrays(layers)
    save_wsp(Wb, os.path.join(WSP, stem + ".wsp"))
    h = os.path.join(ROOT, "cplayer", stem.replace("elite_", "") + "_weights.h")
    save_header(Wb, h, [L.names for L in layers])
    s = open(h).read().replace("WSP_", prefix + "_")
    open(h, "w").write(s)
    n = sum(W.size + b.size for W, b in Wb); nz = sum(int((W != 0).sum() + (b != 0).sum()) for W, b in Wb)
    return f"{stem}: {' -> '.join([str(Wb[0][0].shape[1])] + [str(W.shape[0]) for W, _ in Wb])}, {n} weights ({nz} non-zero), {os.path.basename(h)}"


if __name__ == "__main__":
    a = sys.argv[1:]
    pi = {**M.INTERCEPTOR, **(json.load(open(a[0])) if a else {})}
    pc = {**M.CAMERA, **(json.load(open(a[1])) if len(a) > 1 else {})}
    print(save_mode("elite_interceptor", "INT", M.build_interceptor(pi), pi))
    print(save_mode("elite_camera", "CAM", M.build_camera(pc), pc))

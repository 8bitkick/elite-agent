"""The forward pass: numpy only, the same arithmetic as c/wsp_pilot.c."""
import numpy as np


class Net:
    def __init__(self, path="elite_pilot.npz"):
        z = np.load(path)
        self.n = sum(1 for k in z.files if k.startswith("W"))
        self.W = [z[f"W{i}"] for i in range(self.n)]
        self.b = [z[f"b{i}"] for i in range(self.n)]
        self.names = [list(z[f"names{i}"]) for i in range(self.n)]
        self.notes = [list(z[f"notes{i}"]) for i in range(self.n)]
        self.features = list(z["features"])

    def forward(self, f, trace=False):
        h, acts = np.asarray(f, dtype=np.float32), []
        for i in range(self.n):
            h = self.W[i] @ h + self.b[i]
            if i < self.n - 1:
                h = np.maximum(h, 0)
            acts.append(h)
        return (h, acts) if trace else h

    def act(self, f):
        """The network's output as the game wants it: stick bytes, a fire flag, a speed."""
        jstx, jsty, fire, speed = self.forward(f)
        return int(np.clip(round(jstx), 1, 255)), int(np.clip(round(jsty), 1, 255)), fire > 0.5, int(round(speed))

    def size(self):
        return sum(W.size + b.size for W, b in zip(self.W, self.b))

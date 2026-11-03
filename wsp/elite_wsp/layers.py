"""Writing a ReLU network unit by unit, by name.

The primitives a ReLU network offers natively:

  relu(u), relu(-u)                      the two halves of a signed value; |u| is their sum
  sat(u) = relu(S*u) - relu(S*u - 1)     a comparison as a 0..1 step, decided within 1/S
  relu(v - M*(1 - g))                    v gated by boolean g, for 0 <= v < M
  relu(a + b - 1)                        AND
  a - relu(a - b)                        min(a, b)
"""
import numpy as np


class Layer:
    def __init__(self, inputs, relu=True):
        self.inputs, self.relu = list(inputs), relu
        self.names, self.rows, self.biases, self.notes = [], [], [], []

    def unit(self, name, weights, bias=0.0, note=""):
        assert name not in self.names, name
        row = np.zeros(len(self.inputs))
        for src, w in weights.items():
            row[self.inputs.index(src)] += w
        self.names.append(name); self.rows.append(row); self.biases.append(float(bias)); self.notes.append(note)
        return name

    def passthrough(self, *names):
        for n in names:
            self.unit(n, {n: 1.0}, note="carried forward")

    def sat_pair(self, name, weights, bias, note="", S=1000.0):
        """sat(u) for u = weights . inputs + bias: two units; read it back with sat_ref(name)."""
        w = {k: S * v for k, v in weights.items()}
        self.unit(name + "+", w, S * bias, note)
        self.unit(name + "-", w, S * bias - 1.0, note + " (the saturating half)")
        return name

    def W(self):
        return np.array(self.rows, dtype=np.float32).reshape(len(self.rows), len(self.inputs)), np.array(self.biases, dtype=np.float32)


def sat_ref(name, scale=1.0):
    return {name + "+": scale, name + "-": -scale}


def neg(d, k=1.0):
    return {n: -k * v for n, v in d.items()}


def add(*ds):
    out = {}
    for d in ds:
        for n, v in d.items():
            out[n] = out.get(n, 0.0) + v
    return out

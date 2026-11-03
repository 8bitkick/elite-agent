"""Read the weights back as the program they are: every unit, its note, its non-zero inputs."""
import sys
from .net import Net


def main(path="elite_pilot.npz"):
    net = Net(path)
    prev = net.features
    for i in range(net.n):
        kind = "output (linear)" if i == net.n - 1 else f"layer {i + 1} (relu)"
        print(f"\n== {kind}: {len(net.names[i])} units")
        for j, name in enumerate(net.names[i]):
            terms = [f"{w:+.4g}*{prev[k]}" for k, w in enumerate(net.W[i][j]) if w != 0]
            b = net.b[i][j]
            print(f"  {name:12s} = {' '.join(terms)}{f' {b:+.4g}' if b else ''}   # {net.notes[i][j]}")
        prev = net.names[i]
    print(f"\n{net.size()} weights, all written by hand")


if __name__ == "__main__":
    main(*sys.argv[1:])

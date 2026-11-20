"""Prints what a 909 render says: the trigger's pulses, the oscillator's pitch over time, the comparator's swing,
the envelopes' holds and decays, the output's level over time.

Usage: python -I tools/kick909/inspect.py file.csv [trigger seconds, default 0.005]
"""
import math
import sys


def load(path):
    with open(path, "r", encoding="ascii") as f:
        names = f.readline().strip().split(",")
        columns = {n: [] for n in names}
        for line in f:
            fields = line.strip().split(",")
            for n, x in zip(names, fields):
                columns[n].append(float(x))
    return columns


def crossings(t, v, level=0.0, rising=True):
    out = []
    for i in range(1, len(v)):
        a, b = v[i - 1] - level, v[i] - level
        if (rising and a < 0.0 <= b) or (not rising and a > 0.0 >= b):
            out.append(t[i - 1] + (t[i] - t[i - 1]) * a / (a - b))
    return out


def above(t, v, level):
    """The first span where v is above level: (start, end), or None."""
    start = None
    for i in range(len(v)):
        if start is None and v[i] > level:
            start = t[i]
        elif start is not None and v[i] <= level:
            return start, t[i]
    return (start, t[-1]) if start is not None else None


def main():
    c = load(sys.argv[1])
    t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0.005
    t = [x - t0 for x in c["time"]]
    rate = 1.0 / (c["time"][1] - c["time"][0])

    def at(name, seconds):
        i = min(len(t) - 1, max(0, int(round((seconds + t0) * rate))))
        return c[name][i]

    def span(name, level):
        s = above(t, c[name], level)
        return "none" if s is None else "{:.2f} to {:.2f} ms".format(1e3 * s[0], 1e3 * s[1])

    print("rest: n1 {:.3f}  n4 {:.3f}  cv {:.4f}  c8 {:.4f}  acc {:.3f}  out {:.4f}".format(
        at("n1", -0.001), at("n4", -0.001), at("cv", -0.001), at("c8", -0.001), at("acc", -0.001), at("out", -0.001)))
    print("n1 below 5 V:", "none" if above(t, [-x for x in c["n1"]], -5.0) is None else
          "{:.2f} to {:.2f} ms".format(*[1e3 * x for x in above(t, [-x for x in c["n1"]], -5.0)]))
    print("n4 (the +15 V pulse) above 7.5 V:", span("n4", 7.5), " peak {:.3f} V".format(max(c["n4"])))
    print("n7c (Q7, the reset) above 5 V:", span("n7c", 5.0))
    print("c9 peak {:.3f} V at {:.2f} ms".format(max(c["c9"]), 1e3 * t[c["c9"].index(max(c["c9"]))]))
    print("cmp swing {:.3f} / {:.3f} V; nx {:.3f} / {:.3f} V".format(
        max(c["cmp"][int(0.05 * rate):]), min(c["cmp"][int(0.05 * rate):]),
        max(c["nx"][int(0.1 * rate):]), min(c["nx"][int(0.1 * rate):])))
    print("tri {:.3f} / {:.3f} V after 100 ms".format(max(c["tri"][int(0.1 * rate):]), min(c["tri"][int(0.1 * rate):])))
    print("c7 peak {:.3f} V; c8 peak {:.4f} V; c12 peak {:.3f} V; e1 peak {:.3f} V; nk peak {:.3f} V".format(
        max(c["c7"]), max(c["c8"]), max(c["c12"]), max(c["e1"]), max(c["nk"])))
    peak8 = max(c["c8"])
    print("c8 above 95 % of its peak:", span("c8", 0.95 * peak8), "; above 10 %:", span("c8", 0.1 * peak8))

    ups = crossings(t, c["tri"], 0.0, True)
    downs = crossings(t, c["tri"], 0.0, False)
    edges = sorted(ups + downs)
    print("pitch from the triangle's zero crossings (half periods):")
    for a, b in zip(edges, edges[1:]):
        if a > 0.0 and (len(edges) < 40 or a < 0.06 or abs(a - round(a / 0.05) * 0.05) < 0.01):
            print("  {:7.2f} ms  {:7.2f} Hz".format(1e3 * 0.5 * (a + b), 0.5 / (b - a)))

    window = int(0.01 * rate)
    print("out peak per 10 ms:")
    line = []
    for w in range(0, len(t) // window):
        seg = c["out"][w * window:(w + 1) * window]
        if t[w * window] < -0.0001:
            continue
        line.append("{:.0f}:{:.3f}".format(1e3 * t[w * window], max(abs(x) for x in seg)))
    print("  " + " ".join(line))


if __name__ == "__main__":
    main()

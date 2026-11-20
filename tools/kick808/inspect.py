"""Prints what a render says about the drum: values at set times, the pitch over time, the level over time.

Usage: python -I tools/kick808/inspect.py file.csv [column]
"""
import math
import sys


def load(path, column):
    times, values = [], []
    with open(path, "r", encoding="ascii") as f:
        names = f.readline().strip().split(",")
        index = names.index(column)
        for line in f:
            fields = line.strip().split(",")
            times.append(float(fields[0]))
            values.append(float(fields[index]))
    return times, values


def main():
    path = sys.argv[1]
    column = sys.argv[2] if len(sys.argv) > 2 else "vout"
    t, v = load(path, column)
    rate = 1.0 / (t[1] - t[0])

    def at(seconds):
        i = min(len(v) - 1, int(round(seconds * rate)))
        return v[i]

    print("values (V):", " ".join("{}ms={:.3f}".format(ms, at(ms / 1000.0)) for ms in (0.5, 1, 2, 4, 6, 11, 18)))

    crossings = []
    for i in range(1, len(v)):
        if v[i - 1] < 0.0 <= v[i]:
            crossings.append(t[i - 1] + (t[i] - t[i - 1]) * (-v[i - 1]) / (v[i] - v[i - 1]))
    pitch = []
    for a, b in zip(crossings, crossings[1:]):
        pitch.append(((a + b) / 2.0, 1.0 / (b - a)))
    print("pitch (Hz) by upward crossing:", " ".join("{:.1f}ms:{:.2f}".format(m * 1000, f) for m, f in pitch[:14]))

    window = int(rate * 0.02)
    levels = []
    for start in range(0, len(v) - window, window):
        peak = max(abs(x) for x in v[start:start + window])
        levels.append(peak)
    print("peak per 20 ms (V):", " ".join("{:.3f}".format(p) for p in levels[:15]))
    if len(levels) > 2 and levels[1] > 0 and levels[-1] > 0:
        span = (len(levels) - 2) * 0.02
        print("decay rate after 20 ms: {:.1f} dB/s".format(20.0 * math.log10(levels[-1] / levels[1]) / span))


if __name__ == "__main__":
    main()

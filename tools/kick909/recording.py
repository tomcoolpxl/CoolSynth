"""Measures recordings of a real TR-909 bass drum (analysis only; the Rob Roy set, outside git in
tools/references/robroy-909): the pitch over time from zero crossings, the level per 10 ms.

Usage: python -I tools/kick909/recording.py file.wav [more.wav ...]
"""
import struct
import sys
import wave


def read(path):
    with wave.open(path, "rb") as w:
        channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        data = w.readframes(frames)
    if width != 2:
        raise SystemExit("16-bit only")
    samples = struct.unpack("<{}h".format(frames * channels), data)
    return [s / 32768.0 for s in samples[::channels]], rate


def main():
    for path in sys.argv[1:]:
        x, rate = read(path)
        peak = max(abs(v) for v in x)
        start = next(i for i, v in enumerate(x) if abs(v) > 0.05 * peak)
        edges = []
        for i in range(start + 1, len(x)):
            a, b = x[i - 1], x[i]
            if (a < 0.0 <= b) or (a > 0.0 >= b):
                edges.append((i - 1 + a / (a - b)) / rate)
        t0 = start / rate
        print(path.split("/")[-1].split("\\")[-1], "{} Hz, {:.0f} ms, peak {:.3f}".format(rate, 1e3 * len(x) / rate, peak))
        line = []
        for a, b in zip(edges, edges[1:]):
            mid = 0.5 * (a + b) - t0
            if b - a > 0.0005:
                line.append("{:.1f}:{:.1f}".format(1e3 * mid, 0.5 / (b - a)))
        print("  half periods (ms:Hz):", " ".join(line[:60]))
        window = int(0.01 * rate)
        levels = []
        for w in range(start // window, len(x) // window):
            seg = x[w * window:(w + 1) * window]
            levels.append("{:.0f}:{:.3f}".format(1e3 * (w * window - start) / rate, max(abs(v) for v in seg)))
        print("  peak per 10 ms:", " ".join(levels))


if __name__ == "__main__":
    main()

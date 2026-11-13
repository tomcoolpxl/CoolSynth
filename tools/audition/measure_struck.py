"""Measure struck or plucked notes of a recorded instrument: pitch, decay, and the partials.

For every file in a folder (optionally only those whose name contains all the given words):
    f0, the 60 dB time of the whole note and of the fundamental, and the strongest partials in the
    first 200 ms as ratio to f0 with level in dB below the strongest and their own 60 dB times.

usage: python -I measure_struck.py <folder> [word ...]
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

folder, words = sys.argv[1], [w.lower() for w in sys.argv[2:]]


def onset_trim(x, rate):
    level = np.abs(x)
    peak = float(level.max())
    above = np.nonzero(level > peak * 0.02)[0]
    return x[max(0, int(above[0]) - int(0.002 * rate)):] if len(above) else x


rows = []
for name in sorted(os.listdir(folder)):
    if not name.lower().endswith(".wav") or not all(w in name.lower() for w in words):
        continue
    try:
        rate, data = audiolib.read_wav(os.path.join(folder, name), max_seconds=12.0)
    except ValueError:
        continue
    x = onset_trim(audiolib.mono(data), rate)
    if len(x) < rate // 4:
        continue
    f0 = audiolib.fundamental(x, rate, 0.02, 0.4)
    if f0 <= 0.0:
        continue
    times, levels = audiolib.envelope_db(x, rate)
    whole = audiolib.decay_t60(times, levels)
    fundamental_t60 = audiolib.partial_t60(x, rate, f0)

    frequencies, magnitudes = audiolib.spectrum(x, rate, 0.01, 0.2)
    top = float(magnitudes.max())
    inner = magnitudes[1:-1]
    maxima = np.nonzero((inner > magnitudes[:-2]) & (inner >= magnitudes[2:]) & (inner > top * 0.004))[0] + 1
    partials = []
    for i in maxima:
        hz, level = audiolib.peak_near(frequencies, magnitudes, float(frequencies[i]), 20.0)
        if hz < 0.6 * f0 or hz > min(20000.0, 40.0 * f0):
            continue
        if partials and hz / partials[-1][0] < 1.06:  # two bins of one peak
            if level > partials[-1][1]:
                partials[-1] = (hz, level)
            continue
        partials.append((hz, level))
    partials = sorted(partials, key=lambda p: -p[1])[:7]
    partials.sort()
    described = []
    for hz, level in partials:
        t60 = audiolib.partial_t60(x, rate, hz, last=min(len(x) / rate - 0.15, 6.0))
        described.append(f"{hz / f0:5.2f}x {20.0 * math.log10(max(level, 1.0e-12) / top):5.1f}dB {t60:4.2f}s")
    rows.append((audiolib.hz_to_midi(f0), name, f0, whole, fundamental_t60, described))

rows.sort()
print(f"{len(rows)} notes from {folder}")
print("note   f0      T60 all  T60 f0   partials (ratio, level, T60)")
for midi, name, f0, whole, fundamental_t60, described in rows:
    print(f"{audiolib.note_name(midi):4s} {f0:7.1f} {whole:7.2f} {fundamental_t60:7.2f}   " + " | ".join(described))
    print("       " + name)

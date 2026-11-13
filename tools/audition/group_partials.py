"""List the strongest low partials of groups of recordings: for sets whose files carry no note names.

Files are grouped by the part of their name before a separator (default "_"), their spectra
over the first two seconds after the onset are added up, and the strongest peaks between 40
and 1500 Hz are printed with their note and level. Used to find the pitch of each timpani
drum, where a pitch detector jumps between the partials.

usage: python -I group_partials.py <folder> [separator] [name-filter]
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

folder = sys.argv[1]
separator = sys.argv[2] if len(sys.argv) > 2 else "_"
only = sys.argv[3].lower() if len(sys.argv) > 3 else ""

SIZE = 1 << 18
groups = {}
for name in sorted(os.listdir(folder)):
    if not name.lower().endswith(".wav") or (only and only not in name.lower()):
        continue
    groups.setdefault(name.split(separator)[0], []).append(name)

for group, names in groups.items():
    total = None
    rate = 44100
    for name in names:
        rate, x = audiolib.read_wav(os.path.join(folder, name))
        mono = x[:, :2].mean(axis=1)
        level = np.abs(mono)
        above = np.nonzero(level > level.max() * 0.05)[0]
        start = int(above[0]) + int(0.05 * rate) if len(above) else 0
        body = mono[start:start + int(2.0 * rate)]
        if len(body) < int(0.1 * rate):
            continue
        spectrum = np.abs(np.fft.rfft(body * np.hanning(len(body)), SIZE)) ** 2
        spectrum /= spectrum.max()
        total = spectrum if total is None else total + spectrum
    if total is None:
        continue
    hz = np.fft.rfftfreq(SIZE, 1.0 / rate)
    low, high = np.searchsorted(hz, 40.0), np.searchsorted(hz, 1500.0)
    band = total[low:high]
    peaks = []
    taken = np.zeros(len(band), dtype=bool)
    for index in np.argsort(band)[::-1]:
        if taken[index]:
            continue
        f = hz[low + index]
        width = int(max(3, (f * 0.04) / (rate / SIZE)))  # nothing within two thirds of a semitone of a stronger peak
        taken[max(0, index - width):index + width] = True
        peaks.append((f, band[index]))
        if len(peaks) == 7:
            break
    strongest = peaks[0][1]
    peaks.sort()
    text = "  ".join(f"{f:7.1f} Hz {audiolib.note_name(int(round(audiolib.hz_to_midi(f)))):>4s}{(audiolib.hz_to_midi(f) - round(audiolib.hz_to_midi(f))) * 100:+4.0f}c {10 * np.log10(p / strongest):5.1f}dB"
                     for f, p in peaks)
    print(f"{group:14s} {len(names):3d} files | {text}", flush=True)

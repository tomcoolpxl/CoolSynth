"""Compare two recordings of one note, harmonic by harmonic over time: a classic FM patch against its rebuild.

For each file: the levels of the first twenty harmonics in short windows at several times after
the onset, in dB relative to the strongest harmonic of the first window, and the overall level
at each time. With two files: the differences (second minus first) and their middle value.

usage: python -I fm_compare.py <midi-note> <reference.wav|flac> [mine.wav] [onset-seconds-in-mine]
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

TIMES = [0.02, 0.1, 0.3, 0.8, 2.0, 4.0]
HARMONICS = 20


def read(path):
    if path.lower().endswith(".wav"):
        return audiolib.read_wav(path)
    with tempfile.TemporaryDirectory() as scratch:
        wav = os.path.join(scratch, "x.wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", path, "-c:a", "pcm_s24le", wav], check=True)
        return audiolib.read_wav(wav)


def measure(path, note, start_at=None):
    rate, x = read(path)
    mono = x.mean(axis=1)
    f0 = audiolib.midi_to_hz(note)
    if start_at is None:
        level = np.abs(mono)
        start = int(np.argmax(level > level.max() * 0.02))
    else:
        start = int(start_at * rate)
    window = int(max(0.04, 6.0 / f0) * rate)
    rows = []
    for t in TIMES:
        at = start + int(t * rate)
        if at + window > len(mono):
            rows.append(None)
            continue
        segment = mono[at:at + window]
        size = 1 << int(np.ceil(np.log2(window * 8)))
        spectrum = np.abs(np.fft.rfft(segment * np.hanning(window), size)) / (window / 4.0)
        bin_hz = rate / size
        levels = []
        for k in range(1, HARMONICS + 1):
            low, high = int((k - 0.4) * f0 / bin_hz), int((k + 0.4) * f0 / bin_hz) + 1
            levels.append(float(spectrum[low:high].max()) if high < len(spectrum) else 0.0)
        rows.append((np.array(levels), float(np.sqrt(np.mean(segment ** 2)))))
    return rows


def db(x):
    return 20.0 * np.log10(np.maximum(x, 1.0e-9))


def show(name, rows):
    reference = max(rows[0][0].max(), 1.0e-9)
    start_rms = max(rows[0][1], 1.0e-9)
    print(f"== {name}")
    out = []
    for t, row in zip(TIMES, rows):
        if row is None:
            out.append(None)
            continue
        relative = db(row[0] / reference)
        out.append((relative, float(db(row[1] / start_rms))))
        print(f"  {t:4.2f} s  level {out[-1][1]:6.1f} dB | " + " ".join(f"{v:4.0f}" for v in relative))
    return out


if __name__ == "__main__":
    note = int(sys.argv[1])
    first = show(os.path.basename(sys.argv[2]), measure(sys.argv[2], note))
    if len(sys.argv) > 3:
        onset = float(sys.argv[4]) if len(sys.argv) > 4 else None
        second = show(os.path.basename(sys.argv[3]), measure(sys.argv[3], note, onset))
        print("== second minus first (harmonics the first has above -50 dB)")
        gaps, level_gaps = [], []
        for t, a, b in zip(TIMES, first, second):
            if a is None or b is None:
                continue
            cells = []
            for ra, rb in zip(a[0], b[0]):
                if ra > -50.0:
                    cells.append(f"{rb - ra:+4.0f}")
                    gaps.append(abs(rb - ra))
                else:
                    cells.append("   .")
            level_gaps.append(abs(b[1] - a[1]))
            print(f"  {t:4.2f} s  level {b[1] - a[1]:+6.1f} dB | " + " ".join(cells))
        print(f"middle gap: harmonics {np.median(gaps):.1f} dB, level over time {np.median(level_gaps):.1f} dB")

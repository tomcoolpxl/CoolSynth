"""Look at a rendered file: pitch and level in given windows, and the largest sample-to-sample step.

usage: python -I check_render.py <file.wav> <start:length> [<start:length> ...]      (seconds)
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

rate, data = audiolib.read_wav(sys.argv[1])
x = audiolib.mono(data)
print(f"{sys.argv[1]}: {len(x) / rate:.2f} s, peak {20 * math.log10(float(np.abs(data).max()) + 1e-12):.1f} dB")
for window in sys.argv[2:]:
    start, length = (float(v) for v in window.split(":"))
    a, b = int(start * rate), int((start + length) * rate)
    part = x[a:b]
    hz = audiolib.fundamental(x, rate, start, length)
    level = 20.0 * math.log10(float(np.sqrt(np.mean(part * part))) + 1.0e-12)
    step = float(np.abs(np.diff(part)).max()) if len(part) > 1 else 0.0
    typical = float(np.percentile(np.abs(np.diff(part)), 99.9)) if len(part) > 1 else 0.0
    note = audiolib.note_name(audiolib.hz_to_midi(hz)) if hz > 0 else "-"
    cents = 100.0 * (audiolib.hz_to_midi(hz) - round(audiolib.hz_to_midi(hz))) if hz > 0 else 0.0
    print(f"  {start:5.2f}-{start + length:5.2f} s: {hz:8.2f} Hz {note:>4} {cents:+6.1f} c  rms {level:6.1f} dB  largest step {step:.4f} (99.9th percentile {typical:.4f})")

"""Check the Conservatory demonstrations by measurement: are the notes the right notes, and do they get louder?

make_demos.py plays, for each style, up to eight notes climbing through its recorded range from
velocity 45 to 120, then a chord. For each climbing note this measures the pitch (from the
partials near where the note should be) and the peak level, and reports per style: how far
the notes are off in cents (worst and middle), how many could not be measured, the level of
the first and the last note (the dynamics), and the peak of the whole file.

usage: python -I check_demos.py <catalogue.txt> <library-dir> <audition-dir> [only-substring]
"""
import json
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

catalogue_path, library, audition = sys.argv[1:4]
only = sys.argv[4].lower() if len(sys.argv) > 4 else ""


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


def cents_off(mono, rate, note, partials):
    """How far the sound is above `note`, in cents, from the strongest of its first partials near where the note puts them."""
    f0 = audiolib.midi_to_hz(note)
    if len(mono) < int(0.08 * rate):
        return None
    size = 1 << int(np.ceil(np.log2(len(mono) * 4)))
    spectrum = np.abs(np.fft.rfft(mono * np.hanning(len(mono)), size))
    bin_hz = rate / size
    best = None
    for k in range(1, partials + 1):
        target = k * f0
        if target > 0.45 * rate:
            break
        low, high = int(target * 2 ** (-80 / 1200) / bin_hz), int(np.ceil(target * 2 ** (80 / 1200) / bin_hz))
        if high - low < 4:
            continue
        i = low + int(np.argmax(spectrum[low:high + 1]))
        if i <= low or i >= high:
            continue
        if best is None or spectrum[i] > best[1]:
            best = (1200.0 * np.log2(i * bin_hz / target), spectrum[i])
    return None if best is None or best[1] < spectrum.max() * 0.05 else float(best[0])


sustains, sounds = {}, {}
for line in open(catalogue_path, encoding="utf-8"):
    line = line.split("#", 1)[0].strip()
    if line:
        parts = [p.strip() for p in line.split("|")]
        sustains[(parts[1], parts[2])] = parts[3].lower() == "yes"
        options = dict(word.split("=", 1) for word in parts[5].split()) if len(parts) > 5 else {}
        sounds[(parts[1], parts[2])] = int(options.get("sounds", 0))

index = json.load(open(os.path.join(audition, "conservatory", "index.json"), encoding="utf-8"))
with tempfile.TemporaryDirectory() as scratch:
    for entry in index:
        label = f"{entry['family']} / {entry['instrument']} / {entry['style']}"
        if only and only not in label.lower():
            continue
        manifest = os.path.join(library, slug(entry["family"]), slug(entry["instrument"]), slug(entry["style"]), "manifest.txt")
        rows = [[p.strip() for p in row.split("|")] for row in open(manifest, encoding="utf-8") if row.strip() and not row.startswith("#")]
        roots = sorted({int(r[1]) for r in rows if len(r) >= 15})
        climb = sorted({roots[round(i * (len(roots) - 1) / 7)] for i in range(8)}) if len(roots) > 1 else [roots[0]]
        held = sustains.get((entry["instrument"], entry["style"]), False)
        step, hold = (1.1, 1.0) if held else (0.55, 0.5)
        wav = os.path.join(scratch, "demo.wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", os.path.join(audition, "conservatory", entry["file"]), wav], check=True)
        rate, x = audiolib.read_wav(wav)
        mono = x.mean(axis=1)
        offs, levels, unmeasured = [], [], 0
        for i, note in enumerate(climb):
            start = int((i * step + (0.25 if held else 0.04)) * rate)
            segment = mono[start:int((i * step + hold * 0.9) * rate)]
            levels.append(20.0 * np.log10(float(np.abs(x[int(i * step * rate):int((i * step + hold) * rate)]).max()) + 1.0e-9))
            cents = cents_off(segment, rate, note + sounds.get((entry["instrument"], entry["style"]), 0), 4)
            if only:  # a closer look at the styles asked for: every note
                print(f"    {audiolib.note_name(note):4s} {'unmeasured' if cents is None else f'{cents:+6.1f} cents'}  peak {levels[-1]:6.1f} dB")
            if cents is None:
                unmeasured += 1
            else:
                offs.append(cents)
        peak = 20.0 * np.log10(float(np.abs(x).max()) + 1.0e-9)
        worst = max((abs(c) for c in offs), default=float("nan"))
        middle = float(np.median(offs)) if offs else float("nan")
        flag = "  <-- LOOK" if worst > 25.0 or unmeasured > len(climb) // 2 or peak > -0.5 or levels[-1] < levels[0] - 1.0 else ""
        print(f"{label:46s} notes {len(climb)}  off: middle {middle:+5.1f} worst {worst:5.1f} cents, {unmeasured} unmeasured  "
              f"first {levels[0]:6.1f} dB  last {levels[-1]:6.1f} dB  file peak {peak:5.1f} dB{flag}", flush=True)

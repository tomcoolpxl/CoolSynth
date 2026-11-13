"""Show how loud each recording of the built library's styles is.

For every style (or those whose label contains the filter) prints the manifest's gain and, per
dynamic layer, the spread of the recordings' peaks: lowest, median, highest, in dB below full
scale, before the gain. With "detail" as a fifth argument, prints every recording.

usage: python -I style_levels.py <catalogue.txt> <library-dir> [filter] [detail]
"""
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

catalogue_path, library = sys.argv[1:3]
only = sys.argv[3].lower() if len(sys.argv) > 3 else ""
detail = len(sys.argv) > 4


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


def db(x):
    return 20.0 * np.log10(max(float(x), 1.0e-9))


with tempfile.TemporaryDirectory() as scratch:
    for line in open(catalogue_path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        family, instrument, style, _, _ = [part.strip() for part in line.split("|")]
        label = f"{family} / {instrument} / {style}"
        if only and only not in label.lower():
            continue
        folder = os.path.join(library, slug(family), slug(instrument), slug(style))
        gain = 1.0
        rows = []
        for row in open(os.path.join(folder, "manifest.txt"), encoding="utf-8"):
            parts = [p.strip() for p in row.split("|")]
            if row.startswith("gain"):
                gain = float(parts[1])
            elif row.strip() and not row.startswith("#"):
                rows.append(parts)
        by_layer = {}
        for parts in rows:
            decoded = os.path.join(scratch, "level.wav")
            subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", os.path.join(folder, parts[0]), "-c:a", "pcm_s24le", decoded], check=True)
            rate, x = audiolib.read_wav(decoded)
            peak = float(np.abs(x).max())
            body = x[: int(1.0 * rate)]
            rms = float(np.sqrt(np.mean(body ** 2)))
            by_layer.setdefault(int(parts[2]), []).append((int(parts[1]), int(parts[3]), peak, rms))
            if detail:
                print(f"    {parts[0]}  note {audiolib.note_name(int(parts[1])):4s} layer {parts[2]} take {parts[3]}  peak {db(peak):6.1f} dB  rms {db(rms):6.1f} dB")
        text = f"{label:48s} gain {db(gain):5.1f} dB |"
        for layer in sorted(by_layer):
            peaks = sorted(p for _, _, p, _ in by_layer[layer])
            text += f"  L{layer}: {db(peaks[0]):5.1f} {db(peaks[len(peaks) // 2]):5.1f} {db(peaks[-1]):5.1f}"
        print(text, flush=True)

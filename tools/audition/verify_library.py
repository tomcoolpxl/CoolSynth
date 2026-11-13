"""Check that library files are bit for bit the recordings they came from (after the trim).

Decodes a sample of the FLAC files with ffmpeg and compares every frame with the source WAV.

usage: python -I verify_library.py <catalogue.txt> <libraries-dir> <library-dir> [files-per-style]
"""
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

catalogue_path, libraries, library = sys.argv[1:4]
per_style = int(sys.argv[4]) if len(sys.argv) > 4 else 2


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


checked = different = 0
styles = 0
total_frames = 0
with tempfile.TemporaryDirectory() as scratch:
    for line in open(catalogue_path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        family, instrument, style, _, source = [part.strip() for part in line.split("|")]
        out_dir = os.path.join(library, slug(family), slug(instrument), slug(style))
        manifest = os.path.join(out_dir, "manifest.txt")
        if not os.path.exists(manifest):
            print("NOT BUILT:", family, instrument, style)
            continue
        styles += 1
        rows = [[p.strip() for p in row.split("|")] for row in open(manifest, encoding="utf-8")
                if row.strip() and not row.startswith("#") and not row.startswith("gain")]
        total_frames += sum(int(r[4]) * int(r[6]) for r in rows)
        # The manifest keeps the builder's order: root, layer, take. The same sort of the source folder gives the pairing.
        for row in rows[:: max(1, len(rows) // per_style)][:per_style]:
            flac = os.path.join(out_dir, row[0])
            decoded = os.path.join(scratch, "check.wav")
            subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", flac, "-c:a", "pcm_s24le", decoded], check=True)
            rate, y = audiolib.read_wav(decoded)
            # Find the source by length and content: its trimmed tail must equal the decoded audio.
            match = False
            for name in sorted(os.listdir(os.path.join(libraries, *source.split("/")))):
                if not name.lower().endswith(".wav"):
                    continue
                path = os.path.join(libraries, *source.split("/"), name)
                if os.path.getsize(path) < y.size:  # cheap filter: the source cannot be shorter than what came out of it
                    continue
                r, x = audiolib.read_wav(path)
                x = x[:, :2]
                if r == rate and len(x) >= len(y) and x.shape[1] == y.shape[1] and np.array_equal(x[len(x) - len(y):], y):
                    match = True
                    break
            checked += 1
            if not match:
                different += 1
                print("DIFFERENT:", flac)
print(f"{styles} styles, {checked} files compared with their recordings, {different} different")
print(f"in memory as 32-bit floats: {total_frames * 4 / 1e9:.2f} GB for the whole library")

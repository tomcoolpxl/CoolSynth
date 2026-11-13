"""Check that library files are bit for bit the recordings they came from (after the trim).

Each style folder's sources.txt says which recording a FLAC file was made from and where it
was started. A sample of the files (or all of them) is decoded with ffmpeg and compared frame
by frame with that recording. Also checks that every manifest row names a file that exists
and gives its true length.

usage: python -I verify_library.py <catalogue.txt> <libraries-dir> <library-dir> [files-per-style | all]
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
per_style = None if len(sys.argv) > 4 and sys.argv[4] == "all" else int(sys.argv[4]) if len(sys.argv) > 4 else 2


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


checked = different = styles = zones = bad_rows = 0
total_floats = 0
with tempfile.TemporaryDirectory() as scratch:
    for line in open(catalogue_path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        family, instrument, style, _, source = [part.strip() for part in line.split("|")][:5]
        out_dir = os.path.join(library, slug(family), slug(instrument), slug(style))
        if not os.path.exists(os.path.join(out_dir, "manifest.txt")):
            print("NOT BUILT:", family, instrument, style)
            continue
        styles += 1
        made_from = {}
        for row in open(os.path.join(out_dir, "sources.txt"), encoding="utf-8"):
            parts = [p.strip() for p in row.split("|")]
            if len(parts) == 3:
                made_from[parts[0]] = (parts[1], int(parts[2]))
        rows = [[p.strip() for p in row.split("|")] for row in open(os.path.join(out_dir, "manifest.txt"), encoding="utf-8")
                if row.strip() and not row.startswith("#")]
        rows = [r for r in rows if len(r) >= 15]
        zones += len(rows)
        total_floats += sum(int(r[7]) * 2 for r in rows)
        for r in rows:
            if r[0] not in made_from or not os.path.exists(os.path.join(out_dir, r[0])):
                bad_rows += 1
                print("ROW WITHOUT A FILE:", out_dir, r[0])
        names = sorted(made_from)
        sample = names if per_style is None else names[:: max(1, len(names) // per_style)][:per_style]
        frames_of = {r[0]: int(r[7]) for r in rows}
        for name in sample:
            flac = os.path.join(out_dir, name)
            decoded = os.path.join(scratch, "check.wav")
            subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", flac, "-c:a", "pcm_s24le", decoded], check=True)
            rate, y = audiolib.read_wav(decoded)
            source_name, start = made_from[name]
            source_rate, x = audiolib.read_wav(os.path.join(libraries, *source.split("/"), source_name))
            x = x[start:, :2]
            checked += 1
            same = source_rate == rate and x.shape == y.shape and np.array_equal(x, y) and frames_of.get(name, len(y)) == len(y)
            if not same:
                different += 1
                print("DIFFERENT:", flac, "from", source_name)
print(f"{styles} styles, {zones} zones, {bad_rows} rows without a file; {checked} files compared with their recordings, {different} different")
print(f"in memory as 32-bit floats: {total_floats * 4 / 1e9:.2f} GB for the whole library")

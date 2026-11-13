"""Take the style trims the loudness tool measured into the catalogue and the built library.

CoolSynthPresetNormalizer --styles prints, for each style after an instrument's first, how many
decibels it would need on the preset's level to match the first ("trim | Instrument | Style | dB").
This adds each number to the style's trim= option in the catalogue (a later library build then
writes it) and to the trim line of the style's manifest in the built library (so it holds now,
without a rebuild). Run the tool again afterwards: what it prints should be near zero.

usage: python apply_trims.py <catalogue.txt> <library-dir> <trims.txt>
"""
import os
import re
import sys

catalogue_path, library, trims_path = sys.argv[1:4]


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


trims = {}
for line in open(trims_path, encoding="utf-8"):
    parts = [p.strip() for p in line.split("|")]
    if len(parts) == 4 and parts[0] == "trim":
        trims[(parts[1], parts[2])] = float(parts[3])

lines = open(catalogue_path, encoding="utf-8", newline="").read().replace("\r\n", "\n").split("\n")
changed = 0
for index, line in enumerate(lines):
    body = line.split("#", 1)[0]
    parts = [p.strip() for p in body.split("|")]
    if len(parts) < 5 or (parts[1], parts[2]) not in trims:
        continue
    step = trims[(parts[1], parts[2])]
    options = dict(word.split("=", 1) for word in parts[5].split()) if len(parts) > 5 else {}
    total = round(float(options.get("trim", 0.0)) + step, 1)
    options["trim"] = f"{total:g}"
    lines[index] = " | ".join(parts[:5]) + " | " + " ".join(f"{k}={v}" for k, v in options.items())
    manifest = os.path.join(library, slug(parts[0]), slug(parts[1]), slug(parts[2]), "manifest.txt")
    if os.path.exists(manifest):
        rows = open(manifest, encoding="utf-8").read().split("\n")
        rows = [row for row in rows if not row.startswith("trim")]
        at = next(i for i, row in enumerate(rows) if row.startswith("gain")) + 1
        rows.insert(at, f"trim | {total:.1f}")
        with open(manifest, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(rows))
    changed += 1
    print(f"{parts[1]} / {parts[2]}: {step:+.1f} dB, now {total:+.1f}")

with open(catalogue_path, "w", encoding="utf-8", newline="\n") as handle:
    handle.write("\n".join(lines))
print(changed, "styles trimmed")

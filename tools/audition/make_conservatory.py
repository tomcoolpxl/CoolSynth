"""Write the Conservatory room: one plain preset per sampled instrument, from the catalogue.

Each preset is the instrument's first style and a little room that suits its family; the style
is a switch inside the preset (the Sample generator's Style). The instrument brings its own
dynamics (the Sample generator plays it with the velocity law its recordings were levelled
for), so the amplifier envelope ignores velocity, and its own attack, so the envelope's is
2 ms. The release is the one the instrument's author gave it, read from the built library's
manifest: short for a piano or a bowed string, many seconds for a harp or a bell, which ring
on after the key as the instrument does. Master gains that are already in the file are kept,
so the loudness tool's work survives a rewrite.

usage: python make_conservatory.py <catalogue.txt> <library-dir> <out.csbank>
"""
import os
import re
import sys

catalogue_path, library, out_path = sys.argv[1:4]

# family: release in seconds if the library has none, reverb size, reverb mix, one line about the family's presets
FAMILIES = {
    "Keys": (0.4, 0.40, 0.12, "every key its own recording at three strengths of touch"),
    "Strings": (0.8, 0.55, 0.20, "bowed notes go on for as long as the key is held"),
    "Plucked": (8.0, 0.45, 0.15, "each string rings out as it was recorded"),
    "Brass": (0.6, 0.50, 0.18, "held notes go on for as long as the key is held"),
    "Winds": (0.5, 0.45, 0.16, "held notes go on for as long as the key is held"),
    "Organs": (0.4, 0.70, 0.25, "a stop per style"),
    "Mallets and Bells": (8.0, 0.50, 0.18, "struck notes ring out as they were recorded"),
}


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


def authors_release(family, instrument, style):
    manifest = os.path.join(library, slug(family), slug(instrument), slug(style), "manifest.txt")
    if os.path.exists(manifest):
        for line in open(manifest, encoding="utf-8"):
            parts = [p.strip() for p in line.split("|")]
            if len(parts) == 2 and parts[0] == "release":
                return float(parts[1])
    return None


instruments = []
for line in open(catalogue_path, encoding="utf-8"):
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    family, instrument, style = [part.strip() for part in line.split("|")][:3]
    for entry in instruments:
        if entry["name"] == instrument:
            entry["styles"].append(style)
            break
    else:
        instruments.append({"family": family, "name": instrument, "styles": [style]})

masters = {}
if os.path.exists(out_path):
    current = None
    for line in open(out_path, encoding="utf-8"):
        header = re.match(r"\[(.+)\]\s*$", line.strip())
        if header:
            current = header.group(1)
        elif current and line.strip().startswith("master"):
            masters[current] = line.split("=", 1)[1].strip()

lines = ["# Conservatory: the sampled instruments, one plain preset each. The room is divided into families;",
         "# an instrument's ways of being played are the Sample generator's Style inside its preset.",
         "# Written by tools/audition/make_conservatory.py from resources/instruments/catalogue.txt.", ""]
for entry in instruments:
    fallback, size, mix, note = FAMILIES[entry["family"]]
    styles = entry["styles"]
    release = authors_release(entry["family"], entry["name"], styles[0])
    release_ms = int(round(min(20.0, max(0.03, release if release is not None else fallback)) * 1000.0))
    # A long release is a note ringing on: it holds its level and then lets go, where a short one falls away at once.
    curve = " releaseCurve=0.5" if release_ms >= 3000 else ""
    lines.append(f"[{entry['name']}]")
    lines.append("room = Conservatory")
    lines.append(f"family = {entry['family']}")
    if len(styles) > 1:
        lines.append(f"# Styles: {', '.join(f'{i + 1} {s}' for i, s in enumerate(styles))}. Recorded; {note}.")
    else:
        lines.append(f"# Recorded; {note}.")
    lines.append(f"g1 = Sample sample={slug(entry['name'])} style={slug(styles[0])} loop=on level=0.9")
    lines.append("p1 = Off")
    lines.append(f"env1 = attack=2 decay=1 sustain=1 release={release_ms}{curve} velocity=0")
    lines.append("voice = env2ToP1=0")
    lines.append(f"reverb = on size={size:g} damping=0.5 mix={mix:g}")
    lines.append(f"master = {masters.get(entry['name'], '0')}")
    lines.append("")

with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
    handle.write("\n".join(lines))
print(len(instruments), "presets written to", out_path)

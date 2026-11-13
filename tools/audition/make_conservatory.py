"""Write the Conservatory room: one plain preset per sampled instrument, from the catalogue.

Each preset is the instrument's first style with an envelope and a little room that suit its
family; the style is a switch inside the preset (the Sample generator's Style). Master gains
that are already in the file are kept, so the loudness tool's work survives a rewrite.

usage: python make_conservatory.py <catalogue.txt> <out.csbank>
"""
import os
import re
import sys

catalogue_path, out_path = sys.argv[1], sys.argv[2]

# family: attack ms, release ms, reverb size, reverb mix, one line about the family's presets
FAMILIES = {
    "Keys": (1, 350, 0.40, 0.12, "every key its own recording at three strengths of touch"),
    "Strings": (25, 320, 0.55, 0.20, "bowed notes go on for as long as the key is held"),
    "Plucked": (1, 900, 0.45, 0.15, "each string rings out as it was recorded"),
    "Brass": (12, 220, 0.50, 0.18, "held notes go on for as long as the key is held"),
    "Winds": (15, 200, 0.45, 0.16, "held notes go on for as long as the key is held"),
    "Organs": (4, 260, 0.70, 0.25, "a stop per style"),
    "Mallets and Bells": (1, 1400, 0.50, 0.18, "struck notes ring out as they were recorded"),
}


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


instruments = []
for line in open(catalogue_path, encoding="utf-8"):
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    family, instrument, style, sustains, _ = [part.strip() for part in line.split("|")]
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
    attack, release, size, mix, note = FAMILIES[entry["family"]]
    styles = entry["styles"]
    lines.append(f"[{entry['name']}]")
    lines.append("room = Conservatory")
    lines.append(f"family = {entry['family']}")
    if len(styles) > 1:
        lines.append(f"# Styles: {', '.join(f'{i + 1} {s}' for i, s in enumerate(styles))}. Recorded; {note}.")
    else:
        lines.append(f"# Recorded; {note}.")
    lines.append(f"g1 = Sample sample={slug(entry['name'])} style={slug(styles[0])} loop=on level=0.9")
    lines.append("p1 = Off")
    lines.append(f"env1 = attack={attack} decay=1 sustain=1 release={release} velocity=0.5")
    lines.append("voice = env2ToP1=0")
    lines.append(f"reverb = on size={size:g} damping=0.5 mix={mix:g}")
    lines.append(f"master = {masters.get(entry['name'], '0')}")
    lines.append("")

with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
    handle.write("\n".join(lines))
print(len(instruments), "presets written to", out_path)

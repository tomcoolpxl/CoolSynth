"""Pack the instrument library for download: one zip per family, each with the library's index.

Every zip holds `Library/library.index` and `Library/<family>/...`, so extracting all of them into
`C:\\ProgramData\\CoolSynth` gives the folder CoolSynth reads (SampleLibrary::defaultRoot). The
recordings are FLAC already, so the zips store them as they are. The User folder (one's own sounds)
is left out. Also writes the Hugging Face dataset card (README.md) and SHA-256 sums.

usage: python package_library.py <library-folder> <out-folder> <version>
"""
import hashlib
import os
import sys
import zipfile

library, out, version = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)

families = sorted(d for d in os.listdir(library) if os.path.isdir(os.path.join(library, d)) and d != "User")
index = os.path.join(library, "library.index")
assert os.path.isfile(index), "no library.index in " + library

sums = []
rows = []
for family in families:
    name = f"CoolSynth-Instruments-{family}-{version}.zip"
    path = os.path.join(out, name)
    total = 0
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        z.write(index, "Library/library.index")
        for root, _, files in os.walk(os.path.join(library, family)):
            for f in sorted(files):
                full = os.path.join(root, f)
                arc = "Library/" + os.path.relpath(full, library).replace(os.sep, "/")
                z.write(full, arc)
                total += os.path.getsize(full)
    digest = hashlib.sha256()
    with open(path, "rb") as h:
        for block in iter(lambda: h.read(1 << 20), b""):
            digest.update(block)
    sums.append(f"{digest.hexdigest()}  {name}")
    rows.append((family, name, os.path.getsize(path)))
    print(f"{name}: {os.path.getsize(path) / 1e6:.0f} MB")

with open(os.path.join(out, f"CoolSynth-Instruments-sha256-{version}.txt"), "w", encoding="utf-8") as h:
    h.write("\n".join(sums) + "\n")

pretty = {"keys": "Keys", "strings": "Strings", "plucked": "Plucked", "brass": "Brass", "winds": "Winds",
          "organs": "Organs", "mallets-and-bells": "Mallets and bells"}
table = "\n".join(f"| {pretty.get(f, f)} | `{n}` | {s / 1e6:.0f} MB |" for f, n, s in rows)
card = f"""---
license: cc0-1.0
pretty_name: CoolSynth Instruments
tags:
- audio
- music
- samples
- synthesizer
---

# CoolSynth Instruments {version}

The recorded instruments of [CoolSynth](https://github.com/tomcoolpxl/CoolSynth), a free synthesizer for
Windows (app and VST3): 40 acoustic instruments in seven families, the sounds of its Conservatory room
and of the presets that use them. CoolSynth plays everything else without them.

## Install

1. Download the zips of the families you want (or all of them):

| Family | File | Size |
| --- | --- | --- |
{table}

2. Extract every zip into `C:\\ProgramData\\CoolSynth`. You get `C:\\ProgramData\\CoolSynth\\Library`
   with one folder per family inside.
3. Start CoolSynth (or reload it in your DAW). The instruments load in the background the first time
   a sound uses them.

`CoolSynth-Instruments-sha256-{version}.txt` lists each zip's SHA-256, to check a download.

## Where the recordings come from

Versilian Studios' VSCO 2 Community Edition and the Versilian Community Sample Library (VCSL), both
released into the public domain under CC0. CoolSynth's tools trimmed, looped, levelled and packed
them as FLAC; that work is CC0 too.
"""
with open(os.path.join(out, "README.md"), "w", encoding="utf-8") as h:
    h.write(card)
print("dataset card and checksums written to", out)

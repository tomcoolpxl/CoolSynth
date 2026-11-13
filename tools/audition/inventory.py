"""Inventory of the downloaded libraries: one line per instrument folder.

For every folder that holds WAV files: how many, total size, format of the first file, and the
set of name tokens that look like articulations or dynamics. Writes a JSON file for later steps.

usage: python -I inventory.py <libraries-dir> <out.json>
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

root, out_path = sys.argv[1], sys.argv[2]
NOTE = re.compile(r"(?<![A-Za-z])([A-G])(#|b|s)?(-?\d)(?![0-9A-Za-z])")

instruments = []
for directory, _, files in os.walk(root):
    if ".git" in directory.split(os.sep):
        continue
    wavs = sorted(f for f in files if f.lower().endswith(".wav"))
    if not wavs:
        continue
    size = sum(os.path.getsize(os.path.join(directory, f)) for f in wavs)
    first = os.path.join(directory, wavs[0])
    try:
        rate, data = audiolib.read_wav(first, max_seconds=0.05)
        with open(first, "rb") as handle:
            head = handle.read(64)
        bits = int.from_bytes(head[34:36], "little") if head[12:16] == b"fmt " else 0
        fmt = f"{rate} Hz, {data.shape[1]} ch, {bits} bit"
    except ValueError as error:
        fmt = "unreadable: " + str(error)
    noted = sum(1 for f in wavs if NOTE.search(os.path.splitext(f)[0]))
    tokens = {}
    for f in wavs:
        stem = NOTE.sub(" ", os.path.splitext(f)[0])
        for token in re.split(r"[\s_\-.,()]+", stem):
            token = token.strip().lower()
            if token and not token.isdigit():
                tokens[token] = tokens.get(token, 0) + 1
    instruments.append({
        "path": os.path.relpath(directory, root).replace(os.sep, "/"),
        "files": len(wavs), "megabytes": round(size / 1e6, 1), "format": fmt, "with_note_name": noted,
        "tokens": dict(sorted(tokens.items(), key=lambda kv: -kv[1])[:14]), "examples": wavs[:3],
    })

instruments.sort(key=lambda i: i["path"])
with open(out_path, "w", encoding="utf-8") as handle:
    json.dump(instruments, handle, indent=1)
for i in instruments:
    print(f"{i['megabytes']:8.1f} MB {i['files']:4d} files ({i['with_note_name']:4d} named notes)  {i['format']:26s} {i['path']}")
    print("      ", " ".join(f"{k}:{v}" for k, v in list(i["tokens"].items())[:10]))
print(len(instruments), "folders with audio")

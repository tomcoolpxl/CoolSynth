"""Render the live-grain presets for the listening page (decision D141).

Two Observatory presets take their grains live from the generator before them (combine Live,
decision D139). Each is played as it is and once more with its grain slot silent, so the ear hears
what the live grains add. Writes <audition>/eartest/live-grains/*.flac and its entry at the top of
<audition>/eartest/index.json.

usage: python make_livegrains.py <presets-dir> <CoolSynthRender.exe> <audition-dir>
"""
import json
import os
import re
import subprocess
import sys
import tempfile

presets, render, audition = sys.argv[1:4]
TEST = "live-grains"
out_dir = os.path.join(audition, "eartest", TEST)
os.makedirs(out_dir, exist_ok=True)

SOUNDS = [
    ("Live Shimmer", "shimmer", "A pad, with a shimmer an octave up made live from its own sound",
     # Cmaj7, Am9, Fmaj7, G6/D: two seconds each
     "48:0:2,55:0:2,59:0:2,64:0:2,45:2:2,52:2:2,59:2:2,60:2:2,41:4:2,48:4:2,52:4:2,57:4:2,43:6:2.6,50:6:2.6,55:6:2.6,59:6:2.6",
     11.5),
    ("Frozen Pluck", "pluck", "A plucked string: its first moment is frozen into a cloud that holds while the key is down",
     "48:0:3.6,55:4:3.6,52:8:3.6,60:8:3.6", 14.0),
]

text = open(os.path.join(presets, "04-observatory.csbank"), encoding="utf-8").read().replace("\r\n", "\n")
clips = []
with tempfile.TemporaryDirectory() as scratch:
    bank = os.path.join(scratch, "nograins.csbank")
    blocks = []
    for name, _, _, _, _ in SOUNDS:
        start = text.index(f"[{name}]")
        end = text.find("\n[", start + 1)
        block = text[start:end if end >= 0 else len(text)].strip("\n")
        block = block.replace(f"[{name}]", f"[{name} without grains]", 1)
        block, n = re.subn(r"(?m)^(g2 = Grain .*?)level=[0-9.]+", r"\1level=0", block, count=1)
        if n != 1:
            raise SystemExit(name + ": no grain slot level to silence")
        blocks.append(block)
    with open(bank, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n\n".join(blocks) + "\n")

    number = 1
    for name, key, title, notes, seconds in SOUNDS:
        for variant, source, label in (("", ["--preset", "Observatory: " + name], title),
                                       ("-without", ["--preset", name + " without grains", "--bank", bank], name + " without the grains")):
            wav = os.path.join(scratch, "clip.wav")
            result = subprocess.run([render] + source + ["--notes", notes, "--seconds", f"{seconds:g}", "--out", wav],
                                    capture_output=True, text=True)
            if result.returncode != 0:
                raise SystemExit(name + variant + ": " + (result.stderr or result.stdout).strip()[-300:])
            file = f"{number}-{key}{variant}.flac"
            subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", wav, "-c:a", "flac", os.path.join(out_dir, file)], check=True)
            clips.append({"title": f"{number}  {label}", "file": file})
            print(file)
            number += 1

index_path = os.path.join(audition, "eartest", "index.json")
index = [t for t in json.load(open(index_path, encoding="utf-8")) if t["id"] != TEST]
index.insert(0, {
    "id": TEST,
    "title": "Grains from a live sound: two new presets",
    "facts": "Two new sounds whose grains are taken live from the sound before them. 1 and 3 are the new presets; "
             "2 and 4 are the same with the grains switched off, so you hear what the grains add.",
    "options": ["both good", "Live Shimmer good, Frozen Pluck not", "Frozen Pluck good, Live Shimmer not",
                "neither: say what in the note"],
    "clips": clips,
})
with open(index_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(index, handle, indent=1, ensure_ascii=False)
    handle.write("\n")
print("listening card written:", len(clips), "clips")

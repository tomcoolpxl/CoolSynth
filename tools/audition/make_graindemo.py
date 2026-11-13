"""Render a demonstration of the granular insert for the audition page.

Takes one factory preset (Glass Bell), plays the same short phrase through it four times: as it
is, and with the Grain insert in its first slot at three settings. Writes
<audition>/graindemo/*.flac and index.json. Freeze cannot be shown this way (the render tool
does not move a switch while it plays); try it in the plugin.

usage: python make_graindemo.py <presets-dir> <CoolSynthRender.exe> <audition-dir>
"""
import json
import os
import subprocess
import sys

presets, render, audition = sys.argv[1:4]
out_dir = os.path.join(audition, "graindemo")
os.makedirs(out_dir, exist_ok=True)

SOURCE_FILE, SOURCE_PRESET = "05-greenhouse.csbank", "Glass Bell"
VARIANTS = [
    ("dry", "The preset as it is", None),
    ("cloud", "A cloud a quarter second behind the playing, half and half",
     "i1 = Grain size=120 density=20 position=250 spray=0.3 pitch=0 spread=0.6 mix=0.5"),
    ("octave", "The same, the grains an octave up: a shimmer over the notes",
     "i1 = Grain size=160 density=25 position=300 spray=0.4 pitch=12 spread=0.8 mix=0.45"),
    ("smear", "Long grains scattered over the last two seconds, the cloud alone",
     "i1 = Grain size=400 density=40 position=600 spray=1 pitch=0 spread=1 mix=1"),
]
NOTES = "60:0:0.7:95,64:0.5:0.7:90,67:1:0.7:95,72:1.5:1.4:100,71:3.2:0.5:85,67:3.7:1.6:95"

text = open(os.path.join(presets, SOURCE_FILE), encoding="utf-8").read().replace("\r\n", "\n")
start = text.index(f"[{SOURCE_PRESET}]")
end = text.find("\n[", start + 1)
block = text[start:end if end >= 0 else len(text)].strip("\n")

bank_path = os.path.join(out_dir, "graindemo.csbank")
with open(bank_path, "w", encoding="utf-8", newline="\n") as handle:
    for key, _, insert in VARIANTS:
        lines = block.split("\n")
        lines[0] = f"[Grain demo {key}]"
        if insert:
            lines.append(insert)
        handle.write("\n".join(lines) + "\n\n")

index = []
for key, title, _ in VARIANTS:
    wav = os.path.join(out_dir, key + ".wav")
    result = subprocess.run([render, "--preset", f"Grain demo {key}", "--bank", bank_path, "--notes", NOTES, "--seconds", "9", "--out", wav],
                            capture_output=True, text=True, cwd=os.path.dirname(bank_path))
    if result.returncode != 0 or not os.path.exists(wav):
        print(key, "failed:", (result.stderr or result.stdout).strip()[-300:])
        continue
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", wav, "-c:a", "flac", os.path.join(out_dir, key + ".flac")], check=True)
    os.remove(wav)
    index.append({"title": title, "file": key + ".flac"})
    print(key, "->", result.stdout.strip().split("(")[-1].rstrip(")"))

# A clip kept from an earlier build, to hear a change against: put it in the folder under this name.
if os.path.exists(os.path.join(out_dir, "octave_before.flac")):
    index.append({"title": "An octave up as it was before: replayed attacks at full strength", "file": "octave_before.flac"})

with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8", newline="\n") as handle:
    json.dump({"preset": SOURCE_PRESET, "clips": index}, handle, indent=1)

"""Render a short demonstration of every Conservatory style, for the audition page.

For each style of each instrument the preset plays, through the real plugin (CoolSynthRender):
eight notes climbing through the notes the style was recorded at, from soft to loud, and then
a chord in the middle of its range. Sustaining styles hold their notes longer. The result is
<audition>/conservatory/<instrument>-<style>.flac and an index.json the page reads.

usage: python make_demos.py <catalogue.txt> <library-dir> <CoolSynthRender.exe> <audition-dir> [only-substring]
"""
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

catalogue_path, library, render, audition = sys.argv[1:5]
only = sys.argv[5].lower() if len(sys.argv) > 5 else ""
out_dir = os.path.join(audition, "conservatory")
os.makedirs(out_dir, exist_ok=True)


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


instruments = []
for line in open(catalogue_path, encoding="utf-8"):
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    family, instrument, style, sustains = [part.strip() for part in line.split("|")][:4]
    for entry in instruments:
        if entry["name"] == instrument:
            entry["styles"].append((style, sustains.lower() == "yes"))
            break
    else:
        instruments.append({"family": family, "name": instrument, "styles": [(style, sustains.lower() == "yes")]})

jobs = []
for entry in instruments:
    for number, (style, sustains) in enumerate(entry["styles"]):
        label = f"{entry['family']} / {entry['name']} / {style}"
        if only and only not in label.lower():
            continue
        manifest = os.path.join(library, slug(entry["family"]), slug(entry["name"]), slug(style), "manifest.txt")
        if not os.path.exists(manifest):
            print("not built:", label)
            continue
        rows = [[p.strip() for p in row.split("|")] for row in open(manifest, encoding="utf-8") if row.strip() and not row.startswith("#")]
        roots = sorted({int(r[1]) for r in rows if len(r) >= 15})
        low, high = roots[0], roots[-1]
        # Eight notes spread over the recorded notes, soft to loud, then a chord around the middle.
        step, hold = (1.1, 1.0) if sustains else (0.55, 0.5)
        climb = sorted({roots[round(i * (len(roots) - 1) / 7)] for i in range(8)}) if len(roots) > 1 else [low]
        notes, at = [], 0.0
        for i, note in enumerate(climb):
            velocity = int(round(45 + (120 - 45) * i / max(1, len(climb) - 1)))
            notes.append(f"{note}:{at:.2f}:{hold:.2f}:{velocity}")
            at += step
        at += 0.4
        middle = (low + high) // 2
        chord = [n for n in (middle - 5, middle, middle + 4, middle + 7) if 0 <= n <= 127] if high - low >= 12 else [middle]
        for note in chord:
            notes.append(f"{note}:{at:.2f}:{2.2 if sustains else 1.2:.2f}:96")
        seconds = at + (2.2 if sustains else 1.2) + 3.0
        name = f"{slug(entry['name'])}-{slug(style)}"
        jobs.append({"family": entry["family"], "instrument": entry["name"], "style": style, "number": number, "file": name + ".flac",
                     "notes": ",".join(notes), "seconds": seconds, "wav": os.path.join(out_dir, name + ".wav"),
                     "range": [low, high]})


def run(job):
    command = [render, "--preset", "Conservatory: " + job["instrument"], "--style", str(job["number"]), "--notes", job["notes"],
               "--seconds", f"{job['seconds']:.1f}", "--out", job["wav"]]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(job["wav"]):
        return job, "render failed: " + (result.stderr.strip() or result.stdout.strip())[-300:]
    flac = os.path.join(out_dir, job["file"])
    encoded = subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", job["wav"], "-c:a", "flac", flac], capture_output=True, text=True)
    if encoded.returncode != 0:
        return job, "encode failed: " + encoded.stderr.strip()[-300:]
    os.remove(job["wav"])
    warning = result.stderr.strip()
    return job, ("warning: " + warning[-200:]) if warning else ""


index = []
with ThreadPoolExecutor(max_workers=3) as pool:  # each render loads an instrument into memory
    for job, problem in pool.map(run, jobs):
        if problem:
            print(f"{job['instrument']} / {job['style']}: {problem}", flush=True)
        if os.path.exists(os.path.join(out_dir, job["file"])):
            index.append({k: job[k] for k in ("family", "instrument", "style", "file", "range")})

path = os.path.join(out_dir, "index.json")
existing = json.load(open(path, encoding="utf-8")) if only and os.path.exists(path) else []
done = {(e["instrument"], e["style"]) for e in index}
index = [e for e in existing if (e["instrument"], e["style"]) not in done] + index
with open(path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(index, handle, indent=1)
print(len(index), "demonstrations in", out_dir)

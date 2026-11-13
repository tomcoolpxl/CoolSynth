"""Build the sampled-instrument library from the picked recordings.

Reads resources/instruments/catalogue.txt and, for every style in it, turns the recordings of
its source folder into <library>/<family>/<instrument>/<style>/NNN.flac plus a manifest.txt:

    gain | 3.16
    file | root | layer | take | frames | rate | channels | loop start | loop end | loop fade

gain        one factor for the whole style, applied when it is loaded: it brings the style's loudest
            recording to 0.9 of full scale (at most 24 dB up), so quietly recorded instruments are
            not quiet in the synth; the balance between its notes and dynamics stays as recorded
root        the MIDI note the recording plays (the instrument's own tuning is kept as recorded)
layer       dynamic layer, 0 the softest
take        which of several recordings of that note and layer (round robin), from 0
loop ...    in frames, for styles that sustain; 0 0 0 means the note rings out as recorded

Every file is trimmed to start 2 ms before its onset and is otherwise bit for bit the
recording (FLAC is lossless). ffmpeg does the encoding.

usage: python -I build_library.py <catalogue.txt> <libraries-dir> <library-out-dir> [only-substring]
"""
import math
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

catalogue_path, libraries, out_root = sys.argv[1:4]
only = sys.argv[4].lower() if len(sys.argv) > 4 else ""
dry = "dry" in sys.argv[5:]  # report how each style would be mapped, write nothing

NOTE = re.compile(r"(?<![A-Za-z])([A-G])(#|b|s)?(-?\d)(?![0-9A-Za-z])")
DYNAMIC_ORDER = ["ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"]
WORD_ORDER = {"vsoft": 0, "soft": 1, "quiet": 1, "medium": 2, "med": 2, "loud": 3, "vloud": 4, "hard": 3}
SEMITONE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


def tokens_of(stem):
    return [t.lower() for t in re.split(r"[\s_\-.,()]+", stem) if t]


def named_midi(stem):
    match = None
    for match in NOTE.finditer(stem):
        pass
    if match is None:
        return None
    letter, accidental, octave = match.groups()
    return SEMITONE[letter] + (1 if accidental in ("#", "s") else -1 if accidental == "b" else 0) + 12 * (int(octave) + 1)


def dynamic_of(tokens):
    for t in tokens:
        if re.fullmatch(r"(v|vl|dyn|vel)\d", t):
            return int(t[-1])
        marked = re.fullmatch(r"(ppp|pp|p|mp|mf|f|ff|fff)\d?", t)
        if marked:
            return DYNAMIC_ORDER.index(marked.group(1))
        if t in WORD_ORDER:
            return WORD_ORDER[t]
    return None


def take_of(tokens, pitch_is_named):
    for t in tokens:
        if re.fullmatch(r"(rr|r)\d\d?", t):
            return int(re.sub(r"\D", "", t))
    if pitch_is_named and tokens and re.fullmatch(r"\d\d?", tokens[-1]):
        return int(tokens[-1])
    return 1


def onset_frame(x, rate):
    level = np.abs(x).max(axis=1)
    peak = float(level.max())
    if peak <= 0.0:
        return 0
    above = np.nonzero(level > peak * 0.003)[0]
    return max(0, int(above[0]) - int(0.002 * rate)) if len(above) else 0


def loop_points(x, rate):
    """A loop over the steady part of a sustained note: (start, end, fade) in frames, or zeros.

    The end is moved, within a few periods, to where the stretch before it best matches the
    stretch before the start, so the crossfade joins like with like."""
    mono = x.mean(axis=1)
    frames = len(mono)
    if frames < int(1.6 * rate):
        return 0, 0, 0
    hop = int(0.01 * rate)
    count = frames // hop
    env = np.array([float(np.sqrt(np.mean(mono[i * hop:(i + 1) * hop] ** 2)) + 1.0e-9) for i in range(count)])
    env_db = 20.0 * np.log10(env)
    steady = float(np.median(env_db[count // 4:(3 * count) // 4]))
    # Where the note is up to its steady level, and where it starts to fall away at the end.
    risen = np.nonzero(env_db >= steady - 2.0)[0]
    start = (int(risen[0]) * hop if len(risen) else 0) + int(0.35 * rate)
    falling = np.nonzero(env_db >= steady - 3.0)[0]
    end = (int(falling[-1]) * hop if len(falling) else frames) - int(0.15 * rate)
    start = max(start, int(0.25 * end))
    end = min(end, int(0.94 * frames))
    if end - start < int(0.8 * rate):
        return 0, 0, 0
    fade = int(min(0.5 * rate, 0.3 * (end - start), start - 16))
    if fade < int(0.05 * rate):
        return 0, 0, 0

    window = min(fade, 4096)
    reference = mono[start - window:start]
    search = int(0.03 * rate)
    best, best_score = end, -2.0
    norm_reference = float(np.linalg.norm(reference)) or 1.0
    for candidate in range(end - search, min(end + search, frames - 1)):
        segment = mono[candidate - window:candidate]
        score = float(np.dot(segment, reference)) / (norm_reference * (float(np.linalg.norm(segment)) or 1.0))
        if score > best_score:
            best, best_score = candidate, score
    return start, best, fade


def read_catalogue(path):
    styles = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        family, instrument, style, sustains, source = [part.strip() for part in line.split("|")]
        styles.append({"family": family, "instrument": instrument, "style": style, "sustains": sustains.lower() == "yes", "source": source})
    return styles


def encode(job):
    source, start, destination = job
    command = ["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", source, "-af", f"atrim=start_sample={start}",
               "-c:a", "flac", "-compression_level", "8", destination]
    result = subprocess.run(command, capture_output=True, text=True)
    return destination, result.returncode, result.stderr.strip()


total_in = total_out = 0
for style in read_catalogue(catalogue_path):
    label = f"{style['family']} / {style['instrument']} / {style['style']}"
    if only and only not in label.lower():
        continue
    folder = os.path.join(libraries, *style["source"].split("/"))
    wavs = sorted(f for f in os.listdir(folder) if f.lower().endswith(".wav"))
    out_dir = os.path.join(out_root, slug(style["family"]), slug(style["instrument"]), slug(style["style"]))
    os.makedirs(out_dir, exist_ok=True)

    entries, offsets = [], []
    for f in wavs:
        stem = os.path.splitext(f)[0]
        tokens = tokens_of(NOTE.sub(" ", stem))
        named = named_midi(stem)
        try:
            rate, x = audiolib.read_wav(os.path.join(folder, f))
        except ValueError as error:
            print("  skipped", f, error)
            continue
        if x.shape[1] > 2:
            x = x[:, :2]
        start = onset_frame(x, rate)
        body = x[start:]
        if len(body) < int(0.05 * rate):
            continue
        hz = audiolib.fundamental(audiolib.mono(body), rate, 0.03, min(0.6, len(body) / rate - 0.04))
        measured = audiolib.hz_to_midi(hz) if hz > 0.0 else None
        if named is not None and measured is not None:
            offsets.append(measured - named)
        entries.append({"file": f, "named": named, "measured": measured, "rank": dynamic_of(tokens), "take": take_of(tokens, named is not None),
                        "start": start, "frames": len(body), "rate": rate, "channels": x.shape[1], "peak": float(np.abs(body).max()),
                        "loop": loop_points(body, rate) if style["sustains"] else (0, 0, 0)})

    # The libraries disagree on which octave is number 4: the names are moved by whole octaves to where the notes sound.
    octave_shift = int(round(float(np.median(offsets)) / 12.0)) * 12 if offsets else 0
    kept = []
    for e in entries:
        if e["named"] is not None:
            e["root"] = e["named"] + octave_shift
        elif e["measured"] is not None:
            e["root"] = int(round(e["measured"]))
        else:
            continue
        if 0 <= e["root"] <= 127:
            kept.append(e)
    if not kept:
        print("NOTHING BUILT for", label)
        continue

    if dry:
        named = [e for e in kept if e["named"] is not None]
        roots = sorted({e["root"] for e in kept})
        low = min(named, key=lambda e: e["named"]) if named else None
        # How far each recording is from its note, in cents, whatever partial the detector caught (octaves and fifths fold away).
        cents = sorted(((e["measured"] - e["root"] + 0.5) % 1.0 - 0.5) * 100.0 for e in kept if e["measured"] is not None)
        tuning = f"cents {cents[len(cents) // 10]:+4.0f} {cents[len(cents) // 2]:+4.0f} {cents[(9 * len(cents)) // 10]:+4.0f}" if cents else "cents    -    -    -"
        print(f"{tuning}  shift {octave_shift:+3d}  measured minus named {min(offsets) if offsets else 0.0:+6.1f} {float(np.median(offsets)) if offsets else 0.0:+6.1f} "
              f"{max(offsets) if offsets else 0.0:+6.1f}  named {len(named):3d} of {len(kept):3d}  "
              f"{audiolib.note_name(roots[0])}-{audiolib.note_name(roots[-1])}  lowest file {low['file'] if low else '-'}  {label}", flush=True)
        continue

    ranks = sorted({e["rank"] for e in kept if e["rank"] is not None})
    takes_seen = {}
    kept.sort(key=lambda e: (e["root"], ranks.index(e["rank"]) if e["rank"] is not None else 0, e["take"], e["file"]))
    jobs, lines = [], []
    for number, e in enumerate(kept):
        layer = ranks.index(e["rank"]) if e["rank"] is not None else 0
        key = (e["root"], layer)
        take = takes_seen.get(key, 0)  # takes are renumbered 0, 1, 2 per note and layer
        takes_seen[key] = take + 1
        name = f"{number:03d}.flac"
        jobs.append((os.path.join(folder, e["file"]), e["start"], os.path.join(out_dir, name)))
        lines.append(f"{name} | {e['root']} | {layer} | {take} | {e['frames']} | {e['rate']} | {e['channels']} | "
                     f"{e['loop'][0]} | {e['loop'][1]} | {e['loop'][2]}")

    failed = 0
    with ThreadPoolExecutor(max_workers=8) as pool:
        for destination, code, message in pool.map(encode, jobs):
            if code != 0 or not os.path.exists(destination):
                failed += 1
                print("  ffmpeg failed:", destination, message)
    loudest = max(e["peak"] for e in kept)
    gain = min(10.0 ** (24.0 / 20.0), 0.9 / loudest) if loudest > 0.0 else 1.0
    with open(os.path.join(out_dir, "manifest.txt"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"gain | {gain:.4f}\n")
        handle.write("# file | root | layer | take | frames | rate | channels | loop start | loop end | loop fade\n")
        handle.write("\n".join(lines) + "\n")

    size_in = sum(os.path.getsize(j[0]) for j in jobs)
    size_out = sum(os.path.getsize(j[2]) for j in jobs if os.path.exists(j[2]))
    total_in += size_in
    total_out += size_out
    looped = sum(1 for e in kept if e["loop"][1] > 0)
    roots = sorted({e["root"] for e in kept})
    print(f"{size_out / 1e6:7.1f} MB (from {size_in / 1e6:6.1f})  {len(kept):3d} files  {len(roots):2d} notes "
          f"{audiolib.note_name(roots[0])}-{audiolib.note_name(roots[-1])}  {max(1, len(ranks))} layers  "
          f"{max(takes_seen.values())} takes  {looped} looped  {failed} failed  {label}", flush=True)

print(f"library: {total_out / 1e6:.0f} MB from {total_in / 1e6:.0f} MB of recordings")

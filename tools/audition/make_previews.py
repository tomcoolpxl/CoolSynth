"""Build audition previews for every pitched instrument folder of the downloaded libraries.

For each folder with at least six pitched notes it writes, under <out>/<id>/:
    run.wav    every recorded note of one middle dynamic layer, low to high, 1.3 s each
    long.wav   the middle note of that layer in full (up to 10 s)
    layers.wav the middle note at every recorded dynamic, soft to loud (when there are several)
and collects what it found (range, layers, round robins, sizes) in <out>/index.json.

usage: python -I make_previews.py <libraries-dir> <out-dir> [only-substring]
"""
import json
import math
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

root, out_root = sys.argv[1], sys.argv[2]
only = sys.argv[3].lower() if len(sys.argv) > 3 else ""

NOTE = re.compile(r"(?<![A-Za-z])([A-G])(#|b|s)?(-?\d)(?![0-9A-Za-z])")
DYNAMIC_ORDER = ["ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"]
WORD_ORDER = {"vsoft": 0, "soft": 1, "quiet": 1, "medium": 2, "med": 2, "loud": 3, "vloud": 4, "hard": 3}
SEMITONE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def tokens_of(stem):
    return [t.lower() for t in re.split(r"[\s_\-.,()]+", stem) if t]


def named_midi(stem):
    match = None
    for match in NOTE.finditer(stem):
        pass  # the last note-like token is the pitch in both libraries' naming
    if match is None:
        return None
    letter, accidental, octave = match.groups()
    value = SEMITONE[letter] + (1 if accidental in ("#", "s") else -1 if accidental == "b" else 0)
    return value + 12 * (int(octave) + 1)


def dynamic_of(tokens):
    """(rank, label) of the dynamic layer a file belongs to; rank None when the name says nothing."""
    for t in tokens:
        if re.fullmatch(r"(v|vl|dyn|vel)\d", t):
            return int(t[-1]), t
        marked = re.fullmatch(r"(ppp|pp|p|mp|mf|f|ff|fff)\d?", t)  # "mf", and "mf1" as the harps write it
        if marked:
            return DYNAMIC_ORDER.index(marked.group(1)), marked.group(1)
        if t in WORD_ORDER:
            return WORD_ORDER[t], t
    return None, ""


def robin_of(tokens, pitch_is_named):
    for t in tokens:
        if re.fullmatch(r"(rr|r)\d\d?", t):
            return int(re.sub(r"\D", "", t))
    # "..._loud_01": a trailing number is a take, unless it is the only thing telling the notes apart.
    if pitch_is_named and tokens and re.fullmatch(r"\d\d?", tokens[-1]):
        return int(tokens[-1])
    return 1


def fade_out(x, rate, seconds=0.03):
    n = min(len(x), int(seconds * rate))
    if n > 0:
        x[-n:] *= np.linspace(1.0, 0.0, n, dtype=np.float32)[:, None]
    return x


def trimmed(x, rate):
    """Skips leading silence (below -50 dB of the file's peak), keeping 5 ms before the onset."""
    level = np.abs(x).max(axis=1)
    peak = float(level.max())
    if peak <= 0.0:
        return x
    above = np.nonzero(level > peak * 0.003)[0]
    start = max(0, int(above[0]) - int(0.005 * rate)) if len(above) else 0
    return x[start:]


index = []
for directory, _, files in sorted(os.walk(root)):
    if ".git" in directory.split(os.sep):
        continue
    relative = os.path.relpath(directory, root).replace(os.sep, "/")
    if only and only not in relative.lower():
        continue
    wavs = sorted(f for f in files if f.lower().endswith(".wav"))
    if len(wavs) < 3:
        continue

    notes = []
    for f in wavs:
        stem = os.path.splitext(f)[0]
        tokens = tokens_of(NOTE.sub(" ", stem))
        rank, label = dynamic_of(tokens)
        named = named_midi(stem)
        notes.append({"file": f, "named": named, "rank": rank, "layer": label, "robin": robin_of(tokens, named is not None)})

    # Where the names carry no pitch, measure it (a third of a second is enough to sort by).
    unnamed = [n for n in notes if n["named"] is None]
    if len(unnamed) > len(notes) // 2:
        for n in notes:
            try:
                rate, x = audiolib.read_wav(os.path.join(directory, n["file"]), max_seconds=1.2)
                hz = audiolib.fundamental(audiolib.mono(trimmed(x, rate)), rate, 0.03, 0.6)
                n["midi"] = audiolib.hz_to_midi(hz) if hz > 0.0 else None
            except ValueError:
                n["midi"] = None
        pitched = [n for n in notes if n["midi"] is not None]
        if len(pitched) < max(3, int(0.7 * len(notes))):
            continue  # drums, cymbals, effects
        notes = pitched
        pitch_source = "measured"
    else:
        notes = [n for n in notes if n["named"] is not None]
        for n in notes:
            n["midi"] = float(n["named"])
        pitch_source = "file names"

    ranks = sorted({n["rank"] for n in notes if n["rank"] is not None})
    middle_rank = ranks[len(ranks) // 2] if ranks else None
    # One file per pitch, low to high: the take whose dynamic is nearest the middle layer (not every
    # pitch is recorded at every dynamic), the first round robin among those.
    def distance(n):
        return (abs(n["rank"] - middle_rank) if n["rank"] is not None and middle_rank is not None else 0, n["robin"], n["file"])

    by_pitch = {}
    for n in notes:
        key = round(n["midi"])
        if key not in by_pitch or distance(n) < distance(by_pitch[key]):
            by_pitch[key] = n
    run = [by_pitch[k] for k in sorted(by_pitch)]
    if len(run) < 3:
        continue

    ident = re.sub(r"[^A-Za-z0-9]+", "-", relative).strip("-").lower()
    out_dir = os.path.join(out_root, ident)
    os.makedirs(out_dir, exist_ok=True)

    pieces, rate_used, measured = [], None, []
    for n in run:
        try:
            rate, x = audiolib.read_wav(os.path.join(directory, n["file"]), max_seconds=6.0)
        except ValueError:
            continue
        if rate_used is None:
            rate_used = rate
        if rate != rate_used:
            continue
        x = trimmed(x if x.shape[1] <= 2 else x[:, :2], rate)
        if x.shape[1] == 1:
            x = np.repeat(x, 2, axis=1)
        hz = audiolib.fundamental(audiolib.mono(x), rate, 0.03, 0.6)
        if hz > 0.0:
            measured.append((n["midi"], audiolib.hz_to_midi(hz)))
        pieces.append(fade_out(x[:int(1.3 * rate)].copy(), rate))
    if len(pieces) < 3:
        continue
    joined = np.concatenate(pieces)
    gain = 0.89 / max(1.0e-6, float(np.abs(joined).max()))
    audiolib.write_wav16(os.path.join(out_dir, "run.wav"), rate_used, joined * gain)

    middle = run[len(run) // 2]
    rate, x = audiolib.read_wav(os.path.join(directory, middle["file"]), max_seconds=10.0)
    x = trimmed(x if x.shape[1] <= 2 else x[:, :2], rate)
    seconds_long = len(x) / rate
    audiolib.write_wav16(os.path.join(out_dir, "long.wav"), rate, fade_out(x.copy(), rate) * (0.89 / max(1.0e-6, float(np.abs(x).max()))))

    layers_written = 0
    if len(ranks) > 1:
        same_pitch = sorted((n for n in notes if round(n["midi"]) == round(middle["midi"]) and n["rank"] is not None),
                            key=lambda n: (n["rank"], n["robin"]))
        seen, parts = set(), []
        for n in same_pitch:
            if n["rank"] in seen:
                continue
            seen.add(n["rank"])
            try:
                r, y = audiolib.read_wav(os.path.join(directory, n["file"]), max_seconds=4.0)
            except ValueError:
                continue
            if r != rate:
                continue
            y = trimmed(y if y.shape[1] <= 2 else y[:, :2], r)
            if y.shape[1] == 1:
                y = np.repeat(y, 2, axis=1)
            parts.append(fade_out(y[:int(2.0 * r)].copy(), r))
        if len(parts) > 1:
            both = np.concatenate(parts)  # one gain for all, so soft stays softer than loud
            audiolib.write_wav16(os.path.join(out_dir, "layers.wav"), rate, both * (0.89 / max(1.0e-6, float(np.abs(both).max()))))
            layers_written = len(parts)

    # How far the names and the measured pitches agree tells which octave convention the names use.
    offsets = [m - n for n, m in measured]
    octave_shift = int(round(float(np.median(offsets)) / 12.0)) * 12 if offsets else 0
    real = sorted(n["midi"] + octave_shift for n in run)
    spread = float(np.median(np.abs(np.array(offsets) - np.median(offsets)))) * 100.0 if offsets else 0.0
    size = sum(os.path.getsize(os.path.join(directory, f)) for f in wavs)
    with open(os.path.join(directory, wavs[0]), "rb") as handle:
        head = handle.read(40)
    index.append({
        "id": ident, "path": relative, "files": len(wavs), "megabytes": round(size / 1e6, 1),
        "rate": rate_used, "bits": int.from_bytes(head[34:36], "little"),
        "pitches": len({round(n["midi"]) for n in notes}), "low": audiolib.note_name(real[0]), "high": audiolib.note_name(real[-1]),
        "layers": len(ranks) if ranks else 1, "layer_names": sorted({n["layer"] for n in notes if n["layer"]}),
        "robins": max(n["robin"] for n in notes), "run_notes": len(pieces), "run_layer": middle["layer"],
        "long_seconds": round(seconds_long, 1), "layers_in_preview": layers_written, "pitch_from": pitch_source,
        "tuning_spread_cents": round(spread, 1),
    })
    e = index[-1]
    print(f"{e['megabytes']:7.1f} MB  {e['pitches']:3d} pitches {e['low']:>4}-{e['high']:<4} {e['layers']} layers x{e['robins']} rr  "
          f"long {e['long_seconds']:4.1f}s  spread {e['tuning_spread_cents']:4.1f}c  {relative}", flush=True)

os.makedirs(out_root, exist_ok=True)
index_path = os.path.join(out_root, "index.json")
merged = {}
if only and os.path.exists(index_path):  # a partial run adds to what is there
    with open(index_path, encoding="utf-8") as handle:
        merged = {e["id"]: e for e in json.load(handle)}
merged.update({e["id"]: e for e in index})
with open(index_path, "w", encoding="utf-8") as handle:
    json.dump(sorted(merged.values(), key=lambda e: e["path"]), handle, indent=1)
print(len(index), "instruments written,", len(merged), "in the index")

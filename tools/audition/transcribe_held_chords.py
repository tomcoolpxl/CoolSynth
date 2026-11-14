"""Transcribe a slow part played on a polyphonic synthesizer whose chords change without a break: some keys stay
down while others move, so the loudness never drops and transcribe_chords.py finds no events.

  changes   where the spectrum changes: per 50 ms the log-compressed spectrum (100 ms windows) against the one
            100 ms before, its rises summed; a change is a peak of that above the 88th percentile, 300 ms or more
            from the last. (Or give the times yourself.)
  notes     for each stretch between two changes, from its settled part (200 ms in, up to a second): the lines of a
            fine spectrum, gathered per semitone (the instrument's two oscillators a sixth of a semitone apart count
            as one line); the lowest line left is a key, its harmonics are taken out unless one stands 4 dB above
            the harmonic before it (a low-passed sawtooth's harmonics only fall: one that rises is another key),
            until `max-notes` keys are found or what is left is `floor` dB under the strongest line.
  ties      a key that is in two stretches running is held across the change, not struck again.

Writes JSON: {"tuning_cents", "changes": [...], "notes": [{"start", "length", "midi"}]} with times from <start-s>;
prints each stretch with its keys, new ones marked.

usage: python -I transcribe_held_chords.py <file> <start-s> <end-s> <lowest-midi> <highest-midi> <out.json>
              [mid|side|left|right] [max-notes] [floor-dB] [change times, comma separated, in the file's seconds]
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def name(midi):
    return f"{NAMES[midi % 12]}{midi // 12 - 1}"


def find_changes(signal, rate):
    window, hop, size = int(0.1 * rate), int(0.05 * rate), 8192
    frames = []
    for at in range(0, len(signal) - window, hop):
        spec = np.abs(np.fft.rfft(signal[at:at + window] * np.hanning(window), size))
        frames.append(np.log1p(50 * spec / (spec.max() + 1e-12)))
    flux = [0.0, 0.0] + [float(np.maximum(0.0, frames[i] - frames[i - 2]).sum()) for i in range(2, len(frames))]
    threshold = np.percentile(flux, 88)
    changes, last = [], -1.0
    for i in range(1, len(flux) - 1):
        if flux[i] >= threshold and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]:
            t = i * hop / rate
            if t - last >= 0.3:
                changes.append(t)
                last = t
    return changes


def lines_of(signal, rate, a, b, lowest, top_hz=3000.0):
    piece = signal[int(a * rate):int(b * rate)]
    size = 1 << int(np.ceil(np.log2(len(piece))) + 3)
    w = np.hanning(len(piece))
    spec = np.abs(np.fft.rfft(piece * w, size)) / (w.sum() / 2)
    bin_hz = rate / size
    low, top = int(audiolib.midi_to_hz(lowest - 0.6) / bin_hz), int(top_hz / bin_hz)
    strongest = spec[low:top].max()
    found = []
    for k in range(low, top):
        if spec[k] > spec[k - 1] and spec[k] >= spec[k + 1] and spec[k] > strongest * 10 ** (-45 / 20.0):
            y0, y1, y2 = spec[k - 1], spec[k], spec[k + 1]
            found.append(((k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)) * bin_hz, 20 * np.log10(y1 / strongest)))
    return found


def tuning_of(lines):
    strong = [audiolib.hz_to_midi(hz) for hz, db in lines if db > -18.0]
    best, best_spread = 0.0, 1e9
    for cents in range(-50, 50, 2):
        spread = float(np.mean([abs((m - cents / 100.0) - round(m - cents / 100.0)) for m in strong])) if strong else 0.0
        if spread < best_spread:
            best, best_spread = cents / 100.0, spread
    return best


def keys_of(lines, tuning, lowest, highest, max_notes, floor):
    """The keys whose sawtooth harmonics (level 1/h, to 3 kHz) add up to the lines, each semitone's strongest line
    standing for it: the non-negative mix of keys closest to the lines' powers; a key counts when its level is within
    `floor` dB of the strongest key's. (A first version took the lowest line as a key and removed its harmonics one
    key at a time; a strong fifth harmonic of the bass became a key of its own.)"""
    top = highest + 44
    observed = np.zeros(top - lowest + 1)
    for hz, db in lines:
        m = int(round(audiolib.hz_to_midi(hz) - tuning))
        if lowest <= m <= top and abs(audiolib.hz_to_midi(hz) - tuning - m) <= 0.3:
            observed[m - lowest] = max(observed[m - lowest], 10.0 ** (db / 10.0))
    candidates = list(range(lowest, highest + 1))
    model = np.zeros((len(observed), len(candidates)))
    for column, key in enumerate(candidates):
        f0 = audiolib.midi_to_hz(key)
        for h in range(1, 25):
            if h * f0 > 3000.0:
                break
            offset = 12.0 * np.log2(h)
            if abs(offset - round(offset)) <= 0.35 and key + int(round(offset)) <= top:
                model[key + int(round(offset)) - lowest, column] += 1.0 / (h * h)
    amounts = np.full(len(candidates), observed.max() / 4.0 + 1e-9)
    for _ in range(600):   # multiplicative updates: amounts stay non-negative
        amounts *= (model.T @ observed + 1e-12) / (model.T @ (model @ amounts) + 1e-12)
    strongest = amounts.max()
    # A bass key under A2 counts from 5 dB lower: its fundamental is the weakest line it has in a mix, and a
    # chord without its bass is the first thing an ear misses.
    chosen = [(a, k) for a, k in zip(amounts, candidates) if a >= strongest * 10.0 ** (-(floor + (5.0 if k < 45 else 0.0)) / 10.0)]
    chosen = sorted(chosen, reverse=True)[:max_notes]
    return sorted(k for _, k in chosen)


if __name__ == "__main__":
    path, begin, end = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    lowest, highest, out_path = int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
    channel = sys.argv[7] if len(sys.argv) > 7 else "mid"
    max_notes = int(sys.argv[8]) if len(sys.argv) > 8 else 6
    floor = float(sys.argv[9]) if len(sys.argv) > 9 else 24.0
    with tempfile.TemporaryDirectory() as scratch:
        wav = os.path.join(scratch, "x.wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-ss", str(begin), "-t", str(end - begin), "-i", path, "-c:a", "pcm_s24le", wav], check=True)
        rate, x = audiolib.read_wav(wav)
    x = x.astype(float)
    signal = {"mid": x.mean(axis=1), "side": 0.5 * (x[:, 0] - x[:, 1]), "left": x[:, 0], "right": x[:, 1]}[channel]
    if len(sys.argv) > 10:
        changes = [float(v) - begin for v in sys.argv[10].split(",")]
    else:
        changes = find_changes(signal, rate)
    total = len(signal) / rate
    edges = [0.0] + [c for c in changes if 0.15 < c < total - 0.3] + [total]
    stretches = []
    for a, b in zip(edges[:-1], edges[1:]):
        lines = lines_of(signal, rate, a + min(0.2, 0.3 * (b - a)), min(b - 0.04, a + 1.2), lowest)
        stretches.append((a, b, lines))
    tuning = float(np.median([tuning_of(lines) for _, _, lines in stretches]))
    notes, sounding = [], {}
    for a, b, lines in stretches:
        keys = keys_of(lines, tuning, lowest, highest, max_notes, floor)
        print(f"  {begin + a:7.2f} s  {b - a:5.2f} s  " + " ".join(name(k) + ("" if k in sounding else "*") for k in keys))
        for key in list(sounding):
            if key not in keys:
                notes.append({"start": round(sounding.pop(key), 3), "length": 0.0, "midi": key, "_end": a})
        for key in keys:
            sounding.setdefault(key, a)
    for key, start in sounding.items():
        notes.append({"start": round(start, 3), "length": 0.0, "midi": key, "_end": total})
    for note in notes:
        note["length"] = round(note.pop("_end") - note["start"], 3)
    notes.sort(key=lambda n: (n["start"], n["midi"]))
    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump({"tuning_cents": round(100 * tuning), "changes": [round(c + begin, 2) for c in edges[1:-1]], "notes": notes}, handle, indent=1)
    print(f"{len(notes)} notes in {len(stretches)} stretches; tuning {100 * tuning:+.0f} cents; * = struck at that change")

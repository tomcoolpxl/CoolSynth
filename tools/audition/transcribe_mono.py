"""Transcribe a phrase played on a monophonic synthesizer: which key is down when, and whether it was
struck anew or played legato.

For a side-by-side with a recording that is a played phrase and not single notes: the rebuild
has to play the same notes. Per 5 ms step the fundamental is the candidate (a semitone grid
around the recording's own tuning) whose first ten harmonics carry the most spectrum; an
octave-up candidate loses because it skips every other harmonic, a sub-octave one is marked
down for its empty odd harmonics. The semitone track is smoothed over 35 ms, and steps shorter
than `shortest` (slides, bends, the glide between two keys) are given to their neighbours.

It works on plain oscillators. A hard-synced oscillator's strongest harmonics move about and
mislead it at every note's start: for such a sound cut single notes out of the recording instead
(make_museum.py, "excerpts").

A note is struck (the envelopes start again) where the band above `bright` Hz rises by more
than 6 dB against the 60 ms before, or after silence; otherwise it is legato, which on a
single-trigger synth leaves the envelopes running.

Writes JSON: {"tuning_cents", "notes": [{"start", "length", "midi", "struck"}]}; midi is the
tracked fundamental's note number (add the oscillator's octave to get the key).

usage: python -I transcribe_mono.py <file> <start-s> <end-s> <lowest-midi> <highest-midi> <window-ms> <bright-hz> <shortest-ms> <out.json>
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

path, begin, end = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
lowest, highest = int(sys.argv[4]), int(sys.argv[5])
window_ms, bright_hz, shortest = float(sys.argv[6]), float(sys.argv[7]), float(sys.argv[8]) * 0.001
out_path = sys.argv[9]
HOP = 0.005

with tempfile.TemporaryDirectory() as scratch:
    wav = path
    if not path.lower().endswith(".wav"):
        wav = os.path.join(scratch, "x.wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", path, "-c:a", "pcm_s24le", wav], check=True)
    rate, x = audiolib.read_wav(wav)
mono = x.mean(axis=1)[int(begin * rate):int(end * rate)]
window = int(window_ms * 0.001 * rate)
hop = int(HOP * rate)
size = 1 << int(np.ceil(np.log2(window * 4)))
hann = np.hanning(window)
bin_hz = rate / size
frames = range(0, len(mono) - window, hop)
spectra = np.array([np.abs(np.fft.rfft(mono[at:at + window] * hann, size)) for at in frames])
levels = np.array([10 * np.log10(np.mean(mono[at:at + window] ** 2) + 1e-14) for at in frames])
times = np.array([(at + window / 2) / rate for at in frames])


def harmonic_sums(tuning):
    """Per frame and candidate note: the spectrum at its first ten harmonics (the strongest bin within a quarter tone)."""
    sums = np.zeros((len(spectra), highest - lowest + 1))
    odd = np.zeros_like(sums)
    for c, note in enumerate(range(lowest, highest + 1)):
        f = audiolib.midi_to_hz(note + tuning)
        for k in range(1, 11):
            centre = k * f / bin_hz
            reach = max(1, int(centre * 0.015))
            a, b = int(centre) - reach, int(centre) + reach + 2
            if b < spectra.shape[1]:
                part = spectra[:, a:b].max(axis=1)
                sums[:, c] += part
                if k % 2:
                    odd[:, c] += part
    # A candidate an octave under a wave that has even harmonics collects all of that wave's harmonics
    # at its own even ones and nothing at its odd ones: it counts for less, the emptier its odd harmonics are.
    return sums * np.clip(odd / (sums + 1e-12) / 0.3, 0.0, 1.0)


# The recording's tuning: the offset (in tenths of a semitone) at which the harmonic sums are sharpest.
loud = levels > levels.max() - 30.0
best_tuning, best_score = 0.0, -1.0
for tenth in range(-5, 5):
    score = float(harmonic_sums(tenth / 10.0)[loud].max(axis=1).sum())
    if score > best_score:
        best_tuning, best_score = tenth / 10.0, score
sums = harmonic_sums(best_tuning)
track = lowest + np.argmax(sums, axis=1)

# Smooth: the most frequent note within 35 ms around each step.
half = int(0.0175 / HOP)
smooth = track.copy()
for i in range(len(track)):
    values, counts = np.unique(track[max(0, i - half):i + half + 1], return_counts=True)
    smooth[i] = values[np.argmax(counts)]

# How bright each step is, to find the notes that were struck.
top = int(bright_hz / bin_hz)
bright = 10 * np.log10((spectra[:, top:] ** 2).sum(axis=1) + 1e-12)
back = int(0.06 / HOP)

segments = []  # [first step, last step + 1, note]
i = 0
while i < len(smooth):
    if not loud[i]:
        i += 1
        continue
    j = i
    while j < len(smooth) and loud[j] and smooth[j] == smooth[i]:
        j += 1
    segments.append([i, j, int(smooth[i])])
    i = j

# Steps too short to be notes go to the longer neighbour they touch.
changed = True
while changed:
    changed = False
    for s, (a, b, note) in enumerate(segments):
        if (b - a) * HOP >= shortest:
            continue
        before = segments[s - 1] if s > 0 and segments[s - 1][1] == a else None
        after = segments[s + 1] if s + 1 < len(segments) and segments[s + 1][0] == b else None
        if before is None and after is None:
            segments.pop(s)
        elif after is None or (before is not None and before[1] - before[0] >= after[1] - after[0]):
            before[1] = b
            segments.pop(s)
        else:
            after[0] = a
            segments.pop(s)
        changed = True
        break
merged = []
for a, b, note in segments:
    if merged and merged[-1][1] == a and merged[-1][2] == note:
        merged[-1][1] = b
    else:
        merged.append([a, b, note])

notes = []
for s, (a, b, note) in enumerate(merged):
    after_silence = s == 0 or merged[s - 1][1] < a
    rise = float(bright[a:min(b, a + back)].max() - np.median(bright[max(0, a - back):a])) if a > back else 99.0
    struck = bool(after_silence or rise > 6.0)
    notes.append({"start": round(float(times[a]), 3), "length": round(float((b - a) * HOP), 3), "midi": note, "struck": struck})
    print(f"   {begin + times[a]:7.3f} s  {(b - a) * HOP:6.3f} s  {audiolib.note_name(note):4s} ({note})  {'struck' if struck else 'legato'}")
with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump({"file": os.path.basename(path), "begin": begin, "end": end, "tuning_cents": round(100 * best_tuning), "notes": notes}, handle, indent=1)
print(f"{len(notes)} notes, {sum(n['struck'] for n in notes)} struck; tuning {100 * best_tuning:+.0f} cents")

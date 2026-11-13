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

A note is struck (the envelopes start again) after silence, or where it starts brighter than
the note before it ended: the share of energy above `bright` times the note's own fundamental,
at its most in the new note's first 40 ms, against the middle value of the old note's last
40 ms, by more than 6 dB. The filter follows the keyboard, so a joined note keeps the
brightness it had; a struck one starts the filter's sweep again. (First version: a fixed band
in Hz and the 60 ms before; it missed a third of the struck notes of a bass riff, and the
rebuild started those notes muffled.) Brightness and loudness at a join are read from 20 ms
windows, whatever window follows the pitch. Otherwise the note is legato, which on a
single-trigger synth leaves the envelopes running.

Where the filter's sweep lasts longer than the notes, a struck note is hardly brighter than
the one before it and this test calls it joined; the tell is then only that the tune stays
bright for seconds. For that case the last argument sets the rule: "flash" (the test above),
"dip" (also struck where the loudness sags by 0.6 dB or more at the join: a key let go before
the next went down), or "all" (every note struck).

Writes JSON: {"tuning_cents", "notes": [{"start", "length", "midi", "struck"}]}; midi is the
tracked fundamental's note number (add the oscillator's octave to get the key).

usage: python -I transcribe_mono.py <file> <start-s> <end-s> <lowest-midi> <highest-midi> <window-ms> <bright-multiple> <shortest-ms> <out.json> [flash|dip|all]
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
window_ms, bright_multiple, shortest = float(sys.argv[6]), float(sys.argv[7]), float(sys.argv[8]) * 0.001
out_path = sys.argv[9]
rule = sys.argv[10] if len(sys.argv) > 10 else "flash"
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

# How bright a stretch of steps is for a given note: the share of energy above a multiple of its fundamental.
# Read from short windows (20 ms), centred on the same moments as the pitch track's steps.
short = int(0.020 * rate)
short_size = 1 << int(np.ceil(np.log2(short * 4)))
short_hann = np.hanning(short)
centres = [at + window // 2 for at in frames]
pieces = [mono[max(0, c - short // 2):max(0, c - short // 2) + short] for c in centres]
power = np.array([np.abs(np.fft.rfft(np.pad(p, (0, short - len(p))) * short_hann, short_size)) ** 2 for p in pieces])
short_level = np.array([10 * np.log10(np.mean(p ** 2) + 1e-14) if len(p) else -140.0 for p in pieces])
all_hz = np.arange(power.shape[1]) * rate / short_size
back = int(0.04 / HOP)


def brightness(first, last, note):
    top = all_hz >= bright_multiple * audiolib.midi_to_hz(note + best_tuning)
    stretch = power[max(0, first):max(first + 1, last)]
    return 10 * np.log10(stretch[:, top].sum(axis=1) / (stretch.sum(axis=1) + 1e-20) + 1e-12)

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
    if after_silence or a <= back + 2:
        rise = 99.0
    else:
        rise = float(brightness(a + 1, min(b, a + 1 + back), note).max() - np.median(brightness(a - 2 - back, a - 2, merged[s - 1][2])))
    sag = 0.0 if after_silence or a < 16 else float(np.median(short_level[a - 14:a - 2]) - short_level[a - 4:a + 6].min())
    struck = bool(after_silence or rise > 6.0 or rule == "all" or (rule == "dip" and sag >= 0.6))
    notes.append({"start": round(float(times[a]), 3), "length": round(float((b - a) * HOP), 3), "midi": note, "struck": struck})
    print(f"   {begin + times[a]:7.3f} s  {(b - a) * HOP:6.3f} s  {audiolib.note_name(note):4s} ({note})  {'struck' if struck else 'legato'}")
with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump({"file": os.path.basename(path), "begin": begin, "end": end, "tuning_cents": round(100 * best_tuning), "notes": notes}, handle, indent=1)
print(f"{len(notes)} notes, {sum(n['struck'] for n in notes)} struck; tuning {100 * best_tuning:+.0f} cents")

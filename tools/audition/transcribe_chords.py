"""Transcribe chords played on a polyphonic synthesizer with a mellow, harmonic-rich wave: which keys sound in each chord.

For a side-by-side with a recording that is chords, not single notes. The clip is cut into
events at the rises of its loudness (8 dB within 40 ms). In each event the notes are read from
the peaks of the spectrum of its held part (each peak's frequency refined between bins):

  - the lowest peak left is a note's fundamental;
  - the peaks at its harmonics (within 30 cents) belong to it and are taken out, except one that
    stands 4 dB or more above the harmonic before it: a low-passed sawtooth's harmonics only
    fall, so a harmonic that rises is another key at that pitch (an octave, a twelfth), and it
    stays in as a fundamental of its own;
  - and so on, until five notes are found (the instrument's voices) or what is left is 30 dB
    under the strongest peak.

(A first version scored every semitone by the spectrum at its harmonics; chords came out as
clusters of neighbouring semitones. So did held chords read from their whole length: the
player's vibrato moves every peak by a quarter tone.) The spectrum is taken from 150 ms a
third of the way into the event, and each event has its own tuning: the middle distance of its
strong peaks from the semitone grid, since a vibrato or a bend moves a whole chord together.

Prints each event with its notes and writes JSON: {"tuning_cents", "events": [{"start",
"length", "keys": [midi, ...]}]}.

usage: python -I transcribe_chords.py <file> <start-s> <end-s> <lowest-midi> <highest-midi> <out.json> [max-notes]
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
lowest, highest, out_path = int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
max_notes = int(sys.argv[7]) if len(sys.argv) > 7 else 5

with tempfile.TemporaryDirectory() as scratch:
    wav = path
    if not path.lower().endswith(".wav"):
        wav = os.path.join(scratch, "x.wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", path, "-c:a", "pcm_s24le", wav], check=True)
    rate, x = audiolib.read_wav(wav)
mono = x.mean(axis=1)[int(begin * rate):int(end * rate)]

# Events: where the loudness rises by 8 dB within 40 ms; an event ends 20 dB under its level or at the next rise.
hop = int(0.005 * rate)
frame = int(0.02 * rate)
levels = np.array([10 * np.log10(np.mean(mono[i:i + frame] ** 2) + 1e-14) for i in range(0, len(mono) - frame, hop)])
loud = levels.max() - 35.0
events, i = [], 8
while i < len(levels):
    if levels[i] > loud and levels[i] - levels[i - 8:i].min() > 8.0:
        start = i
        peak = levels[i:i + 40].max()
        j = i + 20
        while j < len(levels) and levels[j] > peak - 20.0 and not (j > i + 30 and levels[j] - levels[j - 8:j].min() > 8.0):
            j += 1
        events.append((start * 0.005, (j - start) * 0.005))
        i = j
    else:
        i += 1


def peaks_of(start, length):
    """[(hz, dB under the strongest)] of the held part's spectral peaks up to 4 kHz, strongest 40."""
    a = int((start + max(0.1, 0.33 * length)) * rate)
    b = a + int(min(0.15, 0.5 * length) * rate)
    piece = mono[a:b]
    size = 1 << int(np.ceil(np.log2(len(piece) * 8)))
    spectrum = np.abs(np.fft.rfft(piece * np.hanning(len(piece)), size))
    bin_hz = rate / size
    top = int(4000 / bin_hz)
    db = 20 * np.log10(spectrum[:top] / spectrum[:top].max() + 1e-9)
    width = max(3, int(0.6 * rate / len(piece) / bin_hz * 2))
    found = []
    for k in range(width, top - width):
        if db[k] > -45.0 and db[k] == db[k - width:k + width + 1].max():
            a0, b0, c0 = db[k - 1], db[k], db[k + 1]
            shift = 0.5 * (a0 - c0) / (a0 - 2 * b0 + c0) if abs(a0 - 2 * b0 + c0) > 1e-9 else 0.0
            found.append(((k + shift) * bin_hz, float(db[k])))
    return sorted(sorted(found, key=lambda p: -p[1])[:40])


all_peaks = [peaks_of(s, l) for s, l in events]


def tuning_of(peaks):
    """How far a set of peaks sits from the semitone grid: the offset (-50..50 cents) their strong ones agree on."""
    strong = [audiolib.hz_to_midi(hz) for hz, db in peaks if db > -20.0 and hz > 60.0]
    if not strong:
        return 0.0
    best, best_spread = 0.0, 1e9
    for cents in range(-50, 50, 5):
        spread = float(np.median([abs((m - cents / 100.0) - round(m - cents / 100.0)) for m in strong]))
        if spread < best_spread:
            best, best_spread = cents / 100.0, spread
    return best


overall = float(np.median([tuning_of(p) for (s, l), p in zip(events, all_peaks) if l >= 0.25])) if events else 0.0
print(f"tuning about {100 * overall:+.0f} cents (each event has its own); {len(events)} events")

result = []
for (start, length), peaks in zip(events, all_peaks):
    if length < 0.25:
        continue
    left = [(hz, db) for hz, db in peaks if audiolib.midi_to_hz(lowest - 0.5) <= hz]
    tuning = tuning_of(peaks)
    keys = []
    while left and len(keys) < max_notes:
        hz, db = left[0]
        if hz > audiolib.midi_to_hz(highest + 0.5) or max(d for _, d in left) < -30.0:
            break
        if db < -30.0:  # too weak to be a played note's fundamental: noise, hum
            left.pop(0)
            continue
        keys.append(int(round(audiolib.hz_to_midi(hz) - tuning)))
        remaining, previous = [], db
        for other_hz, other_db in left[1:]:
            k = round(other_hz / hz)
            is_harmonic = k >= 2 and abs(1200 * np.log2(other_hz / (k * hz))) < 30.0
            if is_harmonic and other_db < previous + 4.0:
                previous = other_db  # its own harmonic: taken out
            else:
                remaining.append((other_hz, other_db))
                if is_harmonic:
                    previous = other_db
        left = remaining
    keys = sorted(set(keys))
    result.append({"start": round(start, 3), "length": round(length, 3), "keys": keys})
    print(f"   {begin + start:7.3f} s  {length:6.3f} s  " + " ".join(f"{audiolib.note_name(k)}({k})" for k in keys))
with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump({"file": os.path.basename(path), "begin": begin, "end": end, "tuning_cents": round(100 * overall), "events": result}, handle, indent=1)
print(len(result), "events transcribed")

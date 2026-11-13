"""What happens where one note passes into the next, in a recording of a monophonic synth and in a rebuild of it.

The user hears a rebuilt phrase go wrong at the joins between notes. For every join of the
transcription (transcribe_mono.py) this measures, in the reference and in the rebuild, each
clip at its own times:

  dip      how far the loudness falls at the join, against the 60 ms before it (dB; a key let
           go before the next goes down leaves a hole, keys that overlap leave none)
  flash    how much brighter the first 40 ms of the new note are than the last 40 ms of the old
           one, in share of energy above a multiple of each note's own fundamental (dB; a note
           struck anew starts the filter's sweep again, a joined one does not)
  slide    how long the pitch takes to arrive within a third of a semitone of the new note (ms)

and prints them side by side, with the transcription's own verdict (struck or joined), sorted by
time. The last lines count how often the two clips disagree by more than 3 dB or 30 ms.

usage: python -I transitions.py <reference> <begin> <end> <rebuild> <notes.json> <key-offset> <bright-multiple> <window-ms> [tune-cents]
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

reference_path, begin, end, rebuild_path, notes_path = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4], sys.argv[5]
bright_multiple, window_ms = float(sys.argv[7]), float(sys.argv[8])
tune = float(sys.argv[9]) / 100.0 if len(sys.argv) > 9 else None
transcription = json.load(open(notes_path, encoding="utf-8"))
notes = transcription["notes"]
if tune is None:
    tune = transcription.get("tuning_cents", 0) / 100.0
HOP = 0.005


def read(path, scratch, a=None, b=None):
    wav = path
    if not path.lower().endswith(".wav"):
        wav = os.path.join(scratch, "x.wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", path, "-c:a", "pcm_s24le", wav], check=True)
    rate, x = audiolib.read_wav(wav)
    mono = x.mean(axis=1)
    return rate, mono[int(a * rate):int(b * rate)] if a is not None else mono


def tracks(mono, rate, tuning):
    """Per 5 ms step: level (dB), the spectrum, and helpers to read brightness and pitch for a given note."""
    window = int(window_ms * 0.001 * rate)
    hop = int(HOP * rate)
    size = 1 << int(np.ceil(np.log2(window * 4)))
    hann = np.hanning(window)
    starts = range(0, len(mono) - window, hop)
    spectra = np.array([np.abs(np.fft.rfft(mono[at:at + window] * hann, size)) ** 2 for at in starts])
    levels = np.array([10 * np.log10(np.mean(mono[at:at + window] ** 2) + 1e-14) for at in starts])
    times = np.array([(at + window / 2) / rate for at in starts])
    hz = np.fft.rfftfreq(size, 1.0 / rate)

    def brightness(frames, midi):
        f0 = audiolib.midi_to_hz(midi + tuning)
        top = spectra[frames][:, hz >= bright_multiple * f0].sum(axis=1)
        return 10 * np.log10(top / (spectra[frames].sum(axis=1) + 1e-20) + 1e-12)

    def pitch(frame, midi):
        f0 = audiolib.midi_to_hz(midi + tuning)
        a, b = np.searchsorted(hz, f0 * 2 ** (-7 / 12)), np.searchsorted(hz, f0 * 2 ** (7 / 12))
        peak = a + int(np.argmax(spectra[frame][a:b]))
        return audiolib.hz_to_midi(hz[peak]) - tuning

    return times, levels, brightness, pitch


def measure(mono, rate, tuning):
    times, levels, brightness, pitch = tracks(mono, rate, tuning)
    rows = []
    for previous, note in zip(notes[:-1], notes[1:]):
        at = note["start"]
        i = int(np.searchsorted(times, at))
        if i < 16 or i + 30 >= len(times):
            rows.append(None)
            continue
        before = float(np.median(levels[i - 14:i - 2]))
        dip = before - float(levels[i - 4:i + 6].min())
        flash = float(brightness(slice(i + 1, i + 9), note["midi"]).max() - np.median(brightness(slice(i - 10, i - 2), previous["midi"])))
        slide = 0.0
        for k in range(0, 40):
            if abs(pitch(i + k, note["midi"]) - note["midi"]) < 0.34:
                slide = k * HOP * 1000.0
                break
        else:
            slide = 200.0
        rows.append((dip, flash, slide))
    return rows


with tempfile.TemporaryDirectory() as scratch:
    rate, reference = read(reference_path, scratch, begin, end)
    own_rate, rebuild = read(rebuild_path, scratch)
ref_rows = measure(reference, rate, tune)
own_rows = measure(rebuild, own_rate, tune)
print(" time    from -> to     transcribed |  reference: dip  flash  slide |  rebuild: dip  flash  slide")
disagree = {"dip": 0, "flash": 0, "slide": 0}
count = 0
for previous, note, a, b in zip(notes[:-1], notes[1:], ref_rows, own_rows):
    if a is None or b is None:
        continue
    count += 1
    disagree["dip"] += abs(a[0] - b[0]) > 3.0
    disagree["flash"] += abs(a[1] - b[1]) > 3.0
    disagree["slide"] += abs(a[2] - b[2]) > 30.0
    marks = ("D" if abs(a[0] - b[0]) > 3.0 else " ") + ("F" if abs(a[1] - b[1]) > 3.0 else " ") + ("S" if abs(a[2] - b[2]) > 30.0 else " ")
    print(f"{begin + note['start']:7.2f}  {audiolib.note_name(previous['midi']):>4s} -> {audiolib.note_name(note['midi']):4s}  {'struck' if note['struck'] else 'joined':10s} |"
          f"        {a[0]:6.1f} {a[1]:6.1f} {a[2]:6.0f} |       {b[0]:6.1f} {b[1]:6.1f} {b[2]:6.0f}   {marks}")
print(f"{count} joins; the clips disagree on the dip at {disagree['dip']}, on the flash at {disagree['flash']}, on the slide at {disagree['slide']}")

"""Model check clips: the real instrument, my preset before a change, and after, playing the same notes.

For each entry it lays the recorded notes out at the same spacing the presets were rendered with,
levels all clips alike (same RMS, peak kept under full scale) and writes 16-bit files plus
modelcheck/index.json for the page. before.wav and after.wav are the preset without effects;
room.wav, if it is there, is the preset as it is played, with its own reverb: a recording has
its room in it, and a dry preset beside it sounds as if it lacked something.

usage: python -I make_modelcheck.py <audition-dir>
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

audition = sys.argv[1]
libraries = os.path.join(audition, "libraries")
out_root = os.path.join(audition, "modelcheck")

CHECKS = [
    {
        "id": "marimba", "title": "Marimba", "preset": "Greenhouse: Marimba", "spacing": 1.5,
        "folder": "VCSL/Idiophones/Struck Idiophones/Marimba", "words": ["med"],
        "notes": ["F2", "C3", "G3", "B3", "F4", "C5", "G5", "B5", "F6"],
        "changed": "Partials retuned from 1 : 3.93 : 9.54 to the measured 1 : 4 : 10.1; upper partials ring about three times "
                   "longer; the bar rings 4 s at middle C instead of 1.1 s, and twice as long per octave down.",
    },
    {
        "id": "kalimba", "title": "Kalimba", "preset": "Greenhouse: Kalimba", "spacing": 1.5,
        "folder": "VCSL/Idiophones/Plucked Idiophones/Kalimba, Kenya", "words": ["rr2"],
        "notes": ["B3", "C#4", "D#4", "F#4", "G#4", "B4", "C#5"],
        "changed": "Rebuilt from a damped string (harmonic overtones) to a clamped steel tine (overtones at 6.3 and 17.5 times "
                   "the note, as measured); rings about 3 s instead of 1.6 s.",
    },
    {
        "id": "harp", "title": "Harp String", "preset": "Greenhouse: Harp String", "spacing": 2.0,
        "folder": "VCSL/Chordophones/Composite Chordophones/Concert Harp", "words": ["mf"],
        "notes": ["A2", "E3", "G3", "B3", "D4", "F4", "A4", "C5", "E5"],
        "changed": "Plucked near the middle of the string instead of a fifth of the way along (the recordings show weak even "
                   "harmonics); the bass strings now ring longer than the treble.",
    },
    {
        "id": "harp2", "title": "Harp String, second try", "preset": "Greenhouse: Harp String", "spacing": 2.0,
        "folder": "VCSL/Chordophones/Composite Chordophones/Concert Harp", "words": ["mf"],
        "notes": ["A2", "E3", "G3", "B3", "D4", "F4", "A4", "C5", "E5"],
        "changed": "\"Before\" is the version you found not close. The pluck was a burst of bright noise: its harmonics up to the "
                   "tenth were 10 to 30 dB stronger than the recorded harp's. It is now the smooth pulse of a finger's pad, softer "
                   "the higher the string: the first harmonics are within a decibel or two of the recording's, the bass rings "
                   "longer, and a breath of finger noise starts each note. You missed the echo: the recording is in stereo, in a "
                   "room, and the first two preset clips are dry and mono. The last clip is the preset as it is played, with its "
                   "own reverb.",
    },
]


def level(x, target_rms=0.09):
    rms = float(np.sqrt(np.mean(x * x))) or 1.0
    gain = target_rms / rms
    peak = float(np.abs(x).max()) * gain
    return x * (gain if peak <= 0.95 else gain * 0.95 / peak)


def onset(x):
    level_now = np.abs(x).max(axis=1)
    above = np.nonzero(level_now > float(level_now.max()) * 0.01)[0]
    return x[max(0, int(above[0]) - 100):] if len(above) else x


index = []
for check in CHECKS:
    folder = os.path.join(libraries, *check["folder"].split("/"))
    files = sorted(f for f in os.listdir(folder) if f.lower().endswith(".wav") and all(w in f.lower() for w in check["words"]))
    # The libraries do not agree on which octave is number 4, so each file's pitch is measured.
    pitched = []
    for f in files:
        rate, x = audiolib.read_wav(os.path.join(folder, f), max_seconds=1.0)
        hz = audiolib.fundamental(audiolib.mono(onset(x)), rate, 0.02, 0.4)
        if hz > 0.0:
            pitched.append((audiolib.hz_to_midi(hz), f))
    pieces, rate_used = [], None
    for note in check["notes"]:
        letter, octave = note[:-1], int(note[-1])
        target = audiolib.NOTE_NAMES.index(letter) + 12 * (octave + 1)
        near = sorted(pitched, key=lambda p: abs(p[0] - target))
        if not near or abs(near[0][0] - target) > 0.7:
            named = [(float(target), f) for f in files if f"_{note}_" in f]  # other strings ringing along can hide the pitch
            if not named:
                print("  no recording for", check["id"], note)
                continue
            near = named
        rate, x = audiolib.read_wav(os.path.join(folder, near[0][1]), max_seconds=check["spacing"] + 1.0)
        rate_used = rate_used or rate
        x = onset(x[:, :2] if x.shape[1] >= 2 else np.repeat(x, 2, axis=1))
        slot = np.zeros((int(check["spacing"] * rate), 2), dtype=np.float32)
        take = x[:int((check["spacing"] - 0.1) * rate)].copy()
        fade = int(0.05 * rate)
        take[-fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)[:, None]
        slot[:len(take)] = take
        pieces.append(slot)
    out_dir = os.path.join(out_root, check["id"])
    os.makedirs(out_dir, exist_ok=True)
    audiolib.write_wav16(os.path.join(out_dir, "real_level.wav"), rate_used, level(np.concatenate(pieces)))
    entry = {k: check[k] for k in ("id", "title", "preset", "changed")}
    entry["source"] = check["folder"]
    entry["files"] = {"real": "real_level.wav"}
    for name in ("before", "after", "room"):
        path = os.path.join(out_dir, name + ".wav")
        if os.path.exists(path):
            rate, y = audiolib.read_wav(path)
            audiolib.write_wav16(os.path.join(out_dir, name + "_level.wav"), rate, level(y))
            entry["files"][name] = name + "_level.wav"
    index.append(entry)
    print(check["id"], "->", sorted(entry["files"]))

with open(os.path.join(out_root, "index.json"), "w", encoding="utf-8") as handle:
    json.dump(index, handle, indent=1)

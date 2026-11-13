"""Museum side-by-sides: a classic synth sound as a reference plays it, and the rebuild, on the same notes.

Each entry names a candidate preset in resources/museum-candidates/museum.csbank (the Museum's
waiting room: a sound enters the plugin's bank only when the user admits it; an admitted entry
plays from the plugin's own Museum room) and a reference, in one of three forms:

  samples    recordings of single notes, one file per note (and velocity): laid end to end at a
             fixed spacing
  phrase     a stretch of a recording of a monophonic synth and its transcription
             (transcribe_mono.py): the rebuild plays the transcribed notes, legato where the
             player did
  excerpts   single notes cut out of a recording (start, length, key, and optionally how many
             cents sharp the recording has that note): laid end to end with a short gap, for a
             sound the transcriber cannot follow (a synced oscillator) or a recording that is
             mostly chords. With cents given, every note is rendered on its own at that pitch.

An entry with "alone" has no reference (the user chose to hear it on its own): only the rebuild
is rendered, on the notes the entry gives.

The preset plays the same notes through CoolSynthRender, without effects, at the reference
instrument's tuning; both clips are levelled alike. Writes <audition>/museum/<id>/reference.flac,
rebuild.flac and <audition>/museum/index.json for the page.

usage: python -I make_museum.py <CoolSynthRender.exe> <audition-dir> [only-id-substring]
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402
import phrase  # noqa: E402

render, audition = sys.argv[1:3]
only = sys.argv[3] if len(sys.argv) > 3 else ""
out_root = os.path.join(audition, "museum")
candidates = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "resources", "museum-candidates", "museum.csbank"))

MINIMOOG_CLIP = "museum/minimoog/audio/synthmania/synthmania_soundcharts_1105_1203.flac"
PROPHET_FOLDER = "museum/prophet5/recordings/synthmania/"

ENTRIES = [
    {
        "id": "dx7-epiano1", "title": "DX7 E.PIANO 1", "preset": "DX7 E.PIANO 1", "spacing": 1.6,
        "samples": "museum/fm-piano1/FM-Piano1 SFZ+FLAC-20190916/samples", "pattern": "{name}v{velocity}.flac",
        # note name as the reference files spell it, MIDI note, velocity
        "notes": [("C3", 48, 80), ("F#3", 54, 80), ("C4", 60, 60), ("C4", 60, 100), ("F#4", 66, 80), ("C5", 72, 80), ("C5", 72, 100),
                  ("C2", 36, 100), ("F#5", 78, 80)],
        "admitted": True,  # by the user on 13 November 2026 (decision D107): it plays from the plugin's own bank
        "reference": "Recorded from Hexter, an open emulation of the DX7, playing the E. Piano 1 voice (FreePats, FM Piano 1, CC0).",
        "source": "The voice's published data (ROM 1A, voice 11): algorithm 5 with feedback 6, six operators' ratios, levels, envelopes "
                  "and velocity sensitivities, converted to radians and decibels a second after Dexed's envelope and Chowning's index table.",
    },
    {
        "id": "dx7-bass1", "title": "DX7 BASS 1", "preset": "DX7 BASS 1", "spacing": 1.6,
        "samples": "museum/synth-bass-2/SynthBass2 SFZ+FLAC-20210405/samples", "pattern": "{name}.flac",
        # the reference file's name (its sounding note an octave up), the DX7 key that makes it (it sounds two octaves under the key), velocity
        "notes": [("C3", 60, 100), ("F#3", 66, 100), ("C4", 72, 100), ("C2", 48, 100), ("F#2", 54, 100), ("F#4", 78, 100), ("C5", 84, 100)],
        "admitted": True,  # by the user on 14 November 2026 (decision D111)
        "reference": "Recorded from Hexter, an open emulation of the DX7, playing the BASS 1 voice (FreePats, Synth Bass 2, CC0). One "
                     "velocity was recorded; the rebuild plays at 100, where it fits.",
        "source": "The voice's published data (ROM 1A, voice 15): algorithm 16 with feedback 7, six operators' ratios, levels, envelopes, "
                  "rate scaling, level scaling and velocity sensitivities, converted to radians and decibels a second. Nothing is fitted.",
    },
]

# The entries of the sounds whose references are played phrases or cut notes are kept beside this file.
EXTRA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "museum_entries.json")
if os.path.exists(EXTRA):
    ENTRIES += json.load(open(EXTRA, encoding="utf-8"))


def level(x, target_rms=0.09):
    rms = float(np.sqrt(np.mean(x * x))) or 1.0
    gain = target_rms / rms
    peak = float(np.abs(x).max()) * gain
    return x * (gain if peak <= 0.95 else gain * 0.95 / peak)


def read_any(path, scratch):
    if path.lower().endswith(".wav"):
        return audiolib.read_wav(path)
    wav = os.path.join(scratch, "decoded.wav")
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", path, "-c:a", "pcm_s24le", wav], check=True)
    return audiolib.read_wav(wav)


def write_flac(path, rate, x, scratch):
    wav = os.path.join(scratch, "out.wav")
    audiolib.write_wav16(wav, rate, x)
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", wav, "-c:a", "flac", path], check=True)


def stereo(x):
    return x[:, :2] if x.shape[1] >= 2 else np.repeat(x, 2, axis=1)


def from_samples(entry, scratch):
    """Single-note files laid end to end: (rate, reference, notes text, seconds)."""
    spacing = entry["spacing"]
    pieces, rate_used = [], None
    for name, _, velocity in entry["notes"]:
        path = os.path.join(audition, *entry["samples"].split("/"), entry["pattern"].format(name=name, velocity=velocity))
        rate, x = read_any(path, scratch)
        rate_used = rate_used or rate
        x = stereo(x)
        above = np.nonzero(np.abs(x).max(axis=1) > float(np.abs(x).max()) * 0.01)[0]
        x = x[max(0, int(above[0]) - 50):] if len(above) else x
        slot = np.zeros((int(spacing * rate), 2), dtype=np.float32)
        take = x[:int((spacing - 0.1) * rate)].copy()
        fade = int(0.08 * rate)
        take[-fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)[:, None]
        slot[:len(take)] = take
        pieces.append(slot)
    notes = ",".join(f"{midi}:{i * spacing:.2f}:{spacing - 0.25:.2f}:{velocity}" for i, (_, midi, velocity) in enumerate(entry["notes"]))
    return rate_used, np.concatenate(pieces), notes, len(entry["notes"]) * spacing


def from_phrase(entry, scratch):
    """A stretch of a recording and its transcription."""
    spec = entry["phrase"]
    rate, x = read_any(os.path.join(audition, *spec["file"].split("/")), scratch)
    reference = stereo(x)[int(spec["begin"] * rate):int(spec["end"] * rate)]
    transcribed = json.load(open(os.path.join(audition, *spec["notes"].split("/")), encoding="utf-8"))["notes"]
    return rate, reference, phrase.notes_spec(transcribed, spec["key_offset"]), spec["end"] - spec["begin"] + 0.3


def from_excerpts(entry, scratch):
    """Single notes cut out of a recording: [start, length, key (, cents)] each, laid end to end."""
    spec = entry["excerpts"]
    rate, x = read_any(os.path.join(audition, *spec["file"].split("/")), scratch)
    x = stereo(x)
    gap, tail = spec.get("gap", 0.35), spec.get("tail", 0.25)
    pieces, notes, at = [], [], 0.0
    entry["_alone"] = []
    for start, length, key, *cents in spec["notes"]:
        take = x[int(start * rate):int((start + length + tail) * rate)].copy()
        fade_in, fade_out = int(0.004 * rate), int(0.03 * rate)
        take[:fade_in] *= np.linspace(0.0, 1.0, fade_in, dtype=np.float32)[:, None]
        take[-fade_out:] *= np.linspace(1.0, 0.0, fade_out, dtype=np.float32)[:, None]
        pieces += [take, np.zeros((int(gap * rate), 2), dtype=np.float32)]
        notes.append(f"{key}:{at + spec.get('lead', 0.0):.3f}:{length:.3f}:{spec.get('velocity', 100)}")
        if cents:
            entry["_alone"].append((at, f"{key}:{spec.get('lead', 0.0):.3f}:{length:.3f}:{spec.get('velocity', 100)}", cents[0],
                                    len(take) / rate + gap))
        at += len(take) / rate + int(gap * rate) / rate
    return rate, np.concatenate(pieces), ",".join(notes), at + 0.3


index = []
if only and os.path.exists(os.path.join(out_root, "index.json")):
    index = [e for e in json.load(open(os.path.join(out_root, "index.json"), encoding="utf-8"))]
with tempfile.TemporaryDirectory() as scratch:
    for entry in ENTRIES:
        if only and only not in entry["id"]:
            continue
        out_dir = os.path.join(out_root, entry["id"])
        os.makedirs(out_dir, exist_ok=True)
        if "alone" in entry:
            notes, seconds = entry["alone"]["notes"], entry["alone"]["seconds"]
        else:
            builder = from_phrase if "phrase" in entry else from_excerpts if "excerpts" in entry else from_samples
            rate, reference, notes, seconds = builder(entry, scratch)
            write_flac(os.path.join(out_dir, "reference.flac"), rate, level(reference), scratch)

        wav = os.path.join(scratch, "rebuild.wav")
        source = ["--preset", "Museum: " + entry["preset"]] if entry.get("admitted") else ["--preset", entry["preset"], "--bank", candidates]
        tune = entry.get("phrase", entry.get("excerpts", {})).get("tune_cents", 0)
        if entry.get("_alone"):
            # Every note on its own, at the pitch the recording has it, put where the reference has that note.
            own_rate, y = 48000, None
            for at, note, cents, room in entry["_alone"]:
                result = subprocess.run([render] + source + ["--no-fx", "--tune", str(cents), "--notes", note, "--seconds", f"{room:.2f}", "--out", wav],
                                        capture_output=True, text=True)
                if result.returncode != 0:
                    raise SystemExit(entry["id"] + ": render failed: " + (result.stderr or result.stdout).strip()[-300:])
                own_rate, one = audiolib.read_wav(wav)
                if y is None:
                    y = np.zeros((int((seconds + 1.0) * own_rate), 2), dtype=np.float32)
                first = int(at * own_rate)
                y[first:first + len(one)] += one[:len(y) - first]
            y = y[:int(seconds * own_rate)]
            write_flac(os.path.join(out_dir, "rebuild.flac"), own_rate, level(y), scratch)
            index = [e for e in index if e["id"] != entry["id"]]
            index.append({k: entry[k] for k in ("id", "title", "preset", "reference", "source")} | {"admitted": bool(entry.get("admitted"))})
            print(entry["id"], "->", entry["preset"], "(each note at the recording's pitch)")
            continue
        result = subprocess.run([render] + source + ["--no-fx", "--tune", str(tune), "--notes", notes, "--seconds", f"{seconds:.1f}", "--out", wav],
                                capture_output=True, text=True)
        if result.returncode != 0 or not os.path.exists(wav):
            print(entry["id"], "render failed:", (result.stderr or result.stdout).strip()[-300:])
            continue
        own_rate, y = audiolib.read_wav(wav)
        write_flac(os.path.join(out_dir, "rebuild.flac"), own_rate, level(y), scratch)
        index = [e for e in index if e["id"] != entry["id"]]
        index.append({k: entry[k] for k in ("id", "title", "preset", "reference", "source")}
                     | {"admitted": bool(entry.get("admitted")), "no_reference": "alone" in entry})
        print(entry["id"], "->", entry["preset"])

order = {entry["id"]: i for i, entry in enumerate(ENTRIES)}
index.sort(key=lambda e: order.get(e["id"], 999))
with open(os.path.join(out_root, "index.json"), "w", encoding="utf-8", newline="\n") as handle:
    json.dump(index, handle, indent=1)

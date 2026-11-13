"""Museum side-by-sides: a classic synth sound as a reference plays it, and the rebuild, on the same notes.

Each entry names a candidate preset in resources/museum-candidates/museum.csbank (the Museum's
waiting room: a sound enters the plugin's bank only when the user admits it; an admitted entry
plays from the plugin's own Museum room) and reference recordings of single notes (one file per
note and velocity). The reference notes are laid end to end at a fixed spacing; the preset
plays the same notes at the same velocities through CoolSynthRender, without effects; both
clips are levelled alike. Writes <audition>/museum/<id>/reference.flac, rebuild.flac and
<audition>/museum/index.json for the page.

usage: python -I make_museum.py <CoolSynthRender.exe> <audition-dir>
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402

render, audition = sys.argv[1:3]
out_root = os.path.join(audition, "museum")
candidates = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "resources", "museum-candidates", "museum.csbank"))

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
        "reference": "Recorded from Hexter, an open emulation of the DX7, playing the BASS 1 voice (FreePats, Synth Bass 2, CC0). One "
                     "velocity was recorded; the rebuild plays at 100, where it fits.",
        "source": "The voice's published data (ROM 1A, voice 15): algorithm 16 with feedback 7, six operators' ratios, levels, envelopes, "
                  "rate scaling, level scaling and velocity sensitivities, converted to radians and decibels a second. Nothing is fitted.",
    },
]


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


index = []
with tempfile.TemporaryDirectory() as scratch:
    for entry in ENTRIES:
        out_dir = os.path.join(out_root, entry["id"])
        os.makedirs(out_dir, exist_ok=True)
        spacing = entry["spacing"]
        pieces, rate_used = [], None
        for name, _, velocity in entry["notes"]:
            path = os.path.join(audition, *entry["samples"].split("/"), entry["pattern"].format(name=name, velocity=velocity))
            rate, x = read_any(path, scratch)
            rate_used = rate_used or rate
            x = x[:, :2] if x.shape[1] >= 2 else np.repeat(x, 2, axis=1)
            above = np.nonzero(np.abs(x).max(axis=1) > float(np.abs(x).max()) * 0.01)[0]
            x = x[max(0, int(above[0]) - 50):] if len(above) else x
            slot = np.zeros((int(spacing * rate), 2), dtype=np.float32)
            take = x[:int((spacing - 0.1) * rate)].copy()
            fade = int(0.08 * rate)
            take[-fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)[:, None]
            slot[:len(take)] = take
            pieces.append(slot)
        write_flac(os.path.join(out_dir, "reference.flac"), rate_used, level(np.concatenate(pieces)), scratch)

        notes = ",".join(f"{midi}:{i * spacing:.2f}:{spacing - 0.25:.2f}:{velocity}" for i, (_, midi, velocity) in enumerate(entry["notes"]))
        wav = os.path.join(scratch, "rebuild.wav")
        source = ["--preset", "Museum: " + entry["preset"]] if entry.get("admitted") else ["--preset", entry["preset"], "--bank", candidates]
        result = subprocess.run([render] + source + ["--no-fx", "--notes", notes, "--seconds", f"{len(entry['notes']) * spacing:.1f}",
                                 "--out", wav], capture_output=True, text=True)
        if result.returncode != 0 or not os.path.exists(wav):
            print(entry["id"], "render failed:", (result.stderr or result.stdout).strip()[-300:])
            continue
        rate, y = audiolib.read_wav(wav)
        write_flac(os.path.join(out_dir, "rebuild.flac"), rate, level(y), scratch)
        index.append({k: entry[k] for k in ("id", "title", "preset", "reference", "source")} | {"admitted": bool(entry.get("admitted"))})
        print(entry["id"], "->", entry["preset"])

with open(os.path.join(out_root, "index.json"), "w", encoding="utf-8", newline="\n") as handle:
    json.dump(index, handle, indent=1)

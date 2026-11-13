"""Listening tests: clips that tell where something the user hears comes from.

The user hears overdrive when several recorders sound together. The engine adds nothing to the
chord (it equals the sum of its notes to 120 dB, decision D107), so the tests separate what is
left.

recorder-chord (done, decision D111): the soprano chord four times, equally loud: as the preset
plays it; without reverb (each note rendered alone and added up); the same with the third and
the fifths tuned pure; and four sine tones at the chord's pitches and levels, no recorder and
no synth in them. The user: the three recorder clips have it, the sine tones do not. So it is
not the ear making tones of its own, not the reverb and not the tuning: it is in the recordings.

recorder-notes: which recording? The chord's four notes one at a time, as loud as they are in
the chord; the chord without its third (E6, whose recording is the noisiest of the four: the
author's gain for it is 18 dB and it stands 26 dB over its noise, the others 37 to 41); and the
chord with that E6 taken from the recording a tone below instead.

recorder-notes (done, decision D113): none of the six has it. So it is that one recording's
overtones against the other notes; its keys now play from the neighbouring recordings.

Writes <audition>/eartest/<id>/*.flac and <audition>/eartest/index.json for the page. A test
marked done moves to the archive page with its outcome, and its clips are kept as the user
heard them: they are not rendered again (the instrument may have changed since).

usage: python -I make_eartest.py <CoolSynthRender.exe> <audition-dir>
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
out_root = os.path.join(audition, "eartest")

HOLD, LENGTH = 2.2, 4.4  # as long as the chord on the Conservatory's demonstration clips
CHORD = {"preset": "Conservatory: Recorders", "style": 0, "notes": [79, 84, 88, 91], "velocity": 96}
TESTS = [
    {
        "id": "recorder-notes", "title": "Recorder chord, note by note", "kind": "parts", **CHORD,
        "done": """You: none of these has it. No single note, and not the chord without the E6 or with the E6 from the recording next to it: the overdrive is the E6 recording's strong overtones clashing with the other notes. At your choice the E6 and F sharp 6 keys now play from the clean recordings beside them.""",
        "doubt": 2,           # the note whose recording is in doubt: the third of the chord, E6
        "stand_in": 86,       # the recording a tone below it, played a tone up in its place
        "facts": "Your first test says the overdrive is in the recorder clips and not in the sine tones: so it is not your ear, not the "
                 "reverb and not the tuning, it is in the recordings. None of them is clipped. One of the chord's four is much noisier "
                 "than the others, the E6, which the library's author turned up by 18 dB. These clips find out whether it is that one: "
                 "the four notes alone, as loud as they are in the chord, then the chord without the E6, then the chord with the E6 "
                 "played from the recording next to it.",
        "options": ["3 (the E6) has it, and so the chord; 5 and 6 are clean", "5 and 6 have it too, but no single note",
                    "another single note has it (which one, in the note)", "none of these has it", "something else"],
    },
    {
        "id": "recorder-chord", "title": "Recorder chord", "kind": "causes", **CHORD,
        # the chord's just intonation against its root (the second note): a fourth below, a major third and a fifth above
        "pure": [3.0 / 4.0, 1.0, 5.0 / 4.0, 3.0 / 2.0],
        "done": "You: the three recorder clips have the overdrive, the sine tones do not. So it is in the recordings, not in the ear, "
                "the reverb or the tuning.",
        "facts": "The soprano chord four times, equally loud: with the preset's reverb, without, without and tuned pure, and as four "
                 "sine tones with no recorder in them.",
        "options": ["all four sound overdriven", "1, 2 and 4, but not 3 (tuned pure)", "1, 2 and 3 (the recorders), not the sines",
                    "only 1 (with the reverb)", "none of them", "something else"],
    },
]


def run(arguments, wav):
    result = subprocess.run([render] + arguments + ["--out", wav], capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(wav):
        raise SystemExit("render failed: " + (result.stderr or result.stdout).strip()[-300:])
    return audiolib.read_wav(wav)


def resample(x, ratio):
    """x played `ratio` times faster (pitch times ratio), by a 32-point windowed sinc."""
    positions = np.arange(int(len(x) / ratio) - 40) * ratio + 20.0
    whole = np.floor(positions).astype(np.int64)
    fraction = positions - whole
    out = np.zeros((len(positions), x.shape[1]), dtype=np.float64)
    for k in range(-15, 17):
        distance = k - fraction
        weight = np.sinc(distance) * (0.5 + 0.5 * np.cos(np.pi * np.clip(distance / 16.0, -1.0, 1.0)))
        out += x[np.clip(whole + k, 0, len(x) - 1)] * weight[:, None]
    return out.astype(np.float32)


def fundamental(mono, rate, expected):
    """A note's fundamental over the held part: (its mean frequency, its amplitude).

    A blown note's pitch wanders by a few cents, and a spectrum's peak follows whichever stretch
    is loudest. The mean frequency is the slope of the fundamental's phase: the band within a
    semitone of the expected pitch as an analytic signal, its phase unwrapped, a straight line
    fitted through it."""
    spectrum = np.fft.fft(mono)
    hz = np.fft.fftfreq(len(mono), 1.0 / rate)
    analytic = np.fft.ifft(np.where((hz > expected * 2 ** (-1 / 12)) & (hz < expected * 2 ** (1 / 12)), 2.0 * spectrum, 0.0))
    held = slice(int(0.5 * rate), int((HOLD - 0.2) * rate))
    phase = np.unwrap(np.angle(analytic[held]))
    slope = np.polyfit(np.arange(len(phase)) / rate, phase, 1)[0]
    return float(slope / (2 * np.pi)), float(np.mean(np.abs(analytic[held])))


def fit(x, length):
    out = np.zeros((length, 2), dtype=np.float32)
    out[:min(length, len(x))] = x[:length]
    return out


def write_flac(path, rate, x, scratch):
    wav = os.path.join(scratch, "out.wav")
    audiolib.write_wav16(wav, rate, x)
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", wav, "-c:a", "flac", path], check=True)


def note_alone(test, note, scratch, length=None):
    base = ["--preset", test["preset"], "--style", str(test["style"]), "--seconds", f"{LENGTH:.1f}", "--no-fx",
            "--notes", f"{note}:0.10:{HOLD:.2f}:{test['velocity']}"]
    rate, x = run(base, os.path.join(scratch, "note.wav"))
    return rate, fit(x, length or len(x))


def causes(test, scratch):
    """With reverb, without, tuned pure, sine tones: [(file, title, audio)], rate, the gain all share (None: level each alike)."""
    base = ["--preset", test["preset"], "--style", str(test["style"]), "--seconds", f"{LENGTH:.1f}"]
    spec = ",".join(f"{n}:0.10:{HOLD:.2f}:{test['velocity']}" for n in test["notes"])
    rate, with_reverb = run(base + ["--notes", spec], os.path.join(scratch, "chord.wav"))
    length = len(with_reverb)
    alone = [note_alone(test, n, scratch, length)[1] for n in test["notes"]]
    dry = sum(alone)
    measured = [fundamental(x.mean(axis=1), rate, audiolib.midi_to_hz(n)) for x, n in zip(alone, test["notes"])]
    root = measured[test["pure"].index(1.0)][0]
    pure = sum(fit(resample(x, root * ratio / hz), length) for x, ratio, (hz, _) in zip(alone, test["pure"], measured))
    time = np.arange(length) / rate
    gate = np.clip((time - 0.1) / 0.03, 0.0, 1.0) * np.clip((0.1 + HOLD + 0.25 - time) / 0.25, 0.0, 1.0)
    sines = sum(amplitude * np.sin(2 * np.pi * hz * time) for hz, amplitude in measured) * gate
    sines = np.repeat(sines[:, None], 2, axis=1).astype(np.float32)
    print("   fundamentals " + " ".join(f"{hz:.1f}" for hz, _ in measured) + " Hz; pure: " + " ".join(f"{root * ratio:.1f}" for ratio in test["pure"]))
    return [("1-reverb.flac", "1  The chord as the preset plays it, with its reverb", with_reverb),
            ("2-dry.flac", "2  The same without reverb", dry),
            ("3-pure.flac", "3  Without reverb, the third and the fifths tuned pure", pure),
            ("4-sines.flac", "4  Four sine tones at the same pitches and levels: no recorder, no synth", sines)], rate, None


def parts(test, scratch):
    """Each note alone, the chord without the note in doubt, and with that note from the neighbouring recording."""
    rate, first = note_alone(test, test["notes"][0], scratch)
    length = len(first)
    alone = [first] + [note_alone(test, n, scratch, length)[1] for n in test["notes"][1:]]
    chord = sum(alone)
    doubt = test["doubt"]
    without = sum(x for i, x in enumerate(alone) if i != doubt)
    neighbour = note_alone(test, test["stand_in"], scratch, length)[1]
    stand_in = fit(resample(neighbour, 2.0 ** ((test["notes"][doubt] - test["stand_in"]) / 12.0)), length)
    # the stand-in as loud as the note it replaces
    held = slice(int(0.5 * rate), int((HOLD - 0.2) * rate))
    stand_in *= float(np.sqrt(np.mean(alone[doubt][held] ** 2)) / (np.sqrt(np.mean(stand_in[held] ** 2)) + 1e-12))
    replaced = without + stand_in
    names = [audiolib.note_name(n) for n in test["notes"]]
    clips = [(f"{i + 1}-{names[i].lower().replace('#', 's')}.flac", f"{i + 1}  {names[i]} alone", x) for i, x in enumerate(alone)]
    clips += [("5-without.flac", f"5  The chord without the {names[doubt]}", without),
              ("6-replaced.flac", f"6  The chord with the {names[doubt]} from the recording next to it", replaced)]
    gain = 0.09 / float(np.sqrt(np.mean(chord[held] ** 2)))  # every clip at the level it has in the chord
    return clips, rate, gain


index = []
index_path = os.path.join(out_root, "index.json")
earlier = {e["id"]: e for e in json.load(open(index_path, encoding="utf-8"))} if os.path.exists(index_path) else {}
with tempfile.TemporaryDirectory() as scratch:
    for test in TESTS:
        out_dir = os.path.join(out_root, test["id"])
        os.makedirs(out_dir, exist_ok=True)
        if test.get("done") and test["id"] in earlier:
            index.append(dict(earlier[test["id"]], done=test["done"], facts=test["facts"]))
            print(test["id"], "is done: its clips are kept as they were heard")
            continue
        clips, rate, gain = (causes if test["kind"] == "causes" else parts)(test, scratch)
        held = slice(int(0.5 * rate), int((HOLD - 0.2) * rate))
        target = float(np.sqrt(np.mean(clips[1][2][held] ** 2)))
        entry = {"id": test["id"], "title": test["title"], "facts": test["facts"], "options": test["options"], "clips": [], "done": test.get("done", "")}
        for name, title, x in clips:
            x = x * (gain if gain is not None else target / (float(np.sqrt(np.mean(x[held] ** 2))) or 1.0))
            write_flac(os.path.join(out_dir, name), rate, x, scratch)
            entry["clips"].append({"title": title, "file": name})
            print(f"{test['id']} {name:16s} peak {20 * np.log10(float(np.abs(x).max()) + 1e-12):6.1f} dB")
        index.append(entry)

os.makedirs(out_root, exist_ok=True)
with open(os.path.join(out_root, "index.json"), "w", encoding="utf-8", newline="\n") as handle:
    json.dump(index, handle, indent=1)

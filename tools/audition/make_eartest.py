"""Listening tests: clips that tell where something the user hears comes from.

The recorder chord. The user hears overdrive when several recorders sound together. The engine
adds nothing to the chord (it equals the sum of its notes to 120 dB, decision D107), so the
test separates what is left: the reverb, the equal-tempered tuning, the recordings, or the ear
and the loudspeakers, which make tones of their own out of loud near-pure tones that are close
together (combination tones). Four clips of the same chord, equally loud:

  1  as the preset plays it, with its reverb
  2  the same without reverb: each note rendered alone and added up
  3  the same notes, the third and the fifths tuned pure: the combination tones then fall on
     one note an octave or two under the chord instead of beating against each other
  4  four sine tones at the chord's pitches and levels: no recorder and no synth in it

Writes <audition>/eartest/<id>/*.flac and <audition>/eartest/index.json for the page.

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
TESTS = [
    {
        "id": "recorder-chord", "title": "Recorder chord", "preset": "Conservatory: Recorders", "style": 0,
        "notes": [79, 84, 88, 91], "velocity": 96,
        # the chord's just intonation against its root (the second note): a fourth below, a major third and a fifth above
        "pure": [3.0 / 4.0, 1.0, 5.0 / 4.0, 3.0 / 2.0],
        "facts": "You still hear overdrive when several recorders play. I cannot find it in the sound: the chord is exactly the sum of "
                 "its notes, nothing clips or clicks, and it is steadier than the flute and ocarina chords you found good. What is left "
                 "is the ear itself: loud, near-pure tones close together make the ear (and small loudspeakers) add tones of their own, "
                 "and with a keyboard's tuning those added tones beat against each other, a rough buzz under the chord. Recorder "
                 "players know it and tune their thirds by it. These four clips tell the possibilities apart. It is the soprano chord, "
                 "equally loud each time; say in the note if you heard it on another recorder.",
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


index = []
with tempfile.TemporaryDirectory() as scratch:
    for test in TESTS:
        out_dir = os.path.join(out_root, test["id"])
        os.makedirs(out_dir, exist_ok=True)
        base = ["--preset", test["preset"], "--style", str(test["style"]), "--seconds", f"{LENGTH:.1f}"]
        spec = lambda notes: ",".join(f"{n}:0.10:{HOLD:.2f}:{test['velocity']}" for n in notes)
        rate, with_reverb = run(base + ["--notes", spec(test["notes"])], os.path.join(scratch, "chord.wav"))
        length = len(with_reverb)
        alone = [fit(run(base + ["--no-fx", "--notes", spec([n])], os.path.join(scratch, "note.wav"))[1], length) for n in test["notes"]]
        dry = sum(alone)

        measured = [fundamental(x.mean(axis=1), rate, audiolib.midi_to_hz(n)) for x, n in zip(alone, test["notes"])]
        root = measured[test["pure"].index(1.0)][0]
        pure = sum(fit(resample(x, root * ratio / hz), length) for x, ratio, (hz, _) in zip(alone, test["pure"], measured))

        time = np.arange(length) / rate
        gate = np.clip((time - 0.1) / 0.03, 0.0, 1.0) * np.clip((0.1 + HOLD + 0.25 - time) / 0.25, 0.0, 1.0)
        sines = sum(amplitude * np.sin(2 * np.pi * hz * time) for hz, amplitude in measured) * gate
        sines = np.repeat(sines[:, None], 2, axis=1).astype(np.float32)

        clips = [("1-reverb.flac", "1  The chord as the preset plays it, with its reverb", with_reverb),
                 ("2-dry.flac", "2  The same without reverb", dry),
                 ("3-pure.flac", "3  Without reverb, the third and the fifths tuned pure", pure),
                 ("4-sines.flac", "4  Four sine tones at the same pitches and levels: no recorder, no synth", sines)]
        held = slice(int(0.5 * rate), int((HOLD - 0.2) * rate))
        target = float(np.sqrt(np.mean(dry[held] ** 2)))
        entry = {"id": test["id"], "title": test["title"], "facts": test["facts"], "options": test["options"], "clips": []}
        for name, title, x in clips:
            x = x * (target / (float(np.sqrt(np.mean(x[held] ** 2))) or 1.0))
            write_flac(os.path.join(out_dir, name), rate, x, scratch)
            entry["clips"].append({"title": title, "file": name})
            print(f"{test['id']} {name:16s} peak {20 * np.log10(float(np.abs(x).max())):6.1f} dB")
        print("   fundamentals " + " ".join(f"{hz:.1f}" for hz, _ in measured) + " Hz; pure: "
              + " ".join(f"{root * ratio:.1f}" for ratio in test["pure"]))
        index.append(entry)

os.makedirs(out_root, exist_ok=True)
with open(os.path.join(out_root, "index.json"), "w", encoding="utf-8", newline="\n") as handle:
    json.dump(index, handle, indent=1)

"""Fit a preset to a recorded phrase: the same notes through a candidate bank, compared band by band over time.

The reference is a stretch of a recording of a monophonic synth and its transcription
(transcribe_mono.py). A bank template with {name} places is rendered for every trial through
CoolSynthRender, playing the transcribed notes, and compared with the reference: ten octave
bands in 10 ms steps wherever the reference is within 30 dB of its loudest, robust mean of the
absolute differences in dB, in the bands the reference has within 48 dB of its level (differences above 12 dB count as 12: a note the transcription got
wrong should not steer the sound). The search moves one value at a time, four trials at once.

The job is a JSON file: {"reference", "begin", "end", "notes", "key_offset", "preset", "template",
"start": {name: value}, "steps": {name: [offsets]}, "scaled": [names moved by factors], "limits": {name: [low, high]},
"tune_cents": how sharp the recorded instrument is, so the rebuild plays at its pitch,
"cc": controller moves for the render ("time:number:value,..."), "key_up_early": seconds, for chords}.
The notes file is a phrase (transcribe_mono.py) or chords (transcribe_chords.py).

usage: python -I phrase_fit.py <CoolSynthRender.exe> <job.json> <rounds> [name=value ...]
"""
import json
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402
import phrase  # noqa: E402

render_exe, job_path, rounds = sys.argv[1], sys.argv[2], int(sys.argv[3])
job = json.load(open(job_path, encoding="utf-8"))
folder = os.path.dirname(os.path.abspath(job_path))
work = tempfile.mkdtemp(prefix="phrasefit")


def read_any(path):
    if path.lower().endswith(".wav"):
        return audiolib.read_wav(path)
    wav = os.path.join(work, "reference.wav")
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", path, "-c:a", "pcm_s24le", wav], check=True)
    return audiolib.read_wav(wav)


rate, x = read_any(os.path.join(folder, job["reference"]))
reference = x.mean(axis=1)[int(job["begin"] * rate):int(job["end"] * rate)]
ref_times, ref_bands, ref_total = phrase.band_levels(reference, rate)
loud = ref_total > ref_total.max() - 30.0
transcription = json.load(open(os.path.join(folder, job["notes"]), encoding="utf-8"))
if "events" in transcription:  # chords: all keys of an event together
    spec = phrase.chords_spec(transcription["events"], key_up_early=job.get("key_up_early", 0.0))
else:
    spec = phrase.notes_spec(transcription["notes"], job["key_offset"])
extra = ["--cc", job["cc"]] if job.get("cc") else []
seconds = job["end"] - job["begin"] + 0.5


def evaluate(trial):
    tag, values = trial
    bank = os.path.join(work, f"fit{tag}.csbank")
    with open(bank, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(job["template"].format(**{k: f"{v:.5g}" for k, v in values.items()}))
    wav = os.path.join(work, f"fit{tag}.wav")
    result = subprocess.run([render_exe, "--preset", job["preset"], "--bank", bank, "--no-fx", "--notes", spec, "--seconds", f"{seconds:.2f}", "--out", wav,
                             "--tune", str(job.get("tune_cents", 0))] + extra, capture_output=True, text=True, cwd=work)
    if result.returncode != 0:
        raise SystemExit("render failed: " + (result.stderr or result.stdout)[-500:])
    own_rate, y = audiolib.read_wav(wav)
    _, bands, total = phrase.band_levels(y.mean(axis=1), own_rate)
    count = min(len(bands), len(ref_bands))
    use = loud[:count]
    difference = bands[:count][use] - ref_bands[:count][use]
    # bands the reference hardly has (48 dB under its level: hiss, the codec's noise) do not count
    heard = ref_bands[:count][use] > job.get("heard_db", -48.0)
    cost = float(np.minimum(np.abs(difference), 12.0)[heard].mean())
    return cost, np.where(heard, difference, np.nan), float(np.nanmean(np.abs(total[:count][use] - ref_total[:count][use])))


def show(values, result):
    print(f"   cost {result[0]:5.2f} dB (level over time {result[2]:4.1f} dB)  " + " ".join(f"{k}={v:.4g}" for k, v in values.items()), flush=True)


current = dict(job["start"])
for argument in sys.argv[4:]:
    name, value = argument.split("=")
    current[name] = float(value)
best = evaluate(("0", current))
show(current, best)
scaled = set(job.get("scaled", []))
with ThreadPoolExecutor(max_workers=4) as pool:
    for turn in range(rounds):
        shrink = 1.0 if turn < 2 else 0.5
        for name, steps in job["steps"].items():
            trials = []
            for i, step in enumerate(steps):
                values = dict(current)
                values[name] = current[name] * (1 + (step - 1) * shrink) if name in scaled else current[name] + step * shrink
                low, high = job["limits"][name]
                values[name] = min(high, max(low, values[name]))
                if abs(values[name] - current[name]) > 1e-9:
                    trials.append((str(i + 1), values))
            for (tag, values), result in zip(trials, pool.map(evaluate, trials)):
                if result[0] < best[0] - 1e-3:
                    best, current = result, values
        show(current, best)
print("fitted: " + " ".join(f"{k}={v:.4g}" for k, v in current.items()))
print("bands (Hz from) " + " ".join(f"{int(e):>6d}" for e in phrase.BAND_EDGES[:-1]))
print("the reference's own levels:  " + " ".join(f"{v:6.1f}" for v in ref_bands[loud].mean(axis=0)))
print("middle, mine minus reference:" + " ".join(f"{v:+6.1f}" for v in np.nanmedian(best[1], axis=0)))
with np.errstate(all="ignore"), __import__("warnings").catch_warnings():
    __import__("warnings").simplefilter("ignore")
    print("mean absolute:              " + " ".join(f"{v:6.1f}" for v in np.nanmean(np.abs(best[1]), axis=0)))

"""Transcribe a phrase played on an FM bass whose notes start with the octave louder than the note itself
(the TX81Z's LatelyBass): which key went down when, for how long, and how hard.

transcribe_mono.py reads such a note an octave high at its start and loses the tune. Here:

  starts    where the spectrum jumps: the rise of every bin above 50 Hz (log-compressed) summed over the
            bins, in 5 ms steps; a peak that stands above 0.35 of the strongest and 70 ms from the last.
  key-up    where, 60 ms or more into a note, the level falls 7 dB within 40 ms (a release; the note's own
            fading is far slower); without one the key stays down to the next start.
  pitch     from the note's body (from 40 ms on, up to 300 ms): on a semitone grid at the recording's own
            tuning, the highest candidate whose harmonics up to 1.5 kHz carry at least 0.9 of the best
            candidate's sum of root amplitudes. The true note carries everything; its octave misses the
            odd harmonics, a quarter of the sum even at a note's start; anything under the true note carries
            the same and is not the highest.
  velocity  the key velocity whose note, by the chip's rules (lib/tx81z.py), has harmonics closest to the
            recording's in the first cycles. The carrier of this voice ignores velocity: only the tone says
            how hard a key was hit.

Writes JSON like transcribe_mono.py's, every note struck, with "velocity": {"tuning_cents", "notes":
[{"start", "length", "midi", "struck", "velocity"}]}; midi is the sounding fundamental (the key is 24 above).

A note's octave is checked once more by its tone: if the octave above or below, by the chip's rules, comes 2 dB
closer to the recording, it is taken (a short note leaves the pitch reading little to go on).

usage: python -I transcribe_fmbass.py <file> <start-s> <end-s> <lowest-midi> <highest-midi> <out.json> [tuning-cents]
       (without the last argument the tuning is read from the stretch's longest notes)
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402
import tx81z  # noqa: E402

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
VELOCITIES = [20, 32, 44, 56, 68, 80, 90, 100, 108, 116, 122, 127]


def read(path, start, end):
    with tempfile.TemporaryDirectory() as scratch:
        wav = os.path.join(scratch, "x.wav")
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-ss", str(start), "-t", str(end - start), "-i", path,
                        "-c:a", "pcm_s24le", wav], check=True)
        rate, x = audiolib.read_wav(wav)
    return float(rate), x.mean(axis=1).astype(float)


def spectrum(seg, size):
    w = np.hanning(len(seg))
    return np.abs(np.fft.rfft(seg * w, size)) / (w.sum() / 2)


def find_starts(mono, rate):
    window, hop = int(0.023 * rate), int(0.005 * rate)
    size = 2048
    low = int(50.0 / (rate / size)) + 1
    frames = [np.log1p(200.0 * spectrum(mono[at:at + window], size)[low:]) for at in range(0, len(mono) - window, hop)]
    frames = np.array(frames)
    flux = np.zeros(len(frames))
    flux[3:] = np.maximum(0.0, frames[3:] - frames[:-3]).sum(axis=1)
    threshold = 0.35 * np.percentile(flux, 99.5)
    starts, last = [], -1.0
    for i in range(1, len(flux) - 1):
        if flux[i] >= threshold and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]:
            t = i * hop / rate          # the jump is seen when the window's leading part has it
            if t - last >= 0.07:
                starts.append(t)
                last = t
            elif flux[i] > flux[int(round(last * rate / hop))]:
                starts[-1] = t
                last = t
    return starts


def refine_start(mono, rate, t):
    """The moment the highs jump, to a millisecond: the steepest rise of the differenced signal's energy near t."""
    highs = np.diff(mono)
    hop, window = int(0.001 * rate), int(0.004 * rate)
    a = max(0, int((t - 0.015) * rate))
    count = int(0.05 * rate / hop)
    energy = np.array([np.mean(highs[a + k * hop:a + k * hop + window] ** 2) + 1e-14 for k in range(count)
                       if a + k * hop + window <= len(highs)])
    if len(energy) < 8:
        return t
    rise = np.log(energy[3:]) - np.log(energy[:-3])
    k = int(np.argmax(rise))
    return (a + (k + 3) * hop + window // 2) / rate - 0.002


def level_track(mono, rate):
    hop, window = int(0.005 * rate), int(0.02 * rate)
    return hop, np.array([10 * np.log10(np.mean(mono[at:at + window] ** 2) + 1e-12) for at in range(0, len(mono) - window, hop)])


def key_up(levels, hop, rate, start, limit):
    a, b = int((start + 0.06) * rate / hop), int(limit * rate / hop)
    span = int(0.04 * rate / hop)
    for i in range(a, min(b, len(levels) - span)):
        if levels[i] - levels[i + span] >= 7.0:
            return i * hop / rate
    return None


def harmonic_levels(seg, rate, f0, count):
    size = 1 << 17
    spec = spectrum(seg, size)
    bin_hz = rate / size
    reach = max(1, int(0.12 * f0 / bin_hz))
    return np.array([spec[max(0, int(round(h * f0 / bin_hz)) - reach):int(round(h * f0 / bin_hz)) + reach + 1].max() for h in range(1, count + 1)])


def pitch(mono, rate, start, end, lowest, highest, cents):
    a = int((start + 0.04) * rate)
    b = int(min(end, start + 0.30) * rate)
    if b - a < int(0.05 * rate):
        a, b = int(start * rate), int(max(end, start + 0.06) * rate)
    seg = mono[a:b]
    # The peaks of the spectrum, each counted once: a candidate gets the root amplitude of every peak that sits on one
    # of its harmonics. (Summing the spectrum at the harmonics instead lets a low candidate, with harmonics every 35 Hz,
    # collect the skirts of a short note's broad peaks and win.)
    size = 1 << 16
    spec = spectrum(seg, size)
    bin_hz = rate / size
    top = int(1500.0 / bin_hz)
    floor = spec[:top].max() * 10.0 ** (-42.0 / 20.0)
    peaks = []
    for k in range(int(25.0 / bin_hz), top):
        if spec[k] >= floor and spec[k] > spec[k - 1] and spec[k] >= spec[k + 1]:
            y0, y1, y2 = spec[k - 1], spec[k], spec[k + 1]
            peaks.append(((k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-18)) * bin_hz, y1))
    scores = {}
    for midi in range(lowest, highest + 1):
        f0 = 440.0 * 2.0 ** ((midi - 69 + cents / 100.0) / 12.0)
        total = 0.0
        for hz, amplitude in peaks:
            h = round(hz / f0)
            if h >= 1 and abs(hz - h * f0) <= max(2.5, 0.02 * hz):
                total += amplitude ** 0.5
        scores[midi] = total
    best = max(scores.values())
    return max(m for m, s in scores.items() if s >= 0.9 * best)


def tuning(mono, rate, starts, lowest, highest):
    """The cents offset at which the longest notes' harmonics are strongest."""
    longest = sorted(range(len(starts) - 1), key=lambda i: starts[i + 1] - starts[i], reverse=True)[:6]
    totals = {}
    for cents in range(-50, 51, 5):
        total = 0.0
        for i in longest:
            seg = mono[int((starts[i] + 0.04) * rate):int(min(starts[i + 1], starts[i] + 0.4) * rate)]
            best = 0.0
            for midi in range(lowest, highest + 1):
                f0 = 440.0 * 2.0 ** ((midi - 69 + cents / 100.0) / 12.0)
                size = 1 << 17
                spec = spectrum(seg, size)
                levels = [spec[int(round(h * f0 / (rate / size)))] for h in range(1, max(2, int(1500.0 / f0)) + 1)]
                best = max(best, float(np.sqrt(levels).sum()))
            total += best
        totals[cents] = total
    return max(totals, key=totals.get)


def velocity_of(mono, rate, start, length, midi, cents, cache):
    f0 = 440.0 * 2.0 ** ((midi - 69 + cents / 100.0) / 12.0)
    periods = 4 if length > 4.5 / f0 + 0.01 else max(2, int((length - 0.005) * f0))
    n = int(round(periods * rate / f0))
    seg = mono[int((start + 0.008) * rate):int((start + 0.008) * rate) + n]
    if len(seg) < n:
        return 100, 0.0
    mine = 20 * np.log10(harmonic_levels(seg, rate, f0, 14) + 1e-9)
    mine -= mine.max()
    best, best_gap = 100, 1e9
    for velocity in VELOCITIES:
        tag = (midi, velocity, periods)
        if tag not in cache:
            model_rate, x = tx81z.render(tx81z.LATELY_BASS, midi + 24, velocity, 1.0, 0.01 + (periods + 1) / (440.0 * 2.0 ** ((midi - 69) / 12.0)))
            f_model = 440.0 * 2.0 ** ((midi - 69) / 12.0)
            m = int(round(periods * model_rate / f_model))
            ref = 20 * np.log10(harmonic_levels(x[int(0.008 * model_rate):int(0.008 * model_rate) + m], model_rate, f_model, 14) + 1e-9)
            cache[tag] = ref - ref.max()
        ref = cache[tag]
        heard = (ref > -36.0) | (mine > -36.0)
        gap = float(np.mean(np.abs(np.maximum(ref, -45.0) - np.maximum(mine, -45.0))[heard]))
        if gap < best_gap:
            best, best_gap = velocity, gap
    return best, best_gap


if __name__ == "__main__":
    path, begin, end = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    lowest, highest, out = int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
    rate, mono = read(path, begin, end)
    starts = [refine_start(mono, rate, t) for t in find_starts(mono, rate)]
    if not starts:
        raise SystemExit("no note starts found")
    hop, levels = level_track(mono, rate)
    cents = int(sys.argv[7]) if len(sys.argv) > 7 else tuning(mono, rate, starts + [len(mono) / rate], lowest, highest)
    notes, cache = [], {}
    for i, start in enumerate(starts):
        limit = starts[i + 1] if i + 1 < len(starts) else len(mono) / rate
        up = key_up(levels, hop, rate, start, limit)
        length = (up if up is not None else limit) - start
        midi = pitch(mono, rate, start, start + length, lowest, highest, cents)
        velocity, gap = velocity_of(mono, rate, start, length, midi, cents, cache)
        # A short note's octave is the hardest call: the octave whose tone, by the chip's rules, is clearly closer wins.
        for other in (midi + 12, midi - 12):
            if lowest <= other <= highest:
                other_velocity, other_gap = velocity_of(mono, rate, start, length, other, cents, cache)
                if other_gap < gap - 2.0:
                    midi, velocity, gap = other, other_velocity, other_gap
        notes.append({"start": round(start, 3), "length": round(length, 3), "midi": midi, "struck": True, "velocity": velocity})
        print(f"  {start:7.3f} s  {length:6.3f} s  {NAMES[midi % 12]}{midi // 12 - 1:<2d} ({midi})  velocity {velocity:3d}  "
              f"(tone within {gap:4.1f} dB){'' if up is not None else '  held to the next note'}")
    with open(out, "w", encoding="utf-8", newline="\n") as handle:
        json.dump({"tuning_cents": cents, "notes": notes}, handle, indent=1)
    print(f"{len(notes)} notes; tuning {cents:+d} cents")

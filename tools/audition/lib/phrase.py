"""A transcribed phrase (transcribe_mono.py) as notes for CoolSynthRender, and a way to compare two
recordings of the same phrase.

notes_spec   the --notes text that makes a single-trigger, low-note-priority mono preset play the
             phrase as it was played: a struck note after a gap of 12 ms (the envelopes start
             again), a legato one overlapping its neighbour (they run on). With low-note priority
             the pitch moves up when the lower key is let go and down when the lower key goes
             down, so the overlap is put on the side that moves the pitch at the transcribed time.
band_levels  the level of ten octave bands (31 Hz to 16 kHz) in 10 ms steps, in dB against the
             clip's own overall level, smoothed over 30 ms: what a listener compares, without
             needing the waveforms to line up.
"""
import numpy as np

OVERLAP, GAP = 0.03, 0.012
BAND_EDGES = [22.0 * 2 ** i for i in range(11)]  # 22, 44, ... 22.5 kHz


def notes_spec(notes, key_offset, velocity=100):
    keys = [n["midi"] + key_offset for n in notes]
    starts = [n["start"] for n in notes]
    ends = [n["start"] + n["length"] for n in notes]
    for i in range(len(notes) - 1):
        touching = starts[i + 1] - ends[i] < 0.02
        if not touching:
            continue
        turn = notes[i + 1]["start"]
        if notes[i + 1]["struck"]:
            ends[i] = turn - GAP
        elif keys[i + 1] > keys[i]:
            ends[i] = turn
            starts[i + 1] = max(starts[i] + 0.005, turn - OVERLAP)
        else:
            ends[i] = turn + OVERLAP
    return ",".join(f"{k}:{max(0.0, s):.3f}:{max(0.02, e - s):.3f}:{velocity}" for k, s, e in zip(keys, starts, ends))


def band_levels(mono, rate):
    """(times, levels[time, band] in dB against the clip's overall level, overall level per step in dB)."""
    window, hop = int(0.046 * rate), int(0.010 * rate)
    size = 1 << int(np.ceil(np.log2(window)))
    hann = np.hanning(window)
    hz = np.fft.rfftfreq(size, 1.0 / rate)
    masks = [(hz >= low) & (hz < high) for low, high in zip(BAND_EDGES[:-1], BAND_EDGES[1:])]
    overall = float(np.mean(mono ** 2)) + 1e-18
    rows, totals = [], []
    for at in range(0, len(mono) - window, hop):
        power = np.abs(np.fft.rfft(mono[at:at + window] * hann, size)) ** 2
        scale = 1.0 / (np.sum(hann ** 2) * size / 2.0)
        rows.append([power[m].sum() * scale for m in masks])
        totals.append(float(np.mean(mono[at:at + window] ** 2)))
    rows = np.array(rows) / overall
    kernel = np.ones(3) / 3.0
    rows = np.apply_along_axis(lambda column: np.convolve(column, kernel, mode="same"), 0, rows)
    times = (np.arange(len(rows)) * hop + window / 2) / rate
    return times, 10 * np.log10(rows + 1e-12), 10 * np.log10(np.array(totals) / overall + 1e-12)

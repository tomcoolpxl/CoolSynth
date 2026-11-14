"""A transcribed phrase (transcribe_mono.py) as notes for CoolSynthRender, and a way to compare two
recordings of the same phrase.

chords_spec  the --notes text for transcribed chords: all keys of a chord together.
notes_spec   the --notes text that makes a single-trigger, low-note-priority mono preset play the
             phrase as it was played: a struck note after a gap of 12 ms (the envelopes start
             again), a legato one overlapping its neighbour (they run on). With low-note priority
             the pitch moves up when the lower key is let go and down when the lower key goes
             down, so the overlap is put on the side that moves the pitch at the transcribed time.
poly_spec    the --notes text for a polyphonic part with held notes (transcribe_held_chords.py): every note as
             listed, from its start for its length; a note held across a chord change is one long note.
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
    velocities = [n.get("velocity", velocity) for n in notes]   # a transcription may say how hard each key was hit
    return ",".join(f"{k}:{max(0.0, s):.3f}:{max(0.02, e - s):.3f}:{v}" for k, s, e, v in zip(keys, starts, ends, velocities))


def poly_spec(notes, key_offset=0, velocity=100, shift=0.0, limit=None):
    out = []
    for n in notes:
        start = n["start"] + shift
        length = n["length"] if limit is None else min(n["length"], limit - n["start"])
        if length > 0.02:
            out.append(f"{n['midi'] + key_offset}:{max(0.0, start):.3f}:{length:.3f}:{n.get('velocity', velocity)}")
    return ",".join(out)


def chords_spec(events, velocity=100, key_up_early=0.0):
    """The --notes text for chords (transcribe_chords.py): every key of an event goes down at its start and up at
    its end, less `key_up_early` seconds where the event's length was measured to the end of the sound's release."""
    notes = []
    for event in events:
        for key in event["keys"]:
            notes.append(f"{key}:{event['start']:.3f}:{max(0.05, event['length'] - key_up_early):.3f}:{velocity}")
    return ",".join(notes)


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

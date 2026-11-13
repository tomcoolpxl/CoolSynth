"""Small audio helpers for looking at downloaded recordings: WAV in and out, pitch, partials, decay.

Only the standard library and numpy. Files are parsed by hand, chunk by chunk, so odd headers
(24-bit, WAVE_FORMAT_EXTENSIBLE, extra chunks, odd padding) do not stop the reader.
"""
import math
import struct

import numpy as np

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def read_wav(path, max_seconds=None):
    """Returns (sample_rate, float32 array of shape [frames, channels]) or raises ValueError."""
    with open(path, "rb") as handle:
        blob = handle.read()
    if len(blob) < 12 or blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    position = 12
    fmt = None
    data = None
    while position + 8 <= len(blob):
        tag = blob[position:position + 4]
        size = struct.unpack("<I", blob[position + 4:position + 8])[0]
        body = blob[position + 8:position + 8 + size]
        if tag == b"fmt ":
            fmt = body
        elif tag == b"data":
            data = body
            break
        position += 8 + size + (size & 1)
    if fmt is None or data is None or len(fmt) < 16:
        raise ValueError("no fmt or data chunk")
    code, channels, rate, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    if code == 0xFFFE and len(fmt) >= 26:  # extensible: the real code is in the sub-format
        code = struct.unpack("<H", fmt[24:26])[0]
    if channels < 1 or rate < 1000:
        raise ValueError("implausible header")
    width = bits // 8
    frames = len(data) // (width * channels)
    if max_seconds is not None:
        frames = min(frames, int(max_seconds * rate))
    raw = data[:frames * width * channels]
    if code == 1 and bits == 16:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif code == 1 and bits == 24:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float32) / float(1 << 23)
    elif code == 1 and bits == 32:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float32) / float(1 << 31)
    elif code == 3 and bits == 32:
        x = np.frombuffer(raw, dtype="<f4").astype(np.float32)
    elif code == 1 and bits == 8:
        x = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
    else:
        raise ValueError(f"unsupported format code {code} with {bits} bits")
    return rate, x.reshape(-1, channels)


def write_wav16(path, rate, x):
    """Writes float data [frames, channels] (or [frames]) as 16-bit PCM."""
    x = np.asarray(x, dtype=np.float32)
    if x.ndim == 1:
        x = x[:, None]
    pcm = (np.clip(x, -1.0, 1.0) * 32767.0).round().astype("<i2").tobytes()
    channels = x.shape[1]
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE" + b"fmt " + struct.pack(
        "<IHHIIHH", 16, 1, channels, rate, rate * channels * 2, channels * 2, 16) + b"data" + struct.pack("<I", len(pcm))
    with open(path, "wb") as handle:
        handle.write(header + pcm)


def mono(x):
    return x.mean(axis=1) if x.ndim == 2 else x


def hz_to_midi(hz):
    return 69.0 + 12.0 * math.log2(hz / 440.0)


def midi_to_hz(midi):
    return 440.0 * 2.0 ** ((midi - 69.0) / 12.0)


def note_name(midi):
    nearest = int(round(midi))
    return f"{NOTE_NAMES[nearest % 12]}{nearest // 12 - 1}"


def spectrum(x, rate, start_seconds, length_seconds, pad=4):
    """Hann-windowed magnitude spectrum of a stretch; returns (frequencies, magnitudes)."""
    begin = int(start_seconds * rate)
    count = min(len(x) - begin, int(length_seconds * rate))
    if count < 256:
        return np.zeros(1), np.zeros(1)
    segment = x[begin:begin + count] * np.hanning(count)
    size = 1 << int(math.ceil(math.log2(count * pad)))
    magnitudes = np.abs(np.fft.rfft(segment, size)) * 2.0 / max(1.0, np.hanning(count).sum())
    return np.fft.rfftfreq(size, 1.0 / rate), magnitudes


def peak_near(frequencies, magnitudes, hz, cents=60.0):
    """The strongest bin within +-cents of hz, refined by a parabola: (frequency, level) or (0, 0)."""
    low, high = hz * 2.0 ** (-cents / 1200.0), hz * 2.0 ** (cents / 1200.0)
    indices = np.nonzero((frequencies >= low) & (frequencies <= high))[0]
    if len(indices) < 3:
        return 0.0, 0.0
    i = indices[np.argmax(magnitudes[indices])]
    if 0 < i < len(magnitudes) - 1:
        a, b, c = magnitudes[i - 1], magnitudes[i], magnitudes[i + 1]
        denominator = a - 2.0 * b + c
        offset = 0.5 * (a - c) / denominator if denominator != 0.0 else 0.0
        step = frequencies[1] - frequencies[0]
        return float(frequencies[i] + offset * step), max(0.0, float(b - 0.25 * (a - c) * offset))
    return float(frequencies[i]), float(magnitudes[i])


def fundamental(x, rate, start_seconds=0.05, length_seconds=0.5, low_hz=25.0, high_hz=4500.0):
    """Estimates the pitch of a note: the lowest strong peak whose harmonics also carry energy.

    Returns Hz, or 0 when nothing pitched is found."""
    frequencies, magnitudes = spectrum(x, rate, start_seconds, length_seconds)
    if len(magnitudes) < 8 or magnitudes.max() <= 0.0:
        return 0.0
    band = (frequencies >= low_hz) & (frequencies <= high_hz)
    if not band.any():
        return 0.0
    floor = magnitudes[band].max() * 0.08
    # Local maxima above the floor, lowest first.
    inner = magnitudes[1:-1]
    maxima = np.nonzero((inner > magnitudes[:-2]) & (inner >= magnitudes[2:]) & (inner > floor))[0] + 1
    best, best_score = 0.0, 0.0
    for i in maxima:
        hz = frequencies[i]
        if hz < low_hz or hz > high_hz:
            continue
        # Score a candidate by the energy at it and at its first harmonics, minus energy at the sub-octave.
        score = 0.0
        for k in (1, 2, 3, 4):
            _, level = peak_near(frequencies, magnitudes, hz * k, 35.0)
            score += level / k
        _, below = peak_near(frequencies, magnitudes, hz * 0.5, 35.0)
        score -= below
        if score > best_score * 1.15:  # prefer the lower candidate unless a higher one is clearly stronger
            best, best_score = peak_near(frequencies, magnitudes, hz, 35.0)[0], score
    return best


def envelope_db(x, rate, hop_seconds=0.01, window_seconds=0.03):
    """RMS level in dB every hop: (times, levels)."""
    hop, window = max(1, int(hop_seconds * rate)), max(2, int(window_seconds * rate))
    count = max(0, (len(x) - window) // hop)
    levels = np.empty(count)
    for n in range(count):
        chunk = x[n * hop:n * hop + window]
        levels[n] = 10.0 * math.log10(float(np.mean(chunk * chunk)) + 1.0e-20)
    return np.arange(count) * hop_seconds + 0.5 * window_seconds, levels


def decay_t60(times, levels_db, top_db=-3.0, bottom_db=-33.0):
    """Fits a line to the decay between two levels below the peak; returns the 60 dB time or 0."""
    if len(levels_db) < 8:
        return 0.0
    peak_index = int(np.argmax(levels_db))
    peak = levels_db[peak_index]
    tail_t, tail_l = times[peak_index:], levels_db[peak_index:] - peak
    usable = (tail_l <= top_db) & (tail_l >= bottom_db)
    # Stop at the first time the level drops below the bottom: later rises are noise.
    below = np.nonzero(tail_l < bottom_db)[0]
    if len(below):
        usable[below[0]:] = False
    if usable.sum() < 5:
        return 0.0
    slope, _ = np.polyfit(tail_t[usable], tail_l[usable], 1)
    return float(-60.0 / slope) if slope < 0.0 else 0.0


def partial_track(x, rate, hz, start_seconds, length_seconds=0.12):
    """Level (linear) of the component near hz in a short window starting at start_seconds."""
    frequencies, magnitudes = spectrum(x, rate, start_seconds, length_seconds)
    return peak_near(frequencies, magnitudes, hz, 40.0)


def partial_t60(x, rate, hz, first=0.03, last=None, step=0.04, window=0.12):
    """60 dB time of one partial, from its level over time; 0 when it cannot be fitted."""
    last = last if last is not None else len(x) / rate - window
    times, levels = [], []
    t = first
    while t < last:
        _, level = partial_track(x, rate, hz, t, window)
        times.append(t + 0.5 * window)
        levels.append(20.0 * math.log10(level + 1.0e-12))
        t += step
    return decay_t60(np.array(times), np.array(levels))

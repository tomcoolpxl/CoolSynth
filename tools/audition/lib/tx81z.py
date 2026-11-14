"""A plain model of a Yamaha TX81Z voice from the chip's rules, slow and exact enough to stand in for the instrument
where no recording exists: other keys, other key velocities. Checked against a recording of a real TX81Z playing
LatelyBass (key 48, velocity 127): the first sixteen harmonics agree within a decibel or two over half a second.

The rules (ymfm's OPZ core for the chip; the TX81Z firmware for the velocity table and the voice data):
  operators are computed 4, 3, 2, 1; phases start at zero at key-on;
  a modulator at level 99 with its envelope open swings the phase of what it drives by four turns (8 pi);
  feedback n swings the feedback operator's own phase by its output, through the mean of its last two
  outputs: 4 pi at feedback 7 and full level, half that for every step down, nothing at 0;
  an operator's level, its key velocity and its envelope are attenuations that add, in dB: 0.75 dB a
  level step under 99; velocity (15 - 2 KVS) + floor(map[velocity] * KVS / 8) steps of 0.375 dB, nothing
  at KVS 0 (so a voice with KVS on is quieter than its level says even at full velocity);
  a decay runs at 0.1067 (4 + (r & 3)) 2^(r >> 2) dB a second, r = 2 * rate + (keycode >> (3 - RS)), keycode
  = floor((sounding note - 13) / 3); first to 3 dB * (15 - D1L) down (never stopping at D1L 0), then at
  the second rate; the release at r = 4 * RR + 2 + the same key part; attack rate 31 is at once;
  the chip runs at 55.93 kHz.
Left out: detune, the LFO, level scaling, slower attacks, the output converter's ten bits.

A voice is {"algorithm": 1..8, "feedback": 0..7, "transpose": semitones, "ops": {1: (...), 2: ..., 3: ..., 4: ...}} with
each operator (ratio, level, D1R, D1L, D2R, RR, KVS, RS, wave 1..8).
"""
import math

import numpy as np

CHIP_RATE = 55930.0

VELOCITY_MAP = bytes.fromhex(
    "FEC0B4AEA8A29E98" "94908D8A8682807D" "7A777472706D6B69" "676563615F5D5C5A" "5856555352514F4E" "4C4B4A4847464443"
    "4241403F3E3C3B3A" "3938373635343231" "302F2E2D2C2B2A29" "2827262524232221" "201F1E1D1C1B1A1A" "1918171616151413"
    "12121110100F0E0E" "0D0C0C0B0B0A0909" "0807070606050504" "0403030202010100")
assert len(VELOCITY_MAP) == 128

# Preset C15, from the firmware's preset table.
LATELY_BASS = {
    "algorithm": 2, "feedback": 7, "transpose": -12,
    "ops": {1: (0.5, 99, 9, 0, 0, 8, 0, 1, 1),
            2: (0.5, 74, 9, 0, 0, 8, 1, 1, 1),
            3: (1.0, 71, 17, 0, 0, 8, 1, 1, 5),
            4: (1.0, 79, 16, 3, 7, 8, 3, 0, 1)},
}

# Which operators modulate each operator, and which sound, per algorithm (the TX81Z's numbering).
ROUTES = {
    1: ({3: [4], 2: [3], 1: [2]}, [1]),
    2: ({2: [3, 4], 1: [2]}, [1]),
    3: ({2: [3], 1: [2, 4]}, [1]),
    4: ({3: [4], 1: [2, 3]}, [1]),
    5: ({3: [4], 1: [2]}, [1, 3]),
    6: ({1: [4], 2: [4], 3: [4]}, [1, 2, 3]),
    7: ({3: [4]}, [1, 2, 3]),
    8: ({}, [1, 2, 3, 4]),
}


def velocity_db(velocity, kvs):
    """What a key velocity takes off an operator's level, in dB."""
    if kvs == 0:
        return 0.0
    return 0.375 * ((15 - 2 * kvs) + (VELOCITY_MAP[max(0, min(127, velocity))] * kvs) // 8)


def decay_db_per_second(effective_rate):
    r = max(0, min(63, effective_rate))
    return 0.1067 * (4 + (r & 3)) * 2.0 ** (r >> 2)


def keycode(sounding_note):
    return max(0, min(31, (sounding_note - 13) // 3))


def wave(shape, phase):
    """The eight operator waves at a phase in turns."""
    p = phase - math.floor(phase)
    s = math.sin(2 * math.pi * p)
    if shape == 1:
        return s
    if shape == 2:
        return s * abs(s)
    if p >= 0.5:
        return 0.0
    if shape == 3:
        return s
    if shape == 4:
        return s * s
    s2 = math.sin(4 * math.pi * p)
    if shape == 5:
        return s2
    if shape == 6:
        return s2 * abs(s2)
    if shape == 7:
        return abs(s2)
    return s2 * s2


def render(voice, key, velocity, held, total, rate=CHIP_RATE, op_scale=None):
    """(rate, samples) of one note: `key` as played, down for `held` seconds, `total` seconds in all."""
    sounding = key + voice["transpose"]
    hz = 440.0 * 2.0 ** ((sounding - 69) / 12.0)
    kc = keycode(sounding)
    n = int(total * rate)
    release_at = int(held * rate)
    ops = voice["ops"]
    gain = {}
    for op, (ratio, level, d1r, d1l, d2r, rr, kvs, rs, shape) in ops.items():
        ksr = kc >> (3 - rs)
        rate1 = decay_db_per_second(2 * d1r + ksr) if d1r else 0.0
        rate2 = decay_db_per_second(2 * d2r + ksr) if d2r else 0.0
        rate_r = decay_db_per_second(4 * rr + 2 + ksr)
        floor_db = 3.0 * (15 - d1l) if d1l else 96.0
        att = np.zeros(n)
        value = 0.0
        for i in range(n):
            att[i] = value
            if i >= release_at:
                value += rate_r / rate
            elif value < floor_db:
                value = min(floor_db, value + rate1 / rate)
            else:
                value += rate2 / rate
            value = min(value, 96.0)
        g = 10.0 ** (-(0.75 * (99 - level) + velocity_db(velocity, kvs) + att) / 20.0) * (op_scale or {}).get(op, 1.0)
        g[att >= 96.0] = 0.0
        gain[op] = g
    modulated_by, carriers = ROUTES[voice["algorithm"]]
    feedback_turns = 2.0 * 2.0 ** (voice["feedback"] - 7) if voice["feedback"] else 0.0
    out = np.zeros(n)
    phase = {op: 0.0 for op in ops}
    step = {op: ops[op][0] * hz / rate for op in ops}
    y = {op: 0.0 for op in ops}
    f1 = f2 = 0.0
    for i in range(n):
        for op in (4, 3, 2, 1):
            turns = phase[op] + 4.0 * sum(y[m] for m in modulated_by.get(op, []))
            if op == 4:
                turns += feedback_turns * 0.5 * (f1 + f2)
            y[op] = gain[op][i] * wave(ops[op][8], turns)
            phase[op] += step[op]
        f2, f1 = f1, y[4]
        out[i] = sum(y[c] for c in carriers)
    return rate, out

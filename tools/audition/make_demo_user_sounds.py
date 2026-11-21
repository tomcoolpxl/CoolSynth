"""Put a demonstration kit and loop in the User folder, so slices and kits (D207) can be tried without one's own files.

Writes, under <user-folder>:
    Demo Kit/01 Kick.wav .. 08 Bass.wav   eight one-shots rendered from factory presets (a folder plays as a kit)
    Demo Loop.wav                          two bars at 120 BPM made of those hits (cut it with Slices: Attacks or
                                           Equal 16)

usage: python make_demo_user_sounds.py <user-folder>
"""
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
RENDER = os.path.join(REPO, "build", "CoolSynthRender_artefacts", "Debug", "CoolSynthRender.exe")
RATE = 48000

user = sys.argv[1]
kit = os.path.join(user, "Demo Kit")
os.makedirs(kit, exist_ok=True)

# (file, preset, note, seconds)
HITS = [
    ("01 Kick.wav", "808 Boom", 36, 0.9),
    ("02 Knock.wav", "FM Knock", 48, 0.5),
    ("03 Metal.wav", "Metal Hit", 60, 0.8),
    ("04 Static.wav", "Static Hit", 60, 0.4),
    ("05 Pluck.wav", "Crystal Pluck", 72, 0.9),
    ("06 Bell.wav", "Glass Bell", 72, 1.5),
    ("07 Chord.wav", "Future Chord", 57, 0.6),
    ("08 Bass.wav", "Square Grit", 36, 0.6),
]


def read24(path):
    with wave.open(path, "rb") as w:
        channels, width, frames = w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = np.frombuffer(w.readframes(frames), dtype=np.uint8)
    assert width == 3, path
    b = raw.reshape(-1, 3).astype(np.int32)
    v = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
    v = np.where(v >= 1 << 23, v - (1 << 24), v)
    return (v.astype(np.float64) / (1 << 23)).reshape(-1, channels)


def write24(path, x):
    x = np.clip(x, -1.0, 1.0 - 1.0 / (1 << 23))
    v = np.round(x * (1 << 23)).astype(np.int32).reshape(-1)
    v = np.where(v < 0, v + (1 << 24), v).astype(np.uint32)
    b = np.stack([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF], axis=1).astype(np.uint8)
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(3)
        w.setframerate(RATE)
        w.writeframes(b.tobytes())


sounds = []
for name, preset, note, seconds in HITS:
    path = os.path.join(kit, name)
    result = subprocess.run([RENDER, "--preset", preset, "--notes", f"{note}:0:{min(0.25, seconds)}",
                             "--seconds", str(seconds), "--out", path], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"{preset}: {result.stdout}{result.stderr}")
    x = read24(path)
    peak = np.max(np.abs(x))
    x = x * (0.7 / peak if peak > 0 else 1.0)  # every hit at the same peak
    fade = int(0.01 * RATE)
    x[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    write24(path, x)
    sounds.append(x)
    print("kit:", name, "from", preset)

# Two bars of eighths at 120 BPM: which hit on each eighth (-1 rest).
eighth = RATE // 4
pattern = [0, 3, 1, 3, 0, 4, 1, 2, 0, 3, 1, 6, 0, 5, 7, 2]
loop = np.zeros((eighth * len(pattern), 2))
for step, hit in enumerate(pattern):
    if hit < 0:
        continue
    x = sounds[hit][: eighth * 2]
    start = step * eighth
    end = min(len(loop), start + len(x))
    loop[start:end] += x[: end - start] * 0.8
loop *= 0.8 / np.max(np.abs(loop))
write24(os.path.join(user, "Demo Loop.wav"), loop)
print("loop: Demo Loop.wav,", len(pattern), "eighths at 120 BPM")

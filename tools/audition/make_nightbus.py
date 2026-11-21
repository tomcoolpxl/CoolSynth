"""Render the Night Bus room (D206) and the Pitch effect (D205) for the listening page.

Writes, under <audition-dir>/nightbus/:
    <id>.wav        each Night Bus preset playing a phrase in its role, at its own level
    pitch-<id>.wav  one plain keys sound five ways: dry, and through the Pitch effect in each mode
and index.json, which make_page.py turns into the "Night Bus" cards at the top of the page.

usage: python make_nightbus.py <audition-dir>
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
RENDER = os.path.join(REPO, "build", "CoolSynthRender_artefacts", "Debug", "CoolSynthRender.exe")
BANK = os.path.join(REPO, "resources", "presets", "13-night-bus.csbank")

out = os.path.join(sys.argv[1], "nightbus")
os.makedirs(out, exist_ok=True)


def chord(notes, start, length, velocity=96):
    return [f"{n}:{start}:{length}:{velocity}" for n in notes]


def line(*events):
    return ",".join(f"{n}:{s}:{l}" for n, s, l in events)


# (id, preset, notes, seconds, controller moves, what to listen for)
PRESETS = [
    ("washed-poly", "Washed Poly",
     ",".join(chord((57, 60, 64, 67), 0, 2.2) + chord((53, 57, 60, 64), 2.2, 2.2) + chord((55, 59, 62, 67), 4.4, 2.2)), 9, "",
     "Warm chords, washed wide and slightly out of focus, with a little crackle (after Loukeman)."),
    ("tine-clav", "Tine Clav",
     ",".join(chord((57, 60, 64), 0, 0.35) + chord((57, 60, 64), 0.5, 0.2) + chord((57, 60, 64), 0.75, 0.35)
              + chord((57, 60, 64), 1.5, 0.35) + chord((55, 59, 62), 2.0, 0.35) + chord((55, 59, 62), 2.5, 0.2)
              + chord((55, 59, 62), 2.75, 0.35) + chord((53, 57, 60), 3.5, 0.9) + chord((52, 55, 59), 4.5, 1.2)), 8, "",
     "Rhodes-and-clav keys in a wobbling tape echo (after Caribou, \"Can't Do Without You\")."),
    ("buzz-drop", "Buzz Drop",
     line((33, 0, 0.4), (33, 0.5, 0.2), (45, 0.75, 0.2), (33, 1.0, 0.4), (36, 1.5, 0.4), (38, 2.0, 0.9), (40, 3.0, 0.4),
          (38, 3.5, 0.4), (36, 4.0, 1.5)), 7, "",
     "A buzzy, gliding drop bass (Caribou). Its Rip macro brings in the rough octave; not heard in this clip."),
    ("square-grit", "Square Grit",
     line((36, 0, 0.2), (36, 0.25, 0.2), (43, 0.5, 0.2), (36, 0.75, 0.2), (39, 1.0, 0.4), (41, 1.5, 0.2), (43, 1.75, 0.6),
          (36, 2.5, 0.2), (36, 2.75, 0.2), (43, 3.0, 0.2), (36, 3.25, 0.2), (39, 3.5, 0.4), (41, 4.0, 0.2), (34, 4.25, 0.9)),
     6, "2.5:1:110", "A hard-saturated square bass (ear, \"Nerves\"); the mod wheel goes up halfway through."),
    ("whisper-air", "Whisper Air", line((60, 0, 3), (67, 1.5, 3), (63, 3, 3)), 8, "",
     "Whispers: breathy noise shaped like vowels, with an airy copy an octave up (ear, meat computer)."),
    ("fizz-melody", "Fizz Melody",
     line((68, 0, 0.25), (72, 0.25, 0.25), (75, 0.5, 0.5), (73, 1.0, 0.25), (72, 1.25, 0.25), (70, 1.5, 0.5), (68, 2.0, 0.25),
          (72, 2.25, 0.25), (77, 2.5, 0.75), (75, 3.25, 0.25), (72, 3.5, 1.0)), 7, "",
     "A bright, fuzzy lead ducking on the beat, with quiet violins under it (Vegyn, \"Debold\")."),
    ("pirate-radio", "Pirate Radio",
     ",".join(chord((60, 63, 67), 0, 1.8) + chord((58, 62, 65), 2, 1.8) + chord((56, 60, 63), 4, 1.8)), 8, "",
     "Keys on a transistor radio that never quite holds the station (the Mk.gee remix)."),
    ("stutter-chords", "Stutter Chords", line((60, 0, 3.8), (57, 4, 3.8)), 9, "",
     "One key plays a chord that stutters, each stutter echoed an octave higher (ear, \"Real Life\")."),
    ("vocal-mask", "Vocal Mask", line((57, 0, 2), (60, 2, 2), (64, 4, 2.5)), 8, "",
     "A sung \"ah\" cut into grains and disguised as a synth, with an octave-down double (Loukeman)."),
    ("spiral-fm", "Spiral FM", line((57, 0, 6)), 8, "",
     "One held key: a plucked pattern that turns against the bar, its echoes falling (after Daphni)."),
    ("trance-glass", "Trance Glass",
     ",".join(chord((64, 67, 71, 76), 0, 1.5) + chord((62, 65, 69, 74), 1.5, 1.5) + chord((60, 64, 67, 72), 3, 1.5)), 7, "",
     "Wide bright saws with a bell on top and crystal echoes climbing an octave each (digicore)."),
    ("bowed-haze", "Bowed Haze", line((55, 0, 2.5), (57, 2, 2.5), (60, 4, 3)), 9, "",
     "Recorded violas, slow to swell, with a darker octave-down shadow (after Astrid Sonne)."),
    ("stepped-lead", "Stepped Lead", line((69, 0, 0.5), (72, 0.5, 0.5), (74, 1.0, 1.5), (76, 2.5, 0.5), (74, 3.0, 2.0)), 7,
     "3:1:127", "A lead that jumps between semitones like a hard-tuned voice; the mod wheel goes up at 3 s."),
    ("sea-pad", "Sea Pad", ",".join(chord((57, 60, 64), 0, 8)), 11, "",
     "A slow, worn pad over a soft bass the arpeggiator repeats (after Huerco S.)."),
]

# The Pitch effect alone: the same keys dry, then through each mode.
DEMO_BANK = os.path.join(out, "pitch-demo.csbank")
DEMO_SOURCE = """g1 = VA shape=saw level=0.5
g2 = VA shape=pulse pw=0.4 fine=5 level=0.4
p1 = Ladder cutoff=2400 res=0.1 key=0.5
env1 = attack=3 decay=700 sustain=0.7 release=400 velocity=0.4
voice = env2ToP1=0 drift=2
mix = level=-10
master = 0
"""
DEMOS = [
    ("dry", "No effect", ""),
    ("heads-clean", "Heads, a fifth up, clean", "i1 = Pitch pitch=7 mix=0.5 window=50 character=0"),
    ("heads-rough", "Heads, a fifth up, rough", "i1 = Pitch pitch=7 mix=0.5 window=50 character=1"),
    ("spectral", "Spectral, a fifth up", "i1 = Pitch pitch=7 mix=0.5 mode=spectral window=50 character=0"),
    ("wide", "Heads, no shift, Spread 15 cents (a wide double)", "i1 = Pitch pitch=0 spread=15 mix=0.5 window=60 character=0.25"),
    ("staircase", "Spectral staircase: an octave up per echo, every 1/8",
     "i1 = Pitch pitch=12 mix=0.35 mode=spectral sync=on division=1/8 feedback=0.5 window=40 character=0"),
]
DEMO_NOTES = ",".join([f"{n}:{s}:{l}" for n, s, l in ((60, 0, 0.4), (64, 0.5, 0.4), (67, 1.0, 0.4), (72, 1.5, 0.9))]
                      + chord((57, 60, 64), 2.6, 2.0))

with open(DEMO_BANK, "w", encoding="utf-8") as f:
    for demo_id, title, effect in DEMOS:
        f.write(f"[Pitch {demo_id}]\nroom = Workshop\n{DEMO_SOURCE}{effect}\n\n")


def render(preset, notes, seconds, cc, bank, path):
    args = [RENDER, "--bank", bank, "--preset", preset, "--notes", notes, "--seconds", str(seconds), "--out", path]
    if cc:
        args += ["--cc", cc]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0 or "error" in (result.stdout + result.stderr).lower():
        raise SystemExit(f"{preset}: {result.stdout}{result.stderr}")
    return result.stdout.strip().splitlines()[-1]


index = {"presets": [], "pitch": []}
for preset_id, name, notes, seconds, cc, listen in PRESETS:
    print(render(name, notes, seconds, cc, BANK, os.path.join(out, preset_id + ".wav")))
    index["presets"].append({"id": preset_id, "title": name, "listen": listen, "file": preset_id + ".wav"})
for demo_id, title, _ in DEMOS:
    print(render(f"Pitch {demo_id}", DEMO_NOTES, 6, "", DEMO_BANK, os.path.join(out, f"pitch-{demo_id}.wav")))
    index["pitch"].append({"id": demo_id, "title": title, "file": f"pitch-{demo_id}.wav"})

with open(os.path.join(out, "index.json"), "w", encoding="utf-8") as f:
    json.dump(index, f, indent=1)
print("index written:", os.path.join(out, "index.json"))

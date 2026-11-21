"""
Writes items.json for the listening pass: the hand-written cards in special.json, then a card for every preset of the
bank (resources/presets/*.csbank) that no special card or earlier pass already covers.

    python -I tools/listening-pass/make_items.py

Each bank card loads its preset, plays a phrase that suits it (a bass line, a chord, a melody in the instrument's
range), and offers before-and-after tries: each macro turned to 100 %, a soft and a hard touch. Its idea is the
preset's own description, the reference the user judges it against. Standard library only.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PRESETS = os.path.join(HERE, "..", "..", "resources", "presets")


def seq(*steps, rel=1.0):
    """A phrase: steps of (note or [notes], at, length[, velocity]) in seconds."""
    out = []
    for step in steps:
        notes, at, length = step[0], step[1], step[2]
        vel = step[3] if len(step) > 3 else 100
        for n in (notes if isinstance(notes, list) else [notes]):
            out.append([n, at, length, vel])
    return [{"seq": out, "rel": rel}]


# The phrases, at 120 BPM (an eighth is 0.25 s). 60 is C4.
PHRASES = {
    "lead": seq((60, 0, .45), (63, .5, .45), (67, 1.0, .45), (70, 1.5, .45), (72, 2.0, 1.2)),
    "bass": seq((36, 0, .2), (36, .25, .2), (48, .5, .2), (36, .75, .2), (43, 1.0, .2), (46, 1.25, .2), (36, 1.5, .45),
                (31, 2.0, .9), rel=.6),
    "sub": seq((36, 0, .9), (31, 1.0, .9), (33, 2.0, 1.3), rel=.8),
    "pad": seq(([48, 55, 58, 62], 0, 2.6), ([44, 51, 55, 60], 2.7, 2.6), rel=2.5),
    "keys": seq(([48, 52, 55, 59], 0, .9), ([45, 52, 55, 60], 1.0, .9), ([53, 57, 60, 64], 2.0, 1.6), rel=1.2),
    "piano": seq(([48, 52, 55], 0, .9), (72, 1.0, .25), (74, 1.25, .25), (76, 1.5, .45), ([53, 57, 60], 2.0, 1.6), rel=1.5),
    "pluck": seq((60, 0, .12), (64, .125, .12), (67, .25, .12), (71, .375, .12), (72, .5, .12), (71, .625, .12),
                 (67, .75, .12), (64, .875, .12), (60, 1.0, 1.0), rel=1.5),
    "bell": seq((72, 0, .4), (79, .5, .4), (76, 1.0, .4), (84, 1.5, 1.5), rel=2.5),
    "hit": seq((48, 0, .3), (48, .75, .3), (55, 1.5, .3), rel=1.5),
    "fx": seq((60, 0, 3.5), rel=2.0),
    "hold": seq(([48, 51, 55], 0, 4.0), rel=1.5),
    "drone": seq(([36, 43], 0, 4.0), rel=2.0),
    "kick": seq((36, 0, .3), (36, .5, .3), (36, 1.0, .3), (36, 1.5, .3), (33, 2.0, .8), rel=.8),
    "strings": seq(([48, 55, 60, 64], 0, 2.4), ([53, 57, 60, 65], 2.5, 2.4), rel=1.5),
    "melody_hi": seq((67, 0, .45), (69, .5, .45), (71, 1.0, .45), (74, 1.5, .45), (72, 2.0, 1.2)),
    "melody_mid": seq((55, 0, .45), (57, .5, .45), (60, 1.0, .45), (62, 1.5, .45), (60, 2.0, 1.2)),
    "melody_low": seq((43, 0, .45), (45, .5, .45), (48, 1.0, .45), (50, 1.5, .45), (48, 2.0, 1.2)),
    "lowline": seq((36, 0, .45), (40, .5, .45), (43, 1.0, .45), (45, 1.5, .45), (43, 2.0, 1.2)),
    "glock": seq((84, 0, .3), (88, .4, .3), (91, .8, .3), (96, 1.2, 1.5), rel=2.0),
    "timpani": seq((43, 0, .4), (38, .75, .4), (43, 1.5, 1.5), rel=2.0),
    "didge": seq((38, 0, 4.0), rel=1.5),
    "organ": seq(([48, 52, 55, 60], 0, 1.4), ([53, 57, 60, 65], 1.5, 1.4), ([55, 59, 62, 67], 3.0, 1.6), rel=1.0),
    "glasses": seq((72, 0, 1.8), (76, .9, 1.8), (79, 1.8, 2.0), rel=2.0),
}


def acid(accents=True, slides=True):
    """Sixteen 303 steps at 125 BPM, twice: (note, accent, slide into the next). Acid plays an octave down."""
    steps = [(48, 1, 0), (48, 0, 0), (60, 0, 1), (58, 0, 0), (48, 0, 0), (51, 1, 0), (48, 0, 0), (55, 0, 1),
             (53, 0, 0), (48, 1, 0), (60, 0, 0), (48, 0, 0), (51, 0, 1), (53, 1, 0), (48, 0, 0), (46, 1, 0)]
    step = 60.0 / 125.0 / 4.0
    out = []
    for bar in range(2):
        for i, (note, accent, slide) in enumerate(steps):
            at = (bar * 16 + i) * step
            length = step * 1.25 if (slide and slides) else step * 0.6
            out.append([note, round(at, 4), round(length, 4), 120 if (accent and accents) else 90])
    return [{"seq": out, "rel": 0.6}]


PHRASES["acid"] = acid()
PHRASES["acid_flat"] = acid(accents=False)
PHRASES["acid_noslide"] = acid(slides=False)


def resolve(item):
    """A card's or a try's "phrase" name into its notes."""
    if "phrase" in item:
        item["play"] = PHRASES[item.pop("phrase")]
    for attempt in item.get("tries", []):
        if "phrase" in attempt:
            attempt["play"] = PHRASES[attempt.pop("phrase")]
    return item

# What each preset plays; anything not named takes its room's default.
KIND = {
    "Basic Saw": "lead", "Basic Pulse": "lead", "Basic Sine": "lead", "Basic Arp": "hold", "Basic Noise": "fx",
    "Basic FM": "keys", "Basic Wavetable": "pad",
    "Tape Flute": "melody_hi", "Vinyl Choir": "pad", "Cassette Keys": "keys", "Blanket Pad": "pad",
    "Bedroom Bass": "bass", "Dusty Mallet": "pluck", "Static Hit": "hit",
    "Neon Brass": "keys", "Gated Pad": "pad", "Night Drive": "hold", "Laser Lead": "lead", "Sunset Strings": "strings",
    "Grid Keys": "keys",
    "Solar Wind": "fx", "Nebula Pad": "pad", "Deep Field": "pad", "Shimmer Drone": "drone", "Satellite Ping": "bell",
    "Granular Sky": "pad", "Live Shimmer": "pad", "Frozen Pluck": "pluck",
    "Glass Bell": "bell", "Rain Chime": "hold", "Crystal Pluck": "pluck", "Kalimba": "pluck", "Harp String": "pluck",
    "Marimba": "pluck",
    "Rust Bass": "bass", "Metal Hit": "hit", "Furnace Drone": "drone", "Concrete Pad": "pad", "Siren": "fx",
    "Juno Pad": "pad", "Prophet Brass": "keys", "OB Strings": "strings", "Moog Bass": "bass", "Clav Keys": "keys",
    "Sync Lead": "lead",
    "Reese": "bass", "Supersaw": "keys", "Riser": "fx", "Pluck Drop": "pluck", "Hyper Lead": "lead",
    "Neuro Growl": "bass", "Future Chord": "keys",
    "Pure Sub": "sub", "808 Boom": "kick", "Wobble": "bass", "FM Knock": "bass", "Donk": "bass", "Fat Analog": "bass",
    "Steinway Grand": "piano", "Yamaha Upright": "piano", "Old Upright": "piano", "Flemish Harpsichord": "piano",
    "Italian Harpsichord": "piano", "FM Keys": "keys",
    "Violins": "strings", "Violas": "strings", "Cellos": "melody_low", "Double Bass": "lowline", "Solo Violin": "melody_hi",
    "Folk Harp": "pluck", "Dan Tranh": "pluck",
    "Horn": "melody_mid", "Trumpet": "melody_hi", "Tuba": "lowline", "Trombone": "melody_low", "Old Trombone": "melody_low",
    "Flute": "melody_hi", "Clarinet": "melody_mid", "Bassoon": "lowline", "Saxello": "melody_mid",
    "Tenor Sax": "melody_mid", "Recorders": "melody_hi", "Ocarina": "melody_hi", "Harmonica": "melody_mid",
    "Didgeridoo": "didge", "Pipe Organ": "organ", "Renaissance Organ": "organ",
    "Vibraphone": "pluck", "Xylophone": "bell", "Balafon": "pluck", "Glockenspiel": "glock", "Tubular Bells": "bell",
    "Tubular Glock": "bell", "Hand Chimes": "bell", "Wine Glasses": "glasses", "Timpani": "timpani",
    "DX7 E.PIANO 1": "piano", "DX7 BASS 1": "bass", "Prophet-5 Sync II": "lead", "Prophet-5 Brass": "keys",
    "Minimoog Fat Bass": "bass", "TX81Z LatelyBass": "bass", "Minimoog Trilogy Lead": "lead",
    "Washed Poly": "keys", "Tine Clav": "keys", "Buzz Drop": "bass", "Square Grit": "lead", "Whisper Air": "pad",
    "Fizz Melody": "lead", "Pirate Radio": "keys", "Vocal Mask": "pad", "High Voice": "melody_hi",
    "Four-Track House": "keys", "Spiral FM": "keys", "Trance Glass": "hold", "Bowed Haze": "pad", "Sea Pad": "pad",
}
ROOM_DEFAULT = {"Clock Tower": "hold", "Museum": "keys", "Conservatory": "piano"}

# Cards elsewhere: special.json's own, and the 20 November pass's, already marked (Chip, Arcade, the SVF,
# Stutter Chords, Stepped Lead), so not asked again.
DONE_BEFORE = {"Coin Pulse", "Boss Bass", "Power-Up", "SCC Lead", "Chip Chord", "Stutter Chords", "Stepped Lead"}


def parse_bank(path):
    """The bank's rooms and presets: name, room, family, idea, macros, the flags that choose a phrase."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    intro, presets, current = [], [], None
    for line in lines:
        s = line.strip()
        m = re.match(r"^\[(.+)\]$", s)
        if m:
            current = {"name": m.group(1), "idea": [], "macros": [], "keys": {}, "lofi": [], "ideaDone": False}
            presets.append(current)
            continue
        if current is None:
            if s.startswith("#"):
                intro.append(s.lstrip("# ").strip())
            continue
        if s.startswith("#"):
            if not current["ideaDone"]:
                current["idea"].append(s.lstrip("#").strip())
            continue
        if not s:
            continue
        key, _, value = s.partition("=")
        key, value = key.strip(), value.strip()
        if key not in ("room", "family"):
            current["ideaDone"] = True
        current["keys"].setdefault(key, []).append(value)
        if key == "macros":
            m = re.search(r"names=([^ ]+)", value)
            if m:
                current["macros"] = m.group(1).split(",")
        if re.match(r"^i\d$", key) and value.startswith("LoFi"):
            hiss = re.search(r"hiss=([\d.]+)", value)
            crackle = re.search(r"crackle=([\d.]+)", value)
            if (hiss and float(hiss.group(1)) > 0) or (crackle and float(crackle.group(1)) > 0):
                current["lofi"].append(key)
    return " ".join(intro), presets


def bank_cards(special_names):
    groups, items = [], []
    for file in sorted(os.listdir(PRESETS)):
        if not file.endswith(".csbank"):
            continue
        intro, presets = parse_bank(os.path.join(PRESETS, file))
        room = presets[0]["keys"]["room"][0] if presets else file
        key = "room-" + re.sub(r"[^a-z0-9]+", "-", room.lower()).strip("-")
        cards = []
        for p in presets:
            name = p["name"]
            if name in special_names or name in DONE_BEFORE:
                continue
            kind = KIND.get(name, ROOM_DEFAULT.get(room, "keys"))
            idea = " ".join(p["idea"]).strip()
            family = p["keys"].get("family", [""])[0]
            tries = []
            for i, macro in enumerate(p["macros"][:4]):
                tries.append({"label": macro + " 100 %", "values": {"pA.macro%d" % (i + 1): "100 %"}})
            tries.append({"label": "Soft touch (40)", "vel": 40})
            tries.append({"label": "Hard touch (127)", "vel": 127})
            listen = ["The idea: " + idea if idea else "No description in the bank.",
                      "Is it a sound you would keep in this room? Anything ugly: clicks, harshness, a level jump, a tail that never ends?"]
            if p["macros"]:
                listen.append("Each macro try against ▶ Play (the macro at 0 %): does it change the sound musically?")
            if room == "Conservatory":
                listen[1] = "Does it sound like a real %s, played here? Any note that jumps out (level, tuning, a different sample)?" % name.lower()
            if room == "Museum":
                listen[1] = "Does it sound like the original you know (the name is the original's)?"
            card = {"id": "B%d" % (len(items) + 1),
                    "group": key, "title": name + (" (" + family + ")" if family else ""),
                    "setup": [["load_preset", {"name": name}]], "view": {}, "play": PHRASES[kind], "tries": tries,
                    "knobs": [],
                    "listen": listen,
                    "keys": ("Its macros, " + ", ".join(p["macros"][:4]) + ": pad Home, page Macros, knobs 1 to 4")
                            if p["macros"] else "", "pre": ""}
            cards.append(card)
            items.append(card)
        if cards:
            groups.append({"key": key, "title": room, "intro": intro})
    return groups, items


def main():
    with open(os.path.join(HERE, "special.json"), encoding="utf-8") as f:
        special = json.load(f)
    covered = set(special.get("covers", []))
    groups, items = bank_cards(covered)
    out = {"title": special["title"], "intro": special["intro"], "bases": special["bases"],
           "groups": special["groups"] + groups, "items": [resolve(i) for i in special["items"]] + items}
    with open(os.path.join(HERE, "items.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
    print("%d cards in %d groups" % (len(out["items"]), len(out["groups"])))


if __name__ == "__main__":
    main()

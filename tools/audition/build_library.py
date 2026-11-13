"""Build the sampled-instrument library from the picked recordings.

Reads resources/instruments/catalogue.txt and, for every style in it, turns the recordings of
its source folder into <library>/<family>/<instrument>/<style>/NNN.flac plus a manifest.txt:

    gain | 0.31          one factor for the whole style, applied as it is loaded
    trim | -6.5          decibels on top of it: makes an instrument's styles equally loud as played
    veltrack | 1.0       how far loudness follows velocity (squared): 0 for an organ
    release | 0.4        the release time the instrument's author gave it, in seconds
    file | root | low key | high key | low velocity | high velocity | take | frames | rate | channels
         | loop start | loop end | loop fade | cents | gain dB

root        the MIDI note the recording sounds
keys        the keys that play it; velocities: the MIDI velocities that play it
take        which of several recordings of those keys and velocities (round robin), from 0
loop ...    in frames, for styles that sustain; 0 0 0 means the note rings out as recorded
cents       how far above its note the recording sounds; the player takes it out
gain dB     this recording's level correction

Where all that comes from: the SFZ mapping files the recordings' author publishes
(fetch_sfz.py downloads them). They give each recording its note, keys, velocities, a level
that makes the layers of a note join up under a square law of velocity, and for most
instruments a tuning correction. The style's gain then puts the middle one of its loudest
notes (the peaks of the recordings its top velocities play, corrections included) at 0.8 of
full scale, and no note above 2.2 times full scale: recordings levelled alike by ear do not
peak alike, and a gain set by the one that peaks highest leaves the whole instrument 10 dB
down. Samples are floats; the preset's loudness is set afterwards. A style without a mapping file is placed by the notes in its file names
(moved by whole octaves to where a pitch detector hears the style), its layers split the
velocities evenly, and its recordings are levelled alike.

Tuning is measured here, not taken from the mapping files: their corrections come from a
tuner that is steady on wind and string tones and wanders on drums and bars (six hits of one
kettledrum get corrections from -12 to +78 cents; its main partial is steady within 2). The
dry run prints how far the two differ per style.

Every file is trimmed to start 2 ms before its onset (or where the author's mapping starts it,
if that is later) and is otherwise bit for bit the recording (FLAC is lossless). ffmpeg does
the encoding; a file already built from the same recording at the same trim is left alone
(sources.txt in each folder keeps the record).

The catalogue's sixth column, "key=value" words:

    sfz=Scale       of several mapping files for the folder, the one with this in its name;
                    sfz=none ignores the author's mapping (it is a kit of sounds, say)
    shift=12        semitones added to the mapping's notes and keys (to place an instrument
                    where it sounds; not used: the user chose the author's keys, D103)
    sounds=12       the instrument sounds this many semitones above the key the mapping gives
                    it (a xylophone is mapped as it is written, an octave under its sound):
                    its tuning is measured there
    files=Sus,Hit   only files whose name contains one of these
    root=37         without a mapping: every file plays this note
    keys=36,48      without a mapping: each file its own key, in name order
    octave=12       without a mapping: the note in the file name plus this, no detector
    trim=-6.5       decibels: how much this style is turned down or up so that it needs the same
                    preset level as the instrument's first style (CoolSynthPresetNormalizer
                    --styles measures it; apply_trims.py writes the numbers here)
    tune=keep       leave the tuning as recorded (pianos and harpsichords are stretched on
                    purpose; a bell has no partial at its note)
    tune=first      measure it from the first partial only (bars, tines and glasses: their
                    overtones are not harmonics)
    tune=drum       measure it from a kettledrum's main partial, all hits of a drum together
    tune=author     take the mapping file's corrections
                    default: measure it from the first eight harmonics

usage: python -I build_library.py <catalogue.txt> <libraries-dir> <sfz-dir> <library-out-dir> [only-substring] [dry]
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
import audiolib  # noqa: E402
import sfzmap  # noqa: E402

catalogue_path, libraries, sfz_root, out_root = sys.argv[1:5]
only = sys.argv[5].lower() if len(sys.argv) > 5 else ""
dry = "dry" in sys.argv[6:]  # report how each style would be mapped, write nothing

NOTE = re.compile(r"(?<![A-Za-z])([A-G])(#|b|s)?(-?\d)(?![0-9A-Za-z])")
DYNAMIC_ORDER = ["ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"]
WORD_ORDER = {"vsoft": 0, "soft": 1, "quiet": 1, "medium": 2, "med": 2, "loud": 3, "vloud": 4, "hard": 3}
SEMITONE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "x"


def tokens_of(stem):
    return [t.lower() for t in re.split(r"[\s_\-.,()]+", stem) if t]


def named_midi(stem):
    match = None
    for match in NOTE.finditer(stem):
        pass
    if match is None:
        return None
    letter, accidental, octave = match.groups()
    return SEMITONE[letter] + (1 if accidental in ("#", "s") else -1 if accidental == "b" else 0) + 12 * (int(octave) + 1)


def dynamic_of(tokens):
    for t in tokens:
        if re.fullmatch(r"(v|vl|dyn|vel)\d", t):
            return int(t[-1])
        marked = re.fullmatch(r"(ppp|pp|p|mp|mf|f|ff|fff)\d?", t)
        if marked:
            return DYNAMIC_ORDER.index(marked.group(1))
        if t in WORD_ORDER:
            return WORD_ORDER[t]
    return None


def take_of(tokens, pitch_is_named):
    for t in tokens:
        if re.fullmatch(r"(rr|r)\d\d?", t):
            return int(re.sub(r"\D", "", t))
    if pitch_is_named and tokens and re.fullmatch(r"\d\d?", tokens[-1]):
        return int(tokens[-1])
    return 1


def onset_frame(x, rate):
    level = np.abs(x).max(axis=1)
    peak = float(level.max())
    if peak <= 0.0:
        return 0
    above = np.nonzero(level > peak * 0.003)[0]
    return max(0, int(above[0]) - int(0.002 * rate)) if len(above) else 0


def loop_points(x, rate):
    """A loop over the steady part of a sustained note: (start, end, fade) in frames, or zeros.

    The end is moved, within a few periods, to where the stretch before it best matches the
    stretch before the start, so the crossfade joins like with like."""
    mono = x.mean(axis=1)
    frames = len(mono)
    if frames < int(1.6 * rate):
        return 0, 0, 0
    hop = int(0.01 * rate)
    count = frames // hop
    env = np.array([float(np.sqrt(np.mean(mono[i * hop:(i + 1) * hop] ** 2)) + 1.0e-9) for i in range(count)])
    env_db = 20.0 * np.log10(env)
    steady = float(np.median(env_db[count // 4:(3 * count) // 4]))
    # Where the note is up to its steady level, and where it starts to fall away at the end.
    risen = np.nonzero(env_db >= steady - 2.0)[0]
    start = (int(risen[0]) * hop if len(risen) else 0) + int(0.35 * rate)
    falling = np.nonzero(env_db >= steady - 3.0)[0]
    end = (int(falling[-1]) * hop if len(falling) else frames) - int(0.15 * rate)
    start = max(start, int(0.25 * end))
    end = min(end, int(0.94 * frames))
    if end - start < int(0.8 * rate):
        return 0, 0, 0
    fade = int(min(0.5 * rate, 0.3 * (end - start), start - 16))
    if fade < int(0.05 * rate):
        return 0, 0, 0

    window = min(fade, 4096)
    reference = mono[start - window:start]
    search = int(0.03 * rate)
    best, best_score = end, -2.0
    norm_reference = float(np.linalg.norm(reference)) or 1.0
    for candidate in range(end - search, min(end + search, frames - 1)):
        segment = mono[candidate - window:candidate]
        score = float(np.dot(segment, reference)) / (norm_reference * (float(np.linalg.norm(segment)) or 1.0))
        if score > best_score:
            best, best_score = candidate, score
    return start, best, fade


def tuning_cents(mono, rate, root, partials):
    """How far above the note `root` a recording sounds, in cents; None if no partial is clear.

    Short frames follow a vibrato instead of splitting it into side bands. In each frame the
    partials are looked for within 70 cents of where the note puts them, each gives the
    fundamental it implies, and the strongest-weighted middle one is the frame's answer. The
    recording's answer is the middle one of its louder frames."""
    f0 = audiolib.midi_to_hz(root)
    frame = int(max(0.12, 8.0 / f0) * rate)
    start = int(0.12 * rate) if len(mono) > int(0.9 * rate) else int(0.03 * rate)
    end = min(len(mono), start + int(2.0 * rate))
    if end - start < frame:
        frame = end - start
    if frame < int(0.04 * rate):
        return None
    size = 1 << int(np.ceil(np.log2(frame * 8)))
    bin_hz = rate / size
    window = np.hanning(frame)
    answers, energies = [], []
    for at in range(start, end - frame + 1, max(1, frame // 2)):
        segment = mono[at:at + frame]
        energy = float(np.dot(segment, segment))
        if energy <= 0.0:
            continue
        spectrum = np.abs(np.fft.rfft(segment * window, size))
        top = float(spectrum.max())
        estimates, weights = [], []
        for k in range(1, partials + 1):
            target = k * f0
            if target > min(6000.0, 0.45 * rate):
                break
            low = int(target * 2.0 ** (-70.0 / 1200.0) / bin_hz)
            high = int(np.ceil(target * 2.0 ** (70.0 / 1200.0) / bin_hz))
            if high - low < 4:
                continue
            i = low + int(np.argmax(spectrum[low:high + 1]))
            if i <= low or i >= high or spectrum[i] < top * 0.01:
                continue  # no peak of its own inside the window, or 40 dB down
            a, b, c = (float(np.log(spectrum[j] + 1.0e-20)) for j in (i - 1, i, i + 1))
            bend = a - 2.0 * b + c
            estimates.append((i + (0.5 * (a - c) / bend if bend != 0.0 else 0.0)) * bin_hz / k)
            weights.append(float(spectrum[i]))
        if not estimates:
            continue
        order = np.argsort(estimates)
        running = np.cumsum(np.array(weights)[order])
        answers.append(estimates[order[int(np.searchsorted(running, running[-1] / 2.0))]])
        energies.append(energy)
    if not answers:
        return None
    loud = [a for a, e in zip(answers, energies) if e >= max(energies) * 0.01]
    return 1200.0 * float(np.log2(float(np.median(loud)) / f0))


def low_peaks(bodies, rate):
    """The strongest peaks between 50 and 700 Hz of several recordings' summed spectra: [(hz, level 0..1)]."""
    size = 1 << 18
    total = None
    for body in bodies:
        segment = body[int(0.05 * rate):int(0.05 * rate) + int(2.0 * rate)]
        if len(segment) < int(0.1 * rate):
            continue
        spectrum = np.abs(np.fft.rfft(segment * np.hanning(len(segment)), size)) ** 2
        total = spectrum / spectrum.max() if total is None else total + spectrum / spectrum.max()
    if total is None:
        return []
    hz = np.fft.rfftfreq(size, 1.0 / rate)
    low, high = int(np.searchsorted(hz, 50.0)), int(np.searchsorted(hz, 700.0))
    band = np.sqrt(total[low:high])
    taken = np.zeros(len(band), dtype=bool)
    peaks = []
    for index in np.argsort(band)[::-1]:
        if taken[index]:
            continue
        width = int(max(3, hz[low + index] * 0.025 / (rate / size)))
        taken[max(0, index - width):index + width] = True
        peaks.append((float(hz[low + index]), float(band[index])))
        if len(peaks) == 12:
            break
    strongest = max(level for _, level in peaks)
    return sorted((f, level / strongest) for f, level in peaks)


def drum_cents(bodies, rate, root):
    """How far above the note `root` a kettledrum sounds, in cents, from all its hits together;
    None if nothing is clear. Its pitch is its main partial: the strong peak within two and a
    half semitones of the note that has a partner about a fifth above it (1.38 to 1.56 times).
    Two such peaks close together are one partial split by an uneven head: their power-weighted
    middle is the pitch."""
    peaks = low_peaks(bodies, rate)
    f0 = audiolib.midi_to_hz(root)
    near = [(f, level) for f, level in peaks if abs(1200.0 * np.log2(f / f0)) < 250.0 and level >= 0.05
            and any(1.38 * f <= g <= 1.56 * f and partner >= 0.05 for g, partner in peaks)]
    if not near:
        return None
    f, level = max(near, key=lambda p: p[1])
    members = [(g, l) for g, l in peaks if abs(1200.0 * np.log2(g / f)) < 130.0 and l >= level * 0.3]
    f = sum(g * l * l for g, l in members) / sum(l * l for _, l in members)
    return 1200.0 * float(np.log2(f / f0))


def read_catalogue(path):
    styles = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = [part.strip() for part in line.split("|")]
        options = dict(word.split("=", 1) for word in parts[5].split()) if len(parts) > 5 else {}
        styles.append({"family": parts[0], "instrument": parts[1], "style": parts[2], "sustains": parts[3].lower() == "yes",
                       "source": parts[4], "options": options})
    return styles


def encode(job):
    source, start, destination = job
    command = ["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", source, "-af", f"atrim=start_sample={start}",
               "-c:a", "flac", "-compression_level", "8", destination]
    result = subprocess.run(command, capture_output=True, text=True)
    return destination, result.returncode, result.stderr.strip()


mappings = sfzmap.load_tree(os.path.join(sfz_root, "vcsl"), "VCSL") + sfzmap.load_tree(os.path.join(sfz_root, "vsco"), "VSCO-2-CE")
if not mappings:
    raise SystemExit("no mapping files under " + sfz_root + ": run fetch_sfz.py first")

total_in = total_out = 0
for style in read_catalogue(catalogue_path):
    label = f"{style['family']} / {style['instrument']} / {style['style']}"
    if only and only not in label.lower():
        continue
    options = style["options"]
    folder = os.path.join(libraries, *style["source"].split("/"))
    wanted = [w.lower() for w in options["files"].split(",")] if "files" in options else None
    wavs = sorted(f for f in os.listdir(folder) if f.lower().endswith(".wav") and (wanted is None or any(w in f.lower() for w in wanted)))
    out_dir = os.path.join(out_root, slug(style["family"]), slug(style["instrument"]), slug(style["style"]))
    tune = options.get("tune", "harmonic")
    shift = int(options.get("shift", 0))
    sounds = int(options.get("sounds", 0))

    mapping, regions = None, []
    if options.get("sfz") != "none":
        mapping, regions, _ = sfzmap.for_folder(mappings, style["source"], options.get("sfz"))
    regions_of = {}
    for region in regions:
        regions_of.setdefault(os.path.basename(region["sample"]).lower(), []).append(region)
    authors_tuning = any(region["tune"] is not None for region in regions)
    need_audio = tune not in ("keep", "author") or (dry and authors_tuning)

    # One entry per zone: a recording the mapping uses twice gets two.
    entries, offsets, unmapped = [], [], 0
    key_list = [int(k) for k in options["keys"].split(",")] if "keys" in options else []
    for index, f in enumerate(wavs):
        if mapping is not None and f.lower() not in regions_of:
            unmapped += 1  # the author's instrument does not use this recording
            continue
        stem = os.path.splitext(f)[0]
        try:
            rate, x = audiolib.read_wav(os.path.join(folder, f))
        except ValueError as error:
            print("  skipped", f, error)
            continue
        if x.shape[1] > 2:
            x = x[:, :2]
        onset = onset_frame(x, rate)
        common = {"file": f, "group": stem.split("_")[0], "rate": rate, "channels": x.shape[1], "cents": 0.0}

        def finish(entry, start):
            body = x[start:]
            if len(body) < int(0.05 * rate):
                return False
            mono = audiolib.mono(body)
            first_second = mono[:int(1.0 * rate)]
            entry.update(common, start=start, frames=len(body), peak=float(np.abs(body).max()),
                         level_db=10.0 * float(np.log10(float(np.mean(first_second ** 2)) + 1.0e-20)),
                         mono=mono if need_audio or mapping is None else None,
                         loop=loop_points(body, rate) if style["sustains"] else (0, 0, 0))
            return True

        if mapping is not None:
            for region in regions_of[f.lower()]:
                entry = {"root": region["root"] + shift, "lokey": region["lokey"] + shift, "hikey": region["hikey"] + shift,
                         "lovel": region["lovel"], "hivel": region["hivel"], "volume": region["volume"], "order": region["order"],
                         "authors_cents": -region["tune"] if region["tune"] is not None else 0.0}
                if finish(entry, max(onset, region["offset"])):
                    entries.append(entry)
            continue

        # No mapping file: the note from the options or the file name, the layer and take from the name's other words.
        named = None if ("root" in options or key_list) else named_midi(stem)
        tokens = tokens_of(NOTE.sub(" ", stem) if named is not None else stem)
        entry = {"named": named, "measured": None, "rank": dynamic_of(tokens), "order": (float(take_of(tokens, named is not None)), 0.0)}
        if not finish(entry, onset):
            continue
        if "root" in options:
            entry["root"] = int(options["root"])
        elif key_list:
            if index >= len(key_list):
                continue
            entry["root"] = key_list[index]
        elif named is not None and "octave" in options:
            entry["root"] = named + int(options["octave"])
        else:
            hz = audiolib.fundamental(entry["mono"], rate, 0.03, min(0.6, entry["frames"] / rate - 0.04))
            entry["measured"] = audiolib.hz_to_midi(hz) if hz > 0.0 else None
            if named is not None and entry["measured"] is not None:
                offsets.append(entry["measured"] - named)
        entries.append(entry)

    veltrack, release = 1.0, 0.6 if style["sustains"] else 8.0
    if mapping is not None:
        veltrack = regions[0]["veltrack"]
        release = next((region["release"] for region in regions if region["release"] is not None), release)
    else:
        # Notes: file names moved by whole octaves to where the style is heard. Keys: half way to the neighbouring
        # notes. Velocities: a note's layers split them evenly. Levels: every recording as loud as the others.
        octave_shift = int(round(float(np.median(offsets)) / 12.0)) * 12 if offsets else 0
        placed = []
        for e in entries:
            if "root" not in e:
                if e["named"] is not None:
                    e["root"] = e["named"] + octave_shift
                elif e["measured"] is not None:
                    e["root"] = int(round(e["measured"]))
                else:
                    continue
            e["root"] += shift
            if 0 <= e["root"] <= 127:
                placed.append(e)
        entries = placed
        roots = sorted({e["root"] for e in entries})
        for e in entries:
            at = roots.index(e["root"])
            e["lokey"] = 0 if at == 0 else (roots[at - 1] + e["root"]) // 2 + 1
            e["hikey"] = 127 if at == len(roots) - 1 else (e["root"] + roots[at + 1]) // 2
            ranks = sorted({o["rank"] for o in entries if o["root"] == e["root"] and o["rank"] is not None})
            layer = ranks.index(e["rank"]) if e["rank"] is not None else 0
            count = max(1, len(ranks))
            e["lovel"], e["hivel"] = (128 * layer) // count, (128 * (layer + 1)) // count - 1
            e["volume"] = -e["level_db"]
            e["authors_cents"] = 0.0
    if not entries:
        print("NOTHING BUILT for", label)
        continue

    # How far off its note each recording is.
    unclear = 0
    if tune == "author":
        for e in entries:
            e["cents"] = e["authors_cents"]
    elif tune == "drum":
        for group in sorted({e["group"] for e in entries}):
            members = [e for e in entries if e["group"] == group]
            reference = members[0]["root"]
            cents = drum_cents([e["mono"] for e in members], members[0]["rate"], reference)
            unclear += 0 if cents is not None else len(members)
            if dry:
                print(f"    {group:10s} measured at {audiolib.note_name(reference)} {cents if cents is not None else float('nan'):+5.0f} cents; the author's: "
                      + " ".join(f"{audiolib.note_name(e['root'])}{e['authors_cents']:+.0f}" for e in members))
            for e in members:
                if cents is None:
                    continue
                # The drum's pitch as a note and the cents left over; the keys that play it stay the author's.
                pitch = reference + cents / 100.0
                e["authors_cents"] += 100.0 * (e["root"] - round(pitch))  # for the comparison only
                e["root"] = int(round(pitch))
                e["cents"] = 100.0 * (pitch - e["root"])
    elif tune != "keep" or (dry and authors_tuning):
        for e in entries:
            cents = tuning_cents(e["mono"], e["rate"], e["root"] + sounds, 1 if tune == "first" else 8)
            if cents is None or abs(cents) > 65.0:
                unclear += 1  # nothing clear near the note: left as recorded
            else:
                e["cents"] = cents
    differs = ""
    if dry and authors_tuning:
        gaps = sorted(abs(e["cents"] - e["authors_cents"]) for e in entries)
        differs = f"  author's tuning differs by {gaps[len(gaps) // 2]:.0f} (middle) to {gaps[(9 * len(gaps)) // 10]:.0f} cents"
        if tune == "keep":
            for e in entries:
                e["cents"] = 0.0

    # Takes: the recordings that share a note, keys and velocities, in the mapping's order.
    entries.sort(key=lambda e: (e["root"], e["lokey"], e["hikey"], e["lovel"], e["hivel"], e["order"], e["file"]))
    takes_seen = {}
    for e in entries:
        zone = (e["root"], e["lokey"], e["hikey"], e["lovel"], e["hivel"])
        e["take"] = takes_seen.get(zone, 0)
        takes_seen[zone] = e["take"] + 1

    # The loudest any note can come out: its peak, its own correction, and the velocity law at the top of its velocities.
    def loudest_possible(e):
        return e["peak"] * 10.0 ** (e["volume"] / 20.0) * (1.0 - veltrack + veltrack * (e["hivel"] / 127.0) ** 2)

    loudest = sorted(loudest_possible(e) for e in entries if e["hivel"] >= 127) or sorted(loudest_possible(e) for e in entries)
    gain = 0.8 / loudest[len(loudest) // 2]
    gain = min(gain, 2.2 / max(loudest_possible(e) for e in entries))
    roots = sorted({e["root"] for e in entries})

    if dry:
        cents = sorted(e["cents"] for e in entries)
        velocities = sorted({(e["lovel"], e["hivel"]) for e in entries})
        source = mapping["name"].split("/")[-1] if mapping is not None else "(file names)"
        print(f"cents {cents[len(cents) // 10]:+4.0f} {cents[len(cents) // 2]:+4.0f} {cents[(9 * len(cents)) // 10]:+4.0f} "
              f"{tune:8s} unclear {unclear:2d}  {len(entries):3d} zones  {unmapped:3d} unused  "
              f"{audiolib.note_name(roots[0])}-{audiolib.note_name(roots[-1])}  {len(velocities)} velocity ranges  "
              f"veltrack {veltrack:.1f}  release {release:4.1f}  {label}  [{source}]{differs}", flush=True)
        continue

    os.makedirs(out_dir, exist_ok=True)
    record_path = os.path.join(out_dir, "sources.txt")
    built_before = {}
    if os.path.exists(record_path):
        for row in open(record_path, encoding="utf-8"):
            parts = [p.strip() for p in row.split("|")]
            if len(parts) == 3:
                built_before[parts[0]] = (parts[1], parts[2])
    jobs, lines, record, name_of = [], [], [], {}
    for e in entries:
        made_from = (e["file"], str(e["start"]))
        if made_from not in name_of:  # two zones of one recording share its file
            name_of[made_from] = f"{len(name_of):03d}.flac"
            destination = os.path.join(out_dir, name_of[made_from])
            if not (os.path.exists(destination) and built_before.get(name_of[made_from]) == made_from):
                jobs.append((os.path.join(folder, e["file"]), e["start"], destination))
            record.append(f"{name_of[made_from]} | {e['file']} | {e['start']}")
        lines.append(f"{name_of[made_from]} | {e['root']} | {e['lokey']} | {e['hikey']} | {e['lovel']} | {e['hivel']} | {e['take']} | "
                     f"{e['frames']} | {e['rate']} | {e['channels']} | {e['loop'][0]} | {e['loop'][1]} | {e['loop'][2]} | "
                     f"{e['cents']:.1f} | {e['volume']:.3f}")

    failed = 0
    with ThreadPoolExecutor(max_workers=8) as pool:
        for destination, code, message in pool.map(encode, jobs):
            if code != 0 or not os.path.exists(destination):
                failed += 1
                print("  ffmpeg failed:", destination, message)
    names = set(name_of.values())
    for stale in os.listdir(out_dir):
        if stale.lower().endswith(".flac") and stale not in names:
            os.remove(os.path.join(out_dir, stale))
    with open(os.path.join(out_dir, "manifest.txt"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"gain | {gain:.6f}\ntrim | {float(options.get('trim', 0.0)):.1f}\nveltrack | {veltrack:.2f}\nrelease | {release:.2f}\n")
        handle.write("# file | root | low key | high key | low velocity | high velocity | take | frames | rate | channels | "
                     "loop start | loop end | loop fade | cents | gain dB\n")
        handle.write("\n".join(lines) + "\n")
    with open(record_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(record) + "\n")

    size_in = sum(os.path.getsize(os.path.join(folder, f)) for f, _ in name_of)
    size_out = sum(os.path.getsize(os.path.join(out_dir, name)) for name in names if os.path.exists(os.path.join(out_dir, name)))
    total_in += size_in
    total_out += size_out
    looped = sum(1 for e in entries if e["loop"][1] > 0)
    print(f"{size_out / 1e6:7.1f} MB (from {size_in / 1e6:6.1f})  {len(entries):3d} zones  {len(names):3d} files  {len(jobs):3d} encoded  "
          f"{len(roots):2d} notes {audiolib.note_name(roots[0])}-{audiolib.note_name(roots[-1])}  {max(takes_seen.values())} takes  "
          f"{looped} looped  {unmapped} unused  {failed} failed  {label}", flush=True)

print(f"library: {total_out / 1e6:.0f} MB from {total_in / 1e6:.0f} MB of recordings")

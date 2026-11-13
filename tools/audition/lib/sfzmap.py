"""Read the SFZ mapping files the libraries' author publishes.

An SFZ file says, for each recording of an instrument, which keys and velocities play it, the
note it sounds, and a level and tuning correction. These are the author's own instruments
(github.com/sgossner/VCSL branch "sfz", github.com/sgossner/VSCO-2-CE branch "SFZ"), so the
library builder takes its mapping from them and falls back to file names only where there is
no mapping file.

Only what those files use is understood: <control> default_path, <global>, <group>, <region>,
and the opcodes sample, key, lokey, hikey, pitch_keycenter, lovel, hivel, xfin_*, xfout_*,
volume, tune, offset, seq_position, seq_length, lorand, hirand, trigger, amp_veltrack,
ampeg_release.
"""
import os
import re

OPCODE = re.compile(r"(?<![A-Za-z0-9_])([a-z_][a-z0-9_]*)=")
HEADER = re.compile(r"<([a-z]+)>")


def _opcodes(text):
    found = {}
    matches = list(OPCODE.finditer(text))
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        found[match.group(1)] = " ".join(text[match.end():end].split())
    return found


def _number(value, fallback):
    try:
        return float(value)
    except (TypeError, ValueError):
        return fallback


def parse(path):
    """One mapping file: {"regions": [...], "keyswitch": bool, "options": first comment line, "release": s, "veltrack": 0..1}."""
    raw = open(path, encoding="utf-8", errors="replace").read()
    first_comment = next((line.strip() for line in raw.splitlines() if line.strip().startswith("//")), "")
    text = "\n".join(line.split("//", 1)[0] for line in raw.splitlines())
    scopes = {"control": {}, "global": {}, "master": {}, "group": {}}
    regions = []
    pieces = HEADER.split(text)  # [before, header, body, header, body, ...]
    for header, body in zip(pieces[1::2], pieces[2::2]):
        values = _opcodes(body)
        if header == "region":
            merged = {**scopes["global"], **scopes["master"], **scopes["group"], **values}
            merged["default_path"] = scopes["control"].get("default_path", "")
            regions.append(merged)
        elif header in scopes:
            scopes[header] = values
            if header == "global":
                scopes["master"], scopes["group"] = {}, {}
            elif header == "master":
                scopes["group"] = {}
    keyswitch = any(key.startswith("sw_") for region in regions for key in region)
    out = []
    for region in regions:
        if "sample" not in region or region.get("trigger", "attack") != "attack":
            continue
        key = _number(region.get("key"), None)
        center = _number(region.get("pitch_keycenter"), key)
        low, high = _number(region.get("lokey"), key), _number(region.get("hikey"), key)
        if center is None:
            center = low
        if center is None or low is None or high is None:
            continue
        # A velocity crossfade becomes a switch at the middle of each fade.
        low_velocity = _number(region.get("lovel"), 0.0)
        high_velocity = _number(region.get("hivel"), 127.0)
        if "xfin_lovel" in region:
            low_velocity = max(low_velocity, (float(region["xfin_lovel"]) + float(region["xfin_hivel"])) / 2.0 + 0.5)
        if "xfout_lovel" in region:
            high_velocity = min(high_velocity, (float(region["xfout_lovel"]) + float(region["xfout_hivel"])) / 2.0 - 0.5)
        out.append({
            "sample": (region["default_path"] + region["sample"]).replace("\\", "/"),
            "root": int(round(center)), "lokey": int(round(low)), "hikey": int(round(high)),
            "lovel": int(round(low_velocity)), "hivel": int(round(high_velocity)),
            "volume": _number(region.get("volume"), 0.0),
            "tune": _number(region.get("tune"), None),
            "offset": int(_number(region.get("offset"), 0.0)),
            "order": (_number(region.get("seq_position"), 0.0), _number(region.get("lorand"), 0.0)),
            "release": _number(region.get("ampeg_release"), None),
            "veltrack": _number(region.get("amp_veltrack"), 100.0) / 100.0,
        })
    return {"regions": out, "keyswitch": keyswitch, "options": first_comment}


def load_tree(sfz_root, library_name):
    """Every mapping file under sfz_root, with each region's sample path made relative to the
    libraries folder ("VCSL/Idiophones/.../x.wav"). VCSL's samples are relative to the mapping
    file; VSCO's to the library root (its files name a default_path)."""
    files = []
    for folder, _, names in os.walk(sfz_root):
        for name in sorted(names):
            if not name.lower().endswith(".sfz"):
                continue
            path = os.path.join(folder, name)
            parsed = parse(path)
            relative_folder = os.path.relpath(folder, sfz_root).replace("\\", "/")
            prefix = library_name + "/" + ("" if relative_folder == "." else relative_folder + "/")
            for region in parsed["regions"]:
                region["sample"] = os.path.normpath(prefix + region["sample"]).replace("\\", "/")
            parsed["name"] = os.path.relpath(path, sfz_root).replace("\\", "/")
            files.append(parsed)
    return files


def for_folder(files, source, pinned=None):
    """The mapping file that covers a source folder ("VCSL/.../Sus"), and its regions there.

    Keyswitch files (several articulations in one) are passed over. Of several candidates the
    one that maps the most recordings of the folder wins; `pinned` (part of a file name)
    chooses by hand. Returns (file or None, regions, names of the other candidates)."""
    wanted = source.lower().rstrip("/")
    candidates = []
    for parsed in files:
        if parsed["keyswitch"]:
            continue
        inside = [r for r in parsed["regions"] if os.path.dirname(r["sample"]).lower() == wanted]
        if inside and (pinned is None or pinned.lower() in parsed["name"].lower()):
            candidates.append((len({r["sample"].lower() for r in inside}), -len(parsed["regions"]), parsed, inside))
    if not candidates:
        return None, [], []
    candidates.sort(key=lambda c: (c[0], c[1]), reverse=True)
    return candidates[0][2], candidates[0][3], [c[2]["name"] for c in candidates[1:]]

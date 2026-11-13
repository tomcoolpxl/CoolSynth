"""Write audition/index.html from previews/index.json: instruments grouped by family, with players and tick boxes.

usage: python make_page.py <audition-dir>
"""
import html
import json
import os
import re
import sys

audition = sys.argv[1]
with open(os.path.join(audition, "previews", "index.json"), encoding="utf-8") as handle:
    entries = json.load(handle)

HIDDEN = re.compile(r"/Releases|/Rel$|Legacy|Shaker|Tambourine|Triangles|Guiro|Cowbells|Conga|Frame Drum|Snare|Mark Trees|"
                    r"Bell Tree|Cymbals|Camo|log_drum|glock_glisses|Misc 1|Flexatone|VSCO-2-CE/Percussion$", re.I)
SECONDARY = re.compile(r"(^|/)(stac\w*|spic\w*|pizz\w*|short|trem\w*|fall|buzz|mute|fx|harmonm-sus|straightm-sus)$", re.I)
FAMILIES = [
    ("Pianos", r"Grand Piano|Upright"),
    ("Harpsichords", r"Harpsichord"),
    ("Harps and plucked strings", r"Harp|Psaltery|Dan Tranh|Strumstick"),
    ("Bowed strings", r"VSCO-2-CE/Strings/"),
    ("Brass", r"/Brass/"),
    ("Organs", r"Organ"),
    ("Winds", r"/Woodwinds/|Recorder|Ocarina|Sax|Harmonica|Didgeridoo"),
    ("Mallets and bells", r"Marimba|Vibraphone|Xylo|Glock|Tubular|Hand Chimes|Balafon"),
    ("Kalimbas and mbiras", r"Kalimba|Mbira|Nyunga"),
    ("Glass", r"Wine Glasses"),
    ("FM synth, recorded (Yamaha TX81Z)", r"TX81Z"),
    ("Timpani", r"Timpani"),
]


def family_of(path):
    for name, pattern in FAMILIES:
        if re.search(pattern, path, re.I):
            return name
    return "Other"


def title_of(path):
    parts = path.split("/")
    library = parts[0]
    rest = parts[1:]
    if library == "VCSL":
        rest = rest[2:] if len(rest) > 2 else rest  # drop the two classification levels
    else:
        rest = rest[1:] if len(rest) > 1 else rest  # drop the section
    return " / ".join(rest), ("VCSL" if library == "VCSL" else "VSCO 2 CE")


groups = {}
for e in entries:
    if HIDDEN.search(e["path"]):
        continue
    groups.setdefault(family_of(e["path"]), []).append(e)

order = [name for name, _ in FAMILIES] + ["Other"]
rows_total = 0
sections = []
for family in order:
    items = groups.get(family, [])
    if not items:
        continue
    main = [e for e in items if not SECONDARY.search(e["path"])]
    extra = [e for e in items if SECONDARY.search(e["path"])]

    def row(e):
        title, library = title_of(e["path"])
        base = "previews/" + e["id"]
        facts = [f"{e['low']} to {e['high']}", f"{e['pitches']} recorded pitches",
                 f"{e['layers']} dynamic layer{'s' if e['layers'] != 1 else ''}",
                 f"{e['robins']} take{'s' if e['robins'] != 1 else ''} per note", f"{e['megabytes']:.0f} MB",
                 f"{e['rate'] / 1000:g} kHz, {e['bits']} bit"]
        players = [("Every note, low to high", "run.wav"), (f"One long note ({e['long_seconds']:g} s)", "long.wav")]
        if e.get("layers_in_preview", 0) > 1:
            players.append(("Same note, soft to loud", "layers.wav"))
        audio = "".join(
            f'<label class="player"><span>{html.escape(label)}</span>'
            f'<audio controls preload="none" src="{html.escape(base + "/" + name)}"></audio></label>'
            for label, name in players)
        return (f'<article class="item" data-id="{html.escape(e["id"])}">'
                f'<label class="pick"><input type="checkbox" data-id="{html.escape(e["id"])}" data-title="{html.escape(title)}">'
                f'<span class="name">{html.escape(title)}</span><span class="lib">{library}</span></label>'
                f'<p class="facts">{html.escape(" · ".join(facts))}</p><div class="players">{audio}</div></article>')

    body = "".join(row(e) for e in main)
    if extra:
        body += (f'<details><summary>{len(extra)} more articulation{"s" if len(extra) != 1 else ""} '
                 f'(short notes, plucks, tremolo, mutes)</summary>' + "".join(row(e) for e in extra) + "</details>")
    rows_total += len(items)
    sections.append(f'<section><h2>{html.escape(family)} <span class="count">{len(items)}</span></h2>{body}</section>')

check_path = os.path.join(audition, "modelcheck", "index.json")
checks_html = ""
if os.path.exists(check_path):
    with open(check_path, encoding="utf-8") as handle:
        checks = json.load(handle)
    blocks = []
    for c in checks:
        base = "modelcheck/" + c["id"] + "/"
        labels = [("real", "The real instrument"), ("before", "My preset before"), ("after", "My preset now")]
        audio = "".join(
            f'<label class="player"><span>{label}</span><audio controls preload="none" src="{html.escape(base + c["files"][key])}"></audio></label>'
            for key, label in labels if key in c["files"])
        blocks.append(
            f'<article class="item check"><div class="pick"><span class="name">{html.escape(c["title"])}</span>'
            f'<span class="lib">{html.escape(c["preset"])}</span></div>'
            f'<p class="facts">{html.escape(c["changed"])}</p><div class="players">{audio}</div>'
            f'<label class="verdict">Your verdict <select data-check="{html.escape(c["id"])}" data-title="{html.escape(c["title"])}">'
            f'<option value="">not judged yet</option><option>the new one is closer</option><option>the old one was closer</option>'
            f'<option>neither is close</option><option>both are fine</option></select></label></article>')
    checks_html = ('<section><h2>Model check <span class="count">my presets against the real instruments</span></h2>'
                   '<p class="lead">The same notes three times: the recording, my preset as it was this morning, and my preset after '
                   'correcting it from measurements of that recording. All three are equally loud and have no effects.</p>'
                   + "".join(blocks) + "</section>")

demo_path = os.path.join(audition, "conservatory", "index.json")
demos_html = ""
if os.path.exists(demo_path):
    with open(demo_path, encoding="utf-8") as handle:
        demos = json.load(handle)
    families = {}
    for d in demos:
        families.setdefault(d["family"], {}).setdefault(d["instrument"], []).append(d)
    blocks = []
    for family, instruments in families.items():
        blocks.append(f'<h3>{html.escape(family)}</h3>')
        for instrument, styles in instruments.items():
            audio = "".join(
                f'<label class="player"><span>{html.escape(s["style"])}</span>'
                f'<audio controls preload="none" src="{html.escape("conservatory/" + s["file"])}"></audio></label>'
                for s in styles)
            blocks.append(
                f'<article class="item check"><div class="pick"><span class="name">{html.escape(instrument)}</span>'
                f'<span class="lib">{len(styles)} style{"s" if len(styles) != 1 else ""}</span></div>'
                f'<div class="players">{audio}</div>'
                f'<label class="verdict">Your verdict <select data-demo="{html.escape(instrument)}">'
                f'<option value="">not judged yet</option><option>good</option><option>wrong notes or out of tune</option>'
                f'<option>too quiet or too loud</option><option>uneven from note to note</option>'
                f'<option>a loop or a click is audible</option><option>something else is wrong</option></select></label></article>')
    demos_html = ('<section><h2>Conservatory <span class="count">the recorded instruments as the synth plays them</span></h2>'
                  '<p class="lead">Each player is the preset played through the plugin: eight notes climbing through the '
                  'instrument, from soft to loud, then a chord. One player per playing style. This is the synth, not the raw '
                  'recording; the raw recordings are further down.</p>' + "".join(blocks) + "</section>")

grain_path = os.path.join(audition, "graindemo", "index.json")
grain_html = ""
if os.path.exists(grain_path):
    with open(grain_path, encoding="utf-8") as handle:
        grain = json.load(handle)
    audio = "".join(
        f'<label class="player"><span>{html.escape(c["title"])}</span>'
        f'<audio controls preload="none" src="{html.escape("graindemo/" + c["file"])}"></audio></label>'
        for c in grain["clips"])
    grain_html = ('<section><h2>Granular effect <span class="count">the new insert on a part</span></h2>'
                  f'<p class="lead">The same phrase on {html.escape(grain["preset"])}, four times. The effect remembers the last four '
                  'seconds of what a part plays and plays them back as a cloud of grains. Its Freeze switch, which holds the cloud for '
                  'as long as it is on, cannot be shown in a recording like this: try it in the plugin (insert type Grain).</p>'
                  f'<article class="item check"><div class="players">{audio}</div>'
                  '<label class="verdict">Your verdict <select data-demo="Granular effect">'
                  '<option value="">not judged yet</option><option>good</option><option>too loud or too quiet</option>'
                  '<option>sounds wrong</option><option>something else is wrong</option></select></label></article></section>')

page = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CoolSynth sample audition</title>
<style>
:root { --bg:#f6f5f2; --panel:#ffffff; --ink:#1d1d1b; --soft:#5d5b55; --line:#dcd9d1; --accent:#0b6e4f; --accent-ink:#ffffff; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#161614; --panel:#1f1f1c; --ink:#ecebe6; --soft:#a5a299; --line:#34332e; --accent:#5fc29f; --accent-ink:#10231c; }
}
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--ink); font:16px/1.5 system-ui, "Segoe UI", sans-serif; }
header, main { max-width: 980px; margin: 0 auto; padding: 0 16px; }
header { padding-top: 28px; }
h1 { font-size: 1.6rem; margin: 0 0 6px; }
h2 { font-size: 1.15rem; margin: 34px 0 10px; padding-bottom: 6px; border-bottom: 1px solid var(--line); }
h3 { font-size: 1rem; margin: 22px 0 6px; color: var(--soft); }
.count { color: var(--soft); font-weight: 400; font-size: .9rem; }
p.lead { color: var(--soft); margin: 0 0 10px; max-width: 70ch; }
.item { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; margin: 10px 0; }
.item.picked { border-color: var(--accent); box-shadow: inset 4px 0 0 var(--accent); }
.pick { display: flex; align-items: center; gap: 10px; cursor: pointer; }
.pick input { width: 20px; height: 20px; accent-color: var(--accent); flex: none; }
.name { font-weight: 600; }
.lib { margin-left: auto; color: var(--soft); font-size: .8rem; border: 1px solid var(--line); border-radius: 999px; padding: 1px 9px; white-space: nowrap; }
.facts { margin: 6px 0 8px 30px; color: var(--soft); font-size: .88rem; }
.players { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 8px 14px; margin-left: 30px; }
.player span { display: block; font-size: .8rem; color: var(--soft); margin-bottom: 2px; }
audio { width: 100%; height: 34px; }
details { margin: 8px 0 0; }
summary { cursor: pointer; color: var(--soft); padding: 6px 0; }
#bar { position: sticky; top: 0; z-index: 2; background: var(--bg); border-bottom: 1px solid var(--line); }
#bar .in { max-width: 980px; margin: 0 auto; padding: 10px 16px; display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
button { font: inherit; border: 0; border-radius: 8px; padding: 8px 14px; background: var(--accent); color: var(--accent-ink); cursor: pointer; }
button.quiet { background: transparent; color: var(--soft); border: 1px solid var(--line); }
#picked { color: var(--soft); }
.check .pick { cursor: default; }
.check .facts, .check .players { margin-left: 0; }
.verdict { display: block; margin-top: 10px; font-size: .9rem; color: var(--soft); }
select { font: inherit; margin-left: 8px; padding: 5px 8px; border-radius: 8px; border: 1px solid var(--line); background: var(--panel); color: var(--ink); }
textarea { width: 100%; min-height: 90px; margin: 10px 0 40px; font: 13px/1.4 ui-monospace, Consolas, monospace; background: var(--panel); color: var(--ink); border: 1px solid var(--line); border-radius: 8px; padding: 10px; }
@media (max-width: 560px) { .facts, .players { margin-left: 0; } }
</style>
</head>
<body>
<header>
  <h1>Sample audition</h1>
  <p class="lead">__COUNT__ instrument recordings from two libraries, VSCO 2 Community Edition and VCSL. Both are CC0: free to build into the synth.
  Nothing here is processed: each player is the raw recording, only made equally loud.</p>
  <p class="lead"><b>Every note, low to high</b> plays 1.3 seconds of each recorded pitch at a middle dynamic, so you hear how even the instrument is across its range.
  <b>One long note</b> is the middle of the range in full. <b>Soft to loud</b> is one pitch at every recorded dynamic.</p>
  <p class="lead">Tick what you would want in the synth, then press <b>Copy my picks</b> and paste the text into the chat.</p>
</header>
<div id="bar"><div class="in">
  <button id="copy">Copy my picks</button>
  <button id="clear" class="quiet">Clear</button>
  <span id="picked">Nothing picked yet</span>
</div></div>
<main>
__DEMOS__
__CHECKS__
__GRAIN__
__SECTIONS__
<h2>Your picks as text</h2>
<textarea id="out" readonly></textarea>
</main>
<script>
const KEY = "coolsynth-sample-picks";
const boxes = [...document.querySelectorAll('input[type=checkbox][data-id]')];
let saved = [];
try { saved = JSON.parse(localStorage.getItem(KEY) || "[]"); } catch (e) { saved = []; }
const verdicts = [...document.querySelectorAll('select[data-check]')];
let savedVerdicts = {};
try { savedVerdicts = JSON.parse(localStorage.getItem(KEY + "-verdicts") || "{}"); } catch (e) { savedVerdicts = {}; }
const demos = [...document.querySelectorAll('select[data-demo]')];
let savedDemos = {};
try { savedDemos = JSON.parse(localStorage.getItem(KEY + "-demos") || "{}"); } catch (e) { savedDemos = {}; }
function refresh() {
  const picked = boxes.filter(b => b.checked);
  const judged = verdicts.filter(v => v.value);
  const heard = demos.filter(v => v.value);
  boxes.forEach(b => b.closest(".item").classList.toggle("picked", b.checked));
  document.getElementById("picked").textContent =
    (picked.length ? picked.length + " picked" : "Nothing picked yet") + (judged.length + heard.length ? ", " + (judged.length + heard.length) + " judged" : "");
  let text = "";
  if (heard.length) text += "Heard on the page:\\n" + heard.map(v => "- " + v.dataset.demo + ": " + v.value).join("\\n") + "\\n";
  if (judged.length) text += "Model check:\\n" + judged.map(v => "- " + v.dataset.title + ": " + v.value).join("\\n") + "\\n";
  if (picked.length) text += "My sample picks:\\n" + picked.map(b => "- " + b.dataset.title + "  [" + b.dataset.id + "]").join("\\n");
  document.getElementById("out").value = text;
  try {
    localStorage.setItem(KEY, JSON.stringify(picked.map(b => b.dataset.id)));
    localStorage.setItem(KEY + "-verdicts", JSON.stringify(Object.fromEntries(verdicts.map(v => [v.dataset.check, v.value]))));
    localStorage.setItem(KEY + "-demos", JSON.stringify(Object.fromEntries(demos.map(v => [v.dataset.demo, v.value]))));
  } catch (e) {}
}
verdicts.forEach(v => { v.value = savedVerdicts[v.dataset.check] || ""; v.addEventListener("change", refresh); });
demos.forEach(v => { v.value = savedDemos[v.dataset.demo] || ""; v.addEventListener("change", refresh); });
boxes.forEach(b => { b.checked = saved.includes(b.dataset.id); b.addEventListener("change", refresh); });
document.getElementById("clear").addEventListener("click", () => { boxes.forEach(b => b.checked = false); refresh(); });
document.getElementById("copy").addEventListener("click", async () => {
  const text = document.getElementById("out").value;
  const button = document.getElementById("copy");
  try { await navigator.clipboard.writeText(text); button.textContent = "Copied"; }
  catch (e) { document.getElementById("out").select(); button.textContent = "Select the text below and copy it"; }
  setTimeout(() => button.textContent = "Copy my picks", 2500);
});
// Only one player at a time.
document.addEventListener("play", e => { document.querySelectorAll("audio").forEach(a => { if (a !== e.target) a.pause(); }); }, true);
// Stopping rewinds: the next play starts from the beginning, never from where it was stopped.
document.addEventListener("pause", e => {
  const a = e.target;
  if (a.tagName === "AUDIO" && !a.ended && !a.seeking) { try { a.currentTime = 0; } catch (err) {} }
}, true);
refresh();
</script>
</body>
</html>
"""
page = page.replace("__COUNT__", str(rows_total)).replace("__SECTIONS__", "\n".join(sections)).replace("__CHECKS__", checks_html).replace("__DEMOS__", demos_html).replace("__GRAIN__", grain_html)
with open(os.path.join(audition, "index.html"), "w", encoding="utf-8", newline="\n") as handle:
    handle.write(page)
print(rows_total, "instruments on the page in", len(sections), "families")
for family in order:
    if groups.get(family):
        print(f"  {len(groups[family]):3d}  {family}")

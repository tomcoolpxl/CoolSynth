"""Write the two pages of the audition folder.

index.html       the listening page: what is waiting for the user's ear, newest first, each with
                 a verdict and a note; one button copies all verdicts as text for the chat.
                   Listening test    clips that tell where something heard comes from (eartest/index.json)
                   Museum            rebuilt classic synth sounds against a reference, not yet admitted
                                     (museum/index.json)
                   Model check       presets corrected from recordings, still to be judged (modelcheck/index.json)
                   Granular effect   the insert's demonstration (graindemo/index.json), until it is judged
                   Conservatory      every recorded instrument as the synth plays it (conservatory/index.json),
                                     folded by family
recordings.html  the archive: what has been judged (admitted Museum sounds, model checks, the
                 granular effect) and the raw recordings the instruments were picked from, with
                 the picks.

A section whose index file is missing or has nothing waiting is left out. Verdicts and picks are
kept in the browser; a verdict on a card that has left the page stays in the store. Every
player's address carries its file's time of writing, so a clip rendered again is never played
from the browser's memory of the old one. REMARKS puts a line on a card (what changed since the
user's verdict, or where its open question is dealt with).

usage: python make_page.py <audition-dir>
"""
import html
import json
import os
import re
import sys

audition = sys.argv[1]
esc = html.escape

# Model checks the user has judged (decisions D94, D105, D107, D111): they move to the archive page.
JUDGED_CHECKS = {"marimba", "kalimba", "harp", "harp2", "harp3"}

# The granular effect was judged good on 13 November 2026 (decision D107): its clips move to the archive.
GRAIN_JUDGED = True

# A sound changed since the user judged it shows as not judged until it is judged again (the note the user
# wrote stays). Each sound has its own stamp, the round in which it last changed: a verdict set on that round's
# version stands; changing one sound again must not hide the verdicts given on the others meanwhile.
REJUDGE = {
    "Museum|Minimoog: Emerson's Fat Bass": "2026-11-14-top",
    "Museum|Minimoog: Trilogy lead": "2026-11-14-glide",
    "Museum|Prophet-5: Sync II": "2026-11-14-joins",
    "Museum|Prophet-5: Brass": "2026-11-14-joins",
    "Conservatory|Recorders": "2026-11-14-joins",
}

# A line on a card: "group|title" -> (its first words, the rest). A line that begins "Changed" asks for a new verdict.
REMARKS = {
    "Museum|Minimoog: Fat Bass with the faint sizzle": ("A second version of the Fat Bass you admitted. Yours is untouched.",
                                                        "The real Minimoog keeps a very faint sizzle on top through each note, far "
                                                        "under the note itself; a little of its raw sound slips past the filter. My "
                                                        "admitted version has none of it. This version has it, measured against the "
                                                        "recording, and with it a little less of the growl (the measurement asks for "
                                                        "less once the sizzle is there). The difference is small: listen for air on "
                                                        "top of the notes. If you admit this one it replaces the admitted Fat Bass; "
                                                        "if you prefer yours, say so and this one goes away."),
    "Museum|Contact opening: the second synth": ("New. The heavier synth that joins the first one. Headphones show its width best.",
                                                 "From 26 seconds into \"We Ride Tonight\", where the second synth comes in, to just "
                                                 "before the cymbals. The real clip has both synths playing, so mine has both too: "
                                                 "the first synth you admitted, underneath, and the new second one on top. "
                                                 "What I measured on the record: it is much brighter than the first, and every "
                                                 "note sits at its own place between the speakers and moves when it is struck "
                                                 "again. That is how an Oberheim polysynth behaves (my reading, not a fact). "
                                                 "The engine now does the same: each voice has its own place, and voices take turns. "
                                                 "Since the first version of this card I closed two gaps by about half: the bass "
                                                 "around the low B (now about 3 dB too quiet, was 6) and the width (the two "
                                                 "oscillators of each note now sit apart, as the record shows; mine is still a "
                                                 "touch narrower). The notes of both synths are my reading of the record, with "
                                                 "errors. Judge the sound more than the notes."),
    "Museum|Contact opening: the first synth": ("New. The first of the two synths, the one that plays alone.",
                                                "From The Sherbs' \"We Ride Tonight\" (1981), which \"Contact\" samples. Nobody has "
                                                "published which instrument it is, so this one is built from the recording itself. "
                                                "What I can hear in it by measurement: every note is two sawtooth oscillators, one "
                                                "tuned a little flat (that is the slow shimmer); one filter that jumps open at each "
                                                "new note and closes over a few seconds; all of it nearly in the middle, not spread "
                                                "left and right; never more than five or six notes at once. That is how a Prophet-5 "
                                                "is built, more than anything else of 1980, but that is my reading, not a fact. "
                                                "I had to read the chords from the recording too: a few inner notes and one or two "
                                                "bass notes may be wrong, so judge the sound more than the notes. The second, heavier "
                                                "synth is not built yet."),
    "Museum|TX81Z: LatelyBass": ("New. Bass speakers or headphones: its notes sit at 33 to 90 Hz.",
                                 "The house bass of the late eighties, from the instrument's own settings (read out of its "
                                 "firmware), nothing tuned by ear. It needed something my FM section did not have: the TX81Z's "
                                 "special wave shapes, now built in. The real one is a recording of a TX81Z playing a bass line; "
                                 "mine plays the same notes, each as hard as the recording says it was hit. On paper the two are "
                                 "within a decibel or two per note. The real recording carries a little hiss and has a touch more "
                                 "of the very lowest bass (its recording chain); mine is clean."),
    "Museum|Minimoog: Emerson's Fat Bass": ("Changed since your verdict, listen again.",
                                            "You heard notes start muffled. A third of the notes were being joined to the note before "
                                            "instead of struck anew, so the filter did not open again on them. Every note is struck now, "
                                            "as in the recording. A middle stretch where the player bends notes with the pitch wheel is "
                                            "left out; two stretches of the riff remain. And the growl you asked for is in: oscillator 3 "
                                            "now shakes the filter, as the chart has it, which also answers your \"too dull or muffled\": "
                                            "the top of the sound is within a decibel of the recording's up to 2.8 kHz now."),
    "Museum|Minimoog: Trilogy lead": ("Changed since your verdict, listen again.",
                                      "You heard the notes' starts smoothed. My version slid into each new pitch and joined most notes to "
                                      "the one before; the recording changes pitch at once and strikes every note anew. Both are as in "
                                      "the recording now. Then you heard the real notes begin with more of a ramp: the real filter "
                                      "takes about a hundredth of a second to open where mine opened at once. Mine does the same now, "
                                      "and its notes end as quickly as the real ones (they rang on four times too long). "
                                      "Then you still heard the beginning of a note as different. I found it: in the first sixth "
                                      "of a second after a note starts, the real one loses its sparkle only half as fast as mine "
                                      "did. Mine swept its filter much too far at every note (and sat too dull at rest). It now "
                                      "sweeps a third as far from a brighter resting point; the first sixth of a second of the "
                                      "notes now follows the real ones within a decibel. "
                                      "Then you wrote that the beginning was still very, very different and that I might not be "
                                      "seeing the note boundaries properly. You were right, twice over. First: on the real one the "
                                      "pitch slides into every note (quickly: an octave up takes a sixteenth of a second, coming "
                                      "down more than twice as long). I had measured that wrongly and taken the slide out; it is "
                                      "back, at the real speeds. Second: the player lifts many keys before pressing the next one, "
                                      "so the sound dips and the next note starts with a jump. Mine held every key down. It now "
                                      "lifts the keys where the real player does, and its notes begin where the real slides begin."),
    "Museum|Prophet-5: Sync II": ("Changed since your verdict, listen again.",
                                  "You heard an extra small note between the notes. That was my cutting of the recording: each cut ran "
                                  "on into the next note of the player's phrase. Each cut now ends before the pitch leaves its note."),
    "Museum|Prophet-5: Brass": ("Changed since your verdict, listen again.",
                                "Built from documents alone it sounded like a PC sound card. It is now fitted to a real Prophet-5 playing "
                                "this program and has a reference beside it: five chords cut from that recording."),
    "Conservatory|Recorders": ("Changed since your verdict, listen to Soprano again.",
                               "Your two tests traced the overdrive to one recording, the soprano's E6, whose strong overtones clash "
                               "with the other notes of a chord. At your choice E6 and F sharp 6 now play from the clean recordings "
                               "beside them. The Soprano clip is new."),
}


def load(*parts):
    path = os.path.join(audition, *parts)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def player(label, source):
    path = os.path.join(audition, *source.split("/"))
    stamp = f"?v={int(os.path.getmtime(path))}" if os.path.exists(path) else ""
    return (f'<label class="player"><span>{esc(label)}</span>'
            f'<audio controls preload="none" src="{esc(source + stamp)}"></audio></label>')


def verdict(group, title, options):
    """A verdict menu and a note, saved under group and title."""
    key = f"{group}|{title}"
    choices = "".join(f"<option>{esc(o)}</option>" for o in options)
    renewed = f'<p class="renewed"><b>{esc(REMARKS[key][0])}</b> {esc(REMARKS[key][1])}</p>' if key in REMARKS else ""
    again = f' data-rejudge="{REJUDGE[key]}"' if key in REJUDGE else ""
    return (renewed + f'<div class="verdict"><select data-key="{esc(key)}" data-group="{esc(group)}" data-title="{esc(title)}"{again} aria-label="Verdict on {esc(title)}">'
            f'<option value="">not judged yet</option>{choices}</select>'
            f'<input type="text" data-note="{esc(key)}" placeholder="a note, if something is off (optional)" aria-label="Note on {esc(title)}"></div>')


# ---- the listening page ---------------------------------------------------------------------
sections, nav, waiting = [], [], []

all_tests = load("eartest", "index.json") or []
tests = [t for t in all_tests if not t.get("done")]
done_tests = [t for t in all_tests if t.get("done")]
if tests:
    cards = []
    for t in tests:
        audio = "".join(player(c["title"], "eartest/" + t["id"] + "/" + c["file"]) for c in t["clips"])
        cards.append(f'<article class="card"><h3>{esc(t["title"])}</h3><p class="facts">{esc(t["facts"])}</p>'
                     f'<div class="players">{audio}</div>' + verdict("Listening test", t["title"], t["options"]) + "</article>")
        waiting.append("the listening test on the " + t["title"].lower())
    nav.append(("eartest", "Listening test"))
    sections.append('<section id="eartest"><h2>Listening test</h2>'
                    '<p class="lead">Not a sound to approve: a few clips that differ in one thing each, to find where something you '
                    'heard comes from. Play them at the loudness you normally listen at, then say which of them have it.</p>'
                    + "".join(cards) + "</section>")

all_museum = load("museum", "index.json") or []
museum = [m for m in all_museum if not m.get("admitted")]
admitted = [m for m in all_museum if m.get("admitted")]
if museum:
    cards = []
    for m in museum:
        waiting.append(m["title"] + " for the Museum")
        base = "museum/" + m["id"] + "/"
        cards.append(
            f'<article class="card"><h3>{esc(m["title"])}</h3>'
            f'<p class="facts">{esc(m["reference"])} {esc(m["source"])}</p>'
            f'<div class="players">'
            + (player("My build, from the documents (no reference)", base + "rebuild.flac") if m.get("no_reference")
               else player("The reference", base + "reference.flac") + player("My rebuild", base + "rebuild.flac"))
            + '</div>'
            # plain words: the user judges by ear and does not use synth terms (attack, decay)
            + verdict("Museum", m["title"], ["admit it", "not yet: too bright", "not yet: too dull or muffled",
                                             "not yet: how notes start is wrong", "not yet: how notes fade or end is wrong",
                                             "not yet: how one note passes into the next is wrong", "not yet: something else"]) + "</article>")
    nav.append(("museum", "Museum"))
    sections.append('<section id="museum"><h2>Museum</h2>'
                    '<p class="lead">A classic synth sound rebuilt from its published settings. The same notes twice, at the same '
                    'velocities, without effects and equally loud: a reference, then my rebuild. It enters the Museum room only if you admit it.</p>'
                    + "".join(cards) + "</section>")

checks = load("modelcheck", "index.json") or []
open_checks = [c for c in checks if c["id"] not in JUDGED_CHECKS]
if open_checks:
    cards = []
    for c in open_checks:
        waiting.append(c["title"])
        base = "modelcheck/" + c["id"] + "/"
        labels = [("real", "The real instrument"), ("before", "My preset before, dry"), ("after", "My preset now, dry"),
                  ("room", "My preset now, in its room")]
        audio = "".join(player(label, base + c["files"][key]) for key, label in labels if key in c["files"])
        cards.append(f'<article class="card"><h3>{esc(c["title"])}</h3><p class="facts">{esc(c["changed"])}</p>'
                     f'<div class="players">{audio}</div>'
                     + verdict("Model check", c["title"], ["close enough now", "closer, but not there", "no better than before", "worse than before"])
                     + "</article>")
    nav.append(("check", "Model check"))
    sections.append('<section id="check"><h2>Model check</h2>'
                    '<p class="lead">A modelled preset against a recording of the real instrument: the same notes each time, equally '
                    'loud. The clips marked dry are without effects.</p>' + "".join(cards) + "</section>")

grain = load("graindemo", "index.json")
if grain and not GRAIN_JUDGED:
    waiting.append("the granular effect")
    audio = "".join(player(c["title"], "graindemo/" + c["file"]) for c in grain["clips"])
    nav.append(("grain", "Granular effect"))
    sections.append('<section id="grain"><h2>Granular effect</h2>'
                    f'<p class="lead">The new insert on a part: it remembers the last four seconds of what the part plays and plays them back '
                    f'as a cloud of grains. The same phrase on {esc(grain["preset"])}, four times. Its Freeze switch cannot be shown in a '
                    'recording; try it in the plugin (insert type Grain).</p>'
                    f'<article class="card"><div class="players">{audio}</div>'
                    + verdict("Granular effect", "Granular effect", ["good", "too loud or too quiet", "sounds wrong", "something else is wrong"])
                    + "</article></section>")

demos = load("conservatory", "index.json") or []
if demos:
    families = {}
    for d in demos:
        families.setdefault(d["family"], {}).setdefault(d["instrument"], []).append(d)
    blocks = []
    for number, (family, instruments) in enumerate(families.items()):
        rows = []
        for instrument, styles in instruments.items():
            audio = "".join(player(s["style"], "conservatory/" + s["file"]) for s in styles)
            rows.append(f'<article class="card"><h3>{esc(instrument)}</h3><div class="players">{audio}</div>'
                        + verdict("Conservatory", instrument, ["good", "wrong notes or out of tune", "too quiet or too loud",
                                                               "uneven from note to note", "a loop or a click is audible",
                                                               "something else is wrong"]) + "</article>")
        blocks.append(f'<details class="family"{" open" if number == 0 else ""}><summary><span class="family-name">{esc(family)}</span>'
                      f'<span class="family-count" data-family-count>{len(instruments)} instruments</span></summary>{"".join(rows)}</details>')
    nav.append(("conservatory", "Conservatory"))
    sections.append('<section id="conservatory"><h2>Conservatory</h2>'
                    f'<p class="lead">The {sum(len(i) for i in families.values())} recorded instruments as the synth plays them, one player per '
                    'playing style: eight notes climbing through the instrument from soft to loud, then a chord. Open a family to hear it.</p>'
                    + "".join(blocks) + "</section>")

STYLE = """
:root { --bg:#f6f5f2; --panel:#ffffff; --ink:#1d1d1b; --soft:#5d5b55; --line:#dcd9d1; --accent:#0b6e4f; --accent-ink:#ffffff; --done:#e7f3ee; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#161614; --panel:#1f1f1c; --ink:#ecebe6; --soft:#a5a299; --line:#34332e; --accent:#5fc29f; --accent-ink:#10231c; --done:#1c2a25; }
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; scroll-padding-top: 64px; }
body { margin:0; background:var(--bg); color:var(--ink); font:16px/1.5 system-ui, "Segoe UI", sans-serif; }
header, main, .bar-in { max-width: 900px; margin: 0 auto; padding: 0 16px; }
header { padding-top: 26px; padding-bottom: 6px; }
h1 { font-size: 1.5rem; margin: 0 0 4px; }
h2 { font-size: 1.2rem; margin: 0 0 4px; }
h3 { font-size: 1rem; margin: 0 0 8px; }
p.lead { color: var(--soft); margin: 0 0 12px; max-width: 72ch; }
section { margin: 34px 0; }
a { color: var(--accent); }
#bar { position: sticky; top: 0; z-index: 2; background: var(--bg); border-bottom: 1px solid var(--line); }
.bar-in { display: flex; gap: 8px 14px; align-items: center; flex-wrap: wrap; padding-top: 9px; padding-bottom: 9px; }
.bar-in nav { display: flex; gap: 4px 14px; flex-wrap: wrap; }
.bar-in nav a { text-decoration: none; font-weight: 600; font-size: .92rem; }
#progress { color: var(--soft); font-size: .92rem; margin-left: auto; }
button { font: inherit; border: 0; border-radius: 8px; padding: 7px 14px; background: var(--accent); color: var(--accent-ink); cursor: pointer; }
button.quiet { background: transparent; color: var(--soft); border: 1px solid var(--line); }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; margin: 10px 0; }
.card.judged { background: var(--done); border-color: var(--accent); }
.facts { margin: 0 0 10px; color: var(--soft); font-size: .88rem; max-width: 80ch; }
.renewed { margin: 10px 0 0; padding: 8px 10px; border-left: 4px solid var(--accent); background: var(--bg); font-size: .9rem; max-width: 84ch; }
.players { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 8px 14px; }
.player span { display: block; font-size: .8rem; color: var(--soft); margin-bottom: 2px; }
audio { width: 100%; height: 34px; display: block; }
.verdict { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 10px; }
select, input[type=text] { font: inherit; font-size: .92rem; padding: 6px 8px; border-radius: 8px; border: 1px solid var(--line); background: var(--bg); color: var(--ink); }
.verdict select { flex: 0 1 250px; min-width: 0; }
.verdict input { flex: 1 1 220px; min-width: 0; }
details.family { border: 1px solid var(--line); border-radius: 10px; margin: 8px 0; padding: 0 12px; }
details.family[open] { padding-bottom: 4px; }
summary { cursor: pointer; padding: 11px 2px; display: flex; gap: 12px; align-items: baseline; }
.family-name { font-weight: 600; }
.family-count { color: var(--soft); font-size: .88rem; margin-left: auto; }
textarea { width: 100%; min-height: 110px; margin: 8px 0 12px; font: 13px/1.4 ui-monospace, Consolas, monospace; background: var(--panel); color: var(--ink); border: 1px solid var(--line); border-radius: 8px; padding: 10px; }
footer { max-width: 900px; margin: 0 auto; padding: 8px 16px 48px; color: var(--soft); font-size: .92rem; }
.pick { display: flex; align-items: center; gap: 10px; cursor: pointer; }
.pick input { width: 20px; height: 20px; accent-color: var(--accent); flex: none; }
.name { font-weight: 600; }
.lib { margin-left: auto; color: var(--soft); font-size: .8rem; border: 1px solid var(--line); border-radius: 999px; padding: 1px 9px; white-space: nowrap; }
.card.picked { border-color: var(--accent); box-shadow: inset 4px 0 0 var(--accent); }
"""

ONE_AT_A_TIME = """
// Only one player at a time.
document.addEventListener("play", e => { document.querySelectorAll("audio").forEach(a => { if (a !== e.target) a.pause(); }); }, true);
// Stopping rewinds: the next play starts from the beginning, never from where it was stopped.
document.addEventListener("pause", e => {
  const a = e.target;
  if (a.tagName === "AUDIO" && !a.ended && !a.seeking) { try { a.currentTime = 0; } catch (err) {} }
}, true);
"""

listening = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CoolSynth listening</title>
<style>__STYLE__</style>
</head>
<body>
<header>
  <h1>Listening page</h1>
  <p class="lead">What is waiting for your ear, newest first. Set a verdict under each sound, add a note where something is off,
  then press <b>Copy my verdicts</b> and paste the text into the chat. Verdicts are kept in this browser, so you can stop and come back.</p>
  __RENEWED__
</header>
<div id="bar"><div class="bar-in">
  <nav>__NAV__</nav>
  <span id="progress"></span>
  <button id="copy">Copy my verdicts</button>
</div></div>
<main>
__SECTIONS__
<section id="text">
  <h2>Your verdicts as text</h2>
  <p class="lead">What the button copies. If copying is blocked, select this text and copy it yourself.</p>
  <textarea id="out" readonly></textarea>
</section>
</main>
<footer>Earlier material, nothing to do there: <a href="recordings.html">what you have judged (the admitted Museum sound, the granular effect, earlier model checks) and the raw recordings the instruments were picked from</a>.</footer>
<script>
const STORE = "coolsynth-verdicts";
const selects = [...document.querySelectorAll("select[data-key]")];
const notes = Object.fromEntries([...document.querySelectorAll("input[data-note]")].map(n => [n.dataset.note, n]));
let saved = {};
try { saved = JSON.parse(localStorage.getItem(STORE) || "{}"); } catch (e) { saved = {}; }
// Verdicts set on the page as it was before it was tidied up.
try {
  const old = JSON.parse(localStorage.getItem("coolsynth-sample-picks-demos") || "{}");
  for (const [name, value] of Object.entries(old)) {
    if (!value) continue;
    const key = name.startsWith("Museum, ") ? "Museum|" + name.slice(8) : name === "Granular effect" ? "Granular effect|Granular effect" : "Conservatory|" + name;
    if (!saved[key]) saved[key] = { v: value, n: "" };
  }
} catch (e) {}
function refresh() {
  const groups = new Map();
  let judged = 0;
  for (const s of selects) {
    const note = notes[s.dataset.key].value.trim();
    s.closest(".card").classList.toggle("judged", !!s.value);
    if (!s.value && !note) continue;
    if (s.value) judged++;
    if (!groups.has(s.dataset.group)) groups.set(s.dataset.group, []);
    groups.get(s.dataset.group).push("- " + s.dataset.title + ": " + (s.value || "no verdict") + (note ? " (" + note + ")" : ""));
  }
  document.getElementById("progress").textContent = judged + " of " + selects.length + " judged";
  document.getElementById("out").value = [...groups].map(([group, lines]) => group + ":\\n" + lines.join("\\n")).join("\\n");
  document.querySelectorAll("details.family").forEach(d => {
    const own = [...d.querySelectorAll("select[data-key]")];
    d.querySelector("[data-family-count]").textContent = own.filter(s => s.value).length + " of " + own.length + " judged";
  });
  for (const s of selects) {
    const before = saved[s.dataset.key] || {};
    // a sound that changed keeps "judged again" only once a verdict is set on the changed version
    const round = s.dataset.rejudge ? (s.value ? s.dataset.rejudge : (before.r === s.dataset.rejudge ? undefined : before.r)) : before.r;
    saved[s.dataset.key] = { v: s.value, n: notes[s.dataset.key].value, r: round };
  }
  try { localStorage.setItem(STORE, JSON.stringify(saved)); } catch (e) {}
}
for (const s of selects) {
  const was = saved[s.dataset.key];
  if (was) {
    const changedSince = s.dataset.rejudge && was.r !== s.dataset.rejudge;  // judged before the sound changed
    if (!changedSince && [...s.options].some(o => o.value === was.v || o.text === was.v)) s.value = was.v;
    notes[s.dataset.key].value = was.n || "";
  }
  s.addEventListener("change", refresh);
  notes[s.dataset.key].addEventListener("input", refresh);
}
document.getElementById("copy").addEventListener("click", async () => {
  const button = document.getElementById("copy");
  const text = document.getElementById("out").value;
  if (!text) { button.textContent = "Nothing judged yet"; }
  else {
    try { await navigator.clipboard.writeText(text); button.textContent = "Copied"; }
    catch (e) { document.getElementById("out").select(); button.textContent = "Copy the text at the bottom"; }
  }
  setTimeout(() => button.textContent = "Copy my verdicts", 2500);
});
__ONE__
refresh();
</script>
</body>
</html>
"""
renewed_line = ""
if waiting:
    renewed_line = (f'<p class="lead"><b>Waiting for you now:</b> {esc(", ".join(waiting))}. Everything else is as you judged it; '
                    'what you accepted has moved to the archive.</p>')
listening = (listening.replace("__STYLE__", STYLE).replace("__ONE__", ONE_AT_A_TIME).replace("__RENEWED__", renewed_line)
             .replace("__NAV__", "".join(f'<a href="#{anchor}">{esc(label)}</a>' for anchor, label in nav))
             .replace("__SECTIONS__", "\n".join(sections)))
with open(os.path.join(audition, "index.html"), "w", encoding="utf-8", newline="\n") as handle:
    handle.write(listening)

# ---- the archive: raw recordings and judged model checks ------------------------------------
entries = load("previews", "index.json") or []

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
    library, rest = parts[0], parts[1:]
    if library == "VCSL":
        rest = rest[2:] if len(rest) > 2 else rest  # drop the two classification levels
    else:
        rest = rest[1:] if len(rest) > 1 else rest  # drop the section
    return " / ".join(rest), ("VCSL" if library == "VCSL" else "VSCO 2 CE")


def recording(e):
    title, library = title_of(e["path"])
    base = "previews/" + e["id"]
    facts = [f"{e['low']} to {e['high']}", f"{e['pitches']} recorded pitches",
             f"{e['layers']} dynamic layer{'s' if e['layers'] != 1 else ''}",
             f"{e['robins']} take{'s' if e['robins'] != 1 else ''} per note", f"{e['megabytes']:.0f} MB",
             f"{e['rate'] / 1000:g} kHz, {e['bits']} bit"]
    players = [("Every note, low to high", "run.wav"), (f"One long note ({e['long_seconds']:g} s)", "long.wav")]
    if e.get("layers_in_preview", 0) > 1:
        players.append(("Same note, soft to loud", "layers.wav"))
    audio = "".join(player(label, base + "/" + name) for label, name in players)
    return (f'<article class="card" data-id="{esc(e["id"])}">'
            f'<label class="pick"><input type="checkbox" data-id="{esc(e["id"])}" data-title="{esc(title)}">'
            f'<span class="name">{esc(title)}</span><span class="lib">{library}</span></label>'
            f'<p class="facts" style="margin-top:6px">{esc(" · ".join(facts))}</p><div class="players">{audio}</div></article>')


groups = {}
for e in entries:
    if not HIDDEN.search(e["path"]):
        groups.setdefault(family_of(e["path"]), []).append(e)

archive_sections, total = [], 0
if done_tests:
    cards = []
    for t in done_tests:
        audio = "".join(player(c["title"], "eartest/" + t["id"] + "/" + c["file"]) for c in t["clips"])
        cards.append(f'<article class="card"><h3>{esc(t["title"])}</h3><p class="facts">{esc(t["facts"])} <b>{esc(t["done"])}</b></p>'
                     f'<div class="players">{audio}</div></article>')
    archive_sections.append('<details class="family"><summary><span class="family-name">Listening tests done</span>'
                            f'<span class="family-count">{len(done_tests)}</span></summary>' + "".join(cards) + "</details>")
if admitted:
    cards = []
    for m in admitted:
        base = "museum/" + m["id"] + "/"
        cards.append(f'<article class="card"><h3>{esc(m["title"])}</h3><p class="facts">{esc(m["reference"])} {esc(m["source"])}</p>'
                     f'<div class="players">{player("The reference", base + "reference.flac")}'
                     f'{player("The preset in the Museum room", base + "rebuild.flac")}</div></article>')
    archive_sections.append('<details class="family"><summary><span class="family-name">Museum: admitted</span>'
                            f'<span class="family-count">{len(admitted)}</span></summary>'
                            '<p class="lead">Classic synth sounds you admitted in a side-by-side. They are in the plugin, in the room Museum.</p>'
                            + "".join(cards) + "</details>")
if grain and GRAIN_JUDGED:
    audio = "".join(player(c["title"], "graindemo/" + c["file"]) for c in grain["clips"])
    archive_sections.append('<details class="family"><summary><span class="family-name">Granular effect: accepted</span>'
                            f'<span class="family-count">{len(grain["clips"])} clips</span></summary>'
                            f'<p class="lead">The insert type Grain on {esc(grain["preset"])}, judged good once it softened the attacks it replays.</p>'
                            f'<article class="card"><div class="players">{audio}</div></article></details>')
judged_checks = [c for c in checks if c["id"] in JUDGED_CHECKS]
if judged_checks:
    cards = []
    for c in judged_checks:
        base = "modelcheck/" + c["id"] + "/"
        labels = [("real", "The real instrument"), ("before", "My preset before"), ("after", "My preset after"), ("room", "My preset after, with reverb")]
        audio = "".join(player(label, base + c["files"][key]) for key, label in labels if key in c["files"])
        cards.append(f'<article class="card"><h3>{esc(c["title"])}</h3><p class="facts">{esc(c["changed"])}</p>'
                     f'<div class="players">{audio}</div></article>')
    archive_sections.append('<details class="family"><summary><span class="family-name">Model checks already judged</span>'
                            f'<span class="family-count">{len(judged_checks)}</span></summary>'
                            '<p class="lead">Marimba and Kalimba: the corrected presets were found closer. Harp String: the first try was not '
                            'close, the second closer but not there, the third close enough.</p>' + "".join(cards) + "</details>")
for family in [name for name, _ in FAMILIES] + ["Other"]:
    items = groups.get(family, [])
    if not items:
        continue
    main_items = [e for e in items if not SECONDARY.search(e["path"])]
    extra = [e for e in items if SECONDARY.search(e["path"])]
    total += len(items)
    archive_sections.append(f'<details class="family"><summary><span class="family-name">{esc(family)}</span>'
                            f'<span class="family-count">{len(items)} recordings</span></summary>'
                            + "".join(recording(e) for e in main_items + extra) + "</details>")

archive = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CoolSynth archive</title>
<style>__STYLE__</style>
</head>
<body>
<header>
  <h1>Archive</h1>
  <p class="lead"><a href="index.html">Back to the listening page.</a> Nothing here needs doing. First what you have judged; then the
  __COUNT__ instrument recordings from the two CC0 libraries (VSCO 2 Community Edition and VCSL), unprocessed, from which you picked the 74
  that became the Conservatory. Your ticks are still shown; changing them and copying sends a new list of picks.</p>
</header>
<div id="bar"><div class="bar-in">
  <span id="progress"></span>
  <button id="copy" class="quiet">Copy my picks</button>
</div></div>
<main>
<section>
__SECTIONS__
</section>
<textarea id="out" readonly hidden></textarea>
</main>
<script>
const KEY = "coolsynth-sample-picks";
const boxes = [...document.querySelectorAll('input[type=checkbox][data-id]')];
let saved = [];
try { saved = JSON.parse(localStorage.getItem(KEY) || "[]"); } catch (e) { saved = []; }
function refresh() {
  const picked = boxes.filter(b => b.checked);
  boxes.forEach(b => b.closest(".card").classList.toggle("picked", b.checked));
  document.getElementById("progress").textContent = picked.length + " picked";
  document.getElementById("out").value = picked.length ? "My sample picks:\\n" + picked.map(b => "- " + b.dataset.title + "  [" + b.dataset.id + "]").join("\\n") : "";
  try { localStorage.setItem(KEY, JSON.stringify(picked.map(b => b.dataset.id))); } catch (e) {}
}
boxes.forEach(b => { b.checked = saved.includes(b.dataset.id); b.addEventListener("change", refresh); });
document.getElementById("copy").addEventListener("click", async () => {
  const button = document.getElementById("copy");
  try { await navigator.clipboard.writeText(document.getElementById("out").value); button.textContent = "Copied"; }
  catch (e) { const out = document.getElementById("out"); out.hidden = false; out.select(); button.textContent = "Copy the text at the bottom"; }
  setTimeout(() => button.textContent = "Copy my picks", 2500);
});
__ONE__
refresh();
</script>
</body>
</html>
"""
archive = (archive.replace("__STYLE__", STYLE).replace("__ONE__", ONE_AT_A_TIME).replace("__COUNT__", str(total))
           .replace("__SECTIONS__", "\n".join(archive_sections)))
with open(os.path.join(audition, "recordings.html"), "w", encoding="utf-8", newline="\n") as handle:
    handle.write(archive)

print(f"index.html: {len(nav)} sections ({', '.join(label for _, label in nav)}); recordings.html: {total} recordings")

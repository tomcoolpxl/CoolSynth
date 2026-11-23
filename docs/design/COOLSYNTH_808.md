# CoolSynth 808

A standalone drum machine app built on CoolSynth's circuit-level TR-808 (D263). This is the specification draft:
what the app does, how it is built, and in which order.
Nothing here is built yet. The draft's open questions are answered at the end; the user approves it before work starts.

## What is decided

- A standalone app first; the VST3 and CLAP formats come later.
- The name is CoolSynth 808; the line under it on the panel is Beat Composer.
  - Not searched in any trademark registry yet: to do before the first release.
  - "TR-808" never appears on the panel. The README says the app is based on the TR-808's circuits and that the
    mark is Roland's.
- The look is the mockup, `docs/design/mockups/coolsynth-808.html`, with colours, details and type still to fine-tune.
  - The panel: a column of knobs per drum, the strip, the row of sixteen steps, play, tempo, shuffle, A/AB/B,
    fill, pattern and MIDI drag.
  - The step row opens downward into a lane per drum, with a step inspector below.
  - Four themes: Studio 1982, Paper, Night and Blueprint. Own colours: the four beat groups are shades of one hue.
  - Under the Lid is the second view: the whole board as one reduced schematic, and each drum complete on a click.
    The first try at it (boxes per stage) is scrapped.
- The sound is the engine's 808 as it is: the sixteen sounds from eleven circuits in the 808 Drums, 808 Snare &
  Clap and 808 Metal generators, the stock 808 Kit's settings as the starting kit.
- The engine moves to its own repository, used by both CoolSynth and CoolSynth 808.

## The engine repository

Done (D265): `tomcoolpxl/coolsynth-engine`, public under MIT, CoolSynth's submodule at `external/coolsynth-engine`.
What follows is the plan as approved; D265 records what was found on the way.

- What moves: `src/synth` and `src/synth/v3`, the flight recorder they log to (`src/diagnostics`), the circuit
  references (`tools/tr808`, `tools/kick808`, `tools/kick909`), their test data (`tests/data`) and the tests that
  touch only the engine.
  - A first count: 46 of the 58 test files include the engine; 37 include none of the app folders.
  - The engine's one link to the app, `BuildInfo`, is cut first.
- What stays in CoolSynth: the parameters, presets and banks, the panel, the MiniLab driver, MIDI, the MCP server,
  the standalone app and the plugin.
- JUCE: 41 of the engine's 74 files use it. The engine links JUCE's modules that the app has already added; built
  alone for its tests, it adds JUCE itself.
- History comes along: `git filter-repo` with one `--path` per folder, in a fresh clone, keeps every engine
  commit (github.com/newren/git-filter-repo; GitHub's guide to splitting a subfolder out).
- The proof nothing changed: the determinism tests hold every preset bit for bit before and after the move.
- What it costs from then on: an engine change is a commit in the engine repository plus a pointer update in
  each app that wants it. Sessions working on the engine and an app touch two repositories.

## The sequencer

Simplicity for the player comes first, authenticity second (the user, D266). Each feature was kept for what it adds
against what it asks of the player, and the set was weighed as a whole: steps, patterns and chains to learn, and a
handful of buttons.

- The pattern:
  - A lane per drum and an accent lane, 16 steps each, sixteenth notes.
  - A and B variations, played as A, B or AB (A then B, alternating), as on the 808.
  - A length per lane, 1 to 16 steps; each lane loops on its own from the pattern's start (polymeter).
  - One accent per step for the whole kit: the drums on an accented step are struck at the Accent knob's level.
- On a step:
  - On or off.
  - A chance: drag the step down to have it play less often; the step's height shows it.
  - A roll: one, two or three hits in the step.
- Playing:
  - Start and stop, tempo, and one swing knob for the whole kit, 50 to 75 % (Roger Linn's swing: the even
    sixteenths delayed; 50 % is straight). The original TR-808 had none; it came from Linn's LM-1.
  - Sixteen patterns a bank, chosen on the step keys.
  - Chaining: hold one pattern key and press another, and the patterns between them play in a row.
  - Fill: plays the bank's fill pattern once, at the next bar, then goes back.
  - Mute and solo per drum.
  - Record: drums played on MIDI or clicked while it runs are written to the nearest step.
  - MIDI clock in and out; dragging a pattern out as a MIDI file.
  - Copy, paste, clear and undo.
- Left out, for simplicity: a swing per lane (swing moves only the even sixteenths, which a kick on the beat never
  plays), per-step locks, playing every Nth round, flams, triplets (pre-scale) and a song mode. Locks, triplets
  and a song mode can come later without changing what is built.
- Every random choice restarts from its seed when the transport starts, as everywhere in CoolSynth (D224).

## Saving

- A kit: every knob and switch of the eleven circuits.
- A pattern set: the banks, their patterns with A and B, and their fill patterns.
- A project file holds one kit and one pattern set; the app reopens the last one.

## To design with the user: the pattern library and keeping your own

Asked for by the user (2026-11-22), to be designed with options before it is built.

- A built-in library of the classic beats the 808 made famous: electro, early hip hop, Miami bass, acid house,
  Detroit techno and others.
  - What the research found so far: under US law a rhythm on its own is generally not protected, though a
    collection's selection and arrangement can be, and a test case is pending (Loyola's IP Bytes, Klemchuk,
    Mondaq's "Beats Me", Bangor University on the reggaeton case). Rules elsewhere may differ.
  - So each beat is written down here from the records themselves or several descriptions, never copied from one
    pattern book or from Roland's own paid packs (Egyptian Lover's, Modern Hip-Hop Beats).
  - Open: naming them after the record, after the style and year, or both; how many; who checks them by ear.
- How the library is shown: ideas from others to weigh.
  - Collections, categories, favourites and recently used, with drag and drop to import (Cherry Audio's CR-78).
  - Searchable tags (Future Drummer).
  - A kit with each pattern or one kit for a project (Patterning 3, DM10).
- Keeping your own patterns, which matters more without a DAW to save the project:
  - where they are kept on disk, naming, duplicating, deleting and undoing a delete;
  - importing and exporting single patterns, sets and kits, and MIDI files in and out;
  - how the library's beats and your own sit side by side, and whether a library beat can be overwritten.
- Three directions to choose from, in `mockups/coolsynth-808-library.html` with the app's 56 beats:
  - the shelf, a browser over the panel with shelves, folders and favourites (as Kontakt's and XO's);
  - the wall, every beat a tile by family and decade, with a drawer to audition and load;
  - in the panel, a browse line stepped through with the arrows or the controller's main knob, the beat shown
    dashed over your own pattern (as a hardware machine's pattern select).
  - Auditioning in time with the transport, from the next bar, comes from Native Instruments' pattern browser.
- Chosen (D273): the shelf, without folders and favourites for now. The classics are read-only; your own patterns
  can be renamed and sent to the Recycle Bin. Audition starts at once, not from the next bar.

## Under the Lid in the app

- The engine's circuits are wave digital filters, so each part's voltage and current follow from its waves at
  every sample (v = (a + b) / 2, i = (a - b) / 2R): the view can show the solver's own currents, live, with the
  knobs working, where the mockup plays one ngspice recording.
- A tap in `CircuitWdf` hands the view a few values per part per screen frame, never per sample, so the sound
  costs the same.
- Each drum's complete drawing is made from its netlist and checked against it, as the mockup's bass drum is.

## The order of the build

- The engine repository, with CoolSynth moved onto it and every test passing. Done (D265).
- The sequencer, in the engine repository so CoolSynth's layers can use it later, with its tests. Built (D266):
  its playback; recording, the chain keys, MIDI clock and handing it edits come with the app.
- The app's shell: a window, the audio device, the 808 Kit from the engine, MIDI in at the General MIDI keys.
  Built (D267), in `tomcoolpxl/CoolSynth808`.
- The panel and the lanes in the four themes. Built (D267).
- Saving kits, patterns and projects. Built (D267): projects, undo, and patterns kept in the library.
- Under the Lid: the taps, then the reduced board, then each drum complete. The reduced board is built (D267), its
  current drawn from the drums struck; the solver's taps and each drum complete are still to do.
- The installer and the first release; the plugin formats after.

## Answered (D264)

- The sequencer lives in the engine repository, general enough for CoolSynth's layers to use later.
- The repositories: `tomcoolpxl/coolsynth-engine` and `tomcoolpxl/CoolSynth808`, both public under the MIT licence,
  as CoolSynth is. CoolSynth's public builds can then fetch the engine without credentials.
- Standalone, CoolSynth 808 follows incoming MIDI clock and start and stop when there is one, and sends its own
  otherwise, as hardware drum machines do. The computer's MIDI timing is smoothed, with tests.
- A pattern set holds banks of sixteen patterns, each with A and B, chosen on the step keys in a pattern mode as
  the hardware does; a fill pattern a bank (D266: one, not four; chaining replaces tracks); as many banks as wanted.

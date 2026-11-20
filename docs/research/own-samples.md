# Playing your own samples: how it is done elsewhere, and CoolSynth

Preliminary research for TODO 26 and 27, 2026-11-20, for the user's discussion. Nothing is decided here; the options
at the end go to the user as a pick list. Goes with TODO 24 (making and keeping your own sounds).

## CoolSynth today

From "I have a WAV" to "it plays across my keys", as the test pass went:

- Copy the WAV into `C:\ProgramData\CoolSynth\Library\User` in Explorer. CoolSynth cannot take a file from
  anywhere else (D137: a sound names a file by its path inside that folder, so a shared sound plays the same).
- Load a sound that already plays. There is no starting sound for samples, so the user took Basic Saw.
- Open the Sound section (pad 1), move to G1 with the main knob, press it to open the type list, choose Sample.
  The slot starts from the Sample type's defaults: the first generated sample.
- Turn knob 1 through the 5 generated samples and the 40 instruments into Your files (files, then each folder as a
  kit, in name order). The card shows CLICK TO LOAD; a press or a click loads the file (D156).
- For slices, turn knob 5 to the slot's fourth page: Slices Equal or Attacks, Count, Sensitivity, First Key (C3).

Where it is slow or confusing:

- The first sample needs four places: Explorer, the sound list, the type list, a long list on one knob.
- The sample keeps the saw's surroundings: the Ladder filter (open, but its macros Warmth and Grit still move it),
  the saw's envelope, its macro names, and its layer level of -11 dB. A sample is not levelled when it loads; the
  test loop peaked at -24 dB and sounded far too quiet (fixed in part by the Master Boost, D225).
- One knob through 45 entries and then every file; nothing jumps to Your files or to a folder (TODO 26).
- Nothing is heard until the press, so finding the right file means loading each one.
- The value on the card cannot be clicked to choose (TODO 26).
- A sliced loop or a kit is a few values on a fourth page; nothing shows where the cuts fall or which key plays
  which piece.

The engine itself held up in the test pass: slices, kits, Reverse, Start and no clicks were all fine.

## What others do

### Drop a file, get an instrument

Almost every sampler starts from a drop.

- Ableton Simpler takes a sample dragged from the browser or from a clip, in Classic mode by default; One-Shot is
  for hits, Slice cuts by transients, beats, regions or by hand ([Live manual, Simpler](https://www.ableton.com/en/manual/live-instrument-reference/);
  [Sound On Sound, "Keep It Simple"](https://www.soundonsound.com/techniques/keep-it-simple)).
- What a dropped sample becomes in Live is itself a preset the user can edit: Live Places, Defaults, Dropping
  Samples ([Sound On Sound, Live 12 Drum Racks revisited](https://www.soundonsound.com/techniques/live-12-drum-racks-revisited)).
- Kontakt: double-click a sample or drop it on free rack space and it makes a new instrument from the default
  instrument, the sample spread across the whole keyboard ([Kontakt manual](https://native-instruments.com/ni-tech-manuals/kontakt-manual/en/loading-and-creating-instruments)).
- Logic: drop a file on the empty area under the tracks to make a Quick Sampler track; drop on the track header to
  replace the sample; several files at once make a Sampler with one file per key ([Apple](https://support.apple.com/HT210998)).
- Bitwig Sampler: drag in an audio file or any clip; 6.1 drops samples straight into a new Sampler with preset
  configurations ([Bitwig](https://www.bitwig.com/sampler/); [6.1 release notes](https://downloads.bitwig.com/6.1/Release-Notes-6.1.html)).
- FL Studio: a sample dropped on the Channel Rack becomes a Sampler channel; Slicex takes a drop on its channel or
  window, Shift adds to what is there ([channel sampler](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/chansettings_sampler.htm);
  [Slicex](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Slicex.htm)).
- Serum 2 and Vital, synths with a sample oscillator, both take a file dropped on the oscillator's display; Serum
  also lists built-in samples in a menu there ([Production Music Live, Serum 2](https://www.productionmusiclive.com/blogs/news/how-to-use-samples-in-serum-2);
  [LANDR, Vital](https://blog.landr.com/vital-synth)).

Drops do not always reach a plug-in. JUCE's FileDragAndDropTarget works from Explorer and from most hosts'
browsers, but reports say FL Studio's Edison and some timeline clips do not arrive, and VST3 on Linux is unreliable
([JUCE forum, FL Studio](https://forum.juce.com/t/drag-and-drop-audio-file-from-fl-studio-into-plugin/28086);
[JUCE forum, Linux](https://forum.juce.com/t/drag-and-drop-not-working-under-vst-in-linux/50410)). A second way in
is needed anyway.

Where the file lives differs. Pigments copies an imported file into its own folders, and its users complain about
where copies land and about missing files after a move ([Arturia forum](https://forum.arturia.com/t/pigments-saving-samples-in-inconsistent-and-weird-places/3743)).

### A browser that plays before it loads

- Ableton's browser has a Preview tab with a waveform and plays the selection when Preview is on
  ([Live manual, the browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/)).
- Bitwig's browser has a preview button and an automatic preview that plays any file as soon as it is selected,
  stepping with the arrow keys ([Bitwig user guide, browsers](https://www.bitwig.com/userguide/latest/browsers)).
- The MPC browser auditions a sample when it is selected if Auto Audition is on ([Akai, the MPC's browser](https://support.akaipro.com/en/support/solutions/articles/69000871930-akai-pro-mpc-series-understanding-the-mpc-s-browser)).
- On the Digitakt II, FUNC and YES previews a file on the +Drive while browsing ([Sweetwater quickstart](https://www.sweetwater.com/sweetcare/articles/elektron-digitakt-ii-quickstart-guide/)).
- Pigments' sample browser has folders the user adds, beside the factory categories; its preset browser has tags
  and favourites (same Arturia thread; [Splice, Pigments sampling](https://splice.com/blog/pigments-sampling-granular-synthesis)).

The hardware lesson for a knob: preview while you turn, and jump by folder.

### A sampler view

Every dedicated sampler shows the waveform with its start, end and loop flags and, in slice mode, the cuts; the
slice markers can be moved by hand (Simpler, Quick Sampler, Slicex, Serum 2's transient slices, up to 64:
[Xfer, Serum 2 what's new](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)). A sampler page is a small
set of controls: the file, gain, start and loop, mode, the amp envelope, a filter. Simpler is the model for "one
page that is enough".

### Kits and slices

- Dropping several samples on a Drum Rack pad fills the next pads; a sliced Simpler converts to a Drum Rack, one
  pad per slice ([Ableton help](https://help.ableton.com/hc/en-us/articles/209072869)).
- The MPC's Chop mode cuts a sample and gives slice 1 to pad A01 and so on ([Akai, Sample mode](https://www.akaipro.com/guides/mpc-sample/sample_mode.htm)).
- The Polyend Tracker slices by transients or into a set count on its Sample Playback page ([Polyend forum](https://backstage.polyend.com/t/sample-slicing-mode/1057/26)).
- Decent Sampler has no drop-to-instrument; a third-party tool writes a preset from a folder, one file per key
  from a chosen note ([Bedroom Producers Blog](https://bedroomproducersblog.com/2024/03/20/decent-sampler-xml-generator/)).

CoolSynth's folder-as-kit already works this way; what it lacks is seeing the pads.

### Levelling on load

- Logic's Quick Sampler offers two drop zones: Original keeps tuning, loudness, loop and length; Optimised sets
  tuning and loudness, finds loop points and crops silence ([Apple, Quick Sampler](https://support.apple.com/en-asia/102041)).
- Simpler has a Gain slider before its Volume; FL's sampler and the MPC normalise only when asked, the MPC to a
  0 dB peak between start and end (Live manual above; FL channel sampler above; [Akai, Sample mode](https://www.akaipro.com/guides/mpc-sample/sample_mode.htm)).

So levelling is offered, either chosen at the drop (Logic) or as a tool (FL, MPC), and never forced.

## Options for CoolSynth

The work splits in two. Some needs no real interface and could come before it: a starting sound, the drop,
preview while turning, levelling, jumps in the list. The rest is the real interface (UI last), which the user wants
to discuss properly.

### A starting sound and a drop now, the sampler view with the real interface (recommended)

What it gives, without the real interface:

- A Sampler sound in the Workshop: G1 Sample, no filter, a clean envelope (instant attack, full sustain, short
  release), its macros on Start, Slices, Tone and Tail, its level set for a levelled sample. It replaces "load
  Basic Saw, then change the type".
- A file or folder dropped anywhere on the panel goes into the current Sample slot, or, when the layer has none,
  loads the Sampler sound with it. A folder is a kit. The file is copied into the User folder (a Dropped folder
  inside it), so D137 holds and a saved sound still names it by path. The drop is a second way in, not the only
  one.
- Levelling as Logic does it: the file keeps its peak or is brought to a set level by a gain stored in the sound,
  the audio untouched. Which, peak or loudness, and whether by default, is the user's choice.
- Knob 1 previews: resting on a file for a moment plays it once on a preview voice outside the sound, at a fixed
  level, as Bitwig's auto-preview and the MPC's Auto Audition do; switchable in Prefs. The same works on the MiniLab.
- Knob 1 jumps: Shift (or a fast turn) moves group by group: generated, instruments, Your files, each folder.

Cost: the sound is a bank entry; the drop, the copy and the levelling gain are a few days with tests; the preview
voice is the largest part (a voice outside the layers, cut when the knob moves on).

Side effects: copies fill the User folder and two drops of the same name need a rule (keep both, numbered); drops
that a host does not pass on still need the folder; levelling changes how loud new sounds are against old ones, so
sounds saved before keep no gain.

### A sampler level of its own, with the real interface

A Sampler view reached from Home or from a Sample slot: the waveform with start, loop and slice markers, the file's
name (clicked, it opens the browser), gain, mode (Classic, One-Shot, Slice, Kit), the envelope and one filter. The
full G1 to G3 chain stays for the advanced; the view is the same sound seen simply, as Simpler is to Sampler.

Cost: the largest; it needs the waveform display, markers that can be dragged, and a mode that maps onto the
existing Slices values. Side effects: two ways to see one slot, which must always agree; the MiniLab's small
screen gets a reduced form.

### One browser for sounds and samples, with the real interface

The click on the lit value (TODO 26) opens a list: folders, the instruments by kind, Your files, recent files and
favourites, with a preview button and auto-preview, typed search. The same browser serves the sounds (TODO 24).

Cost: large, shared with TODO 24, so cheaper together than apart. Side effects: favourites and recents need a file
of their own beside the library; a plain JUCE pop-up menu could stand in before the real interface, but would be
thrown away.

### A pad view for kits and slices, later

Each key's piece shown as a pad, a file dropped on a pad replacing one hit, per-pad level and pitch, a slice
converted into a kit as Ableton does.

Cost: engine work too, as slices and kit files have no settings of their own today. Side effects: the sound format
grows per-pad values. Best after the sampler view.

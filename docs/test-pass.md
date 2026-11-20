# Test pass before the next release

What has changed since 0.3.0 and has not been tried by hand yet.

Each line says what to do and what should happen. Tick it off here, or tell me what went wrong.

## Slices and kits (D207)

- [ ] Put a WAV of your own (a drum loop works well) in the sample library's User folder.
- [ ] Choose it in a Sample slot: turn the Sample knob past the 40 instruments into Your files, and press.
- [ ] Slices Equal, Count 16: from First Key (C3) up, each key plays its own piece.
  - A piece plays to its end after you let go, then the sound releases.
  - Pieces overlap when you play fast; with the layer in Mono a new key cuts the last one.
- [ ] Slices Attacks: low Sensitivity cuts at the strongest hits only, high at every hit.
- [ ] Reverse and Start still work on a slice; no clicks at the cuts.
- [ ] A folder of WAVs as a kit: one file per key, in Explorer's order.
- [ ] Save the sound, load another, load it back: the same file and slices.

## Controllers and MIDI (D211, D216, D217, D218)

- [ ] The app with the MiniLab: knobs, pads, the screen.
- [ ] Unplug the MiniLab while the app runs, then plug it back in.
  - Its screen lets go at once and comes back.
- [ ] The MiniLab driver Off, in both MiniLab programs (Arturia and DAW): the ready setups play the panel.
- [ ] Another MIDI keyboard: Prefs > Your keyboard, its size; its keys are marked on the screen's.
- [ ] In a DAW, the VST3 and the CLAP, each in Ableton, FL Studio and Reaper (TODO C6):
  - Ableton finds CoolSynth (Use VST3 Plug-In System Folders on).
  - FL Studio: a note never plays twice; CoolSynth asks once for channel 1 where FL merges channels.
  - The CLAP's knob pages follow the panel's page in a host that shows them (Bitwig does).
  - A right click on a layer opens its menu at the panel.
  - The panel at Windows scaling 125 % and 150 %, in the app and in a DAW.
  - The guided controller setup, step by step.

## Sound (TODO 3, 4, 13, 18)

- [ ] The listening pass over the bank: levels, the glow, click-to-load, sounds ringing on after a change.
  Supersaw's undertone (your test pass): its sub, layer B, a sine an octave under the lowest key at -5 dB, by design;
  keep it, make it quieter (about -11 dB) or remove it.
- [ ] The sampled instruments, on the audition page.
- [ ] The new noise (D221): hiss and crackle in Bedroom, Basement and Night Bus, and that it mutes in silence.
- [ ] The stutter's drafts (D220): Cut-Up Chords, Wheel Stutter, Glitch Pad.
- [ ] The ladder by ear: drive headroom, bass, the modes' levels, self-oscillation.

## New today (D222, D224)

- [ ] Bounce the same bars twice in a DAW, with a random arp preset (Satellite Ping, Rain Chime):
  - The two bounces null: one inverted against the other gives silence.
  - Playing it live sounds like the bounce.
- [ ] The host parameter A Arp New Patterns on: each play draws a new pattern.
- [ ] Bounce a Conservatory preset straight after loading it: no missing first notes.
- [ ] The screen's info row shows "N of M voices".
  - A patch of your own with three FM4 generators and two ladders shows about 11 voices at most.
- [ ] Very small DAW buffers with a heavy preset: if it crackles, released notes should let go first.

## Results so far (2026-11-19)

- Round 1, app and MiniLab: knobs, faders, pads, Stop, "N of M voices", unplugging and plugging back in: all fine
  once FL Studio's CoolSynth let go of the MiniLab.
- Round 2, slices: choosing a file, a piece per key, playing to the end, Reverse, Start, no clicks: fine. The kit
  (CST2): kick, snare, hat, clap on the four leftmost keys: fine. Mono's choke: the test sounds are too short to hear.
- Round 3, by ear: Vinyl Choir's crackle and hiss, and their fade in silence; Pirate Radio and its Static knob; the
  stutter's Cut-Up Chords, Wheel Stutter and Glitch Pad: fine.
- The voice cap, checked through the MCP server: three FM4 generators and two ladders, sixteen keys: 13 voices.

## Findings, to fix after the rounds

- Pirate Radio's fade-out is a little strong (the user: maybe as it should be).
- Wheel Stutter catches at once: through the processor the stutter starts in the very block the wheel arrives in
  (tests/V3StutterTests.cpp). A 50 ms level trace from the running app had suggested 0.3 s, but the first repeats are
  nearly as loud as the note, so the level cannot show the start. Nothing to fix.
- After a replug the pads stayed white: fixed (D225), the driver sends everything again 1 s and 3 s after connecting;
  the user saw the colours come back.

- With the MiniLab driver switched off (it was, since 12:16 in FL Studio) and the MiniLab left in its DAW program, the
  knobs and faders did nothing: the setup in use was a copy saved by 0.3.0, with the Arturia program's numbers only.
  Fixed (D225): an unchanged copy of a ready setup from an older CoolSynth becomes today's, which takes both
  programs' numbers; a setup the player changed stays as it is.
- After unplugging and plugging the MiniLab back in, the lights were all white: the colours are not sent again.
- Slices by attacks: Sensitivity brought in every attack of the test loop within the knob's first 42 %. Fixed
  (D225, the user's choice): it keeps the strongest so many, by rank, one more slice per step over the whole knob.
- The attack detector found 7 of the loop's 8 hits, ranked the hats strongest and missed the kick at 0.75 s. Fixed
  (D225, the user's choice): flux in third-octave bands, three ranges; all 8 found, kicks first, hats last.
- Choosing a sample by turning one knob through the whole list is slow, and clicking the lit value should set it
  too (TODO 26).
- The value display is too small for a file's name: "CoolSynth test kit" and "CoolSynth test kit/10 clap.wav"
  looked the same, as only the first characters showed. Fixed (D225, the user's choice): the knob's name line says
  where the file is ("Kit: CST2", "CST2/", "Your files"), its value line the file's name without ".wav"
  (a kit: how many files); what does not fit is shortened in the middle, on the panel and on the MiniLab.
  Not built: the single slow scroll of a long name after the knob rests.
- That one file seemed not to play: replayed from the app's saved state it plays at -79 dB, as Start was at 21 % and
  the test clap has nearly all its sound in its first 50 ms. Not an engine fault; the names are now short (CST1, CST2).
- Your own samples play very quietly: a file put into a preset takes that preset's level (Basic Saw's layer is at
  -11 dB, as a saw is loud), and files are not levelled; the loop peaks at -24 dB. Measured again, the loop is only
  3 dB under Basic Saw's own saw: the whole synth is quiet, at -20 LUFS. Fixed (D225, the user's choice): the Master
  fader goes up to +12 dB beyond its top, the Master Boost.
- Looking for Mono, Rain Chime, Prophet Brass and Juno Pad loaded one after the other (a click on a list row loads
  at once) and the edited kit sound was gone without a question. Fixed (D225, the user's choice): an edited sound
  the panel replaces is kept, and the list of rooms offers it first ("Back to your edit: ..."); a press restores it.
  Turning the big knob never loaded: it browses, a press or a click loads.
- Finding Mono took a long time: the layer's play mode is Home's Voice page, knob 1, which nothing points to.
- The flight recorder logs notes and presets but not knob changes, so a note cannot be matched to the settings it
  played with. Done (D225): every knob change made in the app and every file choice is logged.
- Not known yet: whether the app told you the MiniLab was in use by another program.

## Retests of the fixes (2026-11-19 and 20)

All passed:
- the Master Boost: louder past the top, labelled in amber; below the 0 dB mark it is gone;
- Back to your edit: the edited Reese came back from the list of rooms;
- Sensitivity: one slice at 0 %, keys coming in one by one up the knob, each on a hit, no cutoff;
- the two-line names: kits, files in a kit and your own files told apart, over all 15 entries (checked through
  the MCP server; "Kit: Demo Kit" shows as "Kit: D…o Kit" on the main screen's card, whole under the knob);
- the controller setup with the MiniLab driver off: knob 1 reaches CoolSynth in the DAW program and the Arturia
  program, the saved setup having both programs' links.

Found on the way: Supersaw's undertone is its sub, by design (on the listening pass list above).

## The DAW round (2026-11-20, test installer from build 4f3fdb8)

- FL Studio 2025, VST3: CoolSynth loaded, the MiniLab connected to it in its DAW program, 163 notes each arrived
  once, the Master fader moved and came back to 0 dB.
- Not working: the project saved with CoolSynth in it (the file holds its VST3 path and state: Reese, edited)
  opened twice without CoolSynth. FL showed only its Sampler, CoolSynth's log shows no start, and Windows logged no
  crash. Investigated:
  - the saved project holds six channels (808 Kick, Clap, HiHat, Snare, FLEX Bass, CoolSynth), but FL showed one
    Sampler: the project in FL was not that file, FL did not load it;
  - CoolSynth takes the state from that file as a host gives it (the opt-in test `Host state blob`,
    `COOLSYNTH_STATE_BLOB`): the edited Reese, playing at -14.8 dBFS;
  - Image-Line's manual: in trial mode FL Studio saves projects but does not reopen them until it is unlocked. FL's
    settings here have no registration entry where older versions keep one; the user's FL Studio is the trial.
    Reopening is FL's limit, not CoolSynth's: the save-and-reopen check moves to Ableton Live.
- Two FL Studio crashes at 00:49 (combase.dll, fail-fast) were Claude's attempts to open the project from outside
  while FL ran, not the user's FL.
- Ableton Live 12 Lite, VST3: found after a rescan, played the MiniLab's keys and knob 1; the set saved, closed and
  opened again, CoolSynth started again and played (its log: two sessions, 01:03 and 01:05).
- FL Studio 2025, CLAP: loaded from FL's browser (Generators, CLAP), the panel showed, FL's typing keyboard played
  notes 60, 64, 67 and 48 at about -17 dB (its log: `CoolSynth.clap`, 01:14). Not saved: FL is the trial.
- Zrythm 1.0.0 trial, VST3 and CLAP: both found in the plugin scan, both loaded on instrument tracks with their
  panels, and both played notes from clips in Zrythm's piano roll at about -17 dB. The project saved, Zrythm closed
  and opened it again: both CoolSynths started again (logs at 01:40:10, VST3, and 01:40:11, CLAP), their panels
  showed and they played the saved notes.
- Zrythm's own problems on the way, none from CoolSynth:
  - it did not start here: its image loader list (`loaders.cache`) leaves out the SVG loader it ships, so it quits
    with "SVG loader was not found"; started with `GDK_PIXBUF_MODULE_FILE` pointing at a corrected copy, the install
    left as it is;
  - its Vulkan drawing showed stale frames; started with `GSK_RENDERER=gl`, and `GDK_SCALE=2` to be readable;
  - maximized, it drops clicks on the right part of its window; a window sized by hand works;
  - the first close crashed inside zrythm.exe (its backtrace: the piano roll's ruler redrawn after the project was
    freed, both CoolSynths already removed cleanly); the second close was clean.
- Not done yet: Reaper is not installed; the MiniLab's keys were not played into the CLAP or into Zrythm (the notes
  came from FL's typing keyboard and Zrythm's clips).
- Automatic checks (D228): the test program hosts the built VST3 (plays, reopens its state bit for bit, bounces
  twice the same, opens its panel); pluginval passes the VST3 at strictness 5; clap-validator found typed values
  read back wrong and renamed parameters the host never heard of, both fixed; left as accepted: the start sound
  is not the declared defaults, and a curved value can come back a float step off.
- The MiniLab's fader 4 now reaches the Master Boost (0 dB at four fifths, +12 dB at its top): the app's log has it
  up to +12 dB and back; the user heard it louder at the top and smooth through 0 dB.

## The listening pass, priority sounds first (2026-11-20)

Each sound set up and played by Claude through the MCP server; the user listened. All passed:
- Supersaw: its sub (layer B, a sine an octave under the lowest key) stays, the user's choice after hearing it twice.
- The new hiss and crackle in Vinyl Choir, Concrete Pad and Pirate Radio: like tape and records, at the right level,
  and silent once the sound has died away (Pirate Radio measured silent 2.5 s after the release).
- The stutter presets Cut-Up Chords, Wheel Stutter and Glitch Pad: in time, clean. Cut-Up Chords sounded too loud
  from four keys, each playing its minor ninth chord; with one key, as it is levelled (the levelling tool: -19.5
  LUFS against -20), it is in line, and it stays.
- Siren, the ladder at full resonance on its own: a clean tone, the same level low and high.
- The full bank by ear is still to do (TODO 3).

## Then the release (TODO J3)

- The version number is your choice; the release notes go in `docs/releases/`; then the tag.

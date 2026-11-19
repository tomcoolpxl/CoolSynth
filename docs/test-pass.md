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
- Wheel Stutter: whether the stutter catches at once when the wheel passes halfway, to measure through the MCP server
  (it needs a tool that moves a controller such as the mod wheel).

- With the MiniLab driver switched off (it was, since 12:16 in FL Studio) and the MiniLab left in its DAW program, the
  Arturia-program setup does not fit: notes and pads work, knobs and faders do nothing. CoolSynth should notice
  which program the MiniLab is in and take the matching setup, or say so.
- After unplugging and plugging the MiniLab back in, the lights were all white: the colours are not sent again.
- Slices by attacks: Sensitivity brings in every attack of the test loop within the knob's first 42 %, and above
  that nothing changes; to the ear it is on or off. It should spread over the whole knob (by number of slices, or
  in decibels).
- The attack detector found 7 of the loop's 8 hits: it ranks the hats strongest (0.97, 1.0), the kicks weakest
  (0.34, 0.50), and missed the kick at 0.75 s.
- Choosing a sample by turning one knob through the whole list is slow, and clicking the lit value should set it
  too (TODO 26).
- The value display is too small for a file's name: "CoolSynth test kit" and "CoolSynth test kit/10 clap.wav"
  look the same, as only the first characters show. The list holds the kit folder and then each file inside it, so
  you chose the clap file instead of the kit without seeing it.
- That one file seemed not to play: replayed from the app's saved state it plays at -79 dB, as Start was at 21 % and
  the test clap has nearly all its sound in its first 50 ms. Not an engine fault; the names are now short (CST1, CST2).
- Your own samples play very quietly: a file put into a preset takes that preset's level (Basic Saw's layer is at
  -11 dB, as a saw is loud), and files are not levelled; the loop peaks at -24 dB.
- In Home the big knob browses the sounds: looking for Mono, Rain Chime, Prophet Brass and Juno Pad loaded one
  after the other, and the edited kit sound was gone without a question. An edited sound should not be lost that
  easily (a warning, or keep the edit until another is chosen with a press).
- Finding Mono took a long time: the layer's play mode is Home's Voice page, knob 1, which nothing points to.
- The flight recorder logs notes and presets but not knob changes, so a note cannot be matched to the settings it
  played with. To add.
- Not known yet: whether the app told you the MiniLab was in use by another program.

## Then the release (TODO J3)

- The version number is your choice; the release notes go in `docs/releases/`; then the tag.

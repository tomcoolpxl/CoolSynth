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

## Then the release (TODO J3)

- The version number is your choice; the release notes go in `docs/releases/`; then the tag.

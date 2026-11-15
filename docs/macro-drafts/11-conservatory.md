# Macro drafts: 11-conservatory.csbank

What every preset in this bank has to work with (read from the bank and the code):

- One generator, `g1 = Sample` playing a catalogue instrument; `p1 = Off`, and P2 and P3 are Off by
  default. There is no processor anywhere, so there is no cutoff, resonance or drive to move.
- The Sample slot's modulatable parameters are `start`, `lstart`, `lend` and `xfade`
  (SampleGenerator.h). For a catalogue instrument only `start` is read (`renderStyle`, once per
  note); the loop points and loop fade are the instrument's own (`zone->loopStart/loopEnd/loopFade`).
  `g1.amount` does nothing here (it is the ring/AM share from a previous generator, and G1 has none).
- So **Tone is `(none)` in all 40 presets**: there is no filter, the Sample slot has no
  brightness-like parameter, level is not tone, and a filter slot cannot be added without changing
  the sound at macro 0. `start` was considered and rejected: it is a share of each recording's
  length (a Steinway note is about 28 s, so 1 % is about 280 ms, a violin-section note about 7.5 s),
  so the same amount skips a very different time on every key and velocity, and it cuts into the
  recording with only the 2 ms attack as a fade.
- Character is a per-note detune (`src=random`, Note Random, -1..1 per note, times 48 semitones):
  chords beat gently, like a slightly out-of-tune instrument or a loose section.
- Time is `env1.time` (env1 is the amplifier; one line scales attack and release by
  2^(amount * 4), read at each note start). It cannot move the release alone, so the 2 ms attack
  grows or shrinks with it; the amounts below keep the attack between about 0.2 ms and 16 ms
  (32 ms on the Harmonica).
  Ringing instruments (release 15-20 s) get a negative amount, Damp; the others a positive one.
- Movement: LFO 1 is at its defaults (sine, 2 Hz, retrigger on), so a vibrato or tremolo also
  routes `macro4` to `lfo1.rate` to bring the rate up with the depth. Autopan uses GLFO 1 at its
  defaults (sine, synced, 1/4 note, free-running), so every voice pans together.
- No preset has a part B or any existing `mod =` line: at most 5 slots used per preset.

### Steinway Grand (11-conservatory.csbank)
names: (none), Detune, Pedal, Sway
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.6
mod = src=glfo1 dst=pan amount=0.3 via=macro4
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each note a little off pitch (up to about 14 cents), so chords beat like a piano due a tuning.
- Pedal: notes go on ringing after you let go, as with the sustain pedal (release 0.4 s to about 2.1 s; the hammer softens slightly, attack 2 to 11 ms).
- Sway: the piano drifts gently left and right in time with the beat (quarter notes).

### Yamaha Upright (11-conservatory.csbank)
names: (none), Honky, Pedal, Sway
mod = src=random dst=pitch amount=0.004 via=macro2
mod = src=macro3 dst=env1.time amount=0.6
mod = src=glfo1 dst=pan amount=0.3 via=macro4
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Honky: each note off pitch by up to about 19 cents, towards a bar-room upright.
- Pedal: notes ring on after you let go (release 0.5 s to about 2.6 s; attack 2 to 11 ms).
- Sway: the piano drifts gently left and right in time with the beat.

### Old Upright (11-conservatory.csbank)
names: (none), Honky, Pedal, Sway
mod = src=random dst=pitch amount=0.006 via=macro2
mod = src=macro3 dst=env1.time amount=0.6
mod = src=glfo1 dst=pan amount=0.3 via=macro4
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Honky: each note off pitch by up to about 29 cents: a properly neglected honky-tonk.
- Pedal: notes ring on after you let go (release 1 s to about 5.3 s; attack 2 to 11 ms).
- Sway: the piano drifts gently left and right in time with the beat.

### Flemish Harpsichord (11-conservatory.csbank)
names: (none), Detune, Ring, Sway
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.5
mod = src=glfo1 dst=pan amount=0.3 via=macro4
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each note a little off pitch (up to about 14 cents), like an unevenly tuned harpsichord.
- Ring: plucked strings keep ringing after you let go instead of being damped (release 0.4 s to 1.6 s; attack 2 to 8 ms).
- Sway: the harpsichord drifts gently left and right in time with the beat.

### Italian Harpsichord (11-conservatory.csbank)
names: (none), Detune, Ring, Sway
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.5
mod = src=glfo1 dst=pan amount=0.3 via=macro4
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each note a little off pitch (up to about 14 cents).
- Ring: strings keep ringing after you let go (release 0.4 s to 1.6 s; attack 2 to 8 ms).
- Sway: the harpsichord drifts gently left and right in time with the beat.

### FM Keys (11-conservatory.csbank)
names: (none), Detune, Ring, Autopan
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.6
mod = src=glfo1 dst=pan amount=0.5 via=macro4
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each note a little off pitch (up to about 14 cents), a looser, chorus-like chord.
- Ring: notes fade after you let go instead of stopping (release 0.1 s to about 0.5 s; attack 2 to 11 ms).
- Autopan: the classic electric-piano left-right pan, in time with the beat.

### Violins (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents), a less disciplined section.
- Linger: softer starts and notes that fade slowly after you let go (release 0.8 s to 6.4 s; attack 2 to 16 ms).
- Vibrato: adds vibrato on top of the recorded one, deeper and faster as you turn it (up to about 14 cents at 5.4 Hz).

### Violas (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 1 s to 8 s; attack 2 to 16 ms).
- Vibrato: more vibrato, deeper and faster as you turn it (up to about 14 cents at 5.4 Hz).

### Cellos (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.8 s to 6.4 s; attack 2 to 16 ms).
- Vibrato: more vibrato, deeper and faster as you turn it (up to about 14 cents at 5.4 Hz).

### Double Bass (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.8 s to 6.4 s; attack 2 to 16 ms).
- Vibrato: more vibrato (on the Straight style, vibrato where there was none), up to about 14 cents at 5.4 Hz.

### Solo Violin (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.8 s to 6.4 s; attack 2 to 16 ms).
- Vibrato: more vibrato, deeper and faster as you turn it (up to about 14 cents at 5.4 Hz).

### Folk Harp (11-conservatory.csbank)
names: (none), Detune, Damp, Sway
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=glfo1 dst=pan amount=0.3 via=macro4
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each string a little off pitch (up to about 14 cents).
- Damp: letting go of a key damps the string, as a harpist's hand does (release 20 s to about 1.7 s; held notes ring as before).
- Sway: the harp drifts gently left and right in time with the beat.

### Dan Tranh (11-conservatory.csbank)
names: (none), Detune, Ring, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.6
mod = src=lfo1 dst=pitch amount=0.006 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.3
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each string a little off pitch (up to about 14 cents).
- Ring: strings keep ringing after you let go (release 0.3 s to 1.6 s; the pluck softens slightly, attack 2 to 11 ms).
- Vibrato: the wide pressed-string vibrato of the zither, up to about 29 cents at 4.6 Hz.

### Kalimba (11-conservatory.csbank)
names: (none), Detune, Damp, Flutter
mod = src=random dst=pitch amount=0.004 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=vca amount=-0.4 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.3
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each tine a little off pitch (up to about 19 cents), like a hand-tuned kalimba.
- Damp: letting go of a key stops the tine (release 15 s to about 1.2 s; held notes ring as before).
- Flutter: a pulsing like a hand fanning over the sound hole (dips of up to -4.4 dB, up to 4.6 Hz).

### Horn (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.004 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and notes that fade slowly after you let go (release 0.7 s to 5.6 s; attack 2 to 16 ms).
- Vibrato: a horn vibrato, deeper and faster as you turn it (up to about 19 cents at 5.4 Hz).

### Trumpet (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.005 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.5 s to 4 s; attack 2 to 16 ms).
- Vibrato: a trumpet vibrato, deeper and faster as you turn it (up to about 24 cents at 5.4 Hz).

### Tuba (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.004 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.6 s to 4.8 s; attack 2 to 16 ms).
- Vibrato: a tuba vibrato, deeper and faster as you turn it (up to about 19 cents at 5.4 Hz).

### Trombone (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.005 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.6 s to 4.8 s; attack 2 to 16 ms).
- Vibrato: a trombone vibrato, deeper and faster as you turn it (up to about 24 cents at 5.4 Hz).

### Old Trombone (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.005 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.6 s to 4.8 s; attack 2 to 16 ms).
- Vibrato: a trombone vibrato, deeper and faster as you turn it (up to about 24 cents at 5.4 Hz).

### Flute (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and notes that fade slowly after you let go (release 0.7 s to 5.6 s; attack 2 to 16 ms).
- Vibrato: more vibrato on top of the recorded one (up to about 14 cents at 5.4 Hz).

### Clarinet (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.7 s to 5.6 s; attack 2 to 16 ms).
- Vibrato: a light vibrato on a straight-toned clarinet (up to about 14 cents at 5.4 Hz).

### Bassoon (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.7 s to 5.6 s; attack 2 to 16 ms).
- Vibrato: more vibrato on top of the recorded one (up to about 14 cents at 5.4 Hz).

### Saxello (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.5 s to 4 s; attack 2 to 16 ms).
- Vibrato: more vibrato on top of the recorded one (up to about 14 cents at 5.4 Hz).

### Tenor Sax (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.006 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and long fades after you let go (release 0.5 s to 4 s; attack 2 to 16 ms).
- Vibrato: a wide jazz-ballad vibrato on the straight tone (up to about 29 cents at 5.4 Hz).

### Recorders (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.004 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents), a recorder consort slightly adrift.
- Linger: softer starts and notes that fade after you let go (release 0.3 s to 2.4 s; attack 2 to 16 ms).
- Vibrato: a breath vibrato, deeper and faster as you turn it (up to about 19 cents at 5.4 Hz).

### Ocarina (11-conservatory.csbank)
names: (none), Loose, Linger, Vibrato
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.004 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each note's intonation a little off (up to about 14 cents).
- Linger: softer starts and notes that fade after you let go (release 0.3 s to 2.4 s; attack 2 to 16 ms).
- Vibrato: a breath vibrato, deeper and faster as you turn it (up to about 19 cents at 5.4 Hz).

### Harmonica (11-conservatory.csbank)
names: (none), Loose, Linger, Flutter
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=1
mod = src=lfo1 dst=vca amount=-0.4 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.3
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Loose: each reed a little off pitch (up to about 14 cents).
- Linger: softer starts and notes that fade after you let go (release 0.1 s to 1.6 s; attack 2 to 32 ms).
- Flutter: the hand-cupping tremolo of a harmonica player (dips of up to -4.4 dB, up to 4.6 Hz).

### Didgeridoo (11-conservatory.csbank)
names: (none), Detune, Linger, Pulse
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=vca amount=-0.5 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.25
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each note a little off pitch (up to about 14 cents), so layered drones beat against each other.
- Linger: softer starts and a long fade after you let go (release 0.6 s to 4.8 s; attack 2 to 16 ms).
- Pulse: a rhythmic breath pulse through the drone (dips of up to -6 dB, up to 4 Hz).

### Pipe Organ (11-conservatory.csbank)
names: (none), Detune, Linger, Tremulant
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=vca amount=-0.3 curve=unipolar via=macro4
mod = src=lfo1 dst=pitch amount=0.001 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.42
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each pipe a little off pitch (up to about 14 cents), so chords shimmer like an organ out of tune.
- Linger: notes fade after you let go instead of stopping with the wind (release 0.5 s to 4 s; attack 2 to 16 ms).
- Tremulant: the organ's tremulant stop, a wavering of level and a little pitch (dips of up to -3.1 dB and 5 cents, up to 6.4 Hz).

### Renaissance Organ (11-conservatory.csbank)
names: (none), Detune, Linger, Tremulant
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=vca amount=-0.3 curve=unipolar via=macro4
mod = src=lfo1 dst=pitch amount=0.001 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.42
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each pipe a little off pitch (up to about 14 cents).
- Linger: notes fade after you let go (release 0.3 s to 2.4 s; attack 2 to 16 ms).
- Tremulant: the tremulant stop, a wavering of level and a little pitch (dips of up to -3.1 dB and 5 cents, up to 6.4 Hz).

### Marimba (11-conservatory.csbank)
names: (none), Detune, Damp, Roll
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=vca amount=-0.6 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.5
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each bar a little off pitch (up to about 14 cents).
- Damp: letting go of a key stops the bar (release 15 s to about 1.2 s; held notes ring as before).
- Roll: a fast pulsing through each note, towards a mallet roll (dips of up to -8 dB, up to 8 Hz).

### Vibraphone (11-conservatory.csbank)
names: (none), Detune, Damp, Motor
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=vca amount=-0.5 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.3
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each bar a little off pitch (up to about 14 cents).
- Damp: the damper pedal: letting go of a key stops the bar (release 15 s to about 1.2 s; held notes ring as before).
- Motor: the vibraphone's spinning-fan tremolo, deeper and faster as you turn it (dips of up to -6 dB, up to 4.6 Hz).

### Xylophone (11-conservatory.csbank)
names: (none), Detune, Damp, Roll
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=vca amount=-0.6 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.5
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each bar a little off pitch (up to about 14 cents).
- Damp: letting go of a key stops the bar (release 15 s to about 1.2 s; held notes ring as before).
- Roll: a fast pulsing through each note, towards a mallet roll (dips of up to -8 dB, up to 8 Hz).

### Balafon (11-conservatory.csbank)
names: (none), Detune, Damp, Roll
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=vca amount=-0.6 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.5
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each bar a little off pitch (up to about 14 cents).
- Damp: letting go of a key stops the bar (release 20 s to about 1.7 s; held notes ring as before).
- Roll: a fast pulsing through each note, towards a mallet roll (dips of up to -8 dB, up to 8 Hz).

### Glockenspiel (11-conservatory.csbank)
names: (none), Detune, Damp, Shimmer
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=pitch amount=0.002 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each bar a little off pitch (up to about 14 cents).
- Damp: letting go of a key stops the bar (release 15 s to about 1.2 s; held notes ring as before).
- Shimmer: a light vibrato that makes the ringing shimmer (up to about 10 cents at 5.4 Hz).

### Tubular Bells (11-conservatory.csbank)
names: (none), Detune, Damp, Shimmer
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=pitch amount=0.002 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each bell a little off pitch (up to about 14 cents), so chords beat like old bells.
- Damp: letting go of a key damps the bell (release 20 s to about 1.7 s; held notes ring as before).
- Shimmer: a light vibrato that makes the ringing shimmer (up to about 10 cents at 5.4 Hz).

### Tubular Glock (11-conservatory.csbank)
names: (none), Detune, Damp, Shimmer
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=pitch amount=0.002 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each tube a little off pitch (up to about 14 cents).
- Damp: letting go of a key damps the tube (release 20 s to about 1.7 s; held notes ring as before).
- Shimmer: a light vibrato that makes the ringing shimmer (up to about 10 cents at 5.4 Hz).

### Hand Chimes (11-conservatory.csbank)
names: (none), Detune, Damp, Shimmer
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=pitch amount=0.002 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each chime a little off pitch (up to about 14 cents).
- Damp: letting go of a key damps the chime against the shoulder, as ringers do (release 20 s to about 1.7 s; held notes ring as before).
- Shimmer: a light vibrato that makes the ringing shimmer (up to about 10 cents at 5.4 Hz).

### Wine Glasses (11-conservatory.csbank)
names: (none), Detune, Linger, Waver
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=0.75
mod = src=lfo1 dst=pitch amount=0.003 via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.36
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each glass a little off pitch (up to about 14 cents), so chords beat like a real glass harp.
- Linger: softer starts and notes that fade slowly after you let go (release 0.5 s to 4 s; attack 2 to 16 ms).
- Waver: a wavering pitch, the glass-harp wobble (up to about 14 cents at 5.4 Hz).

### Timpani (11-conservatory.csbank)
names: (none), Detune, Damp, Roll
mod = src=random dst=pitch amount=0.003 via=macro2
mod = src=macro3 dst=env1.time amount=-0.9
mod = src=lfo1 dst=vca amount=-0.6 curve=unipolar via=macro4
mod = src=macro4 dst=lfo1.rate amount=0.5
- (none): no filter, and the Sample slot has nothing that changes brightness.
- Detune: each drum a little off pitch (up to about 14 cents).
- Damp: letting go of a key muffles the head, as a timpanist's hand does (release 20 s to about 1.7 s; held notes ring as before).
- Roll: a fast pulsing through each stroke, towards a timpani roll (dips of up to -8 dB, up to 8 Hz).

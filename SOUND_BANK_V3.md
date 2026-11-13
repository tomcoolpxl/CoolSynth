# SOUND_BANK_V3.md

# CoolSynth factory bank V3: "Places"

Status: design, 2026-11-12. Nothing in this file is authored into YAML yet. The bank is written
against the engine we want, not the engine we have; every preset that needs something the V2
engine cannot do is a placeholder and says so. The module tags in the "Needs" columns are the
input to `REQUIREMENTS_V3.md`, which turns them into engine requirements.

The current bank (15 hand-written presets plus 57 YAML presets in `src/presets/FactoryPresets.cpp`)
is to be deleted in full when the first V3 rooms land. It was voiced for the old 2-pole filter and
its names, categories and sound choices were rejected.

## 1. Rules for the bank

1. Every room is a place, and every room must be recognisable within five seconds of playing any
   preset in it. The "signature" line of each room is the test: attack, timbre, motion, space.
2. Rooms do not overlap on purpose. Where two rooms could share a sound (strings in Outrun and in
   Studio 1982, bass everywhere) the room decides the treatment, and the rule for that is written
   next to the room.
3. Six presets per room (Main Stage has seven), plus a Workshop of init templates: 68 presets.
   Fewer, distinct presets beat more, similar ones.
4. Each preset lists: what you hear, the recipe in modules, what the engine still needs for it
   ("Needs"), and a status:
   - **Now**: buildable in the V2 engine as intended.
   - **Now~**: buildable in V2 as a recognisable approximation; the Needs column says what
     upgrades it later.
   - **PH**: placeholder; cannot be voiced convincingly until the listed modules exist.
5. Every preset reacts to velocity, the mod wheel does something audible, pitch bend is 2
   semitones unless a preset says otherwise, and levels are normalised with the existing
   `CoolSynthPresetNormalizer` tool after authoring.
6. Names are short, no vintage brand names, no numbers in names except Studio 1982.

## 2. Module tags

The tag is what a preset needs; "have" means the V2 engine has it today. The full requirement per
tag, with counts, is in `REQUIREMENTS_V3.md` section 2.

Generators

| Tag | Meaning | V2 |
| --- | --- | --- |
| VA | Virtual-analogue oscillator (pulse / triangle / saw / sine, PolyBLEP) | have (2 slots) |
| NOISE | Noise generator | have (one colour) |
| SUB | Dedicated sub oscillator one or two octaves under the note | workaround: osc B sine 32' |
| SWARM | Unison inside the oscillator: 2 to 7 detuned copies with spread (supersaw) | no (only whole-voice Unison mode, detune from Vintage) |
| WT | Wavetable oscillator with position morph | no |
| FM2 / FM4 | Two- or four-operator phase-modulation FM with ratio and index envelope | no (poly-mod osc B to pitch is exponential FM, not the same sound) |
| KS | Karplus-Strong plucked string (pick position, damping) | no |
| MODAL | Modal resonator for bars, bells, mallets | no |
| SAMPLE | One-shot or looped sample playback | no |
| GRAIN | Granular cloud from a sample or an oscillator | no |
| CHIP | Chip-style sources: LFSR periodic noise, quantised pulse widths, 4-bit tables | no |

Processors (per voice)

| Tag | Meaning | V2 |
| --- | --- | --- |
| LP4 | Transistor ladder low-pass, four pole | have |
| SVF | Two-pole state-variable multimode: LP / HP / BP / notch | no |
| COMB | Tuned comb filter with feedback | no |
| FORMANT | Vowel formant filter with morph | no |
| FOLD | Wavefolder | no |
| CRUSH | Bit depth and sample-rate reduction | no |
| VDRIVE | Per-voice drive before or after the filter | no (drive is global) |
| RING | Ring modulation between two generators | no |

Modulators

| Tag | Meaning | V2 |
| --- | --- | --- |
| ENV | ADSR envelopes (amp, filter) | have (2, exponential) |
| ENV3 | Third free envelope, curve control, loop mode | no |
| LFO-G | Global LFO, shared phase across voices | have (1, Hz only) |
| LFO-V | Per-voice LFO with key retrigger, fade-in, one-shot | no |
| LFO-SYNC | Tempo-synced LFO rates (1/4, 1/8, 1/8T, 1/16 ...) | no |
| S&H | Sample-and-hold and smoothed-random LFO shapes | no |
| STEP | 16-step modulation sequencer, tempo-synced | no |
| GENV | Global envelope: triggered by first note, runs over bars | no |
| MATRIX | Any-source to any-destination modulation matrix with bipolar amount | no (fixed poly-mod routes) |
| MACRO | Macro controls mapped to several parameters | no |
| AT / MPE | Aftertouch and MPE per-note expression | no |

Note layer and parts

| Tag | Meaning | V2 |
| --- | --- | --- |
| ARP | Arpeggiator with Euclid, chance, ratchet, accent, latch | have |
| CHORDMEM | Chord memory: one key plays a stored chord, feeds the arp | no |
| SEQ | Step sequencer with per-step parameter locks | no |
| LAYER | Two parts (patches) at once: layer or split, own mix | no |
| UNISON-N | Whole-voice unison with a chosen count and detune | no (count fixed to all voices) |

Global FX

| Tag | Meaning | V2 |
| --- | --- | --- |
| DRIVE, CHORUS, PHASER, DELAY, PLATE, COMP | Existing rack | have |
| DELAY-SYNC | Tempo-synced delay times, ping-pong | no |
| SHIMMER | Pitch-shifted (+12) reverb feedback | no |
| PRE-DELAY | Reverb pre-delay and input high-pass | no |
| LOFI | Tape and lo-fi block: wow, flutter, hiss, crackle, bandwidth, crush | no |
| PUMP | Tempo-synced ducker (sidechain feel) | no |
| EQ | Tilt or three-band EQ | no |
| FLANGER | Flanger | no |

## 3. The rooms

### Bedroom (lo-fi, cozy)

Signature: dull top end, slow pitch wobble, hiss and crackle, soft attacks, slow chorus.
Everything sounds like it came off a cassette. Room rule: the lo-fi block is on in every preset.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Cassette Keys | Soft electric-piano keys, wobbling, hissy | VA tri 8' + VA sine 4' at 0.3; LP4 1.8 kHz; amp A5 D600 S0.3 R400; LOFI wow 0.4 Hz flutter 6 Hz hiss; CHORUS 0.3 Hz; PLATE small | LOFI | PH (Now~ with Vintage and a 3-cent pitch LFO) |
| 2 | Blanket Pad | Warm, dark, slow pad | VA saw 8' x2 detuned 8 c; LP4 900 Hz; amp A900 R1500; LFO-G 0.2 Hz to pitch 3 c; PLATE | LOFI later | Now |
| 3 | Dusty Mallet | Soft kalimba-like mallet with crackle | MODAL mallet, decay 400 ms; LOFI crackle | MODAL, LOFI | PH (Now~ sine + tri, fast filter env) |
| 4 | Bedroom Bass | Round, dull bass with a slight wobble | VA tri 16' + SUB sine 32'; LP4 350 Hz; amp D300; mono | LOFI later | Now |
| 5 | Tape Flute | Breathy flute, vibrato on the wheel | VA sine 8' + NOISE 0.25 through LP4 2.5 kHz; LFO-G 5 Hz to pitch on the wheel; PLATE | SVF band-pass for the breath | Now |
| 6 | Vinyl Choir | Hollow vowel pad, dusty | PWM pulse pair; FORMANT slow vowel sweep; LOFI; PLATE | FORMANT, LOFI | PH (Now~ PWM + PHASER) |

### Arcade (chip)

Signature: naked pulse waves, no filter movement, instant on and off, pitch steps and slides,
periodic noise. Thin, fast, cheerful. Room rule: filter open, no reverb, delay at most one slap.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Coin Pulse | 25 % pulse lead, vibrato that fades in | VA pulse 0.25 8'; filter open; amp A1 D0 S1 R20; LFO-V 6 Hz to pitch, fade-in 300 ms; DELAY one slap | LFO-V fade-in | Now~ (vibrato on the wheel) |
| 2 | Boss Bass | 12.5 % pulse bass with octave-jump LFO | VA pulse 0.12 16'; LFO-G square 1/16 to pitch one octave; filter open | LFO-SYNC | Now~ (free Hz) |
| 3 | Power-Up | Bleep with a fast pitch rise | VA pulse 0.5; filter env to pitch +12 st (poly-mod); amp D120 | none | Now |
| 4 | Chip Chord | One key plays a triad arpeggiated at 1/32 | CHORDMEM major triad; ARP up 1/32 gate 1.0; VA pulse 0.5 | CHORDMEM | PH (Now with a played triad and ARP 1/32) |
| 5 | Static Hit | Pitched periodic-noise percussion | CHIP LFSR noise tracking pitch; amp D80, no sustain | CHIP | PH (Now~ white noise + LP4 env) |
| 6 | SCC Lead | 4-bit wavetable lead with stepped pulse width | WT 32-sample 4-bit tables; CRUSH 4 bit | WT, CRUSH | PH |

### Outrun (synthwave, neon)

Signature: stacked saws, chorus, delay and reverb on everything, gated rhythms, bright and wide,
driving. Room rule against Studio 1982: Outrun is produced (wet, stacked, gated); Studio 1982 is
played (dry, single voicing).

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Neon Brass | Big filter-envelope brass stack | VA saw x2 (+7 c); LP4 1.2 kHz env 0.6; amp A15 D400 S0.5; UNISON-N 2; CHORUS; DELAY dotted 1/8; PLATE | DELAY-SYNC, UNISON-N | Now~ |
| 2 | Gated Pad | Pad chopped at 1/16 | saw + pulse pad; LFO-SYNC square 1/16 to amp through MATRIX; PLATE | LFO-SYNC, MATRIX (to amp) | PH (Now~ hold the chord with ARP pattern Chord at 1/16) |
| 3 | Night Drive | Octave-jumping 1/16 bass line | VA saw 16' + pulse; LP4 700 Hz env; ARP up, 2 octaves, 1/16, gate 0.6; DRIVE light | none | Now |
| 4 | Laser Lead | Sync sweep lead with glide | osc A synced to B; filter env to osc B pitch (poly-mod); mono legato glide 60 ms; DELAY | none | Now |
| 5 | Grid Keys | DX-style bright electric piano | FM4, two 2-op pairs, tine ratio 1:14, index envelopes; CHORUS | FM4 | PH |
| 6 | Sunset Strings | Wide detuned string ensemble | VA saw x2; UNISON-N 4 detune 12 c; pan spread; PHASER 0.3 Hz; PLATE | UNISON-N | Now~ (Unison mode + Vintage) |

### Observatory (space, ambient)

Signature: nothing starts or stops quickly, timbre drifts over 10 to 30 seconds, huge bright
reverb tails, sparse sparkles. Room rule: attack never under 500 ms except Satellite Ping.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Nebula Pad | Timbre morphs over 20 s | WT position from LFO-V 0.05 Hz; SVF LP soft; amp A2000 R4000; PLATE large with PRE-DELAY 80 ms | WT, LFO-V, PRE-DELAY | PH (Now~ PWM + LFO to cutoff) |
| 2 | Shimmer Drone | Sustained note with an octave-up haze | VA saw + sine; SHIMMER +12 st feedback 0.6 | SHIMMER | PH |
| 3 | Satellite Ping | Sparse random blips with long echoes | VA sine 2'; amp D150; ARP random, chance 0.35, 1/8; DELAY dotted 1/4 feedback 0.7; PLATE | DELAY-SYNC | Now~ |
| 4 | Solar Wind | Filtered noise swell, no pitch | NOISE to SVF band-pass resonant; cutoff from LFO-G 0.07 Hz; amp A3000; PLATE | SVF | Now~ (LP4 resonance) |
| 5 | Deep Field | Sub-heavy slow pad | VA saw 32' + sine 16'; LP4 400 Hz from LFO 0.05 Hz; amp A2500; PLATE | none | Now |
| 6 | Granular Sky | Frozen cloud from a bowed-glass sample | GRAIN density high, pitch spray 5 c; PLATE | GRAIN, SAMPLE | PH |

### Greenhouse (glass, bells, mallets, strings)

Signature: percussive attacks, inharmonic or sine-rich decays, no sustain, organic. This is the
"glass" room. Room rule: sustain level 0 everywhere.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Glass Bell | FM bell, ratio 3.5, long decay | FM2 ratio 3.5, index env D2000; PLATE | FM2 | PH (Now~ sine + osc B 2' poly-mod to pitch) |
| 2 | Marimba | Wooden mallet, short | MODAL bar; mallet hardness from velocity; D400 | MODAL | PH (Now~ sine + tri, fast env) |
| 3 | Kalimba | Thumb pluck with a click | KS pluck, damping high; NOISE click 5 ms | KS | PH |
| 4 | Rain Chime | Random high sine plucks | VA sine 2' + tri; amp D300; ARP random 1/16 chance 0.5 over 3 octaves; DELAY; PLATE | none | Now |
| 5 | Crystal Pluck | Bright saw pluck | VA saw + pulse 4'; LP4 env fast D180 res 0.3, key tracking full; PLATE | none | Now |
| 6 | Harp String | Plucked string, bright pick | KS pick position 0.2; release damps | KS | PH |

### Basement (dark industrial, techno)

Signature: distortion and resonance, low, menacing, metallic; sweeps that scream. Nothing pretty.
Room rule: drive or fold on every preset.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Acid | Resonant squelch with accent and slide | VA saw 16'; LP4 res 0.75 env 0.8; amp D200; mono glide 40 ms; velocity to filter for accent; DRIVE | none | Now |
| 2 | Rust Bass | Folded, overdriven mono bass | VA saw + pulse 16'; FOLD per voice from env; LP4 | FOLD or VDRIVE | PH (Now~ global DRIVE) |
| 3 | Metal Hit | Ring-modulated metallic stab | VA saw RING VA at ratio 2.76; amp D250; PLATE short | RING | PH (Now~ poly-mod B to pitch) |
| 4 | Furnace Drone | Low detuned drone with comb movement | VA saw x2 32' -9 c; COMB from slow LFO; NOISE low | COMB | Now~ (LP4 with LFO) |
| 5 | Concrete Pad | Dark crushed pad with no bass | VA saw x2; SVF HP 200 Hz then LP4 1 kHz; CRUSH 12 bit 22 kHz; LOFI | SVF, CRUSH | PH (Now~ dark LP4 pad) |
| 6 | Siren | Self-oscillating filter sweep with echoes | LP4 res 1.0 self-oscillating, oscillators off, starts from the ladder's own noise floor; cutoff from LFO-G 0.1 Hz saw; DELAY feedback 0.8 | none | Now |

### Studio 1982 (retrosynth classics)

Signature: clean Prophet, Juno and Oberheim-style voicings, audible filter envelopes, light chorus,
dry. Played, not produced. Room rule: at most one global effect per preset.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Juno Pad | PWM pad with chorus | VA pulse, PWM from LFO 0.5 Hz depth 0.3, + saw; LP4 1.5 kHz; amp A400; CHORUS | none | Now |
| 2 | Prophet Brass | Two saws, filter envelope | VA saw x2 +5 c; LP4 900 Hz env 0.7; amp A20 D350 S0.6 | none | Now |
| 3 | OB Strings | Detuned saws, slow, phaser | VA saw x2 -7 c; amp A600 R900; PHASER 0.3 Hz; pan spread | none | Now |
| 4 | Moog Bass | Legato mono bass with sub | VA saw 16' + saw 8' + SUB sine 32'; LP4 500 Hz env 0.5; mono legato glide 30 ms | third generator slot for the true three-oscillator version | Now (osc B as sub) |
| 5 | Clav Keys | Sharp pulse clav | VA pulse 0.3; LP4 2.2 kHz env fast D90; key tracking full; velocity to amp and filter | none | Now |
| 6 | Sync Lead | Hard-sync sweep lead | osc A synced to B, B +7 st; filter env to B pitch (poly-mod); mono | none | Now |

### Main Stage (modern)

Signature: wide unison, bright and digital, the timbre moves inside the note, tempo-locked pumping
and stutter. Louder and more processed than Outrun. Section 4 says what "modern" means here.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Supersaw | Seven-voice detuned saw wall | SWARM 7 saw detune 25 c spread 1.0; SVF HP 120 Hz; amp A10 R300; PLATE large; LAYER a sub part under it | SWARM, SVF, LAYER | PH (Now~ Unison mode + pan spread) |
| 2 | Future Chord | Wide chord stab that pumps | CHORDMEM min7; SWARM 4; WT soft table; PUMP 1/4 depth 0.6; CHORUS | CHORDMEM, SWARM, WT, PUMP | PH |
| 3 | Neuro Growl | Talking FM and wavetable bass | WT + FM2; FORMANT vowel from LFO-SYNC 1/8; VDRIVE; SVF | WT, FM2, FORMANT, LFO-SYNC, VDRIVE | PH |
| 4 | Reese | Two slowly beating saws | VA saw x2 16' +12 c; LP4 800 Hz; DRIVE light; mono | none | Now |
| 5 | Hyper Lead | Stepped-pitch glitch lead | VA saw + SWARM 3; S&H LFO-SYNC 1/16 to pitch quantised to semitones; CRUSH; DELAY sync | S&H, LFO-SYNC, CRUSH, MATRIX | PH (Now~ square LFO to pitch) |
| 6 | Pluck Drop | Pluck with a pitch-drop transient | VA saw + pulse; env to pitch -12 st D40 (poly-mod); LP4 env; DELAY 1/8 sync; PLATE | DELAY-SYNC | Now~ |
| 7 | Riser | Eight-bar noise and pitch rise | NOISE + VA saw; GENV 8 bars tempo-locked to pitch and cutoff; PLATE | GENV, LFO-SYNC | PH |

### Engine Room (bass)

Signature: mono or legato, tight, low; one of each bass family. Room rule: no reverb, no pads;
other rooms keep at most one bass each.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Pure Sub | Sine sub | VA sine 32'; amp A2 D80 S1 R60; mono | none | Now |
| 2 | 808 Boom | Pitch-drop boom with a long decay | VA sine 32'; env to pitch -24 st D60 (poly-mod); amp D1200; DRIVE | none | Now |
| 3 | FM Knock | FM bass with a click transient | FM2 ratio 1:1, index env D30; LP4 | FM2 | PH (Now~ poly-mod B to pitch) |
| 4 | Wobble | Tempo-synced filter wobble | VA saw x2 16'; LP4 res 0.5 from LFO-SYNC 1/8 triangle depth 0.6 | LFO-SYNC | Now~ (free Hz) |
| 5 | Donk | Resonant short pluck bass | VA pulse 16'; LP4 res 0.7 env 0.9 D120 | none | Now |
| 6 | Fat Analog | Three detuned saws over a sub | SWARM 3 saw 16' detune 10 c + SUB sine | SWARM, SUB | Now~ (Unison mode + Vintage) |

### Clock Tower (motion)

Signature: rhythm from the patch itself: arps, Euclid, ratchets, step-modulated filters. One held
note moves. Room rule: every preset is tempo-locked.

| # | Name | What you hear | Recipe | Needs | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Euclid Pulse | 5-in-8 Euclidean pluck | VA pulse; LP4 env D150; ARP Euclid 5/8 at 1/16 | none | Now |
| 2 | Ratchet Run | Ratchets and accents | VA saw; ARP up 1/16, ratchet x2 chance 0.3, accent every 4 | none | Now |
| 3 | Step Filter | 16-step cutoff sequence on a held chord | VA saw x2; LP4 from STEP 16 steps at 1/16 | STEP | PH (Now~ square LFO) |
| 4 | Random Walk | Random-walk melody | VA tri + saw; ARP random walk, 2 octaves, chance 0.8; DELAY | none | Now |
| 5 | Polyrhythm Pad | 3:2 LFO rates on pulse width and cutoff | VA pulse + saw pad; LFO-V 1/4 to PW; LFO-G 1/4T to cutoff | LFO-V, LFO-SYNC | PH (Now~ one LFO) |
| 6 | Chord Trigger | One key plays an arpeggiated chord | CHORDMEM sus2; ARP up/down 1/8 over 2 octaves | CHORDMEM | PH |

### Workshop (templates)

Init patches: one generator, filter open, short envelopes, no effects. These are starting points,
not sounds.

| # | Name | Needs | Status |
| --- | --- | --- | --- |
| 1 | Init Saw | none | Now |
| 2 | Init Pulse | none | Now |
| 3 | Init Sine | none | Now |
| 4 | Init Noise | none | Now |
| 5 | Init Arp | none | Now |
| 6 | Init FM | FM2 | PH |
| 7 | Init Wavetable | WT | PH |

## 4. What "modern synth sounds" means in this bank

The Main Stage room is the answer to "add modern synth sounds". Modern here is the vocabulary of
the current software synths (Serum 2, Vital, Pigments, Phase Plant) and the bass-music, future-bass
and hyperpop production built on them:

- **Unison width**: many detuned copies of a saw inside one note (supersaw), spread across the
  stereo field, with the low end high-passed and a sub layered underneath.
- **Timbre that moves inside the note**: wavetable position sweeps, FM index envelopes, formant
  vowels, all driven by tempo-synced LFOs, step sequencers and envelopes with shaped curves.
- **Tempo-locked dynamics**: pumping (sidechain-style ducking on the beat), gated and stuttered
  amplitude, delays in note values.
- **Digital edge**: bit-crushing, hard-tuned stepped pitch (hyperpop), resampled distortion.
- **Big spaces as part of the sound**: long reverbs and shimmer under short notes.

Serum 2 ships Wavetable, Multisample, Sample, Granular and Spectral oscillators; Vital's browser
categories are Bass, Lead, Keys, Pad, Percussion, Sequence, Experiment, SFX and Template. The
small bespoke machines point the same way with a different accent: MiniFreak (22 oscillator
algorithms including the Plaits models, SuperWave, Karplus-Strong, formant and speech, a tri-mode
analogue filter, two LFOs with a drawable shape, cycling envelope, mod matrix, macros), OP-1
field (Cluster, Digital, DNA, DR Wave, D Synth, FM, Phase, Pulse, Sampler, String, Voltage,
Dimension, Vocoder), Roland SH-4d (SH-4d, SH-3d, Sync, SH-101, JUNO-106, Cross FM, Ring,
Wavetable, Chord, Drawing, PCM models with the filter, LFO and amp kept when the model changes),
Korg NTS-1 mk2 (VPM, noise and user oscillators, six filter types, three LFOs, serial mod, delay
and reverb) and Dirtywave M8 (Braids-derived Macrosynth, FM, physical modelling, sampler).
Mutable Instruments Plaits is the reference catalogue for "one engine per sound family": virtual
analogue, waveshaper, 2-op FM, formant, additive, wavetable, chord, speech, grain swarm, filtered
noise, dust particles, string, modal, and three drum models.

The pattern across all of them: a fixed voice layout whose oscillator slots accept different
engines, a filter and modulation section that stays put when the engine changes, and a small
rack of serial effects. That is the direction `REQUIREMENTS_V3.md` takes.

## 5. Counts

| Room | Presets | Now or Now~ | Placeholder |
| --- | --- | --- | --- |
| Bedroom | 6 | 3 | 3 |
| Arcade | 6 | 3 | 3 |
| Outrun | 6 | 4 | 2 |
| Observatory | 6 | 3 | 3 |
| Greenhouse | 6 | 2 | 4 |
| Basement | 6 | 3 | 3 |
| Studio 1982 | 6 | 6 | 0 |
| Main Stage | 7 | 2 | 5 |
| Engine Room | 6 | 5 | 1 |
| Clock Tower | 6 | 3 | 3 |
| Workshop | 7 | 5 | 2 |
| **Total** | **68** | **39** | **29** |

Authoring order once the engine direction is decided: Workshop, Studio 1982 and Engine Room first
(nearly everything is Now), then each room as its modules land. Placeholders are not shipped as
half-sounds; a room ships when its signature can be heard.

## 6. Sources

- Vital browser categories: https://forum.vital.audio/t/categories-for-arps-brass-etc/6362
- Serum 2 engines: https://www.xferrecords.com/products/serum-2
- Pigments sound design overview: https://www.arturia.com/products/software-instruments/pigments/sound-design
- Hyperpop and modern bass sound design: https://splice.com/blog/tag/hyperpop/ and
  https://www.digitalrenaissance.education/magazine/sound-design-best-vst-like-serum-2-vital-phaseplant-and-others
- Mutable Instruments Plaits manual: https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/
- Arturia MiniFreak: https://www.soundonsound.com/reviews/arturia-minifreak and
  https://www.arturia.com/products/hardware-synths/minifreak/details
- Teenage Engineering OP-1 / OP-1 field engines: https://www.synthtopia.com/content/2023/02/10/teenage-engineering-op-1-op-1-field-synth-engines-compared/
- Roland SH-4d oscillator models: https://static.roland.com/manuals/sh-4d_v200/en-US/8648833186192395.html and
  https://musicradar.com/news/roland-sh-4d-guide
- Korg NTS-1 mk2: https://www.korg.com/uk/products/synthesizers/nts_1_mk2/
- Dirtywave M8: https://www.soundonsound.com/reviews/dirtywave-m8-model02

## 7. Shipped so far

The statuses in the room tables were written against the V2 engine; this table is the current
state on the V3 engine. What is in the bank files today (`resources/presets`, one file per room,
loudness-normalised):

| Room | Shipped | Waiting for |
| --- | --- | --- |
| Workshop | Init Saw, Init Pulse, Init Sine, Init Arp, Init Noise, Init FM, Init Wavetable | none |
| Basement | Acid, Rust Bass, Metal Hit, Furnace Drone, Concrete Pad, Siren | none |
| Studio 1982 | Juno Pad (two layers), Prophet Brass, OB Strings, Moog Bass, Clav Keys, Sync Lead | none |
| Engine Room | 808 Boom, Wobble, FM Knock, Pure Sub, Donk, Fat Analog | none |
| Outrun | Neon Brass, Gated Pad, Night Drive, Laser Lead, Grid Keys, Sunset Strings | none |
| Bedroom | Cassette Keys, Blanket Pad, Dusty Mallet, Bedroom Bass, Tape Flute, Vinyl Choir | none |
| Arcade | Coin Pulse, Boss Bass, Power-Up, Chip Chord, Static Hit, SCC Lead | none |
| Greenhouse | Glass Bell, Marimba, Kalimba, Rain Chime, Crystal Pluck, Harp String | none |
| Observatory | Solar Wind, Nebula Pad, Shimmer Drone, Satellite Ping, Deep Field (two layers), Granular Sky | none |
| Clock Tower | Euclid Pulse, Ratchet Run, Step Filter, Random Walk, Polyrhythm Pad, Chord Trigger | none |
| Main Stage | Reese, Supersaw (with its sub part), Future Chord, Neuro Growl, Riser, Pluck Drop, Hyper Lead | none |
| Conservatory | 40 recorded instruments in seven families, one plain preset each, playing styles as a switch (decisions D96 to D100). Keys: Steinway Grand, Yamaha Upright, Old Upright, Flemish Harpsichord, Italian Harpsichord, FM Keys. Strings: Violins, Violas, Cellos, Double Bass, Solo Violin. Plucked: Folk Harp, Dan Tranh, Kalimba. Brass: Horn, Trumpet, Tuba, Trombone, Old Trombone. Winds: Flute, Clarinet, Bassoon, Saxello, Tenor Sax, Recorders, Ocarina, Harmonica, Didgeridoo. Organs: Pipe Organ, Renaissance Organ. Mallets and Bells: Marimba, Vibraphone, Xylophone, Balafon, Glockenspiel, Tubular Bells, Tubular Glock, Hand Chimes, Wine Glasses, Timpani | the user's ear (demonstrations on the audition page) |

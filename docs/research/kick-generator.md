# A kick generator

Research for TODO 30 and D229: a dedicated kick drum generator, a new generator slot type, that must also make a convincing Succession theme kick.
Researched online on 2026-11-20; every source is linked where it is used and listed at the end.

## How the TR-808 bass drum works

The 808's kick has no oscillator and no envelope on its tone: a trigger pulse rings a resonant filter, and the ring is the drum.
Werner, Abel and Smith split the circuit into blocks and modelled each one (DAFx 2014):
- Trigger logic: the CPU's 1 ms pulse, at the accent voltage, 4 to 14 V depending on the accent knob.
- Pulse shaper: a low shelf with a diode, so the pulse's rising edge passes at full height and its falling edge is clipped to about a diode drop.
- Bridged-T network: a band-pass in an op-amp's feedback, centred near 49.5 Hz in their analysis (Roland's service chart says 56 Hz). Its ringing is the drum.
- Feedback buffer: the Decay knob (VR6) sets how much of the output returns to the bridged-T; more feedback, longer ring, and the paper's circuit bends take it to many seconds.
- Tone, level and output: a passive low-pass (Tone), a divider (Level) and a high-pass.

Two pitch effects, which the paper says are often confused:
- The attack: for about 6 ms an envelope generator lowers the bridged-T's resistance, which raises its centre frequency by more than an octave and changes its Q. Too short to hear as a pitch, it makes the attack "punchier" and "crisper".
- The sigh: a retriggering pulse leaks into the bridged-T through R161, and a transistor's nonlinear current shifts the frequency with the signal's level. The paper's measured pitch starts near 57 Hz and settles near 49.5 Hz over about 250 ms, about 2.4 semitones.

Other behaviour worth keeping:
- Accent is not just louder: a higher trigger voltage changes the attack and the sigh.
- No "machine gun": a new hit lands on a resonator still ringing, so repeated hits differ slightly.
- The model is bilinear-transform biquads in transposed direct form II, one unit delay to break the feedback loop, and a few memoryless nonlinearities.

Follow-ups:
- Werner's 2016 thesis rebuilt each subcircuit with wave digital filters, including the nonlinear bridged-T, and reports an almost exact match to the circuit.
- Gordon Reid's Synth Secrets: the decay is positive feedback, too much rings forever, and long decays drift flat.
- Deruty (2025): the 808's character holds only over a narrow pitch range, so some producers move the song's key to the 808.

Why 808 bass is tuned:
- With a long decay the kick is the bass line, so it must sit on the key's notes; guides put it between about 30 and 60 Hz, F1 and G1 being common.
- The stock 808 had no pitch knob (56 Hz on internal trimmers), so producers sampled one note and played it across the keyboard: trap's "808".

## How the TR-909 kick differs

The 909 builds the kick like a small synthesizer, with separate circuits for the attack and the body, as Roland's engineers put it.
- Body: a VCO whose pitch an envelope throws up and lets fall quickly to the note, shaped towards a sine (a sawtooth through a waveshaper in Synth Secrets; others describe a triangle through a shaper), then a VCA with its own decay.
- Attack: a short pulse and low-pass filtered noise, through their own envelope; the Attack knob sets their level.
- Knobs: Tune (the pitch sweep), Attack, Decay, Level, and Accent.
- Emulations add the missing parts: Synsonic's BD-909 has Pitch with keyboard tracking, Tune Decay, Tune Depth, Accent, Attack, Noise Decay, Decay, Hold and Distort.

So the 808 is a struck resonator whose pitch moves because of its circuit; the 909 is an oscillator whose pitch moves because an envelope tells it to.

## What kick synths expose

- Sonic Academy Kick 3: a synthesized sub with drawn pitch and amplitude curves, click layers, tuning in cents, and length and pitch by gate, velocity, portamento and key tracking.
- D16 Punchbox: a synthesized kick plus sampled click and top layers; key tracking moves only the kick. Nepheton is D16's synthesized 808.
- Vengeance Metrum: a sine with a pitch envelope, three sample layers, three distortion models.
- Ableton DS Kick: Pitch (Hz), Env (pitch), Attack, Click, Decay, Drive, OT (overtones).
- Bitwig: v1 Kick has Tune, Decay, Click, Tone and a pitch envelope (Amount, Decay, Contour); v8 Kick (808-like) Tune with key tracking, Decay, Punch, Shape, Drive, Click; v9 Kick (909-like) adds Buzz (noise); v0 Zap Kick has a Gated switch.
- Microtonic: decay 10 ms to 10 s, a decaying pitch modulation of up to 96 semitones, noise with its own envelope.
- FL Studio's Fruit Kick: Click as a phase offset of the sine, Distort as hard clipping.

What recurs:
- the pitch (or key tracking), the pitch envelope's amount, time and curve, the decay;
- a click or attack transient, noise, a shape or drive, a tone low-pass;
- velocity to punch and level, and a one-shot or gated mode.

## Key tracking, glide and the tail

- A tuned kick plays its note where it lasts: the tail. Kick 3 and Punchbox track only the body; the click stays put.
- 808 slides come from mono legato with glide: overlapping notes slide, separate notes retrigger. Guides give glide times from 20 ms to 500 ms; one producer used about 180 ms.
- A long tail must keep ringing through the slide; a sample-based 808 can only stretch, a resonator or oscillator can glide properly.

## Aliasing and quality at twice the host rate

- A sine sweep is clean: 60 semitones above 55 Hz is still under 2 kHz.
- A sine started at its peak (Fruit Kick's Click) is a step: it needs the band-limited step the VA and Chip generators use. A fixed start phase keeps hits alike (the Cycling '74 thread).
- Click pulses: band-limited edges, or a raised-cosine pulse like the Modal generator's strike.
- Drive on the high-pitched attack aliases; first-order antiderivative anti-aliasing (Parker, Zavalishin, Le Bivic), already in the Drive processor's curves, handles it.
- The 808's filters move every sample: a topology that tolerates that (the paper's TDF-II, or the engine's trapezoidal SVF); long tails need denormal care and a DC blocker.

## What CoolSynth can do today

- 808 Boom (Engine Room): a VA sine two octaves down, Envelope 3 dropping the pitch 24 semitones in 60 ms, a 1.4 s one-shot amplifier, Drive, mono legato with 70 ms glide. A good modern 808 bass.
- The Noise generator (Band or White) in a second slot gives a click, shaped by an envelope routed to its level.
- The Modal generator is the nearest thing to a bridged-T: a struck resonator with a set decay, and a retrigger strikes the ringing bar again.

What that cannot do:
- It spends all three voice envelopes and several matrix routes on one drum.
- The matrix's pitch sweeps every key-tracked generator, the click's too.
- No 808 behaviour: no attack shift, no level-dependent sigh, no accent that changes the tone, no hit on a ringing body.

What a dedicated generator adds: its own per-sample pitch, amplitude, click and noise envelopes in one slot, the 808's circuit behaviour, a one-shot that outlasts the key, and the voice's envelopes free for the rest of the sound.

## How it fits the engine

- Pitch comes in as `frequencyHz` with the slot's octave, semitone, fine, glide and bend; the generator tunes its tail to it, so it needs no Tune knob.
- One-shot: `holdsRelease()` keeps the voice after key-up until the tail dies, then `silentUntilNextNote()` frees it.
- `strike()` hits the ringing body again (the 808's retrigger); a legato note slides it.
- Two samples late through the Chip generator's step accumulator; mono, panned; `link()` hands on its signal (no wrap track on the 808 engine).
- A small per-voice tolerance from the slot's seed, reset by `reseed()` (D222).
- The type goes at the end of `GeneratorTypes`, its cost into `VoiceCost.h` once measured.

## Design options

### Option A: the 808 circuit model

What it gives: the real 808 behaviour, including the circuit bends Werner's paper was written for.
Knobs:
- Decay: 0.05 to 10 s (log), default 0.6 s: the feedback buffer, past stock to near self-oscillation.
- Tone: 0 to 100 %, default 50 %: the passive low-pass.
- Accent: 0 to 100 %, default 50 %: how far velocity moves the trigger between 4.7 and 11.5 V (D258).
- Attack Shift: 0 to 200 %, default 100 %: the Q43 path's strength.
- Attack Time: 1 to 30 ms (log), default 6 ms: the envelope generator (C38).
- Sigh: 0 to 300 %, default 100 %: the leakage through R161.
- Pulse Width: 0.1 to 10 ms (log), default 1 ms.
- Rails: 5 to 15 V, default 15 V: lower clips the op-amps harder.
- Tolerance: 0 to 100 %, default 30 %: per-voice component spread.
- Trigger: One Shot or Gate.

How: the paper's blocks, scaled so the settled tail sits at `frequencyHz` (the bridged-T's capacitors scaled, the feedback buffer not, so the decay stays in seconds).
Cost: a handful of biquads and two exponentials a sample, probably cost class 1. The work is in the calibration, against the paper's figures and its companion audio.
Side effects: only 808 sounds; a 909 or a modern kick still needs today's patching.

### Option B: pitch envelope, click and drive (909 and modern)

What it gives: the Kick 3, DS Kick and v1 Kick family, and the 909 with a shaped wave and its noise.
Knobs:
- Punch: 0 to 60 semitones, default 24.
- Punch Time: 1 to 500 ms (log), default 40 ms.
- Punch Curve: -100 to 100 %, default -50 %.
- Decay: 0.02 to 10 s (log), default 0.5 s.
- Hold: 0 to 2 s, default 0.
- Shape: 0 to 100 %, default 0: sine to the 909's shaped wave.
- Click: 0 to 100 %, default 30 %.
- Click Tone: 500 Hz to 16 kHz, default 4 kHz.
- Noise: 0 to 100 %, default 0.
- Drive: 0 to 24 dB, default 0.
- Tone: 0 to 100 %, default 100 %.
- Trigger: One Shot or Gate.

How: a phase-accumulated sine with a fixed start phase, band-limited steps for the start and the click, anti-aliased drive.
Cost: cheap, class 1, and quick to build.
Side effects: an 808 can be imitated (a sine, a fast pitch drop, a slow fall for the sigh) but not its accent, level-dependent sigh or retrigger.

### Option C, recommended: one generator with a Model switch

What it gives: the 808 model and the pitch-envelope engine in one slot, with knob names shared, as the Chip generator shares its knobs across chips.
Knobs:
- Model: 808, 909, Modern. Default 808.
- Decay: 0.05 to 10 s (log), default 0.6 s.
- Punch: 0 to 48 semitones, default 12: the 808's attack shift, the 909's and Modern's pitch drop.
- Punch Time: 1 to 500 ms (log), default 6 ms.
- Sigh: 0 to 12 semitones, default 2.4: the 808's leakage; on the others a slow fall over the tail.
- Click: 0 to 100 %, default 30 %: the 808's pulse, the 909's Attack, Modern's transient.
- Noise: 0 to 100 %, default 0: the 909's burst; on the 808 a modification.
- Shape: 0 to 100 %, default 0: the wave (sine to shaped); on the 808 the op-amps' clipping.
- Accent: 0 to 100 %, default 50 %: how much velocity changes punch, sigh and level as well as loudness.
- Drive: 0 to 24 dB, default 0.
- Tone: 0 to 100 %, default 50 %.
- Trigger: One Shot or Gate.

How: two engines behind the switch, sharing the click, noise, drive, tone, one-shot and output code; the 808 engine first, as Succession needs it.
Cost: class 1 either way (only one engine runs); more work than A or B alone, less than both apart.
Side effects: some knobs mean slightly different things per model, which the panel must say; the 808 bends that do not fit (pulse width, rails, tolerance as a knob) become fixed values.

Why recommended: it covers the Succession 808, the 909 and modern kicks in one slot within twelve knobs, keeps the 808's real behaviour where A alone would, and follows the Chip generator's pattern of a model switch over shared knobs.

## A Succession-style preset

What is known:
- Wikipedia and NPR confirm TR-808 beats under the piano and strings; Britell says he turns the bass up "a little too much" on purpose.
- Published arrangements (Musicnotes, hdpiano, Hooktheory) put the theme in C minor at 140 BPM.
- D229 records that the 808s are tuned to the C minor bass; I found no source in this pass that states the tuning or any settings, so the preset is a sound-alike.

A sketch:
- Model 808, key tracking on, mono, retrigger on, no glide: each hit strikes the ringing body.
- Tuned so the tail sits on C2 (65.4 Hz, MIDI 36), about three semitones above the stock 808 and inside its usable range; C1 (32.7 Hz) as a deeper alternative for big speakers. The bass line's other roots (A flat, F, G) play the same way.
- Decay about 1.3 s: at 140 BPM a beat is 0.43 s, so the boom runs through about three beats unless the next hit cuts in.
- Punch 12 and Punch Time 6 ms (stock), Sigh 2.4 to 3 semitones: the falling "sigh" under the tail.
- Tone 35 %: dark, little click; Click 20 %.
- Accent 60 %, played with velocity, so downbeats hit harder.
- Drive 6 dB, then the Drive insert lightly: the distorted bass Time describes.
- Levelled to -20 LUFS like the other presets.

## Sources

- Werner, Abel, Smith, "A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit", DAFx-14: https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf
- The paper's companion page with audio: https://ccrma.stanford.edu/~kwerner/papers/dafx14.html
- Werner, "Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters", thesis, 2016: https://ccrma.stanford.edu/papers/virtual-analog-modeling-of-audio-circuitry-using-wave-digital-filters
- Gordon Reid, "Synth Secrets: Practical Bass Drum Synthesis", Sound On Sound: https://www.soundonsound.com/techniques/practical-bass-drum-synthesis
- Deruty, "Harmonic and Transposition Constraints Arising from the Use of the Roland TR-808 Bass Drum", 2025: https://arxiv.org/abs/2502.07524v1
- Computer Music on the 808's bridged-T and the 909's separate circuits: https://www.pressreader.com/australia/computer-music/20190220/282381220809560 and https://www.pressreader.com/australia/computer-music/20190220/282432760417112
- MusicRadar, "What is the 808 kick": https://www.musicradar.com/news/what-is-the-808-kick-and-why-do-we-all-still-love-it
- Synsonic BD-909 manual: https://synsonic-instruments.com/wp-content/uploads/2022/01/BD-909-Doku.pdf
- CDM on a 909 kick from a triangle and a shaper: https://cdm.link/make-a-909-kick-on-the-make-noise-0-coast-and-more-drum-modeling-fun/
- Sonic Academy Kick: https://www.sonicacademy.com/products/kick/ and the Kick 3 release: https://www.kvraudio.com/news/sonic-academy-releases-kick-3-62006
- D16 Punchbox review: https://bedroomproducersblog.com/2016/07/11/d16-punchbox-review/ and Nepheton: https://d16.pl/nepheton
- Vengeance Metrum: https://www.pluginboutique.com/products/2167-Metrum
- Ableton DS Kick: https://www.ableton.com/en/manual/max-for-live-devices
- Bitwig kicks: https://www.bitwig.com/userguide/latest/kick
- Microtonic manual: https://www.manualsdir.com/manuals/349731/sonic-charge-microtonic.html?page=14
- Fruit Kick: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruit%20Kick.htm
- Cycling '74 forum on kick click and start phase: https://cycling74.com/forums/kick-drum-synthesis-click-problem
- 808 tuning: https://iconcollective.edu/808-mixing-tips and https://blog.landr.com/what-is-an-808/
- 808 glides: https://violetrecording.com/how-to-make-808-slides/ and https://support.soundtrap.com/hc/en-us/articles/8136899014546-How-to-use-808-with-glide
- Parker, Zavalishin, Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution", DAFx-16: https://dafx.de/paper-archive/details/vem_XXF5qBbfiWOH2RVVAA
- Succession: https://en.wikipedia.org/wiki/Succession_(Main_Title_Theme), https://www.npr.org/2023/05/25/1178008172/nicholas-britell-succession, https://en.wikipedia.org/wiki/Succession_(soundtrack), https://time.com/5662975/succession-theme-song/
- Key and tempo: https://www.hooktheory.com/theorytab/view/nicholas-britell/succession-main-title, https://www.musicnotes.com/sheetmusic/nicholas-britell/succession/MN0187889, https://hdpiano.com/lesson/succession-main-title-by-nicholas-britell

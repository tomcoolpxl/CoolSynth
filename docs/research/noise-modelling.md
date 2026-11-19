# Added noise: how it is done elsewhere, and CoolSynth

Research for TODO 18, 2026-11-19. The user found the noise in CoolSynth's effects a little artificial at times
and asked first of all for an online investigation of current practice. The user's choices are in
`DECISIONS_LOG.md` D221.

## What others do

### Tape hiss

Tape hiss has a nominally constant level ([Sound On Sound, tape noise reduction](https://www.soundonsound.com/techniques/tape-noise-reduction)).

Its spectrum is not flat.
- Playback equalisation turns over at 1,326 Hz (120 µs, Type I tape) or 2,274 Hz (70 µs); 70 µs gives about
  4.5 dB less hiss ([BASF Technical Bulletin 4](http://www.ant-audio.co.uk/Tape_Recording/Library/EQ.pdf)).
- Measured hiss without noise reduction rises with frequency ([High Fidelity, June 1981](https://www.gammaelectronics.xyz/high-fidelity_mag_1981-06_nad.html)).
- Dolby B takes about 10 dB of hiss away between 2 and 7 kHz; tape noise is heard mostly above 1 kHz
  ([Wikipedia, Dolby noise reduction](https://en.wikipedia.org/wiki/Dolby_noise-reduction_system)).
- Inferred from these together: fairly low and flat below 1 to 2 kHz, rising through the treble, then rolled off
  at the top by the same tape losses as the music. Airwindows TapeDither shapes its noise to roll off where tape
  noise does ([Airwindows](https://www.airwindows.com/tapedither-redux)).

Its level: an unweighted cassette without noise reduction has 50 to 60 dB of signal to noise (a magazine scan
with garbled text, [HiFi Stereo Review, March 1983](https://www.worldradiohistory.com/Archive-All-Audio/Archive-HiFI-Stereo/80s/HiFi-Stereo-Review-1983-03.pdf),
and a [forum post](https://www.tapeheads.net/threads/where-did-the-hiss-go.9605/)).

It is stereo: "Satin's tape hiss is stereo, just like a 2-track tape machine" ([u-he Satin user guide](https://u-he.com/downloads/manuals/plugins/satin/_Satin-user-guide.pdf)).

Modulation (asperity) noise rides on the signal itself: changes in the spacing between head and tape put noise
sidebands around it ([Laughlin, CMU](https://www.andrew.cmu.edu/user/dl0p/laughlin/pdf/354.pdf)). Satin models
it apart from the hiss (Asperity, -100 to -50 dB, stereo), and it leaves a noise tail on the signal.

Noise that rises and falls with the music is a known defect.
- Sound On Sound calls it noise pumping, a side effect of noise reduction (with dbx, bass notes bring a halo of
  noise).
- Wikipedia calls it breathing; Dolby spread its reduction over frequency to avoid it, and its effect is in the
  treble only and about 10 dB at most.

### Vinyl

The main source is Moliner and Välimäki, DAFx 2022 ([arXiv 2206.06259](https://arxiv.org/pdf/2206.06259v2.pdf)).
- Clicks last from under 20 µs to 4 ms, and their size varies greatly within the same recording.
- On gramophone records they are dense, about 2,000 a second; only the strongest are heard one by one and the rest
  are the crackle.
- Clicks made from random numbers (the 2008 antiquing method) sound static, while real ones keep changing timbre.
- The noise repeats roughly with the disc's rotation, with variation between turns; its hiss is not stationary.
- A thump is a break of under 2 ms followed by a damped low tail whose frequency falls, over 50 ms or more.
- Rumble and hum belong to the picture too.

The 2008 paper is Välimäki et al., "Digital Audio Antiquing", JAES 56(3) ([AES](https://aes2.org/publications/elibrary-page/?id=14378));
it is paywalled and its [project page](http://research.spa.aalto.fi/publications/papers/antiquing/) has no detail.

Stereo: each groove wall carries one channel, so dust often clicks in one channel only (inferred; restoration
reports support it, [pspatialaudio](https://pspatialaudio.com/clicks_&_noise.htm)).

A community SuperCollider patch shows the usual layering: sparse pops with random decay, resonance and pan, a
dense quiet crackle bed whose rate drifts (100 to 500 a second), band-limited hiss, rumble and a looped pattern
([sccode](https://sccode.org/1-4Sj); hearsay level).

### Products

XLN RC-20 Retro Color ([manual](https://assets.xlnaudio.com/documents/rc-20-retro-color_manual.pdf)).
- Noise is the first module, so it is distorted, crushed, filtered and fluttered with the source; Post moves it
  to the end.
- Follow at 0 % keeps the noise level static; Duck pushes it under the audio.
- Its noise types are looped samples, by [MusicRadar's review](https://musicradar.com/reviews/tech/xln-audio-rc-20-retro-color-646595).

ChowTapeModel, open source ([DegradeNoise.h](https://raw.githubusercontent.com/jatinchowdhury18/AnalogTapeModel/master/Plugin/Source/Processors/Degrade/DegradeNoise.h),
[DegradeProcessor.cpp](https://raw.githubusercontent.com/jatinchowdhury18/AnalogTapeModel/master/Plugin/Source/Processors/Degrade/DegradeProcessor.cpp)).
- White noise from its own generator per channel, added before the degrade low-pass, so it is filtered with the
  signal.
- An Envelope setting, off at 0, makes the noise follow the level. A user called its hiss rather static
  ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=7601824)).

u-he Satin: Hiss, Asperity and Auto Mute, which soft-gates both when no input is detected, and companders
modelled on Dolby A and B and dbx I and II.

AudioThing Vinyl Strip ([guide](https://www.audiothing.net/docs/AudioThing_Vinyl_Strip.pdf)): Dust Amount, Dust
Rate and Noise; an envelope you switch on stops dust and noise when no sound passes.

iZotope Vinyl ([help](https://s3.amazonaws.com/izotopedownloads/docs/vinyl/en/print/index.html)): mechanical noise
(clicks and rumble) apart from electrical noise (hum), dust and scratches with amounts and gains, scratches
repeating at the record's speed, wear that depends on the input level. Its method is not published.

Synths.
- The Minimoog's noise comes from a reverse-biased transistor junction, with a White/Pink switch
  ([Model D manual](https://cdn.inmusicbrands.com/Moog/Model%20D/Minimoog_Model_D_Manual.pdf)).
- Serum's noise oscillator is a stereo or mono sample player ([MusicRadar](https://musicradar.com/how-to/how-to-get-more-out-of-xfer-records-serums-oscillators));
  Pigments uses sampled noise types ([Arturia](https://arturia.com/products/software-instruments/pigments/overview)).
- Noise colours: white flat, pink -3 dB per octave, brown -6, blue +3, violet +6 ([Wikipedia](https://en.wikipedia.org/wiki/Colors_of_noise)).

Not confirmed: the 2008 paper's click model, RC-20's default Follow, iZotope Vinyl's stereo and level-following,
Diva's, Vital's and the Prophet-5's noise, Waves Abbey Road Vinyl (only an unofficial manual), Baby Audio TAIP's
noise shaping, a published hiss spectrum in dB per hertz.

## CoolSynth on 2026-11-19

LoFi (`src/synth/LoFi.h`), one per effect slot.
- A peak follower on the louder channel (attack 5 ms, release 0.4 s) opens a gate, min(1, follower × 6): fully
  open only above about -15.6 dBFS, and below that the noise falls one for one with the signal (D52, which itself
  noted that real tape has a constant floor). The gate drives both hiss and crackle.
- Hiss: uniform white noise through a one-pole low-pass at 6 kHz, flat to 6 kHz and then -6 dB per octave, about
  -33 dBFS at full Hiss (estimate). Left and right draw their own values: uncorrelated, as in practice.
- Crackle: a Poisson process (each sample has a chance of 40 × crackle / sample rate), sizes uniform over about
  12 dB with a random sign; every click is the same 0.4 ms differentiated exponential, in effect a one-sample spike
  with a small opposite tail, whose length depends on the sample rate; the same click in both channels.
- The noise is added after the Bandwidth low-pass, the rate hold and the bit crush, so Bandwidth does not dull it.
- One fixed seed in every LoFi: two slots with the same settings started together make the same noise (inferred).
- Presets with hiss or crackle: Bedroom 46, 71, 99, 145; Basement 116; Night Bus 18, 194, 300, 416 (line numbers
  of that day).

The Noise generator (`src/synth/v3/NoiseGenerator.h`) is in line with practice: white, pink within about 0.1 dB
per octave of -3.01, band and chip, seeded per slot and voice, an equal-power width. Other noise sources (the
ladder's -110 dB floor, the string's pluck noise, jitter and drift) are fine.

## Why it may sound artificial, strongest first

The noise follows every note: each note brings its own hiss in 5 ms and drags it out over 0.4 s, so the noise
sounds attached to the instrument instead of the medium; in RC-20, Chow and AudioThing following is an option.

The clicks are one shape, mono and centred, narrow in size and sparse, with no bed, pops or rotation.

The hiss has a flat spectrum to 6 kHz (too much low-mid, too little sizzle) and is not dulled by Bandwidth.

There is no modulation noise, and every LoFi slot shares one seed.

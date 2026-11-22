# The TR-808's other voices: what exists (D241)

Research for TODO 33: the 808's voices beyond the bass drum (`kick-808-wdf.md`), as generators and as a kit, aimed at house use. A survey of open projects, papers, the service notes and reference recordings, on 2026-11-20.

In short: open circuit-level models exist for three of the eleven voices (cymbal, cowbell, snare); only the cymbal's is checked, against SPICE; nothing open is checked against a real 808 with numbers. The service notes' own measurement table and Michael Fischer's recordings are the references to check against.

## The voices and their circuits

From the service notes (page 6, the circuit descriptions, and page 13, the schematic):
- Bridged-T voices, like the bass drum: low, mid and high tom or conga (one switch each), rim shot or claves.
  - Toms and congas get their pitch drop from D80 and D81; the low tom adds a little pink noise as "artificial reverberation".
- Snare: two bridged-T networks (the drum's two modes, their balance on VR8) and the snappy noise with its own envelope (VR9).
- The metallic bank: six square-wave oscillators on one HD14584 hex Schmitt trigger, shared by the cowbell (two of them, 540 and 800 Hz), the cymbal and both hats (all six through band-passes and "swing" VCAs).
  - The closed hat cuts the open hat: Q23 turns on through R173 and ends its decay.
- Hand clap: band-passed white noise (about 1 kHz) into two VCAs, one a long "reverb" envelope (Q70), the other a sawtooth envelope from IC23 (AN6912) that fires three short bursts within about 30 ms.
- Maracas: white noise through Q65 and a high-pass (Q68), with its envelope from Q66 and Q67.

## Source material

- The service notes, June 15, 1981, 16 pages: https://synthfool.com/docs/Roland/TR_Series/TR808/TR808_service_manual.pdf; schematics also at http://machines.hyperreal.org/manufacturers/Roland/TR-808/.
- Page 14, "Checking Voices": each voice's amplitude, frequencies and time to a tenth at 12 o'clock.
  - Bass drum 56 Hz; snare 238 and 476 Hz; claves 2500 Hz; rim shot 455 and 1667 Hz; cowbell 540 and 800 Hz.
  - Cymbal 350, 800 and 1200 ms; open hat 90, 450 and 600 ms.
  - The low conga's row repeats the high tom's (165, 185, 220 Hz): likely a misprint, to check.
- Page 15, design changes: clap R346 1 kOhm to 680 Ohm; the noise generator IC24 reworked (R129, R311, R127, C202); gate resistors 22 kOhm to 10 kOhm; 22 pF on COMMON TRIG; cowbell C6 0.01 to 0.022 uF and R44 390 kOhm to 150 kOhm from serial 000300.
- Known drawing errors: the clap's C144 (0.47 uF electrolytic) is drawn reversed, as the notes' own text implies (https://electro-music.org/forum/post-337313.html); the noise generator's corrections are in the text, not the drawing.

## Papers

- Werner, Abel, Smith, "The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model", ICMC-SMC 2014, CC BY 3.0: https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf, read.
  - The six oscillators' nominal frequencies (205.3, 369.6, 304.4 and 522.7 Hz; the fifth and sixth trimmed to 800 and 540 Hz), duty 47.98 %; earlier units (before serial 000300) differ on the fifth and sixth.
  - Two bridged-T band-passes near 3440 and 7100 Hz with their coefficient formulas; an attack smoother; three swing VCAs with fitted clipping curves; Sallen-Key high-passes, one resonant near 10.5 kHz; the tone stage; the output buffer.
  - 4x oversampled, bilinear transforms with prewarping; checked against SPICE by eye, not against a real 808.
- Werner, Abel, Smith, "More Cowbell", AES 137th Convention, paper 9207: https://www.aes.org/e-lib/browse.cfm?elib=17530, paywalled; the abstract only.
- Werner's thesis, Stanford 2016: https://purl.stanford.edu/jy057cz8322; its abstract covers the bass drum only (the fetch of the PDF failed).
- Sound On Sound, Synth Secrets (Gordon Reid): the snare (https://www.soundonsound.com/techniques/practical-snare-drum-synthesis), block level, no values; the cowbell article's 587 and 845 Hz are the CR-8000's, not the 808's.
- The clap: a KVR thread (https://www.kvraudio.com/forum/viewtopic.php?p=8606994) gives bursts about 11 ms apart and a band-pass near 1140 Hz, Q 1.95; unconfirmed.

## Open projects

Circuit-level:
- Mz808 (https://github.com/simozzer/Mz808), AGPL-3.0: every voice "built from the circuits", the bass drum and cymbal after Werner; its README says it is not calibrated against hardware.
- WDR-8, The Acid Kit for VCV Rack (https://github.com/Simon-L/WDR-8), GPL-3.0: the snare in wave digital filters (chowdsp_wdf) with discrete noise envelopes; no comparison with hardware.

Sound-alikes:
- Mutable Instruments Plaits and Peaks (https://github.com/pichenettes/eurorack), MIT: an "808 style" metallic hat (six squares at fixed ratios, a band-pass, an asymmetric VCA, a high-pass) and an analog snare with an 808-style branch; Rust port at https://github.com/sourcebox/mi-plaits-dsp-rs, MIT.
- iO-808 (https://github.com/vincentriemer/io-808), MIT, Web Audio: every voice from Synth Secrets and the block diagram; no measurements.
- 0x808 (https://github.com/averagenative/0x808), MIT, C.
- SC-808 for SuperCollider and Hora's Treasure for VCV Rack: no open licence.
- A reference document of each voice's topology, frequencies and Q, each claim marked verified or inferred: https://github.com/2AMLogic/gf180-monosynth/pull/8, Apache-2.0; secondary.

Hardware clones: the Yocto kit (a wiki notes R108 and R111 swapped in its cymbal drawing); Tiptop's 808 modules, one-to-one clones on +-12 V.

## Reference recordings

- Michael Fischer's set, 1994: 116 WAVs, 16-bit 44.1 kHz, from serial 103852, each voice's own output, LEVEL full, the knobs at 0, 2.5, 5, 7.5 and 10 (25 files each for bass drum, snare and cymbal; 5 for toms, congas and open hat; one for the rest); "absolutely free", mirrored as CC0-1.0 at https://github.com/tidalcycles/sounds-tr808-fischer. No accent variations.
- Other packs are processed or of unclear settings and terms.

## How others model them, and at what cost (2026-11-21)

Searched again before the direction was put to the user (D250). Nobody publishes a cost per voice, and nobody open checks a voice beyond the bass drum against a real 808 with numbers.

Papers:
- Only three voices have one: the bass drum (DAFx 2014, the thesis), the cymbal (ICMC 2014, open) and the cowbell ([AES 137th, paper 9207](https://www.aes.org/e-lib/browse.cfm?elib=17530), $33 for non-members; its band-pass worked by Mason's gain formula, "physical and behavioral models of each sub-circuit").
- None on the toms, congas, rim shot, claves, snare, clap, maracas or hats; the hats share the cymbal paper's oscillators and VCAs.

Commercial:
- Roland's ACB (TR-8, TR-8S, TR-08, the TR-808 plug-in) models "each analog circuit, right down to the component level" ([TR-08](https://www.roland.com/uk/products/tr-08/)); nothing more is published. Roland's engineers told Computer Music they recreated what they "believe to be the ideal" 808 and 909 by knowledge and ear, as no two units sound alike ([Computer Music, 2019](https://www.pressreader.com/australia/computer-music/20190220/282432760417112)).
  - The plug-in plays 11 voices ([Roland](https://www.roland.com/uk/products/rc_tr-808/)); one user measured 1.1 to 2.5 % of an i7-4790K, the same however many voices played ([KVR](https://kvraudio.com/forum/viewtopic.php?p=7291556)): every voice computed all the time.
- D16's Nepheton 2: no method or per-voice cost published; users find its effects, not its voices, the cost ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=8810752), [CDM review](https://cdm.link/2023/12/nephaton2-808-plugin-review/)).
- AudioRealism's ADM: 25 generators "modeled after the classics' analog circuits" and 4 PCM players; its 909 a hybrid of model and samples ([manual](https://www.scribd.com/document/370622048/Audiorealism-ADM-Manual-pdf), [MusicRadar](https://musicradar.com/reviews/tech/audio-realism-adm-121891)).
- Tactile Sounds' TS-808 (free, Windows): SPICE simulations from the service notes, then tuned until waveforms and spectra matched Goldbaby's Tape808 samples ([Synthtopia](https://www.synthtopia.com/content/2010/04/24/free-virtual-roland-tr-808-drum-machine-ts-808/), [KVR](https://www.kvraudio.com/product/ts_808_by_tactile_sounds)).
  - Its cowbell: the schematic's 540 and 800 Hz, the samples' 565 and 849 Hz, shipped at 523.25 and 783.99 Hz, a fifth in tune.
- Cherry Audio's Transistor 808 modules (Misfit Audio, Voltage Modular): every voice a module; the hats keep the 808's separate envelopes and filters per oscillator group; the kick a sine with a pitch envelope and a transient, so behavioural ([hats](https://docs.cherryaudio.com/misfit-audio/transistor-808-hats), [cymbal](https://docs.cherryaudio.com/misfit-audio/transistor-808-cymbal), [kick](https://docs.cherryaudio.com/misfit-audio/transistor-808-kick)).
- Hora's Treasure (VCV Rack): the voices in 7 modules, each in a Vult DSP and a VCV DSP version; no costs published ([gearnews](https://www.gearnews.com/treasure-tr-808-emulation-vcv-rack-virtual-modular-software)).
- WDR-8's snare (chowdsp_wdf) took its author over a year and needed updates to cut its CPU ([VCV forum](https://community.vcvrack.com/t/the-acid-kit-new-modules-for-the-rack-x0x-crowd/18600)).
- Plaits: its hat six squares into a clocked-noise mix, a high-pass and a transistor VCA ([Tidal's Plaits reference](https://tidalcycles.org/docs/reference/mi-ugens-plaits)); its snare "an 808 snare model, revisited" ([source mirror](https://cadlab.io/project/1344/master/files/plaits/dsp/drums/analog_snare_drum.h)).

So the field splits three ways: component models with nothing published (ACB, ADM); SPICE or circuit reading tuned to samples (TS-808, Mz808, WDR-8); and behavioural sound-alikes (Plaits, Cherry Audio, iO-808). CoolSynth's bass drum is the only one checked stage by stage against SPICE and then against two real units.

What the circuits need, from the service notes and secondary write-ups, to check against the schematic:
- Toms and congas: one bridged-T split in two, both halves for the tom, one bypassed for the conga; the diodes shorten the time constant early in each hit, the pitch drop ([808 Tom Synthesis](https://baratatronix.squarespace.com/blog/808-tom-synthesis), [service notes p. 6](https://www.manualslib.com/manual/1294765/Roland-Tr-808.html?page=6)). Which tom D80 and D81 belong to: the sources disagree.
- Cowbell: two squares near 1 : 1.44, two VCAs, one envelope, a band-pass, a buffer; small detunings "largely destroy the cowbell illusion" ([Sound On Sound](https://www.soundonsound.com/techniques/synthesizing-cowbells-claves), [Hackaday](https://hackaday.com/2022/05/12/how-the-roland-808-cowbell-worked), [a rebuild](https://frisnit.com/?p=919)).
- Clap: the generating cycle through Q73 and IC23 is in the service notes; the "reverb" is not detailed ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=8606994)). Bursts about 11 ms apart (KVR) against three within 30 ms (above): to settle from the schematic and Fischer's clap.
- Maracas: noise gated by Q65 into IC19 through the filter Q68, the envelope from Q66 and Q67 (service notes p. 6).
- Snare: two versions. Page 9's "portion changed" (C58, C61, R191, R200) takes the shells from about 250 and 500 Hz to
  173 and 336 Hz; Fischer's and Audiorealism's units ring at 172 and 332 to 340 Hz, the changed one, as do Roland's ACB
  remakes and most 808s; early units (Planet Rock's, 244 and 486 Hz) the drawn one
  ([Norgatronics](https://norgatronics.blogspot.com/2021/11/808-snare-mutations.html)). Page 14 still gives the drawn one.

Cost, estimated here and not measured: the Kick's 808 circuit takes 3.1 % of a block a voice on average and 6.6 % at its 99th percentile, its behavioural predecessor 0.33. Full circuits of the other voices would take some 2 to 7 % each while they ring and nothing at rest; all eleven ringing at once could pass a sound's 50 % budget.

## What this means for CoolSynth

- Usable as code, credited: Plaits and Peaks (MIT), iO-808 (MIT), chowdsp_wdf (BSD-3); the cymbal paper's equations (CC BY 3.0).
- Facts only: Mz808 (AGPL), WDR-8 (GPL-3), the cowbell paper, the thesis, Sound On Sound.
- Best starting point per voice:
  - Cymbal and hats: the cymbal paper, the only block-by-block circuit model; the hats share its oscillators and VCAs.
  - Cowbell: the same oscillators (the fifth and sixth) and the page-13 schematic.
  - Snare: the schematic, with WDR-8 and Mz808 as other readings; neither is calibrated.
  - Toms, congas, rim shot, claves, clap, maracas: no good open model; the schematic, the page-6 text and the page-14 table, checked against Fischer's recordings. New work, but on the bass drum's machinery for the bridged-T voices.
- Chosen by the user (D250): every voice as its full circuit, all sixteen sounds, as three family generators (808 Drums, 808 Snare & Clap, 808 Metal) that fill one layer; the bass drum's gaps first, then outward by shared circuit; a stock 808 Kit preset.

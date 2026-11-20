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

## What this means for CoolSynth

- Usable as code, credited: Plaits and Peaks (MIT), iO-808 (MIT), chowdsp_wdf (BSD-3); the cymbal paper's equations (CC BY 3.0).
- Facts only: Mz808 (AGPL), WDR-8 (GPL-3), the cowbell paper, the thesis, Sound On Sound.
- Best starting point per voice:
  - Cymbal and hats: the cymbal paper, the only block-by-block circuit model; the hats share its oscillators and VCAs.
  - Cowbell: the same oscillators (the fifth and sixth) and the page-13 schematic.
  - Snare: the schematic, with WDR-8 and Mz808 as other readings; neither is calibrated.
  - Toms, congas, rim shot, claves, clap, maracas: no good open model; the schematic, the page-6 text and the page-14 table, checked against Fischer's recordings. New work, but on the bass drum's machinery for the bridged-T voices.
- To put to the user when the stage starts: the fidelity per voice (circuit with SPICE and recordings at house's settings, a full audit, or behavioural) and the order (house's voices first, or by shared circuit).

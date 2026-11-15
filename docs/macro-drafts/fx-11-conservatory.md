# Effect routes: 11-conservatory.csbank (D179 draft)

Every preset in this bank has one effect slot, `f1 = Reverb` (the plate; keys `size`, `damping`, `mix`,
`predelay`, `lowcut`, `modulation`, `shimmer`, all modulatable; `size` 0..1 linear sets the tank decay
0.25 + 0.70 * size). No preset has an EQ (no tilt or high shelf), so **Tone stays unused ("-") in all 40**:
the reverb's damping is not tone. No names change, so no `names:` lines. No insert has drive, lofi,
chorus or phaser, so Character gets no route either.

Every route below is `src=macroN` (nothing at macro 0), one `fx` line per preset (limit 8), no mix is
raised (the only level change is the slightly longer tail of a bigger plate, well under +3 dB), no delay
so no feedback. Tail lengths quoted are the plate's RT60 from D178 (2.4 s at size 0.2, 4.4 s at 0.5,
10.5 s at 0.8, damping 0.3), interpolated; at these presets' damping 0.5 they are a little shorter.

### Steinway Grand (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Pedal: the room blooms with the held notes (plate size 0.4 to 0.55, tail about 3.5 s to 5 s)

### Yamaha Upright (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Pedal: the room blooms with the held notes (plate size 0.4 to 0.55, tail about 3.5 s to 5 s)

### Old Upright (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Pedal: the room blooms with the held notes (plate size 0.4 to 0.55, tail about 3.5 s to 5 s)

### Flemish Harpsichord (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.1
- Ring: the notes ring on into a slightly larger room (plate size 0.4 to 0.5, tail about 3.5 s to 4.4 s; kept small so the plucks stay clear)

### Italian Harpsichord (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.1
- Ring: the notes ring on into a slightly larger room (plate size 0.4 to 0.5, tail about 3.5 s to 4.4 s)

### FM Keys (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Ring: the release and the room grow together (plate size 0.4 to 0.55, tail about 3.5 s to 5 s)

### Violins (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the bowed release hangs in a bigger hall (plate size 0.55 to 0.7, tail about 5 s to 7.5 s)

### Violas (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the bowed release hangs in a bigger hall (plate size 0.55 to 0.7, tail about 5 s to 7.5 s)

### Cellos (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the bowed release hangs in a bigger hall (plate size 0.55 to 0.7, tail about 5 s to 7.5 s)

### Double Bass (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the bowed release hangs in a bigger hall (plate size 0.55 to 0.7, tail about 5 s to 7.5 s)

### Solo Violin (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the bowed release hangs in a bigger hall (plate size 0.55 to 0.7, tail about 5 s to 7.5 s)

### Dan Tranh (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Ring: the strings ring on into a larger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Horn (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release carries into a bigger hall (plate size 0.5 to 0.65, tail about 4.4 s to 6.8 s)

### Trumpet (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release carries into a bigger hall (plate size 0.5 to 0.65, tail about 4.4 s to 6.8 s)

### Tuba (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release carries into a bigger hall (plate size 0.5 to 0.65, tail about 4.4 s to 6.8 s)

### Trombone (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release carries into a bigger hall (plate size 0.5 to 0.65, tail about 4.4 s to 6.8 s)

### Old Trombone (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release carries into a bigger hall (plate size 0.5 to 0.65, tail about 4.4 s to 6.8 s)

### Flute (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Clarinet (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Bassoon (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Saxello (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Tenor Sax (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Recorders (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Ocarina (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Harmonica (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the release floats into a bigger room (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Didgeridoo (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the drone's release hangs in a bigger space (plate size 0.45 to 0.6, tail about 4 s to 5.8 s)

### Pipe Organ (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.12
- Linger: the release rolls round a cathedral (plate size 0.7 to 0.82, tail about 7.5 s to 11 s)

### Renaissance Organ (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.12
- Linger: the release rolls round a cathedral (plate size 0.7 to 0.82, tail about 7.5 s to 11 s)

### Glockenspiel (11-conservatory.csbank)
fx = src=macro4 dst=f1.shimmer amount=0.2
- Shimmer: besides the slight pitch waver, an octave-up halo rises in the tail (plate shimmer 0 to 0.2; band-limited 180 Hz-5 kHz and soft-limited in the reverb, it cannot run away)

### Tubular Bells (11-conservatory.csbank)
fx = src=macro4 dst=f1.shimmer amount=0.2
- Shimmer: besides the slight pitch waver, an octave-up halo rises in the tail (plate shimmer 0 to 0.2)

### Tubular Glock (11-conservatory.csbank)
fx = src=macro4 dst=f1.shimmer amount=0.2
- Shimmer: besides the slight pitch waver, an octave-up halo rises in the tail (plate shimmer 0 to 0.2)

### Hand Chimes (11-conservatory.csbank)
fx = src=macro4 dst=f1.shimmer amount=0.2
- Shimmer: besides the slight pitch waver, an octave-up halo rises in the tail (plate shimmer 0 to 0.2)

### Wine Glasses (11-conservatory.csbank)
fx = src=macro3 dst=f1.size amount=0.15
- Linger: the glasses' release hangs in a bigger room (plate size 0.5 to 0.65, tail about 4.4 s to 6.8 s)

Left without effect routes: Folk Harp, Kalimba, Marimba, Vibraphone, Xylophone, Balafon, Timpani
(their Time macro is Damp, a shorter ring of the instrument itself; a damped instrument in a hall keeps
the hall, so shrinking the reverb would not read as damping, and their Movement macros Sway, Flutter,
Roll and Motor are level or pan motions a reverb setting would not deepen).

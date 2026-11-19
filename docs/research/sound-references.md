# Sound references: how these artists get their sound, and what CoolSynth needs

Research report, 2026-11-15. It was written in a side session, and nothing in the engine was changed.
Everything here comes from online sources, linked in each section and listed again in section 10.
I have not heard the songs. Descriptions of how they sound come from reviews, interviews and
listings. Anything marked **(inferred)** is my reading, not a finding.

## 0. Who and what, and how sure

The names were dictated, so here is what I take each one to mean. You confirmed on 2026-11-15
that every song is by one of the artists you named, and every song below was found under one of them.

| You said | Who or what it is | How sure | Why |
|---|---|---|---|
| "Ear and Loukman" | **Two acts**: **ear** (a duo: Jonah Paz and Yaelle Avtan, USA) and **Loukeman** (a Toronto producer) | High | ear made "Nerves" and "Real Life", Loukeman made "Giverrr". Both are on your song list. Vegyn released a Loukeman remix. |
| "Vegyn" | **Vegyn** (London producer; PLZ Make It Ruins label; worked with Frank Ocean) | Certain | |
| "Meat Computer" | **meat computer** (a rapper and producer from Manitoba, Canada) | Certain | |
| Caribou, "Can't Do Without You" | **Caribou** (Dan Snaith), lead single of *Our Love*, 2014 | Certain | |
| "Healing Spiritual Echo" | **meat computer**, "healing spiritual echo", on *slept on the floor still dreamt about you (pop edition)*, 2:44 | High | The title is on his catalogue page. **Flag:** the release date (22 January 2026) and tempo (about 106 BPM) come from Shazam listings seen in search results. The Shazam page itself would not load. |
| "Debold" | **Vegyn**, "Debold", on *Only Diamonds Cut Diamonds*, 2019 | Certain | **Flag:** there is also "Debold (Mk.gee Remix V7)" from 2020. I assume you mean the original, which is the better-known version (about twice the listeners on Last.fm). Both are covered. |
| "Giver" with three r's | **Loukeman**, "Giverrr", *Sd-2*, 2024, track 7 | Certain | |
| "Nerves" | **ear**, "Nerves", their first single, November 2024 | High | Other songs share the title, but this one fits the artist list. |
| "Real Life" | **ear**, "Real Life", first track of *The Most Dear and The Future*, 2025 | High | The exact title is ear's. Loukeman's "Real Life Man" (*Sd-2*, track 13) is the only near match; it is noted briefly in 2.2. |

## 1. The short answer: what kind of synth this is

Your hunch is mostly right: **this sound is mainly effects and sample handling, not unusual
oscillators.** The synths in these songs are plain: square waves, saws, sines, warm analogue-style
polysynths and stock DAW instruments. What makes the sound is what happens to them afterwards.

Across all five acts the same six ingredients come back:

1. **Voices that are pitched, chopped and disguised.** Caribou loops a pitch-shifted Marvin Gaye
   line. Loukeman runs R&B vocals through cheap pitch-shifters until they sound like synths.
   Vegyn uses chopped-and-screwed vocals. ear chop their own whispers into stutters. meat computer
   raps in a very high, whispery voice.
2. **Processing that is "bad" on purpose.** Loukeman likes pitch-shifters for their artefacts ("the
   worse they are, the better"). He keeps two saturators on the master bus and overloads a
   reverb until it turns to mush. ear "really saturate a square wave" for their bass.
3. **Stutters and hard edits.** ear and Vegyn build songs from cut-up loops. Chords and vocals
   stutter, drums are clipped.
4. **Plain synths, warmly voiced.** Oberheim Matrix-6, PPG wavetables and Ableton Operator sines
   (Loukeman). Logic's stock instruments and Arturia copies of DX7, Prophet and Matrix-12
   (Vegyn). A Juno-106, a Rhodes and a clav sound (Caribou). Stock Ableton square waves (ear).
5. **Found sound.** Phone recordings, glasses clinking, birds, rain, dial tones and snatches of
   conversation (ear, Vegyn).
6. **Movement played by hand over a whole track.** Filters held low and then thrown open,
   a flanger washed over the whole mix, a tape delay whose time keeps drifting (Caribou).

CoolSynth already covers ingredients 4 and 6 well, and much of 2 and 5. It has no answer yet for
ingredients 1 and 3: there is no pitch-shift effect, no way to chop a sample across the keys, and no
stutter effect. Those are the top three things to build (section 6).

Whether these five share one space, what to call it, and where to look for more references with
some variety in them is in section 9.

## 2. The references one by one

### 2.1 Caribou, "Can't Do Without You" (2014)

**The song.** About 128 BPM (automatic listings say 128 or 129; the key is disputed between
A minor and C major). Snaith calls it one big crescendo. A filtered beat, keys and a short vocal
loop start it. Layers come in blocks of 8 and 16 bars, filters open, a flanger whooshes in, and
around bar 129 the drop arrives: the kick falls away and a buzzy bass and synths take over. It
winds down on reverb and delay. The extended mix runs about 6.5 minutes.
Sources: [Attack deconstruction](https://www.attackmagazine.com/technique/deconstructed/caribou-cant-do-without-you/),
[Dummy interview](https://dmy.co/features/caribou-our-love-interview/),
[klang](https://songs.klang.io/en/s/Caribou/Can't%20Do%20Without%20You/sdwXDAitXGs),
[AcousticBrainz](https://acousticbrainz.org/2388561a-a505-4080-bb67-af2d51187b88).

**Sources and instruments.**
- The first vocal loop is taken from an a cappella of Marvin Gaye's "Ain't That Peculiar". It is
  pitch-shifted. Snaith sings the falsetto "I can't do without you" line and the ad-libs himself.
  There are three vocal loops. ([Attack](https://www.attackmagazine.com/technique/deconstructed/caribou-cant-do-without-you/),
  [Vice](https://www.vice.com/en/article/caribou-suddenly-home-dan-snaith-gloria-barnes-sample/))
- Two live drummers play over a programmed kick, a layered clap and a busy programmed hi-hat
  (triplets and sixteenths). (Attack)
- Keys that sound like a mix of Rhodes and clav. Snaith owns a Fender Rhodes. ([Tape Op](https://tapeop.com/interviews/105/caribou))
- A "walking" synth line, a saw-wave synth, stabby eighth notes in the final build, a sub drone,
  and a buzzy bass whose melody repeats an earlier synth line. (Attack)
- Gear in that era: Roland Juno-106, modular synths, Fender Rhodes, Ableton Live as the DAW.
  Snaith says many songs on the album started on the Juno. ([Tape Op](https://tapeop.com/interviews/105/caribou),
  [Equipboard](https://equipboard.com/pros/caribou)) Owen Pallett arranged strings on the album and
  David Wrench mixed it. ([Exclaim!](https://exclaim.ca/music/article/caribou-bittersweet_symphony),
  [Crack](https://crackmagazine.net/article/long-reads/caribou/2))
- Plug-ins he uses in general, not tied to this song: u-he Diva, Omnisphere, Klanghelm MJUC for
  warmth, Valhalla SpaceModulator for flanging. ([Equipboard](https://equipboard.com/pros/caribou), fan-compiled)

**The effects that define it** (all from the Attack deconstruction, which I checked myself):
- **Filters drive the build.** The beat, vocals and keys start filtered low. The filters are
  bypassed at the drop, which opens the mix at once. Volume, cutoff and resonance are tweaked
  all the time over the vocal loop.
- **A flanger on the whole mix** pushes into the drop. A second, softer flange wash carries the
  track into the final turn.
- **A tape-style delay on the keys whose delay time keeps shifting**, so the repeats wobble in pitch.
- **Spring reverb on the clap**, and slapback delays that shadow the hi-hat in the outro.
- **Automated distortion and tone on the bass**, ending the drop with two "rips" of buzz.
- **Small moves:** keys nudged off the grid for a lazier feel, the hi-hat band-limited in the
  breakdown, the vocal pulled down in the drop.
- **Sidechain pumping is not documented.** No source mentions it, so treat any pumping as a guess.

**Caribou's newer method (*Honey*, 2024).** He changed his own voice with AI voice conversion and
kept his phrasing and breath. Reviewers hear voices of other ages and genders. No source names
the tool. ([JamBase](https://www.jambase.com/article/caribou-album-honey-come-find-me-ai-single),
[Flood](https://floodmagazine.com/176553/caribou-honey))

**Character.** Old soul vocal fragments looped into a hook, live drums against glassy synths, and
filters and sends played by hand into a festival-sized swell.

### 2.2 Loukeman, "Giverrr" (*Sd-2*, 2024)

**The song.** 1:45 long. Shazam lists it as electronic at 77 BPM, written and produced by Loukeman.
Chordify (user-made data) says 150 BPM in A-flat major. **(inferred)** 77 and 150 are probably the
same groove counted in half time and double time. No sample credit was found. A Rate Your Music
listener counts it among the album's near-bedroom-pop tracks, and Last.fm tags it house and
outsider house. **No source describes its instruments or vocal.** **(inferred)** Expect a short,
soft sketch with his own pitched, blurred vocal; he is credited as lyricist.
Sources: [Shazam](https://www.shazam.com/song/1718530713/giverrr),
[Chordify](https://chordify.net/chords/loukeman-songs/giverrr-chords),
[RYM](https://rateyourmusic.com/release/album/loukeman/sd-2/),
[Last.fm](https://www.last.fm/music/Loukeman/_/Giverrr).

**"Real Life Man"** (*Sd-2*, track 13, 4:56): a WhoSampled summary seen in search results says it
samples Bryson Tiller's "Exchange" (the page itself was not readable). PAN M 360 hears thumping but
subdued kicks. ([PAN M 360](https://panm360.com/en/records/loukeman-sd-2-top-albums-of-2024/))

**How he works.** The best source is his MusicRadar interview, which I read in full
([MusicRadar, Plugin Week 2025](https://www.musicradar.com/artists/i-put-a-pitch-shifter-on-the-master-bus-i-love-how-it-glues-everything-together-and-f-cks-everything-up-loukeman-reveals-his-favourite-plugins-and-unusual-production-techniques)):
- **DAW and set-up:** Ableton (he started on FL Studio). Mostly laptop, plus a mixer and a few synths.
- **Synths:** his favourite is the **Oberheim Matrix-6**. On "Gorgeous Stuff" a Matrix patch had an
  ugly crackle that he kept: two takes, saturated, nothing else. His favourite soft synth is the
  **Waldorf PPG Wave 3.V**, which he likes because it is dry, with no built-in reverb or delay.
  **Ableton Operator** is "all over everything", mostly a plain sine or triangle.
- **Drum machines:** **Elektron Analog Rytm MKI** ("Winzzz" is all Rytm except the vocal, with the
  Rytm's master distortion on) and the **Elektron Octatrack**, which he uses to chop samples and run
  synths and guitars through ([Highsnobiety](https://www.highsnobiety.com/p/loukeman-wants-to-wake-up-early-and-make-beats-interview/)).
- **Pitch-shifters are his main tool.** He likes the phase shift they cause, which makes everything
  wide and washed out. On "Idrk" the whole track runs through a pitch-shifter on the master. He
  names a Waves pitch-shifter and Apple's stock **AUPitch**. Its "tightness" control gives a sound
  with no attack, so he puts it on drum loops and prints the result. **(inferred)** AUPitch is
  Apple's time-domain pitch unit; in Apple's developer notes its Tightness and Smoothness run from
  0 to 100 ([Core Audio list](https://lists.apple.com/archives/coreaudio-api/2005/May/msg00144.html)).
- **Saturation everywhere:** always two saturators on the master (Soundtoys Little Radiator and
  Brainworx bx_saturator V2). He saturates whole drum groups. He overloads a reverb on his Allen &
  Heath mixer until it turns to "mushy" saturation; the drums on "Idrk" are pushed into that reverb.
- **Disguised samples:** he writes his own chords first, then forces a sample to fit, which bends
  it out of shape. A descending "synth" on "Brokenheart" is really a filtered vocal through Sonic
  Charge **Permut8**, which he calls a flanger and a filter. Permut8 is a 12-bit delay whose read
  position can be swept, jumped or randomised, so it covers flanging, beat repeats and bit
  crushing ([Sound On Sound](https://www.soundonsound.com/node/4906681),
  [CDM](https://cdm.link/sonic-charge-permut8-all-your-lo-fi-digital-mangling-effects-in-one-place/)).
- **Vocals:** Melodyne's polyphonic mode and AI stem separators pull vocals out of songs. He likes
  the artefacts of both. "Baby You're a Star" re-pitches a replayed Mario vocal.
- **Guitar:** played straight into the computer, then through UAD amp models and iZotope Trash.
- **Wish list:** Roland D-50, Black Corporation Deckard's Dream, "granular stuff".
- He also says he uses bad pitch-shifting and smoothing to blur transients into a washed-out,
  phasey sound ([Mixmag](https://mixmag.net/feature/the-mix-115-loukeman-interview-sd-3)). He
  started out making mash-ups and likes putting a country guitar, an R&B vocal and a house drum
  together ([Billboard Canada](https://ca.billboard.com/music/loukeman-sd3-interview)).

**Character.** Fuzzy, broken beats that sound like they are coming apart. Chopped, pitched pop and
R&B vocals, acoustic guitar, early-2000s dance synths, soft bass and crunchy drums, all degraded on
purpose. ([PAN M 360](https://panm360.com/en/records/loukeman-sd-2-top-albums-of-2024/),
[FADER](https://thefader.com/2024/01/11/loukeman-shares-new-album-sd-2))

### 2.3 Vegyn, "Debold" (*Only Diamonds Cut Diamonds*, 2019)

**The song.** Track 7, 2:43. Vegyn made it alone and calls it his favourite on the album. About
118 BPM in A-flat major (songdata.io, seen only in search results). Reviewers hear a fuzzy groove
with synthetic melodies over a sedated four-on-the-floor beat ([Paste](https://www.pastemagazine.com/music/vegyn/vegyn-only-diamonds-cut-diamonds-review)),
string-laden and close to Aphex Twin ([Paper](https://www.papermag.com/vegyn-only-diamonds-cut-diamonds)),
with glistening synth melodies, skittering percussion and spaced-out harmonics
([twntythree](https://twntythree.com/vegyn-releases-debut-album-only-diamonds-cut-diamonds-shares-new-video-for-track-debold/)).
**No source says whether it has a vocal or which instrument plays the lead.**

**The Mk.gee remix (2020).** It keeps the Aphex feel but moves to a warped, "screwed-up" two-step
beat. The FADER pictures it playing on a pirate-radio transistor radio.
([FADER](https://www.thefader.com/2020/08/13/mkgee-takes-vegyns-debold-to-the-dreamworld-dancefloor-for-a-new-remix),
[Bandcamp](https://vegyn.bandcamp.com/album/odcd-alt-versions))

**How he works.**
- **DAW:** Logic; he says he is fully committed to it ([Talkhouse](https://talkhouse.com/loraine-james-and-vegyn-make-club-music-thats-not-actually-for-the-club)).
  He called the album all software-based ([The Quietus](https://thequietus.com/?p=33895)).
- **Instruments on that album** (fan-compiled from his Instagram, [Equipboard](https://equipboard.com/pros/vegyn)):
  Logic's EXS24 sampler, Alchemy and Retro Synth; Arturia DX7 V, Synclavier V, Prophet V and
  Matrix-12 V. Hardware named elsewhere: Sequential OB-6 and Prophet-5, a Rhodes, a Nord Lead, which
  he praises for warm pads (Talkhouse). Owen Pallett added uncredited strings to the album
  ([Paper](https://www.papermag.com/vegyn-only-diamonds-cut-diamonds)).
- **Source material:** heavy sampling and field recordings, reportedly many made on an old Motorola
  Razr phone ([Wikipedia](https://en.wikipedia.org/wiki/Only_Diamonds_Cut_Diamonds), not confirmed
  in an interview); dial tones, hospital ventilators and snatches of conversation
  ([Crack](https://crackmagazine.net/?p=137007)).
- **Processing:** chopped and screwed vocals and tempo switches inside a track
  ([Mixmag](https://mixmag.net/feature/vegyn-interview-plz-make-it-ruins)); sidechained bleeps and
  fragmented samples ([Pilot](https://pilotmagazine.uk/home/vegyn-interview)); so many plug-ins that
  his computer crashes. No interview names an effect plug-in or a setting.
- **Arranging:** he builds an A loop and a B loop and stacks layers. Then he records 10 to 20 passes
  over the looped track and keeps the best sections. Drums come last, because drums and tempo decide
  the genre. (Talkhouse)

**Character.** Warm synth pads and melodies against field recordings, vocal fragments and skittery,
metallic percussion. Writers call the mood "jubilant melancholia".

### 2.4 ear, "Nerves" (2024) and "Real Life" (2025)

**Who.** An American duo, Jonah Paz and Yaelle Avtan, who met at Bard College in 2024. Labels: Surf
Gang and A24 Music. Critics call their style "laptop twee".
([Wikipedia](https://en.wikipedia.org/wiki/Ear_(band)), [Laptop twee](https://en.wikipedia.org/wiki/Laptop_twee))

**"Nerves".** Their first single, self-released in November 2024; part of it was recorded on
Avtan's iPhone in the Bard library. It opens as a hushed, near-ambient ballad: strings that sound
like horns, whispers, thunder, low rumbles and a short piano part. Halfway through, a bass drops in,
cuts out, then comes back sharper and more synth-like with a new pattern, joined by panned riffs, a
simple beat and vocal ad-libs. The gentle layered vocals turn into chopped snippets. The bass was an
accident: it sat at the side of the session until they dragged it in.
([No Bells](https://nobells.blog/ear-interview-band/),
[Nialler9](https://nialler9.com/who-are-ear-the-laptop-twee-duo-playing-two-dublin-shows-this-month/),
[Bowdoin Orient](https://bowdoinorient.com/2026/05/01/the-new-sound-of-indietronic/))

**"Real Life".** The first track they ever made, and the opener of *The Most Dear and The Future*
(Surf Gang, September 2025), about 2:20 long. The idea was a song built from familiar, everyday
sounds. They sketched its melody on a MacBook keyboard. Shy, gentle vocals over a lilting,
sing-song synth, with stuttering electronic chords underneath that never quite turn dissonant.
Listeners mention a counting metronome near the end. The FADER ranked it in its best songs of 2025.
([No Bells](https://nobells.blog/ear-interview-band/),
[FADER](https://www.thefader.com/2025/12/09/best-songs-2025),
[pi.fyi](https://www.pi.fyi/rec/cmkmwf05w069fsz3jtwd1w0p6)) No tempo or key listing was found for
either song.

**How they work.**
- **Ableton Live, mostly its own sounds and effects.** On "Ceremony" every drum is an Ableton stock
  sound; they like the stereo spread of its drum-synth clap. (No Bells, Wikipedia)
- **The bass:** Paz says it is just a square wave, saturated hard. **(inferred)** Probably
  Ableton's own Saturator; no source names the device.
- **Their own voices,** hushed and mumbled; if a take fails after a try or two they change the
  melody, the pitch or the delivery. The vocals overlap, repeat, drift out of sync, and get chopped
  into stuttering pieces. ([Crack](https://crackmagazine.net/article/profiles/ear-interview-rumspringa/),
  [Ones To Watch](https://onestowatch.com/en/blog/ear-choose-life-in-their-latest-album-rumspringa))
- **Found sound:** phone recordings of their lives, friends' voices, clinking glasses, birds, a
  printer, wind chimes, crunching leaves, rain on a tin roof. Later, film clips and instructional
  tapes. (Crack, [WKNC](https://wknc.org/2026/04/10/album-review-the-most-dear-and-the-future-by-ear/),
  [Stereogum](https://stereogum.com/2514649/an-iphone-is-their-acoustic-guitar/columns/sounding-board))
- **Drums:** clipped drums and amen breaks on the first album, skittering breakbeats later. Jazz
  guitar from Paz, recorded as voice memos. (Crack, Nialler9, Wikipedia)
- **Editing is most of the work:** they build section by section, sometimes bar by bar, and cut
  until a song is only its point. (Crack, No Bells)

**Character.** Quiet, close and breathy, with found-sound collage, broken by sudden bursts of fat,
squelchy square-wave bass. The payoffs are held back on purpose.
([Elie Nis](https://www.elienis.com/ear-interview-jonah-paz-yaelle-avtan-rumspringa/))

### 2.5 meat computer, "healing spiritual echo"

**The song.** On *slept on the floor still dreamt about you (pop edition)*, 2:44
([Dork catalogue](https://readdork.com/artists/meat-computer/tracks)). Listings seen in search
results give about 106 to 107 BPM in C major and a release on 22 January 2026; the pages
themselves would not load. **No source describes how it sounds.** It is not on his Bandcamp.

**What is documented about him.** He makes music in Manitoba. From 2017 he released shoegaze and
bedroom pop with pitched-down vocals as Earthboy; in 2020 he renamed himself and began rapping in a
very high voice over experimental production ([NTS](https://nts.live/artists/131651-meat-computer)).
Highsnobiety calls him a whisper-warbler with an ASMR-like delivery and says the whispering is
natural, not sped up ([Highsnobiety](https://www.highsnobiety.com/p/hyperspeed-rap/)). He mumbles a
freestyle and writes lyrics to it the next day. Beats are often co-produced. He is placed in
hyperpop, experimental and alternative hip-hop ([Bandcamp](https://meat-computer.bandcamp.com),
[felt](https://felt.substack.com/p/meat-computer-meet-computer)). **Nothing is documented about his
DAW, plug-ins, synths or effects.**

**(inferred)** Given his documented traits, expect a high, soft, whispered vocal over glitchy,
washed-out electronic production. To get more than this, the song itself has to be heard.

## 3. What CoolSynth has that matters here

From `docs/design/SOUND_BANK_V3.md`, `docs/interface-v3.md`, the bank files and the engine headers:

- **Generators:** VA (sine, triangle, saw, pulse; unison up to 7, sub), Noise (White, Pink, Band,
  Chip), FM2, FM4, Wavetable (Classic, Digital, Vocal, Chip tables, unison), String, Modal,
  Sample, Grain. The **Sample** generator plays a factory sample, one of 40 recorded instruments,
  or **your own file from the User folder** (D137), as one recording spread over every key: speed
  and pitch change together. The **Grain** generator reads your own file too (its first minute):
  with Scan at 1 the grains move through the file at its own speed while each grain plays at the
  note's pitch, so **pitch and time are independent**. With Combine Live it takes grains from the
  generator before it (D139).
- **Processors per voice:** Ladder (LP24, LP12, BP, HP12, drive), SVF (LP, HP, BP, notch, drive),
  Crusher, Folder, Drive (Soft, Hard, Tube, with tone), Formant (vowels A to U), Comb.
- **Effects, six per layer (I1-I6) and six on the master (F1-F6):** Drive (amount, mix), LoFi
  (wow, flutter, hiss, crackle, bandwidth, bits, rate), Phaser, Flanger (feedback ±0.95), Chorus,
  Delay (synced divisions, Tape or Ping-Pong; repeats get darker through a soft clip and filters),
  Reverb (plate with pre-delay, low cut, modulation, octave shimmer), Compressor (one knob), EQ (low
  and high shelf, tilt), Pump (tempo-synced ducker), Grain (granular on the last four seconds of a
  layer, freeze, ±24 semitones), Room.
- **Modulation:** three envelopes, two LFOs per voice, two global LFOs (tempo sync, eight shapes
  including sample-and-hold and smooth random), a global envelope over bars, a 16-step modulator,
  chaos, random, four macros, a 24-slot matrix with curves (including quantised), and 8 effect
  routes that let global sources move effect settings.
- **Playing:** two layers (Stack, Split, Velocity), chord memory, an arpeggiator with Euclid,
  chance, ratchets and accents, Glide, and Brightness and Space knobs on the whole sound.

**What the code shows is missing for these references:**
- **No pitch-shift effect.** The only shifter is the octave-up inside the reverb's shimmer loop (D50).
- **No way to chop a sample.** A user file plays as one recording over all keys; there are no
  slices, no keymap of several files, no time-stretch in the Sample generator, and no formant control.
- **No stutter or beat-repeat effect.** The Grain effect cannot stand in: it softens replayed
  attacks on purpose (D106), so it no longer stutters.
- **No filter effect.** A layer or the master can be filtered only through the EQ's fixed shelves.
  The per-voice Ladder and SVF do not reach reverb and delay tails.
- **A synced delay cannot wobble:** with Sync on, the Time setting is ignored, so an effect route on
  Time does nothing. Unsynced, a global LFO on Time does wobble it.
- **No spring reverb, no audio input, no vocoder.** The Drive effect has only amount and mix (the
  per-voice Drive has the curves).
- **Factory samples are only Glass, Choir, Strings, Air and Harp** (plus the 40 instruments). No
  voices, whispers, field recordings or breaks.

## 4. Each song mapped to CoolSynth

"Now" means CoolSynth can make it today. "Now~" means a recognisable approximation that one of
the builds in section 6 would improve. The recipes are sketches for preset design, not tested sounds.

| Song | Part | CoolSynth today | Status | Missing |
|---|---|---|---|---|
| Can't Do Without You | Rhodes-clav keys | Layer A: FM tine as in Grid Keys; Layer B: VA pulse clav as in Clav Keys; Stack. Ladder low, a macro or the global envelope opens it over bars. | Now | A sampled Rhodes or clav (none in the Conservatory) |
| | Wobbling tape delay | Delay, Tape mode, Sync **off**, time set by hand (1/8 dotted at 128 BPM is about 352 ms); a global LFO at 0.2-0.4 Hz moves Time through an effect route | Now~ | Wobble on a synced delay |
| | Pitched soul vocal loop | Your own vocal file in Sample (Loop on) or Grain (Scan 1, pitch by key); SVF; Reverb | Now~ | Pitch effect; slices |
| | Buzzy drop bass | VA saw + pulse one octave down, Drive Tube, macro on drive and tone ("rips"), Ladder | Now | |
| | Filters low, then opened at the drop | Brightness knob, or a macro on every cutoff; global envelope over 8 bars | Now~ | A filter effect over the whole layer, tails included |
| | Full-mix flange wash | Master F-slot Flanger, its Mix on a macro through an effect route | Now | |
| | Spring reverb on the clap | Plate or Room only | Missing | Spring reverb |
| Giverrr (and Loukeman generally) | Warm, crackly poly | VA saw ×2 + pulse, Ladder LP24, drift, slow attack; LoFi crackle 0.2 | Now | |
| | PPG-style dry wavetable | Wavetable Digital, no effects | Now~ | Stepped PPG-style tables |
| | Operator sine or triangle | VA sine or triangle | Now | |
| | Pitch-shifter on everything | Closest: slow Chorus + light Flanger for the phasey width; Grain effect at pitch 0 for a smeared copy | Now~ | **Pitch effect** with a "tightness" (window) control |
| | Two saturators on the master | F-slots: Drive + Drive + Compressor | Now | A saturator with colour (tape, tube) |
| | Reverb overloaded into mush | Effect order Reverb → Drive (high amount) | Now | |
| | Vocal disguised as a synth | Your vocal file in Grain → SVF band-pass → Flanger → Reverb | Now~ | Pitch effect; filter effect |
| Debold | Bright synth melody | VA saw + pulse or FM2 lead, light Drive, LoFi bits 12, Delay 1/8 dotted synced | Now | |
| | Strings | Layer B: Conservatory Violins or OB Strings | Now | |
| | Sidechained bleeps | Pump at 1/4 on a pluck | Now | |
| | Skittering percussion | Noise (Chip) through Ladder with a short envelope, arpeggiator with ratchets and chance | Now~ | |
| | Mk.gee remix: pirate radio | LoFi bandwidth 0.35, wow 0.4, hiss; EQ low and high cut; Drive | Now | |
| Nerves | Saturated square bass | VA pulse 50 % one octave down → Drive Hard, high drive → Ladder LP24; mono, short envelope; mod wheel on drive | Now | |
| | Whispers | Noise Band → Formant, vowel drifting on smooth random → SVF high-pass → Reverb | Now | Your own voice: audio input or file |
| | Piano and horn-like strings | Conservatory Steinway or upright; strings with a Formant touch | Now | |
| | Thunder and rumble | Noise Pink → Ladder 200 Hz, slow attack, long Reverb; or your own recording in Sample | Now | |
| | Chopped vocal stutter | Step modulator to VCA on a vocal file | Now~ | **Slices; stutter effect** |
| Real Life | Lilting, sing-song synth | VA pulse lead, light vibrato (as Coin Pulse), short Delay | Now | |
| | Stuttering chords | Chord memory + Step modulator to VCA with gaps, as in Gated Pad | Now~ | Stutter effect (repeat what just played) |
| | Counting metronome, found sounds | Your own recordings in Sample | Now~ | Slices or a keymap of several files |
| healing spiritual echo | High, whispered voice | Your vocal file in Sample played 5 to 12 semitones above its root (it gets thin and chipmunk-like) | Now~ | Pitch with formant control |
| | Glitch, echo | Delay synced, LoFi bits, Pump | Now~ | Stutter effect |

Drums appear in every one of these songs (amen breaks, Analog Rytm kits, live drums). CoolSynth
is a synth and has no drum part. The slice mode in section 6 would let a layer play a chopped
break, which covers the ear and Vegyn cases.

Samples from records (Marvin Gaye, Mario, Bryson Tiller) cannot ship with CoolSynth. In CoolSynth
they would be your own files, or recordings made for the factory bank.

## 5. Factory presets that can be designed now

These are proposals. The names follow the bank's rules (short, no brands) and are yours to change.
They could form a new room for this style, or go into Bedroom, Main Stage and Workshop.

1. **Square Grit** (ear, "Nerves"): the saturated square bass from the table. Macros: drive, tone,
   decay, wheel to drive. *Now.*
2. **Whisper Air** (ear, meat computer): band noise through a wandering Formant, high-passed, in a
   soft plate. *Now.*
3. **Tine Clav** (Caribou keys): FM tine layer plus pulse clav layer, a low Ladder that a macro
   opens, and an unsynced tape delay wobbled by a global LFO. *Now.*
4. **Buzz Drop** (Caribou bass): saw and pulse, Tube drive and tone on a macro for the rips. *Now.*
5. **Washed Poly** (Loukeman): warm two-saw poly with drift and a little crackle, slow Chorus and a
   light Flanger, Reverb into Drive, two Drives and a Compressor on the master. *Now~*; it gets its
   real character when the Pitch effect exists.
6. **Fizz Melody** (Vegyn, "Debold"): bright synth lead with light fuzz and 12-bit grit, Pump at 1/4,
   dotted-eighth delay; violins as layer B, low. *Now.*
7. **Pirate Radio** (Mk.gee remix): a narrow, wobbly, hissy transistor-radio chain. *Now.*
8. **Stutter Chords** (ear, "Real Life"): chord memory, pulse and saw, the Step modulator gating
   the VCA in an uneven pattern. *Now~*; better with the stutter effect.
9. **Vocal Mask** (Workshop template): Grain on a User file (Scan 1, pitch by key), SVF band-pass,
   Flanger, Reverb into Drive. Silent until you name a file. *Now.*
10. **High Voice** (Workshop template): Sample on a User file, played above its root, Delay and
    Drive. *Now~*; better with formant control.

## 6. What to build, in order

1. **A Pitch effect** (a new effect type for layers and the master). Pitch ±24 semitones and fine
   cents; a window or "tightness" control (short windows blur attacks into the soft, attack-less
   sound Loukeman uses on drums); a character control from clean to deliberately rough (fewer
   heads, harder crossfades); stereo detune (left and right shifted opposite ways, for the wide,
   phasey wash); feedback; mix. **Why first:** it is the most-named effect across the references
   (Loukeman's main tool, Caribou's vocal loop, Vegyn's screwed vocals, meat computer's high voice),
   and it works on synths as well as voices. The two-head delay-line shifter behind the shimmer
   (D50) is a starting point, and its slight warble suits this style.
2. **Slices for the Sample generator.** Cut a User file into equal slices or at its attacks, one
   slice per key from a root key, one-shot, with a choke option; and a keymap of several files.
   **Why:** chopped vocals are in all five references, and slices also cover drum breaks (ear's amen
   breaks, Vegyn's loops) and found-sound kits (ear, Vegyn). Without it a vocal file can only play
   as one pitched recording.
3. **A stutter effect** (a new effect type): on the tempo grid, by chance or held from a macro or
   the mod wheel, it catches the last 1/4 to 1/64 of what played and repeats it, with gate, decay,
   a pitch drop per repeat and mix. **Why:** stuttering vocals and chords define both ear songs;
   glitch edits run through Vegyn and meat computer; Permut8's repeats are part of Loukeman's sound.
   The Grain effect cannot do this by design (D106).
4. **A filter effect:** one knob from low-pass through open to high-pass, resonance, and SVF modes,
   movable by effect routes. **Why:** Caribou's build depends on filtering whole parts, reverb
   included, and throwing the filters open at the drop. Loukeman masks vocals with a filter. The EQ
   cannot do it.
5. **Formant control when pitching a sample** (in the Pitch effect or the Sample generator).
   **Why:** a high voice without the chipmunk sound (meat computer), and voices of other ages and
   genders (Caribou's *Honey*, which used AI voice conversion; a formant shift is the
   non-AI cousin). It is more work, and quality matters here, so it comes after 1 to 4.
6. **Audio input into a layer:** sing or play into CoolSynth's effects and grains. **Why:** ear,
   Caribou and meat computer build on their own voices. **Cost:** large. The plugin would need an
   input bus and the standalone app an input device, and it changes what CoolSynth is.
7. **Smaller things:** let a synced delay wobble around its division (Caribou's keys); a spring
   reverb type (Caribou's clap); a saturator effect with colour and tone (Loukeman's master chain).
8. **Factory sources:** record a small licence-free set of voices (phrases, whispers, breaths),
   field recordings (glass, birds, rain, phone noise) and one break, so the presets above work
   without your own files.

Lower priority: stepped PPG-style wavetables (Loukeman's favourite soft synth), a Matrix-6 or
Juno-106 voicing for the Museum, a sampled Rhodes and clav for the Conservatory.

**The top three: the Pitch effect, sample slices, the stutter effect.** The four corners researched
later (section 9.7) keep this top three, move the saturator with colour up to fifth, and add a
Tune mode to the Pitch effect.

## 7. Choices for you

**Decided by you on 2026-11-15:**
- **Build first: the Pitch effect** (then sample slices, then the stutter effect; its Tune mode
  follows the plain shift, section 9.7). **Built** the same day (D205): Heads and Spectral modes,
  with a Delay and Feedback for the stepped echoes.
- **Room plan: one new room** for the whole space, with its variety inside it (sections 5, 9.5
  and 9.7 list the proposed presets). **Built** the same day (D206): Night Bus, all sixteen
  presets, the Pitch effect in each; Vocal Mask and High Voice play from factory sound.
- **Slices** built the same day (D207): Equal or at attacks, one per key, played to their end, and a
  folder of files as a kit.
- **Corners to research:** all four, now in section 9.6.

The options as they were put:

- **Build order.** Recommended: 1 Pitch, 2 Slices, 3 Stutter. If you build Slices first, vocal and
  break presets become possible sooner, but every chopped vocal still lacks the washed-out
  pitch-shift sound. If you build Stutter first, the ear-style presets improve first.
- **Where the presets go.** A new room for this style keeps it recognisable, in line with the bank's
  rule that every room is a place. Spreading the presets into Bedroom, Main Stage and Workshop means
  no new room, but the style is diluted.
- **Factory voices and field recordings.** If you record them, the presets work for anyone, at the
  cost of a recording session. If you leave them to the User folder, they cost nothing, but presets
  like Vocal Mask stay silent until a file is named.
- **One room or two** (section 9). One room for the whole space keeps the variety inside it. Two
  rooms, a club side and a bedroom side, are each easier to recognise in five seconds, but each
  holds less variety.
- **Which corners of the map to research next:** decided on 2026-11-15. You chose all four, and
  they are in section 9.6.

## 8. Gaps in this research

- I could not listen to the songs, so no description here is first-hand. Telling me what you like
  in each one (the bass, the vocal, the stutter, the space) would sharpen the recipes.
- "healing spiritual echo" and "Giverrr" have no published description at all. Their recipes rest
  on each artist's documented habits.
- Pitchfork's ear feature, the DJ Mag deconstruction of "Can't Do Without You", WhoSampled and the
  Shazam pages would not load. Tempos and keys come from automatic listings and are not checked.
- No source documents sidechain pumping on "Can't Do Without You".

## 9. The space you are in, and where to look next

### 9.1 Is there a common thread?

Yes, but it is a way of making music rather than one genre. **(inferred: this is my synthesis of
the sources in sections 2 and 9.2)** All five make music mostly on a laptop, alone or in a pair,
out of pieces of other things: borrowed or home-recorded voices, found sounds and plain stock
synths. They wear the result down on purpose and lay a soft, sincere feeling over a dance beat,
or over no beat at all. In detail, they share:

- **A voice used as texture, not as a polished lead:** pitched, whispered, chopped or half buried.
- **Plain synths:** squares, saws, sines, warm polysynths and stock DAW instruments.
- **Wear on purpose:** saturation, cheap pitch-shifting, crackle, low bit depth.
- **Editing as the main craft:** loops cut, stacked and rearranged, bar by bar.
- **Tenderness without irony.** Vegyn's mood is described as "jubilant melancholia" (section 2.3).
  The writer who named "laptop twee" sets it against irony-heavy indie
  ([Welcome.jpeg](https://www.welcomejpeg.com/p/the-rise-of-laptop-twee)). Loukeman calls a
  PinkPantheress song he loves "perfectly emo"
  ([Wonderland](https://www.wonderlandmagazine.com/2026/04/28/loukeman-on-repeat/)).

### 9.2 Is there a name for it?

No single genre name covers all five. Each act sits in a neighbouring scene, and the scenes touch.

| Act | Scene the sources put it in | Neighbours (from listener-similarity lists and articles) |
|---|---|---|
| Caribou | Indietronica; Last.fm's top tags include "poptron". The oldest of the five (2014). | Four Tet, Daphni (Snaith's own club alias), Hot Chip, Gold Panda ([Last.fm](https://www.last.fm/music/Caribou)) |
| Loukeman | Outsider house, lo-fi house | Delroy Edwards, Ross From Friends, DJ Seinfeld, Anthony Naples, Jump Source, Cult Member, SASSY 009 ([Last.fm](https://www.last.fm/music/Loukeman/+similar)) |
| Vegyn | Experimental electronic around his PLZ label and the Frank Ocean circle | Mount Kimbie, John Glacier, Oli XL, James K, Four Tet, Jai Paul, Astrid Sonne, ML Buch ([Last.fm](https://www.last.fm/music/Vegyn/+similar)) |
| ear | Laptop twee: indie and twee pop made in a DAW, glitchy, with breakbeats and bitcrush | Bassvictim, Worldpeace DMT, Frost Children, Feeble Little Horse ([Wikipedia](https://en.wikipedia.org/wiki/Laptop_twee), [Welcome.jpeg](https://www.welcomejpeg.com/p/the-rise-of-laptop-twee)); ML Buch, James K, Astrid Sonne, Ana Roxanne ([Last.fm](https://www.last.fm/music/ear/+similar), page not confirmed as the duo's) |
| meat computer | Digicore, cloud rap, hyperpop | d0llywood1, oaf1, capoxxo, emorave, Bladee and Ecco2k ([Last.fm](https://www.last.fm/music/meat+computer/+similar)) |

Where they touch:
- Vegyn released a Loukeman remix of his "Last Night I Dreamt I Was Alone"
  ([Bandcamp](https://vegyn.bandcamp.com/music)). The FADER says Vegyn is a fan who invited Loukeman
  to perform ([FADER](https://thefader.com/2024/01/11/loukeman-shares-new-album-sd-2)). A reviewer
  compares Loukeman's "Idrk" to Vegyn ([PAN M 360](https://panm360.com/en/records/loukeman-sd-2-top-albums-of-2024/)).
- Four Tet appears beside both Caribou and Vegyn. Snaith counts Four Tet among his closest musical
  advisers ([Line of Best Fit](https://thelineofbestfit.com/features/interviews/caribou-interview-2014)).
- Laptop twee is described as hyperpop maximalism joined to the optimism of 2000s indie bands
  ([Wikipedia](https://en.wikipedia.org/wiki/Laptop_twee), citing Pitchfork). That puts ear next
  to meat computer's hyperpop side. Yung Lean has cosigned ear
  ([Welcome.jpeg](https://www.welcomejpeg.com/p/the-rise-of-laptop-twee)).
- Vegyn's and ear's listener lists share ML Buch, James K and Astrid Sonne.

**(inferred)** A working name for the room could be "the laptop collage space": soft,
sample-built, deliberately worn electronic music that sits between the club and the bedroom.

### 9.3 There is real variety in it

The space is wide, and that is good for a room. **(inferred)** Three directions separate the songs:

1. **Club or bedroom:** a four-on-the-floor crescendo, or a whisper with no beat.
2. **Warm or broken:** clean, glassy and warm, or saturated, crackling and smeared.
3. **Borrowed or own voice:** someone else's record chopped up, or the artists' own voices.

| Song | Club or bedroom | Warm or broken | Voice |
|---|---|---|---|
| Can't Do Without You | Club (128 BPM festival build) | Warm | Borrowed (Marvin Gaye) and his own |
| Debold | Club-leaning (four on the floor, laid back) | Warm, a little fuzzy | Not documented |
| Giverrr | Between the two (short, near bedroom pop) | Broken | Probably his own (inferred) |
| Real Life | Between the two | Broken (stuttering chords) | Their own |
| Nerves | Bedroom, then club in its second half | Hushed, then broken (saturated bass) | Their own |
| healing spiritual echo | Bedroom (inferred) | Fragile (inferred) | His own |

### 9.4 Where to look next, by corner

Each corner adds different ingredients. The artists below come from the listener-similarity lists
and articles in 9.2, and from Loukeman's own favourites
([Wonderland](https://www.wonderlandmagazine.com/2026/04/28/loukeman-on-repeat/)). **I have not
researched how any of them produce;** that is the next step for whichever corners you pick.

| Corner | Artists to try | What it would add to CoolSynth (inferred) |
|---|---|---|
| Warm club, the older generation | Four Tet, Daphni, Gold Panda, Hot Chip | Chopped instrument samples, clean vocal house, long builds |
| Rough house (Loukeman's side) | Delroy Edwards (one of the first who made Loukeman want to make music), Ross From Friends, DJ Seinfeld, Anthony Naples, Jump Source | Tape-worn drums, filtered loops, dance focus |
| Wonky and half-spoken (Vegyn's side) | Mount Kimbie, John Glacier, Oli XL, Jai Paul, James K | Off-grid beats, spoken vocals, heavy bass |
| Ambient bedroom | ML Buch, Astrid Sonne, Ana Roxanne, Huerco S. (a Loukeman favourite for its pad and repeating bass line) | Pads, drones, no drums |
| Laptop twee (ear's side) | Bassvictim, Worldpeace DMT, Frost Children, Feeble Little Horse | Guitars, bitcrush, breakbeats |
| Digicore and voice (meat computer's side) | d0llywood1, Bladee and Ecco2k, emorave | Hard-tuned vocals, trap drums, huge reverbs |
| Loukeman's own favourites | PinkPantheress, araabMUZIK, The Avalanches | Sample-pop hooks, hard sampled drums, warm acoustic collage |

### 9.5 What this means for a CoolSynth room

- **A signature for the five-second test** (the bank's first rule) **(inferred):** made on a laptop
  out of pieces of other things. A voice turned into texture, everything a little worn, warm
  underneath.
- **Variety comes from role and position, not from new tricks.** The ten presets in section 5 cover
  bass, keys, pad, lead, chords, a voice texture and effect chains, and spread across the map:
  Buzz Drop, Tine Clav and Fizz Melody at the warm club end; Square Grit, Washed Poly and Stutter
  Chords in the broken middle; Whisper Air and High Voice at the bedroom end; Vocal Mask on a
  borrowed voice; Pirate Radio as a chain for any of them.
- **Two corners have no preset yet:**
  - **Ambient bedroom:** a slow pad over a repeating bass line, with tape wow and a long reverb.
    Buildable now.
  - **Digicore:** a hard-tuned vocal. This needs a pitch-correction effect, a larger build than
    anything in section 6.

### 9.6 The four corners, researched

You chose all four corners on 2026-11-15. Each was researched online, as the five songs were.
Production details are documented unevenly: well for Four Tet, Mount Kimbie, Ross From Friends and
Jump Source, hardly at all for Jai Paul, John Glacier, Bassvictim and d0llywood1. Pitchfork could not
be read directly, so its scores come through Wikipedia. Sources for every row are listed under
"The four corners" in section 10. **(inferred)** marks my reading.

**Ambient bedroom**

| Artist | Songs to hear | How it is made | New ingredient, and CoolSynth |
|---|---|---|---|
| ML Buch | "Pan Over the Hill", "Flames Shards Goo" (*Suntub*, 2023; Pitchfork 8.0, high on many best-of lists) | Seven-string guitar in open tunings with bends and slides, fretless bass, many virtual guitars, guitar turned into MIDI by plug-ins that misread it (she keeps the mistakes). Vocals sung in her parked car by the sea for the sound of the glass. Guitar re-amped through the car stereo, recorded in saunas, pools and tall grass. Three years of wind, leaves and a creek recordings as "glue". | Elastic, bent guitar through a small speaker in a small glass room. CoolSynth: String with Glide, Room, Drive. **(inferred)** Missing: a small-speaker colour (band-limited cabinet). |
| Astrid Sonne | "Do you wanna (have a baby)", "Staying here" (*Great Doubt*, 2024; the Guardian's pick of the album) | Ableton Live and an OP-1, her viola, an Eventide H9 on stage, modular synths at EMS Stockholm. Drums recorded in a room and hallway with several mics, turned into her own sample bank. Much sampling of her own viola, voice and synths, back and forth between audio and MIDI. "Staying here": a guitar tutorial turned into MIDI and arpeggiated. | Real bowed strings and hymn-like piano against hard, dry, sampled drums. CoolSynth: Conservatory Violas, Cellos and pianos, the arpeggiator. Missing: slices for her drum bank. |
| Ana Roxanne | "Camille" (*Because of a Flower*, 2020; Metacritic 83), "It's a Rainy Day on the Cosmic Shore" (*~~~*, 2015) | A Moog IIIP; she began by looping her voice in a lot of reverb on a borrowed loop pedal. Hindustani classical training. Her voice stacked into a one-person choir, reverb-heavy synth harmonies, tape hiss, field recordings (tide, wind chimes, thunder), a French film clip on "Camille". | Your own voice looped into a choir inside a very long reverb. CoolSynth: Choir sample, Formant, Reverb, LoFi. Missing: audio input with a looper (item 6), factory voices (item 8); **(inferred)** a longer hall than the plate. |
| Huerco S. | "A Sea of Love" (opens *For Those of You Who Have Never*, 2016; Pitchfork 8.1, in its 50 best ambient albums) | Hour-long jams on one synth, the good parts cut out, looped and resampled; no outside samples. Two laptops, no audio interface, recorded into Audacity. Reviewers hear tape warble, wide detuning, digital haze and buried "ghosts of rhythm". | One long, static loop of your own playing, worn by warble and detune, with a dub pulse far below. CoolSynth: VA unison detune, LoFi wow, flutter and bits, Delay (Tape), Reverb, Pump, all **now**. A user file loops only on the loop stored in the file. |

**Rough house (Loukeman's side)**

| Artist | Songs to hear | How it is made | New ingredient, and CoolSynth |
|---|---|---|---|
| Delroy Edwards | "I Love Sloane" (1:16, *Hangin' At The Beach*, 2016; a Loukeman favourite), "4 Club Use Only" (2012) | Recorded on a Tascam Portastudio four-track; cheap synths and drum machines (Equipboard lists an Alpha Juno-1, Pro-One, Sequential TOM, TR-8). He avoids recording on the computer. Chopped bits of his own cassettes, slowed down and printed to tape. Dialogue samples, such as a 911 call. | The four-track tape as the whole mix: grain, crackle, a narrow top. CoolSynth: LoFi on the master, Sample's linked speed and pitch for slowed samples. Missing: drum kits through slices (item 2), a tape-style saturator (item 7). |
| Ross From Friends | "Talk To Me You'll Understand" (2015; about 19 million YouTube plays, the lo-fi house breakout) | GarageBand, then Logic and Ableton. Samples fished from YouTube, liked best when ripped and compressed several times over. Later a Prophet module, OB-6, Eurorack, a cheap Sony tape recorder, a glitch delay, and his own Max device that records everything he plays so he can sample himself. He layered his own spoken phrase over the track. | Codec damage as a sound, and sampling yourself. **(inferred)** A "ripped" codec mode for LoFi or Crusher would be new. Self-sampling needs audio input (item 6); the glitch delay fits the stutter effect (item 3). |
| DJ Seinfeld | "U" (2017; his breakout, 126 BPM) | "Just Ableton" and a few plug-ins (Serum, Omnisphere). YouTube clips converted to MIDI for melodies. "U" was made in a couple of hours: an old piano sequence plus a Bob Geldof monologue found by searching YouTube for sad monologues. Reviewers hear crunchy drums, a mist of distortion, imitation tape hiss and a Korg M1 organ. | A sad piano loop under a spoken voice, plain house drums. CoolSynth: Conservatory pianos, Drive, LoFi. Missing: factory spoken phrases and organ, piano and bass sounds in the 1990s workstation style (item 8). |
| Anthony Naples | "Mad Disrespect" (2012, 120 BPM; Mister Saturday Night's first release, on RA's year-end list) | Ableton from about 2006; his first records were Logic and plug-ins only (a MusicRadar snippet; sources conflict). Sampled records (WhoSampled lists Pharrell and Kanye, Roy Ayers, Leftfield). Reviewers hear dusty chords and a lo-fi piano-and-strings loop filtered into the foreground. | Filtered loops of sampled records, mastered loud. Missing: the filter effect (item 4); **(inferred)** a limiter or clipper beyond the one-knob Compressor. |
| Jump Source (Patrick Holland, Loukeman's co-producer, and Priori) | "Affect" (feat. Loukeman, *Fold*, 2026), *Guide To Action* (2016, reviewed by RA) | A 24-channel desk, Waldorf Micro Q, Roland JV-1080, Oberheim Matrix 1000, MPC2000XL, Eventide H3000 and H9, Alesis Midiverb II. Ableton for sequencing; effect sends through a patch bay, so parts are printed with live effect moves. Parts run through preamps that each distort differently, and are reamped through guitar amps. Dub runs through their work. | Dub effect throws played by hand, and a different saturation on each part. CoolSynth: Delay (Tape) with high feedback on a macro, per-layer Drive, effect routes, all **now**. The H3000's character points at the Pitch effect (item 1). |

The lo-fi house recipe of 2016 to 2018, as writers describe it: 1990s deep house as if recorded to
cassette, minor-seventh pads, muffled or distorted drums under an overblown kick, cassette hiss and
tape's narrow top, pitched-down samples heavy with reverb, R&B diva vocals, spoken word from films
or YouTube, and the sounds of the 808, 909, 303, DX7 and M1 ([Outsider house](https://en.wikipedia.org/wiki/Outsider_house),
[HighClouds](https://highclouds.org/?p=29579),
[Vice](https://www.vice.com/en/article/lo-fi-house-youtube-related-video-algorithm-essay)).

**Vegyn and warm club**

| Artist | Songs to hear | How it is made | New ingredient, and CoolSynth |
|---|---|---|---|
| Four Tet | "Baby" (*Sixteen Oceans*, 2020, about 127 BPM), "Only Human" (as KH, 2019) | Ableton's arrange view, mostly with the mouse; no sampler. Omnisphere, some modular, an old four-track for loops. A sample folder kept since the late 1990s: field recordings (cracking ice), his own guitar through a Boss distortion pedal. About ten loops from each drum machine, then he stops using it. He says he mostly uses presets. "Only Human" is a Nelly Furtado vocal, sped up and looped. He copies a melody to a second synth and reverses or shortens it, so the two answer each other. | Two parts that answer each other. CoolSynth's two layers fit; **(inferred)** a per-layer reverse or shortened copy of the notes would be new. A sped-up vocal loop works now in Sample. His sampler's gate trick needs the stutter effect. |
| Daphni (Dan Snaith's club alias) | "Cherry" (2022), "Ne Noya" (*Jiaolong*, 2012) | Tracks made in an afternoon for that weekend's DJ sets, on a modular synth he built himself; takes are final. A looped phrase from an old African or Middle Eastern record against a modular bass or lead. "Cherry" is, in his words, a spiralling polyrhythm on an FM synth. | A pattern whose length does not fit the bar, so it keeps turning over. CoolSynth **now**: FM2 or FM4 with the Step modulator set to 5 or 7 steps, or a Euclid pattern (up to 16 steps). A record loop on layer A needs slices. |
| Mount Kimbie | "Carbonated" (*Crooks & Lovers*, 2010), "Made to Stray" (2013) | Early work almost all in Fruity Loops with its stock drums and few synths. Field recordings in a 50-metre wind tunnel, under bridges and in Southwark Cathedral, kept with the room's ring. Later a Dave Smith drum machine, an out-of-tune cartridge drum machine, a harmonium, a Juno. Female vocals cut up, pitched up high and played "more like a keyboard". Live: loop pedals and a Kaoss Pad. | The room a sound was recorded in, and a vocal played as a keyboard. CoolSynth: Comb, Room and Reverb for the ring; slices to map a vocal over the keys; factory field recordings (item 8). |
| Jai Paul | "BTSTU" (2011; sampled by Drake and Beyoncé), "Jasmine" (2012) | Recorded at home; he plays and mixes everything, his brother A. K. Paul plays bass and designs sounds. Equipboard (unreliable) lists Logic and a Yamaha CS1x. Samples from Bollywood soundtracks, R&B, film and game clips. Critics hear fuzzed-out synths, vocals almost buried, heavy echo on claps, tuned-up vocals. | A loud, over-driven pop mix. **(inferred)** Heavy compression and saturation on the whole mix. Missing: a saturator with colour (item 7) and more Compressor control; the Pitch effect for the tuned-up voice. |
| John Glacier | "If Anything" (2021, produced by Vegyn), "Found" (*Like a Ribbon*, 2025; Pitchfork 8.2) | A poet who began posting her own beats and spoken demos in 2018; on her first album other producers sent beats and she recorded in Vegyn's studio. Kwes Darko produced *Like a Ribbon*. Reviewers hear her voice warped and manipulated, and glitchy, reverbed pianos. | A flat, spoken voice as the lead. Missing: audio input (item 6) or factory spoken phrases. Pianos through Reverb and the stutter effect. |

**Voice-led: laptop twee and digicore**

| Artist | Songs to hear | How it is made | New ingredient, and CoolSynth |
|---|---|---|---|
| Bassvictim | "Air on a G String" (2023, 123 BPM), "Grow Up" (*Forever*, Pitchfork 7.5) | The first beat was made with his mouth, the singer toasting over it. The latest album is laptop only, made in a bedroom. Takes are rarely revised. Writers hear heavy wubs and booms, folk-leaning piano, clanging percussion, howling synths. No DAW or plug-in is named. | A dry, deadpan voice over a pounding electro bass. CoolSynth: VA bass, Drive, Pump. Missing: a beatboxed beat needs slices or audio input. |
| Worldpeace DMT | "Love Yourself" (*The Velvet Underground & Rowan*, 2025; the album "laptop twee" was coined for) | All recorded alone in his room, reportedly Ableton with MIDI strings. MIDI cello, an "underwater" vocoder on backing vocals, stuttered vocals without Auto-Tune, a "bitpop" drop, chopped acoustic chords flung across the stereo field, glockenspiel, steel pan, a barking dog. | MIDI-orchestra cuteness and a vocoder. CoolSynth: the 40 recorded instruments, Modal for bells and steel pan, Crusher. Missing: a vocoder; the stutter effect. |
| Frost Children | "Flatline" (2023, a pounding, squelching house beat), "All I Got" (130 BPM) | Started on FL Studio, now Ableton with a cap on layers per project. A closet studio; mastered on laptop speakers. Two voices sing at once over EDM, hardstyle and bloghouse drops; songs start acoustic and are turned into dance tracks. | Big sing-along drops at 130 to 165 BPM. CoolSynth **now**: VA with seven-voice unison, Pump, Drive (as in Supersaw and Future Chord). |
| Bladee and Ecco2k, with Whitearmor and Gud (Drain Gang) | Bladee, "Be Nice 2 Me" (2018, about 139 BPM); Bladee and Ecco2k, "The Flag Is Raised" (*Crest*, 2022, Pitchfork 8.0); Ecco2k, "Peroxide" (2019, 165 BPM) | Whitearmor: Ableton, a Yamaha CS1x, an Access Virus TI, once an SP-404. Gud: GarageBand, then FL Studio, then Ableton; Massive, Reaktor, Kontakt, an Octatrack; Soundtoys Decapitator on 808 kicks, iZotope Trash on snares. A TC-Helicon VoiceLive 2 for live Auto-Tune. The group began as friends who wanted to sing in Auto-Tune. Ecco2k's voice is a real falsetto, tuned. | The hard-tuned voice, and bright trance pads and bell leads. CoolSynth **now**: VA or FM4 with Chorus and the Reverb's shimmer; an 808 from a pitch-dropping sine into Drive or Folder (as 808 Boom). Missing: a Tune mode (below). |
| d0llywood1 | "nonchalant" (2020, picked by Pitchfork's "The Ones"; her 2024 mixtape made the FADER's best albums of the year) | Produces some tracks herself; the scene largely learned from YouTube tutorials and a cracked FL Studio. She says distortion is what brings the texture out, with the bass up and the vocals distorted. Auto-Tune, and sound effects such as gunshots. | A distorted, tuned voice over overdriven 808s. Missing: a Tune mode, a saturator with colour (item 7), factory sound effects (item 8). |

**What a hard-tuned voice needs, technically** (from [Antares](https://antarestech.com/blog/pitch-correction-the-complete-guide-to-tuning-vocals)
and [Wikipedia](https://en.wikipedia.org/wiki/Auto-Tune)): a pitch detector that finds the voice's
fundamental within a few milliseconds from a clean, dry, single voice (reverb and noise break the
tracking); a snap of every note to the nearest note of a chosen key and scale, or to the nearest
semitone; a retune speed of 0 to 10 ms for the hard, stepped effect (15 to 25 ms is typical for
melodic rap, 25 to 50 ms for invisible correction); humanising off; and a separate formant control.
**(inferred)** In CoolSynth this is a Tune mode of the planned Pitch effect, working on a single
voice: a vocal file in Sample or Grain, or audio input. On a synth the stepped feel already exists:
the matrix's quantised curve on pitch, with no Glide.

### 9.7 What the corners change

**The top three stay the same: the Pitch effect, sample slices, the stutter effect.** Every corner
asks for at least two of them. What moves (section 6's numbering):

- **The saturator with colour (item 7) moves up to fifth.** It is named in every corner: Delroy
  Edwards' four-track, Jump Source's different preamps, Jai Paul's loud mix, Gud's Decapitator on
  808s, d0llywood1's distorted voice. Add a limiter or clipper to it **(inferred)**, for the mixes
  that are mastered loud (Anthony Naples, Jai Paul).
- **The Pitch effect grows a Tune mode** as its second stage, after the plain shift, for the Drain
  Gang and digicore voice. **Formant control (item 5)** belongs with it (Ecco2k's falsetto, meat
  computer's high voice).
- **Audio input (item 6) gains weight:** Ana Roxanne's looped choir, Ross From Friends sampling
  himself, John Glacier's spoken lead, ML Buch's car-glass vocals. It is still the largest build.
- **Factory sources (item 8) get a clearer list:** spoken phrases and monologues, breaths and
  whispers, field recordings (wind, a creek, rain, a room's ring), a breakbeat, and organ, piano and
  bass sounds in the 1990s workstation style.
- **New, lower down:** a vocoder (Worldpeace DMT); **(inferred)** a codec or "ripped" mode in LoFi
  (Ross From Friends); **(inferred)** a small-speaker colour (ML Buch); **(inferred)** a longer
  hall reverb (Ana Roxanne); **(inferred)** a per-layer reversed or shortened copy of the notes
  (Four Tet).

**More presets, one per corner, buildable now** (proposals, as in section 5; they widen the map
rather than repeat it):

1. **Sea Pad** (ambient bedroom, after Huerco S.): a wide detuned VA saw pad, a low Ladder, slow
   attack, a soft sine bass repeating on the arpeggiator in layer B, LoFi wow and 12-bit grit, a
   long tape delay and a large plate.
2. **Four-Track House** (rough house): a minor-seventh chord from chord memory on saw and pulse,
   a low Ladder, LoFi with a narrow top, hiss and a little wow, Drive, Pump at 1/4.
3. **Spiral FM** (warm club, after Daphni): an FM2 pluck whose pitch the Step modulator plays over
   7 steps against the bar, quantised to semitones, with warm Drive and a dotted delay.
4. **Trance Glass** (digicore): a seven-voice VA saw stack with an FM bell on top, Chorus, and a
   plate with shimmer.
5. **Bowed Haze** (ambient bedroom, after Astrid Sonne): Conservatory Violas through Drive and a
   small Room, slow attack.
6. **Stepped Lead** (digicore, for a synth only): a VA lead with the quantised pitch curve and no
   Glide, so it jumps like a hard-tuned voice.

## 10. Sources

Caribou
- https://www.attackmagazine.com/technique/deconstructed/caribou-cant-do-without-you/ (read in full)
- https://www.vice.com/en/article/caribou-suddenly-home-dan-snaith-gloria-barnes-sample/
- https://tapeop.com/interviews/105/caribou
- https://equipboard.com/pros/caribou
- https://dmy.co/features/caribou-our-love-interview/
- https://crackmagazine.net/article/long-reads/caribou/2
- https://www.thefader.com/2014/09/11/interview-caribou
- https://exclaim.ca/music/article/caribou-bittersweet_symphony
- https://thelineofbestfit.com/features/interviews/caribou-interview-2014
- https://songs.klang.io/en/s/Caribou/Can't%20Do%20Without%20You/sdwXDAitXGs
- https://acousticbrainz.org/2388561a-a505-4080-bb67-af2d51187b88
- https://www.jambase.com/article/caribou-album-honey-come-find-me-ai-single
- https://floodmagazine.com/176553/caribou-honey
- Not readable: https://djmag.com/content/caribou-cant-do-without-you-deconstruction

Loukeman
- https://www.musicradar.com/artists/i-put-a-pitch-shifter-on-the-master-bus-i-love-how-it-glues-everything-together-and-f-cks-everything-up-loukeman-reveals-his-favourite-plugins-and-unusual-production-techniques (read in full)
- https://mixmag.net/feature/the-mix-115-loukeman-interview-sd-3
- https://ca.billboard.com/music/loukeman-sd3-interview
- https://www.highsnobiety.com/p/loukeman-wants-to-wake-up-early-and-make-beats-interview/
- https://www.welcomejpeg.com/p/loukeman-makes-music-sound-new-again
- https://thefader.com/2024/01/11/loukeman-shares-new-album-sd-2
- https://panm360.com/en/records/loukeman-sd-2-top-albums-of-2024/
- https://stereogum.com/2494504/loukeman-announces-new-album-sd-3-hear-to-the-sky/music
- https://www.shazam.com/song/1718530713/giverrr
- https://chordify.net/chords/loukeman-songs/giverrr-chords
- https://rateyourmusic.com/release/album/loukeman/sd-2/
- https://www.last.fm/music/Loukeman/_/Giverrr
- https://pocketmags.com/us/future-music-magazine/may-2024/articles/loukeman
- https://lists.apple.com/archives/coreaudio-api/2005/May/msg00144.html (AUTimePitch tightness and smoothness)
- https://www.soundonsound.com/node/4906681 and https://cdm.link/sonic-charge-permut8-all-your-lo-fi-digital-mangling-effects-in-one-place/ (Permut8)
- Not readable: https://www.whosampled.com/Bryson-Tiller/Exchange/sampled/

Vegyn
- https://talkhouse.com/loraine-james-and-vegyn-make-club-music-thats-not-actually-for-the-club
- https://equipboard.com/pros/vegyn
- https://mixmag.net/feature/vegyn-interview-plz-make-it-ruins
- https://thequietus.com/?p=33895
- https://pilotmagazine.uk/home/vegyn-interview
- https://www.papermag.com/vegyn-only-diamonds-cut-diamonds
- https://en.wikipedia.org/wiki/Only_Diamonds_Cut_Diamonds
- https://www.pastemagazine.com/music/vegyn/vegyn-only-diamonds-cut-diamonds-review
- https://crackmagazine.net/?p=137007
- https://www.thefader.com/2020/08/13/mkgee-takes-vegyns-debold-to-the-dreamworld-dancefloor-for-a-new-remix
- https://twntythree.com/vegyn-releases-debut-album-only-diamonds-cut-diamonds-shares-new-video-for-track-debold/
- https://vegyn.bandcamp.com/album/only-diamonds-cut-diamonds
- https://vegyn.bandcamp.com/album/odcd-alt-versions
- https://vegyn.bandcamp.com/music
- https://www.last.fm/music/Vegyn/_/Debold
- https://songdata.io/track/3SHHe08AIWoVkXNejY4jab/Debold-by-Vegyn (search snippet only)

ear
- https://en.wikipedia.org/wiki/Ear_(band)
- https://en.wikipedia.org/wiki/Laptop_twee
- https://nobells.blog/ear-interview-band/
- https://crackmagazine.net/article/profiles/ear-interview-rumspringa/
- https://www.elienis.com/ear-interview-jonah-paz-yaelle-avtan-rumspringa/
- https://nialler9.com/who-are-ear-the-laptop-twee-duo-playing-two-dublin-shows-this-month/
- https://bowdoinorient.com/2026/05/01/the-new-sound-of-indietronic/
- https://stereogum.com/2514649/an-iphone-is-their-acoustic-guitar/columns/sounding-board
- https://onestowatch.com/en/blog/ear-choose-life-in-their-latest-album-rumspringa
- https://wknc.org/2026/04/10/album-review-the-most-dear-and-the-future-by-ear/
- https://www.thefader.com/2025/12/09/best-songs-2025
- https://www.thefader.com/2026/03/16/ear-fader-documentary-watch
- https://www.pi.fyi/rec/cmkmwf05w069fsz3jtwd1w0p6
- https://www.welcomejpeg.com/p/the-rise-of-laptop-twee
- https://readdork.com/track/ear-nerves-2024
- https://www.shazam.com/song/1777050586/nerves

meat computer
- https://nts.live/artists/131651-meat-computer
- https://meat-computer.bandcamp.com
- https://readdork.com/artists/meat-computer/tracks
- https://readdork.com/track/meat-computer-healing-spiritual-echo
- https://www.highsnobiety.com/p/hyperspeed-rap/
- https://felt.substack.com/p/meat-computer-meet-computer
- https://www.shazam.com/song/1870401474/healing-spiritual-echo (would not load; details from search results)

The shared space (section 9)
- https://en.wikipedia.org/wiki/Laptop_twee
- https://www.welcomejpeg.com/p/the-rise-of-laptop-twee
- https://bowdoinorient.com/2026/05/01/the-new-sound-of-indietronic/
- https://www.last.fm/music/Caribou
- https://www.last.fm/music/Loukeman/+similar
- https://www.last.fm/music/Vegyn/+similar
- https://www.last.fm/music/ear/+similar
- https://www.last.fm/music/meat+computer/+similar
- https://www.wonderlandmagazine.com/2026/04/28/loukeman-on-repeat/
- https://thelineofbestfit.com/features/interviews/caribou-interview-2014

The four corners (section 9.6)

Ambient bedroom
- https://exclaim.ca/music/article/ml_buch_interview_suntub
- https://stereogum.com/2273067/i-was-singing-about-the-film-of-sky-whatever-that-is-ml-buch-brings-her-singular-sound-world-to-la/interviews/qa
- https://crackmagazine.net/article/profiles/golden-hour-ml-buch/
- https://crackmagazine.net/article/profiles/golden-hour-ml-buch/2
- https://subbacultcha.nl/?p=53381
- https://talkhouse.com/ml-buch-and-astrid-sonne-take-it-to-the-edge-of-too-much
- https://en.wikipedia.org/wiki/Suntub
- https://anotherthoughtanotherthought.substack.com/p/high-speed-calm-air-ml-buch-being
- https://thevinylfactory.com/features/gear-talk-astrid-sonne/
- https://www.ableton.com/en/blog/astrid-sonne-a-newfound-voice/
- https://toneglow.substack.com/p/tone-glow-120-astrid-sonne
- https://www.pressreader.com/usa/the-guardian-usa/20240203/282243785486382
- https://acrn.com/2024/02/12/album-review-astrid-sonne-great-doubt (search snippet only)
- https://thefader.com/2025/04/03/astrid-sonne-big-ears-2025-interview
- https://daily.bandcamp.com/features/ana-roxanne-interview
- https://en.wikipedia.org/wiki/Tilde_Tilde_Tilde
- https://en.wikipedia.org/wiki/Because_of_a_Flower_(album)
- https://www.backseatmafia.com/album-review-ana-roxanne-because-of-a-flower-a-blissful-vocal-gem-from-kranky/
- https://ra.co/reviews/25236
- https://kcrw.com/music/shows/music-special/private-playlist-ana-roxanne-her-prabha-atre
- https://anaroxanne.bandcamp.com/
- https://en.wikipedia.org/wiki/For_Those_of_You_Who_Have_Never_(And_Also_Those_Who_Have)
- https://factmag.com/2016/05/25/huerco-s-interview-for-those-of-you-who-have-never
- https://www.thefader.com/2016/05/31/huerco-s-for-those-of-you-interview
- https://daily.redbullmusicacademy.com/2013/10/huerco-s-on-pre-columbian-america
- https://ra.co/reviews/19106
- https://straylandings.co.uk/_content/reviews/2016-06-08-for-those-of-you-who-have-never-and-also-those-who-have/
- https://tinymixtapes.com/node/151859 (dead page; quote from the search index)

Rough house
- https://equipboard.com/pros/delroy-edwards
- https://daily.redbullmusicacademy.com/2014/11/delroy-edwards-interview
- https://ra.co/reviews/19594
- https://ra.co/reviews/24921
- https://www.dazeddigital.com/music/article/32336/1/hangin-at-the-beach-with-delroy-edwards
- https://www.1001tracklists.com/track/9p420p5/delroy-edwards-i-love-sloane/index.html
- https://laclubresource.bandcamp.com/album/hangin-at-the-beach
- https://songbpm.com/@delroy-edwards/i-love-sloane
- https://daily.bandcamp.com/features/ross-from-friends-interview
- https://www.attackmagazine.com/features/interview/ross-from-friends-on-the-one-production-pressure/
- https://www.attackmagazine.com/technique/beat-dissected/ross-from-friends-inspired-beat/
- https://output.com/blog/talkback-with-ross-from-friends
- https://www.loudandquiet.com/interview/ross-from-friends-just-a-normal-guy-in-his-twenties-making-off-the-wall-house-music-named-after-one-of-the-most-popular-sitcom-characters-of-all-time/
- https://xlr8r.com/features/20-questions-ross-from-friends/
- https://mixmag.net/feature/ross-from-friends-feature
- https://mixmag.net/feature/ross-from-friends-interview-lofi-tread
- https://en.wikipedia.org/wiki/Ross_from_Friends
- https://www.vice.com/en/article/lo-fi-house-youtube-related-video-algorithm-essay
- https://xlr8r.com/features/get-familiar-dj-seinfeld/
- https://mixmag.net/feature/dj-seinfeld-interview-cover-lo-fi-mirrors-ninja-tune
- https://musicradar.com/news/dj-seinfeld-interview
- https://soundcloud.com/stories/post/dj-seinfeld-exclusive-interview-sound-advice
- https://factmag.com/2017/11/06/dj-seinfeld-time-spent-away-from-u-interview
- https://ja.ra.co/reviews/21715
- https://songbpm.com/@dj-seinfeld/u
- https://www.musicradar.com/news/anthony-naples-interview (search snippets only)
- https://www.juno.co.uk/junodaily/2012/08/13/at-home-with-anthony-naples/
- https://www.whosampled.com/Anthony-Naples/ (search snippet only)
- https://www.vice.com/en/article/anthony-naples-brings-techno-down-to-earth/
- https://stereogum.com/2049105/anthony-naples-thrives-in-the-fog/franchises/interview
- https://en.wikipedia.org/wiki/Anthony_Naples
- https://pt.ra.co/reviews/12100
- https://songbpm.com/@anthony-naples/mad-disrespect
- https://www.stampthewax.com/?p=41207
- https://www.musicradar.com/music-tech/its-a-unique-moog-synth-that-looks-nothing-like-the-rest-of-their-line-up-thats-where-our-name-comes-from-jump-source-on-rare-moogs-closet-studios-and-why-the-best-gear-has-to-have-character
- https://mixmag.net/feature/the-mix-112-dj-jump-source-patrick-holland-priori-montreal-interview
- https://stereogum.com/2500205/the-complex-thrills-of-jump-sources-fold/columns/crossing-wires
- https://www.hhv-mag.com/review/jump-source-fold/?lang=en
- https://ra.co/reviews/20196
- https://en.wikipedia.org/wiki/Outsider_house
- https://highclouds.org/?p=29579

Vegyn and warm club
- https://www.soundonsound.com/people/four-tet-kieran-hebden
- https://www.musicradar.com/news/four-tet-tape-notes
- https://tapenotes.co.uk/project/tn140-fourtet
- https://cdm.link/?p=29332
- https://www.ableton.com/en/blog/four-tet-explains-his-live-setup
- https://de.ra.co/reviews/23555
- https://exclaim.ca/music/article/hear_four_tet_sample_nelly_furtado_on_new_track_only_human
- https://hypebeast.com/2020/1/four-tet-baby-song-stream
- https://acousticbrainz.org/67bcdc9e-d99d-4654-aa4d-8bccc56fb401
- https://www.mixgraph.io/tracks/four-tet-baby
- https://ra.co/reviews/34886
- https://thequietus.com/?p=31559
- https://xlr8r.com/features/deep-inside-daphni-jiaolong/
- https://factmag.com/2012/10/01/it-isnt-a-particularly-macho-endeavour-dan-snaith-talks-daphni-his-reaction-to-the-edm-shooting-match
- https://xlr8r.com/?p=95090
- https://www.interviewmagazine.com/music/mount-kimbie-cold-spring-fault-less-youth
- https://www.attackmagazine.com/?p=53091
- https://brightonsource.co.uk/features/mount-kimbie-interview/
- https://crackmagazine.net/article/profiles/mount-kimbie-crooks-and-lovers-retrospective/2
- https://dmy.co/features/song-of-the-week-mount-kimbie-made-to-stray/
- https://exclaim.ca/music/article/mount_kimbie-cold_spring_fault_less_youth
- https://en.wikipedia.org/wiki/BTSTU
- https://en.wikipedia.org/wiki/Jasmine_(song)
- https://www.clashmusic.com/features/back-to-save-the-universe-jai-pauls-btstu-edit-ten-years-on/
- https://equipboard.com/pros/jai-paul
- https://www.timeout.com/london/music/john-glacier-if-you-follow-the-crowd-youre-the-one-missing-out
- https://thelineofbestfit.com/new-music/song-of-the-day/john-glacier-if-anything
- https://www.loudandquiet.com/reviews/john-glacier-shiloh-lost-for-words/
- https://en.wikipedia.org/wiki/Like_a_Ribbon
- https://offlicencemagazine.com/blog/john-glacier-is-as-cool-as-ever-on-found

Voice-led
- https://en.wikipedia.org/wiki/Bassvictim
- https://www.nme.com/features/music-interviews/bassvictim-forever-interview-radar-3917740
- https://www.interviewmagazine.com/music/bassvictim-maria-manow-ike-clateman-peace-and-war
- https://www.bylinebyline.com/articles/bassvictim-bass-punk
- https://www.turnandwork.com/music/bassvictim-alice-basspunk/
- https://songbpm.com/@bassvictim
- https://www.interviewmagazine.com/music/worldpeace-dmt-rowan-please-bassvictim
- https://friendsand.substack.com/p/worldpace-dmt-and-rowan-please-the
- https://stereogum.com/2514847/worldpeace-dmts-stick-around-for-tea-is-not-your-fathers-laptop-twee/music
- https://www.welcomejpeg.com/p/worldpeace-dmt-is-dosing-the-underground
- https://soyoungmagazine.com/worldpeace-dmt-share-video-for-love-yourself-and-announce-co-headline-tour-with-the-femcels/
- https://en.wikipedia.org/wiki/Frost_Children
- https://www.dazeddigital.com/music/article/69870/1/frost-children-ninajirachi-hyperpop-interview-post-skrillex-world-sisters
- https://officemagazine.net/embracing-cringe-frost-children
- https://www.welcomejpeg.com/p/the-frost-children-interview
- https://mixmag.net/feature/meet-frost-children-sibling-duo-maximalist-edm-bloghouse
- https://stereogum.com/2211522/frost-children-flatline/music
- https://songbpm.com/@frost-children
- https://www.dazeddigital.com/music/article/56278/1/whitearmor-drain-gang-music-for-weddings-interview
- https://equipboard.com/pros/whitearmor
- https://equipboard.com/pros/yung-gud
- https://daily.redbullmusicacademy.com/2014/06/yung-gud-interview/
- https://www.thefader.com/2019/09/17/bladee-interview-drain-gang-trash-island
- https://www.thefader.com/2021/04/23/ecco2k-interview-pxe-drain-gang
- https://en.wikipedia.org/wiki/E_(Ecco2k_album)
- https://en.wikipedia.org/wiki/Crest_(album)
- https://en.wikipedia.org/wiki/Bladee
- https://songbpm.com/@bladee/be-nice-2-me
- https://songbpm.com/@ecco2k/peroxide
- https://en.wikipedia.org/wiki/D0llywood1
- https://www.thefader.com/2024/06/06/d0llywood1-digicore-hyperpop-soundcloud-interview
- https://www.thefader.com/2024/01/12/rap-blog-d0llywood1-slow-motion-flexes-on-her-unbelievers
- https://en.wikipedia.org/wiki/Digicore
- https://antarestech.com/blog/pitch-correction-the-complete-guide-to-tuning-vocals
- https://en.wikipedia.org/wiki/Auto-Tune

CoolSynth files read: `GEMINI.md`, `docs/design/SOUND_BANK_V3.md`, `docs/interface-v3.md`,
`resources/presets/*.csbank`, `src/synth/v3/Inserts.h`, `EffectRoutes.h`, `ModMatrix.h`,
`Modulation.h`, `SampleGenerator.h`, `GrainGenerator.h`, `GrainInsert.h`, `ShaperProcessors.h`,
`ResonatorProcessors.h`, `src/synth/ModernFx.h`, `src/synth/GlobalDelay.h`, `DECISIONS_LOG.md`
(D50, D54, D106).

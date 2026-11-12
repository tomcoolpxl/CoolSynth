# TODO

## Sound quality backlog — from the 2026-11-12 engine review

Ordered by what you would hear first. Each line is one review cycle. Audition the committed engine pass (ladder, exponential envelopes, plate, chorus) before starting any of these; several numbers below are candidates for retuning by ear.

1. Audition and retune factory presets for the new resonance law: the knob now maps linearly to ladder feedback (k = 4r, self-oscillation at the top) where it used to map cubically to Q, so presets that sat at 60-100 % resonance will ring harder than intended. Retune only the affected values in `coolsynth_30_presets_with_scifi_additions.yaml` and regenerate.
2. 2x oversampling around the voice render (oscillators, mixer tanh, ladder) with a half-band decimator, so the saturators stop aliasing on bright patches and the A7 alias test can tighten from -30 dB to -60 dB. Doubles voice CPU; 8 voices leave room.
3. Delay: replace `juce::dsp::DelayLine` (linear interpolation, bright digital repeats) with an own line using Hermite interpolation and a one-pole low-pass plus a gentle high-pass in the feedback path, so repeats darken like tape or BBD. Keep the tanh feedback clip. Optional later: ping-pong, tempo sync.
4. Vintage: today it is a fixed per-voice pitch offset chosen once at note-on. Extend it toward what the word promises: slow random-walk pitch drift per voice (around 0.1-0.3 Hz), a small per-voice filter cutoff offset, and per-voice envelope time scaling, all scaled by the one existing `vintageAmount` knob. Keep the deterministic seeding so renders stay reproducible.
5. Unison detune: a dedicated detune amount independent of vintage, spreading voices symmetrically in cents, with the existing 1/sqrt(N) level compensation.
6. Ladder filter tuning pass: audition the bass makeup factor (currently returns half the low end lost to feedback), consider exposing a small filter drive amount, and decide whether per-stage saturation (fuller Moog-style) is wanted as an optional mode next to the current input-saturated, cleaner OTA-like voicing.
7. Drive stage: 2x oversampling on the tanh (pre-gain reaches 16x) and a tilt or low-pass after it so high drive stays musical instead of fizzy.
8. Plate reverb controls: a pre-delay of up to 100 ms, an input high-pass to keep bass out of the tank, and a modulation depth trim. Audition the size-to-decay mapping (0.25 to 0.95).
9. Triangle oscillator: the leaky-integrator triangle loses level and rounds off at high pitches; replace with a PolyBLAMP-corrected triangle or a wavetable so it tracks the saw and pulse in level.
10. Master safety: a soft clipper after master gain, so self-oscillating resonance plus drive plus delay feedback can never hand the host a hard clip. Keep it transparent below 0 dBFS.
11. Measurement-style regressions: THD of a sine through the ladder at low resonance below a stated bound; attack reaching full level within the programmed time; reverb RT60 within a range for a given size; alias tests retargeted once oversampling lands.
12. Phaser: optional 6-stage mode and a triangle LFO; the 4-stage first-order design gives only two notches, which is thin for a pad phaser.
13. Unison voice count (2 / 4 / 8) next to the detune of item 5. With count 2, detune and 100 % pan spread this gives the "same patch left and right, slightly detuned" stack without any architecture change; today unison always takes all eight voices.
14. Multi-part layering (two patches at once): the engine class is already self-contained, so a second `SynthEngineV2` summed before the FX rack is cheap. The cost is everything around it: a second copy of the patch parameters under a part prefix, part level / pan / detune / transpose / key and velocity ranges, a state and preset format bump (no compatibility needed), and a part selector in the UI. Write the design into `DESIGN_V2.md` before touching code; decide shared versus per-part FX and the voice budget per part first.

## ARP Expansion Phase F — Factory preset audition and sign-off

The format boundary, preset source-pipeline refresh, and curated arp-bank implementation slice are now in place and locally build-test verified. The remaining Phase F work is the manual ear-check and any final preset trims that fall directly out of it.

- Manually audition the new curated arp presets in standalone and confirm the names, pattern modes, Euclidean rhythms, chance, ratchet, and accent behaviors all match the intended musical character.
- If the ear check exposes obvious voicing or loudness mismatches, retune only the affected preset values and rerun the current local build-test verification.
- After the manual preset pass, update `DONE.md` and `GEMINI.md` to close full ARP Expansion Phase F if no further code changes are needed.

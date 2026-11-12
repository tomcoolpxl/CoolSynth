# TODO

## Sound quality backlog — what remains after the two engine passes of 2026-11-12

Ordered by what you would hear first. Each line is one review cycle. Audition the current engine before starting any of these; several numbers in the shipped designs are candidates for retuning by ear.

1. Audition pass on the factory presets: the resonance retune was done by formula (equal cutoff-to-passband ratio), the makeup, clipper and darker delay all shift loudness and tone, and the exponential envelopes change pluck feel. Adjust only what the ear flags; then run `scripts/apply_normalized_master_gains.ps1` if overall levels drifted.
2. Unison voice count (2 / 4 / 8) and a dedicated unison detune amount independent of vintage, with the existing 1/sqrt(N) level compensation. With count 2, detune and 100 % pan spread this is the "same patch left and right, slightly detuned" stack without any architecture change.
3. Multi-part layering (two patches at once): the engine class is already self-contained, so a second `SynthEngineV2` summed before the FX rack is cheap. The cost is everything around it: a second copy of the patch parameters under a part prefix, part level / pan / detune / transpose / key and velocity ranges, a state and preset format bump (no compatibility needed), and a part selector in the UI. Model the controls on the Prophet-10 OS 2.0 stack/split (five voices per program, layer balance) and the Jupiter-8 Whole/Dual/Split with its balance slider. Write the design into `DESIGN_V2.md` before touching code; decide shared versus per-part FX and the voice budget per part first.
4. Ladder tuning by ear: the bass makeup factor (half the low end lost to feedback), whether to expose a small filter drive amount, and whether a per-stage-saturated Moog-style mode is wanted next to the current input-saturated voicing.
5. Plate reverb controls: a pre-delay of up to 100 ms, an input high-pass to keep bass out of the tank, and a modulation depth trim. Audition the size-to-decay mapping (0.25 to 0.95).
6. Measurement-style regressions still missing: THD of a sine through the ladder at low resonance below a stated bound; attack reaching full level within the programmed time; reverb RT60 within a range for a given size.
7. Delay extras once the base voicing is approved: ping-pong, tempo sync.

## ARP Expansion Phase F — Factory preset audition and sign-off

The format boundary, preset source-pipeline refresh, and curated arp-bank implementation slice are now in place and locally build-test verified. The remaining Phase F work is the manual ear-check and any final preset trims that fall directly out of it.

- Manually audition the new curated arp presets in standalone and confirm the names, pattern modes, Euclidean rhythms, chance, ratchet, and accent behaviors all match the intended musical character.
- If the ear check exposes obvious voicing or loudness mismatches, retune only the affected preset values and rerun the current local build-test verification.
- After the manual preset pass, update `DONE.md` and `GEMINI.md` to close full ARP Expansion Phase F if no further code changes are needed.

# Decisions log

Decisions taken without asking while the user was away. Each entry lists every option that
was on the table, with its consequence and side effects, the option taken and why. Review and
overrule anything here; each entry says what changing it would take.

Entries are newest last.

## D1. Juno Pad moves from phase 2.1 to phase 2.2 (2026-11-13, phase 2.1)

The plan ships Juno Pad with the LFO. Its signature is pulse-width modulation from an LFO, and
an LFO reaches a destination only through the matrix, which is phase 2.2.

- **Move Juno Pad to 2.2 (taken, recommended).** It ships with the matrix, one chunk later,
  with the route the bank file will use for every later preset. No throwaway code.
- Add a hardwired "LFO1 to G1 pulse width" parameter now. Juno Pad ships an hour earlier, but
  the parameter is a dead end once the matrix exists and stays in the patch format.
- Ship Juno Pad without PWM. It would not sound like a Juno pad, which is the whole preset.

Changing it: nothing to undo; Juno Pad lands in 2.2 either way.

## D2. Tempo for synced LFOs: the host's, else the arp's internal tempo (2026-11-13, phase 2.1)

A synced LFO needs a tempo when the host gives none (the standalone never does).

- **Host tempo when the host reports one, otherwise the existing arp internal tempo parameter
  (taken, recommended).** One tempo for the arp and every synced modulator, so an arp and a
  synced wobble never drift apart. No new parameter.
- A separate part tempo parameter. More flexible, but two tempos that can disagree, and a
  second knob to set in every synced preset.
- Fixed 120 BPM without a host. Simple, but no way to set the standalone's tempo.

Changing it: give `PartSettings` its own tempo and read it in `Part::renderChunk`.

## D3. What the "16-step" LFO shape is (2026-11-13, phase 2.1)

The plan lists a 16-step shape without saying what it holds; the STEP modulator with editable
values is a separate source in phase 2.3.

- **A rising staircase of 16 equal levels per cycle (taken, recommended).** Predictable and
  useful on its own: through a pitch route it plays a stepped run, on a filter a stepped sweep.
  Bank name `steps`.
- A looping random 16-step pattern, new per note. Overlaps sample-and-hold and cannot be set.
- The STEP modulator's values. Couples two modulators and duplicates the STEP source.

Changing it: `LfoShape::steps` in `src/synth/v3/Lfo.h`.

## D4. LFO delay as well as fade-in (2026-11-13, phase 2.1)

The plan names a fade-in. Logic's and Halion's LFOs, and the Juno's, also have a delay before
the modulation starts (checked online: Logic Pro "Delay" and "Attack", Halion one-shot modes).

- **Delay plus fade-in (taken, recommended).** Delayed vibrato, the classic use, needs both. One
  more parameter per LFO (nine instead of eight).
- Fade-in only. Delayed vibrato would start rising at once.

Changing it: drop `delay` from the LFO group; presets then must not use it.

## D5. LFO behaviour details (2026-11-13, phase 2.1)

Smaller calls made while writing the class, each with what else was possible:

- **Free-running local LFOs advance only while their voice sounds (taken).** The alternative,
  advancing all 16 voices' LFOs all the time, costs CPU for no audible gain: with retrigger off
  the phase a note meets is arbitrary either way.
- **Delay and fade restart at every note even with retrigger off; retrigger decides only the
  phase (taken).** The alternative, no fade at all when free-running, loses the
  free-running-vibrato-that-fades-in patch.
- **A synced, free-running LFO follows the host's song position while the transport plays
  (taken).** The alternative, ignoring the song position, lets loops drift between passes.
- **Global LFOs default to synced and free-running; local ones to free rate and retrigger
  (taken).** Matches the design text ("tempo-synced, one phase shared by all voices"). The
  alternative was identical defaults everywhere.
- **Fade-in is a straight line (taken).** An exponential or S-curve fade was the alternative;
  the straight line is what the Juno and most plug-ins do and what the test checks.
- **Shape start points: sine and triangle at 0 rising, the saws and steps at their low end
  (saw down at its top), square high (taken).** The alternative, all shapes from their low
  point, makes sine and triangle start with a jump when routed to pitch.

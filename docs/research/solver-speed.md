# Making the circuit solver cheaper

Research for the 303's load spike (D340, D341), 2026-11-24, for the 303, the 808 and CoolSynth alike: they share
`CircuitWdf.h`. Each sample solves v = p + K f(v) over the nonlinear ports only (m up to 22, K dense), by Newton
with the chord method. The user's rule: a speed-up goes in on its own only if the output stays byte for byte the
same; anything else is put to the user with its measured difference.

## Where the time went

The 303 voice at 96 kHz, before D341, 76 % of a core:
- the kept factors' triangular solves 19 %, the dense products 17 %, the Newton loop 18 %;
- the oscillator's follower 9 %, exp 9 %, the devices' currents 6 %, refactoring 3 %.

Done in D341: the products row-blocked (the same bits), the follower warm-started (the user's pick; no measurable
accuracy lost). Now some 62 %.

The solver's tolerances are not over-tight: u-he's Repro-1 stops near 150 nV
([Urs Heckmann on KVR](https://www.kvraudio.com/forum/viewtopic.php?p=6453705)), ours at 100 nV.

## The same bits

Kept exact as long as every row's sum keeps its order.

SIMD across voices or across same-shaped circuits
- Surge XT runs four voices in one SSE register ([sst-filters](https://github.com/surge-synthesizer/sst-filters)).
- Urs Heckmann: a vector Newton beats one stage at a time "by a magnitude"
  ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=6421715)).
- For CoolSynth's voices and the 808's look-alike drums; nothing for the mono 303. Lanes wait for the slowest.

Skipping K's exact zeros, or code generated per circuit with m fixed
- [Chowdhury and Rau, DAFx26](https://www.dafx.de/paper-archive/2026/papers/DAFx26_paper_23.pdf): the reduced
  circuit under half the cycles; restructured for parallel work, fewer cycles even with more instructions.
- Exact zeros come only from nullors and ideal sources; the 303's filter and VCA have none in K.

## Within the solver's tolerance, not the same bits

An explicit inverse of the kept Jacobian in place of its LU factors
- Each chord step one product, no chain of dependent steps; building it some 3 times a factorisation.
- Perhaps half the triangular solves' 19 %. All three apps.

Splitting the system into independent blocks (block-triangular form)
- As circuit simulators do before factoring ([Davis and Natarajan, KLU](https://www.springerprofessional.de/en/sparse-matrix-methods-for-circuit-simulation-problems/3690150));
  Holters calls sub-circuits "very attractive" ([EUSIPCO 2016](https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Holters_treecachesolver_EUSIPCO16.pdf)).
- Only where K is exactly zero; the gain depends on whether a circuit splits.

Low-rank updates of the kept inverse at a note's attack
- Only the columns of devices whose conductance moved change: rank one a diode, two a transistor
  (Sherman-Morrison; SPICE's bypass is the same idea, [Berkeley EE219](https://people.eecs.berkeley.edu/~newton/Classes/EE219fa98/ee219a3_2/tsld012.htm)).
- Aimed at the 303's note-start spike.

Better starts at hard edges
- Holters' solution cache ([ACME.jl](https://github.com/HSU-ANT/ACME.jl)): on an SD-1 clone samples needing over 15
  iterations fell from 4,357 to 88; a k-d tree suits few dimensions, not 22.
- A drum hit from rest repeats: each sample of a hit started from the same sample of the last hit (the 808).
- Starts from the dominant exponential's inverse
  ([Holmes and van Walstijn, DAFx-15](https://pureadmin.qub.ac.uk/ws/files/17395135/improving_the_robustness.pdf)).

A vectorised exp of 1-ulp accuracy
- [SLEEF](https://sleef.org/x86.xhtml): a junction moves some 1e-16 of VT, far under 100 nV. Perhaps two thirds of
  exp's 9 %. A cruder fast exp (1e-3) moves a junction 26 uV: past the tolerance.

The kept factors in float, everything else in double
- Iterative refinement ([Baboulin et al.](https://arxiv.org/pdf/0808.2794)); reported from 1.25 times faster to
  slower ([survey, 2024](https://arxiv.org/pdf/2412.19322)). Float throughout fails: near 10 V its step is 1 uV.

Woodbury for knob changes
- [Holters and Zölzer, DAFx-11](https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Holters-Zölzer-2011-Physical-Modelling-of-a-Wah-wah-Effect-Pedal-as-a-Case-Study-for-Application-of-the-Nodal-DK-Method-to-Circuit.pdf):
  a pot change a 2x2 inverse, not a new analysis. For knob sweeps, not the profile measured.

## Approximations

Quality modes and fixed iteration counts
- Diva's Draft to Divine; the 808's Lighter and Lightest (D339). The user's call, with the difference measured.

Not for us
- Lookup tables and neural device models: tables grow with the dimension (22 here); a network's error is
  trained, not bounded ([Yeh, TASLP 2010](https://ccrma.stanford.edu/~dtyeh/papers/yeh10_taslp.pdf)).
- Linearly implicit schemes: an even load, but the WDF replaced; Holters finds the gain vanishes when the nonlinear
  part is implicit ([DAFx24](https://dafx.de/paper-archive/2024/papers/DAFx24_paper_18.pdf)).
- Wright omega for diodes ([D'Angelo et al., DAFx19](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_5.pdf)):
  one exponential at a time, errors of millivolts; at most a first guess.
- Broyden's updates: less robust than the chord method (Holmes); homotopy has no time bound (Yeh).

## Load spikes

What others do: a constant cost per sample, capped or fixed iterations, better starts. The low-rank updates and the
better starts above aim at the spike without loosening the tolerance.

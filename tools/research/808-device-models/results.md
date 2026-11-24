# 808 device models: the full tables (2026-11-24)

Written by `measure.py --summary` from the renders behind `docs/research/808-device-models.md`: for each variant and
voice the largest change over its cases, the case named. Errors are rms differences over the reference's rms, each
about its rest; decays to -20 dB of a 5 ms rms envelope; pitch at each resonator's node. Op-amp, Schmitt-edge and
combined comparisons on the metal board were rendered at the looser solver settings (see the report). The metal
board's "main" pitch is the output's spectral peak, which jumps between partials when the oscillators move.

### oa_ti: op-amps: TI's RC4558 macromodel (1.7 V/us, about +-13 V, 25 mA)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 23.1 (bd_dec1_acc) | 24.9 (bd_dec1_acc ntop) | +0.21 | -0.08 | -0.88 (bd_dec1_acc 62 Hz) | +0.3 (bd_acc) | -8.6 (bd sigh) |
| Toms, congas | 2.6 (hc) | 2.6 (hc no2) | -0.03 | +0.07 | +0.25 (lc 198 Hz) | +1.5 (lc) | +1.0 (lt_t1_acc body) |
| Rim shot, claves | 10.2 (cl_acc) | 25.9 (cl_acc ne63) | -0.27 | -0.50 | -1.33 (rs_acc 2520 Hz) | -4.2 (cl) | +1.4 (cl high) |
| Snare | 7.4 (sd_tone0) | 15.5 (sd_acc no1) | -0.37 | -0.56 | -1.40 (sd_tone1_acc 157 Hz) | +0.7 (sd_snap1_acc_l1) | +0.2 (sd_tone1_acc low) |
| Clap, maracas | 0.4 (cp_acc_l1) | 25.1 (cp_acc_l1 n2) | +0.00 | +0.01 | +0.01 (cp_acc_l1 2520 Hz) | +0.0 (cp) | - () |
| Cowbell | 0.3 (cb_acc_l1) | 3.9 (cb_acc_l1 nv2) | -0.02 | -0.02 | -0.03 (cb_acc_l1 794 Hz) | -0.0 (cb) | -0.0 (cb_acc_l1 main) |
| Cymbal | 1.2 (cy_t1_acc_l1) | 3.4 (cy_acc nv2) | -0.01 | +0.01 | +0.04 (cy 16000 Hz) | +0.0 (cy_t0) | +0.0 (cy main) |
| Open hat | 1.2 (oh_d1) | 3.4 (oh_acc_l1 nv2) | -0.01 | +0.01 | +0.04 (oh_d0 8000 Hz) | -0.3 (oh_d0) | -0.0 (oh_d0 main) |
| Closed hat | 1.5 (ch) | 3.4 (ch_acc_l1 nv2) | +0.03 | +0.01 | +0.04 (ch 8000 Hz) | +0.1 (ch) | -0.0 (ch_acc_l1 main) |

### oa_upc: op-amps: the TI model retuned to the uPC4558 (1.0 V/us, +-14 V)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 10.7 (bd_dec1_acc) | 12.7 (bd_dec1_acc ntop) | +0.11 | -0.03 | -0.43 (bd_dec1_acc 62 Hz) | +0.2 (bd_acc) | -3.5 (bd sigh) |
| Toms, congas | 4.8 (ht_acc_l1) | 4.8 (ht_acc_l1 no1) | -0.11 | -0.10 | -0.34 (ht_acc_l1 250 Hz) | +1.6 (mc_acc) | +0.7 (hc body) |
| Rim shot, claves | 12.2 (cl_acc) | 24.7 (cl_acc ne63) | -0.68 | -0.82 | -1.34 (cl_acc 2520 Hz) | -2.7 (cl) | +1.6 (cl high) |
| Snare | 11.3 (sd_tone0) | 21.4 (sd_acc no1) | -0.57 | -0.87 | -2.00 (sd_tone1_acc 157 Hz) | -4.8 (sd_snap0) | +0.2 (sd_tone1_acc low) |
| Clap, maracas | 0.3 (cp_acc_l1) | 25.1 (cp_acc_l1 n2) | +0.00 | +0.01 | +0.01 (cp 2520 Hz) | +0.0 (cp) | - () |
| Cowbell | 0.3 (cb_acc_l1) | 7.8 (cb_acc_l1 nv2) | -0.02 | -0.02 | -0.02 (cb_acc_l1 794 Hz) | -0.0 (cb) | -0.0 (cb main) |
| Cymbal | 1.4 (cy_t1_acc_l1) | 7.5 (cy_acc nv2) | -0.01 | +0.02 | +0.06 (cy_t1_acc_l1 16000 Hz) | +0.0 (cy_t0) | +0.0 (cy main) |
| Open hat | 1.3 (oh_d1) | 7.5 (oh_acc_l1 nv2) | +0.00 | +0.02 | +0.05 (oh_d0 8000 Hz) | -0.3 (oh_d0) | -0.0 (oh_d0 main) |
| Closed hat | 1.6 (ch) | 7.5 (ch_acc_l1 nv2) | +0.03 | +0.01 | +0.05 (ch 8000 Hz) | +0.1 (ch) | -0.0 (ch_acc_l1 main) |

### tr_gr: transistors: 2SC1815-GR (Kaz-electro, measured)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 138.9 (bd_dec1_acc) | 145.4 (bd_dec1_acc ntop) | -0.24 | -0.16 | -8.78 (bd_dec1_acc 62 Hz) | +9.1 (bd_acc) | -60.7 (bd_dec1_acc sigh) |
| Toms, congas | 1.5 (ht_acc_l1) | 32.0 (hc nfl) | -0.19 | -0.03 | -0.06 (ht_acc_l1 250 Hz) | +0.1 (lt_acc) | -0.4 (lt_t1_acc body) |
| Rim shot, claves | 26.1 (rs) | 41.5 (rs_acc_l1 nf) | +0.23 | +0.12 | +5.74 (rs_acc 1260 Hz) | +17.4 (rs) | +0.0 (rs high) |
| Snare | 4.5 (sd) | 426.0 (sd_snap0 nc48) | -0.04 | +0.07 | +1.20 (sd_tone0 6350 Hz) | +3.5 (sd) | -0.0 (sd_snap1_acc_l1 high) |
| Clap, maracas | 21.1 (ma_acc) | 43.1 (cp n2) | -0.88 | -0.79 | -2.85 (cp 1000 Hz) | -2.2 (ma) | - () |
| Cowbell | 1.1 (cb_acc_l1) | 7.7 (cb nv2) | +0.01 | -0.00 | +0.36 (cb 1000 Hz) | +2.8 (cb) | -0.0 (cb main) |
| Cymbal | 13.3 (cy) | 7.7 (cy nv2) | +0.12 | +0.22 | +0.88 (cy 12699 Hz) | +6.4 (cy_t0) | +0.0 (cy main) |
| Open hat | 7.2 (oh_d1) | 7.7 (oh_d0 nv2) | -0.21 | -0.21 | -0.62 (oh_d1 3175 Hz) | -3.6 (oh_d1) | -0.0 (oh_acc_l1 main) |
| Closed hat | 14.2 (ch) | 7.7 (ch nv2) | -0.12 | -0.06 | +0.81 (ch 4000 Hz) | +5.4 (ch) | +0.0 (ch main) |

### tr_y: transistors: 2SC1815-Y (Kaz-electro, measured)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 153.2 (bd_dec1_acc) | 161.3 (bd_dec1_acc ntop) | -0.68 | -0.46 | -14.40 (bd_dec1_acc 62 Hz) | +5.9 (bd_dec1_acc) | -86.3 (bd_dec1_acc sigh) |
| Toms, congas | 5.9 (lt_t1_acc) | 45.5 (hc nfl) | -0.42 | -0.12 | -0.30 (ht_acc_l1 250 Hz) | +0.3 (lt_t1_acc) | -2.3 (lt_t1_acc body) |
| Rim shot, claves | 36.9 (rs) | 72.7 (rs_acc_l1 nf) | -0.18 | -0.31 | -7.72 (rs_acc 630 Hz) | +26.3 (rs) | +0.0 (rs high) |
| Snare | 10.3 (sd) | 677.5 (sd_snap0 nc48) | -0.12 | -0.09 | +2.27 (sd_tone0 6350 Hz) | +7.9 (sd) | -0.0 (sd_snap1_acc_l1 high) |
| Clap, maracas | 48.2 (ma_acc) | 44.7 (cp nc70) | -2.09 | -2.00 | -3.91 (ma_acc 16000 Hz) | +3.3 (cp) | - () |
| Cowbell | 2.4 (cb_acc_l1) | 3.6 (cb nv2) | -0.08 | -0.02 | +0.49 (cb 1000 Hz) | +5.8 (cb) | -0.0 (cb main) |
| Cymbal | 27.0 (cy) | 6.7 (cy_t1_acc_l1 nv) | +0.51 | +0.16 | +1.59 (cy 12699 Hz) | +6.4 (cy_t0) | +0.0 (cy_t0 main) |
| Open hat | 16.9 (oh_d1) | 5.8 (oh_d1 ne1) | -0.40 | -0.57 | -1.38 (oh_d1 8000 Hz) | -5.4 (oh_d1) | -0.0 (oh_d1 main) |
| Closed hat | 30.7 (ch) | 3.6 (ch nv2) | -0.20 | -0.11 | +1.67 (ch 4000 Hz) | +7.9 (ch) | +0.1 (ch main) |

### tr_dc: transistors: today's card plus the GR part's VAF, RB, IKF, RE, RC only

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 0.7 (bd) | 0.7 (bd nout) | -0.01 | -0.03 | -0.04 (bd_dec1_acc 62 Hz) | -0.0 (bd_dec0) | +0.4 (bd sigh) |
| Toms, congas | 0.8 (lt_t1_acc) | 1.4 (mt nfl) | -0.01 | -0.01 | -0.04 (ht_acc_l1 250 Hz) | +0.0 (ht_acc_l1) | -0.3 (lt_t1_acc body) |
| Rim shot, claves | 0.8 (rs_acc) | 0.8 (rs_acc ne63) | -0.05 | -0.03 | -0.15 (rs_acc_l1 8000 Hz) | -0.1 (rs) | +0.0 (rs_acc low) |
| Snare | 1.0 (sd_acc) | 1.7 (sd_snap0 nc48) | -0.04 | -0.04 | -0.19 (sd_snap1_acc_l1 10079 Hz) | -1.0 (sd_tone1_acc) | -0.0 (sd_snap0 high) |
| Clap, maracas | 2.7 (ma_acc) | 21.1 (cp n2) | -0.10 | -0.12 | -0.35 (ma 4000 Hz) | +0.4 (cp_acc_l1) | - () |
| Cowbell | 0.4 (cb) | 0.4 (cb ncbo) | -0.01 | -0.02 | -0.07 (cb 1000 Hz) | -0.0 (cb) | +0.0 (cb main) |
| Cymbal | 2.7 (cy) | 0.9 (cy_t0 nj) | -0.18 | -0.06 | -0.11 (cy 12699 Hz) | -0.1 (cy_t0) | -0.0 (cy_t0 main) |
| Open hat | 2.6 (oh) | 0.1 (oh_acc_l1 ne1) | -0.09 | -0.03 | -0.12 (oh_acc_l1 12699 Hz) | -0.1 (oh_acc_l1) | -0.0 (oh_acc_l1 main) |
| Closed hat | 3.7 (ch) | 0.3 (ch_acc_l1 nce) | -0.13 | -0.06 | -0.15 (ch_acc_l1 12699 Hz) | -0.0 (ch) | -0.0 (ch main) |

### tr_is: transistors: today's card with the GR part's IS, BF, NF, BR only

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 40.7 (bd_dec1_acc) | 43.3 (bd_dec1_acc ntop) | +0.07 | +0.24 | +2.23 (bd_dec1_acc 62 Hz) | +3.3 (bd_dec1_acc) | +12.2 (bd_dec1_acc late) |
| Rim shot, claves | 1.5 (rs) | 1.7 (rs_acc nf) | +0.03 | +0.03 | -0.90 (rs_acc 630 Hz) | -0.4 (rs) | +0.0 (rs_acc low) |
| Snare | 1.2 (sd) | 1.7 (sd ne49) | +0.10 | +0.08 | +0.13 (sd 2000 Hz) | -0.6 (sd_acc) | +0.0 (sd high) |
| Clap, maracas | 6.0 (ma) | 16.3 (cp n2) | -0.09 | -0.16 | +0.72 (ma 4000 Hz) | +0.7 (ma) | - () |
| Cowbell | 0.3 (cb) | 0.3 (cb ncbo) | +0.03 | +0.02 | +0.01 (cb 4000 Hz) | -0.0 (cb) | +0.0 (cb main) |
| Open hat | 3.0 (oh) | 2.3 (oh ne1) | +0.03 | -0.01 | -0.15 (oh 3175 Hz) | -1.9 (oh) | -0.0 (oh main) |

### tr_ise: transistors: today's card with the GR part's low-current recombination (ISE, NE, ISC, NC) only

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 150.2 (bd_dec1_acc) | 157.3 (bd_dec1_acc ntop) | -0.21 | +0.06 | -11.25 (bd_dec1_acc 62 Hz) | +3.8 (bd_dec1_acc) | -68.5 (bd_dec1_acc sigh) |
| Rim shot, claves | 31.7 (rs) | 54.5 (rs_acc nf) | +0.35 | +0.12 | -6.79 (rs 630 Hz) | +19.2 (rs) | -0.0 (rs_acc low) |
| Snare | 8.9 (sd) | 14.5 (sd ne49) | +0.08 | +0.06 | +1.93 (sd 6350 Hz) | +7.1 (sd) | -0.0 (sd high) |
| Clap, maracas | 21.3 (ma) | 40.2 (cp nc70) | -0.46 | -0.87 | -3.66 (cp 1000 Hz) | -2.4 (ma) | - () |
| Cowbell | 1.7 (cb) | 1.7 (cb ncbo) | -0.03 | +0.02 | +0.28 (cb 1587 Hz) | +5.7 (cb) | +0.0 (cb main) |
| Open hat | 14.8 (oh) | 2.7 (oh ne1) | -0.03 | -0.17 | -0.54 (oh 6350 Hz) | -0.7 (oh) | -0.0 (oh main) |

### tr_ac: transistors: today's card with the GR part's capacitances, TF and TR only

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 5.3 (bd_dec1_acc) | 48.5 (bd_acc npulse) | -0.10 | -0.10 | -0.36 (bd_dec1_acc 62 Hz) | +0.1 (bd_dec1_acc) | -1.8 (bd_acc sigh) |
| Rim shot, claves | 19.5 (rs_acc) | 33.0 (cl ne63) | +0.05 | -0.02 | +0.25 (rs_acc 12699 Hz) | +0.0 (cl) | +0.0 (rs high) |
| Snare | 4.2 (sd) | 92.4 (sd np1) | -0.10 | -0.09 | -1.04 (sd 20159 Hz) | -3.0 (sd_acc) | -0.0 (sd_acc high) |
| Clap, maracas | 9.9 (cp) | 58.0 (cp n2) | +0.48 | +0.08 | -0.27 (ma 4000 Hz) | +0.1 (cp) | - () |
| Cowbell | 1.1 (cb) | 9.5 (cb nv2) | -0.01 | -0.05 | +0.21 (cb 1000 Hz) | -0.1 (cb) | -0.0 (cb main) |
| Open hat | 9.8 (oh) | 9.5 (oh nv2) | -0.29 | -0.08 | -0.37 (oh 12699 Hz) | -0.1 (oh) | -0.0 (oh main) |

### fol_ideal: followers as the engine's ideal buffers

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 2.7 (bd_tone0_acc) | 0.0 (bd_dec1_acc ntop) | +0.23 | +0.23 | +0.23 (bd_tone0_acc 50 Hz) | -0.0 (bd_dec0) | +0.0 (bd_dec1_acc late) |
| Cowbell | 0.0 (cb_acc_l1) | 0.0 (cb_acc_l1 nhb) | -0.00 | -0.00 | +0.00 (cb 4000 Hz) | +0.0 (cb) | +0.0 (cb_acc_l1 main) |
| Cymbal | 0.6 (cy) | 0.0 (cy_t1_acc_l1 nhb) | +0.00 | +0.01 | +0.23 (cy_t0 2000 Hz) | +6.4 (cy_t0) | +0.0 (cy_t0 main) |
| Open hat | 7.7 (oh_acc_l1) | 0.0 (oh_d1 ne1) | +0.34 | +0.39 | +0.61 (oh_acc_l1 10079 Hz) | -0.4 (oh_d0) | -0.0 (oh_acc_l1 main) |
| Closed hat | 8.7 (ch) | 0.1 (ch nce) | +0.37 | +0.48 | +0.70 (ch_acc_l1 12699 Hz) | -0.0 (ch) | -0.0 (ch main) |

### fol_gr: followers only: 2SC1815-GR

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 0.5 (bd_tone0_acc) | 0.0 (bd_dec1_acc ntop) | -0.04 | -0.04 | -0.04 (bd_tone0_acc 50 Hz) | -0.0 (bd_tone0_acc) | +0.0 (bd_dec1_acc late) |
| Cowbell | 0.0 (cb_acc_l1) | 0.0 (cb_acc_l1 nhb) | +0.00 | +0.00 | -0.00 (cb_acc_l1 4000 Hz) | +0.0 (cb) | +0.0 (cb main) |
| Cymbal | 0.1 (cy) | 0.0 (cy_t1_acc_l1 nhb) | -0.00 | -0.00 | -0.05 (cy_acc 1587 Hz) | -0.1 (cy_t0) | -0.0 (cy_t0 main) |
| Open hat | 1.1 (oh_acc_l1) | 0.0 (oh_d1 nhb) | -0.05 | -0.07 | -0.08 (oh_acc_l1 8000 Hz) | +0.0 (oh_d0) | +0.0 (oh_acc_l1 main) |
| Closed hat | 1.2 (ch_acc_l1) | 0.0 (ch nce) | -0.05 | -0.08 | -0.10 (ch_acc_l1 12699 Hz) | +0.0 (ch) | +0.0 (ch main) |

### sch_edge: Schmitt: typical thresholds, 455 ohm output, 100 ns edges

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Cowbell | 1.1 (cb) | 1.3 (cb nsum) | -0.00 | -0.03 | -0.20 (cb 1000 Hz) | -0.4 (cb) | +0.0 (cb_acc_l1 main) |
| Cymbal | 2.5 (cy_t1_acc_l1) | 1.3 (cy_t1_acc_l1 nsum) | -0.07 | -0.06 | -0.10 (cy 12699 Hz) | -0.1 (cy_t0) | -0.0 (cy_acc main) |
| Open hat | 2.5 (oh) | 1.3 (oh_d1 nsum) | -0.05 | -0.05 | -0.09 (oh_acc_l1 12699 Hz) | -0.2 (oh_d1) | -0.0 (oh_acc_l1 main) |
| Closed hat | 3.8 (ch_acc_l1) | 1.3 (ch_acc_l1 nsum) | +0.06 | -0.07 | -0.13 (ch_acc_l1 12699 Hz) | -0.0 (ch) | -0.0 (ch main) |

### sch_edge_max: Schmitt: typical thresholds, 784 ohm output, 200 ns edges

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Cowbell | 2.0 (cb) | 2.2 (cb nsum) | -0.00 | -0.05 | -0.37 (cb 1000 Hz) | -3.0 (cb) | +0.0 (cb_acc_l1 main) |
| Cymbal | 4.3 (cy_t1_acc_l1) | 2.3 (cy_t1_acc_l1 nsum) | -0.12 | -0.10 | -0.17 (cy 12699 Hz) | -0.1 (cy_t0) | -0.0 (cy_t1_acc_l1 main) |
| Open hat | 4.1 (oh) | 2.3 (oh_d1 nsum) | -0.08 | -0.08 | -0.15 (oh_acc_l1 12699 Hz) | -0.2 (oh_d1) | +0.0 (oh_d0 main) |
| Closed hat | 6.0 (ch_acc_l1) | 2.2 (ch_acc_l1 nsum) | +0.10 | -0.12 | -0.21 (ch_acc_l1 12699 Hz) | -0.1 (ch_acc_l1) | -0.0 (ch main) |

### sch_lo: Schmitt: VT+ 1.9, VT- 1.6 V (both at their minimum)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Cowbell | 101.1 (cb) | 155.1 (cb nlb) | -7.69 | -12.10 | -35.24 (cb 794 Hz) | -45.2 (cb) | -689.5 (cb main) |
| Cymbal | 150.1 (cy_t1_acc_l1) | 153.4 (cy_t0 nlb) | +2.34 | +1.89 | +3.59 (cy_t0 1000 Hz) | -26.4 (cy_t1_acc_l1) | -180.7 (cy_t1_acc_l1 main) |
| Open hat | 146.5 (oh_d0) | 153.3 (oh_d0 nlb) | +2.45 | +1.46 | +3.43 (oh 3175 Hz) | -3.3 (oh_d1) | -181.0 (oh_d0 main) |
| Closed hat | 170.8 (ch) | 153.4 (ch nlb) | +1.77 | +2.94 | +5.36 (ch 6350 Hz) | -11.8 (ch) | -181.0 (ch_acc_l1 main) |

### sch_hi: Schmitt: VT+ 3.4, VT- 3.15 V (both at their maximum)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Cowbell | 55.5 (cb_acc_l1) | 155.1 (cb nlb) | -0.64 | -3.43 | +6.06 (cb 1000 Hz) | -23.4 (cb_acc_l1) | +0.1 (cb_acc_l1 main) |
| Cymbal | 147.8 (cy_t1_acc_l1) | 153.0 (cy_t0 nlb) | +2.76 | +1.49 | +5.80 (cy_t1_acc_l1 1260 Hz) | +13.0 (cy_t0) | -193.6 (cy_t0 main) |
| Open hat | 147.4 (oh_d0) | 154.0 (oh_acc_l1 nlb) | +0.94 | +1.32 | +4.71 (oh 3175 Hz) | -1.2 (oh_d0) | -193.5 (oh_acc_l1 main) |
| Closed hat | 156.4 (ch_acc_l1) | 153.7 (ch_acc_l1 nlb) | +1.71 | +2.16 | +5.17 (ch 5040 Hz) | +5.6 (ch) | +124.5 (ch main) |

### sch_wide: Schmitt: VT+ 3.1, VT- 2.1 V (1.0 V hysteresis, the maximum)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Cowbell | 14.5 (cb_acc_l1) | 125.4 (cb_acc_l1 nhb) | +0.43 | -0.60 | +2.82 (cb 1000 Hz) | -5.6 (cb_acc_l1) | +0.0 (cb_acc_l1 main) |
| Cymbal | 131.9 (cy_t0) | 126.0 (cy_t0 nhb) | +1.04 | -0.35 | -1.71 (cy_t1_acc_l1 1000 Hz) | -32.4 (cy_t1_acc_l1) | -0.2 (cy_t0 main) |
| Open hat | 130.4 (oh_d0) | 126.0 (oh nhb) | -1.40 | -0.49 | -1.50 (oh_d0 3175 Hz) | -4.8 (oh_d1) | -0.3 (oh_d0 main) |
| Closed hat | 141.4 (ch) | 125.6 (ch_acc_l1 nhb) | +0.58 | +0.40 | -1.39 (ch_acc_l1 3175 Hz) | -10.3 (ch_acc_l1) | -0.1 (ch_acc_l1 main) |

### sch_narrow: Schmitt: VT+ 2.65, VT- 2.4 V (0.25 V hysteresis, the minimum)

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Cowbell | 9.0 (cb_acc_l1) | 146.5 (cb nhb) | +0.25 | -0.36 | +1.85 (cb 1000 Hz) | -2.7 (cb_acc_l1) | -0.0 (cb main) |
| Cymbal | 150.3 (cy_t1_acc_l1) | 146.7 (cy nhb) | +2.11 | +1.76 | +7.71 (cy_t1_acc_l1 1260 Hz) | -18.2 (cy_t1_acc_l1) | -282.5 (cy_t0 main) |
| Open hat | 143.3 (oh_d0) | 146.8 (oh nhb) | -1.38 | +1.16 | +4.15 (oh_d1 4000 Hz) | -2.6 (oh_d0) | -282.6 (oh_d0 main) |
| Closed hat | 166.4 (ch) | 147.0 (ch_acc_l1 nhb) | +1.41 | +2.51 | +4.84 (ch 4000 Hz) | -6.7 (ch_acc_l1) | -282.5 (ch_acc_l1 main) |

### all: uPC4558 op-amps, 2SC1815-GR transistors and followers, the Schmitt's typical edges

| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |
|---|---|---|---|---|---|---|---|
| Bass drum | 141.7 (bd_dec1_acc) | 148.6 (bd_dec1_acc ntop) | -0.29 | -0.16 | -9.06 (bd_dec1_acc 62 Hz) | +9.3 (bd_acc) | -62.2 (bd_dec1_acc sigh) |
| Toms, congas | 5.5 (ht_acc_l1) | 32.0 (hc nfl) | -0.21 | -0.11 | -0.35 (ht_acc_l1 250 Hz) | +1.6 (mc_acc) | +0.7 (hc body) |
| Rim shot, claves | 26.0 (rs) | 40.4 (rs_acc nf) | -0.65 | -0.81 | +5.48 (rs_acc 1260 Hz) | +17.4 (rs) | +1.6 (cl high) |
| Snare | 11.5 (sd_tone0) | 426.0 (sd_snap0 nc48) | -0.52 | -0.80 | -1.97 (sd_tone1_acc 157 Hz) | +3.6 (sd_tone0) | +0.2 (sd_tone1_acc low) |
| Clap, maracas | 21.1 (ma_acc) | 47.2 (cp_acc n2) | -0.88 | -0.78 | -2.85 (cp 1000 Hz) | -2.2 (ma) | - () |
| Cowbell | 1.1 (cb_acc_l1) | 9.9 (cb_acc_l1 nv2) | -0.03 | -0.04 | +0.21 (cb 1000 Hz) | -0.0 (cb_acc_l1) | -0.0 (cb main) |
| Cymbal | 12.0 (cy) | 9.7 (cy_acc nv2) | -0.13 | +0.18 | +0.80 (cy 12699 Hz) | +6.4 (cy_t0) | +0.0 (cy main) |
| Open hat | 6.9 (oh) | 9.7 (oh_acc_l1 nv2) | -0.25 | -0.23 | -0.65 (oh_d1 20159 Hz) | -3.6 (oh_d1) | -0.0 (oh_acc_l1 main) |
| Closed hat | 13.3 (ch) | 9.7 (ch_acc_l1 nv2) | -0.12 | -0.11 | +0.69 (ch 4000 Hz) | +5.4 (ch) | +0.0 (ch main) |

### The fitted parts' measures

| Variant | C39: bd sigh, body pitch c; h2, h3 dB | Toms' Rfb: t20 % lt, mt, lc, hc | C60/R134: oh t20 % d0, d.5, d1 | C139: clap tail dB | R312: ring c rs, cl | R313: cl t20 % | R324: rs over cl dB |
|---|---|---|---|---|---|---|---|
| oa_ti | -8.6 -0.0; -1.22 -1.26 | +0.1 +0.0 +1.5 +1.4 | -0.3 -0.0 +0.0 | -0.00 | +0.7 +1.4 | -4.2 | +0.02 |
| oa_upc | -3.5 -0.0; -0.48 -0.49 | +0.1 +0.0 +1.5 +1.4 | -0.3 +0.0 +0.0 | -0.00 | +0.8 +1.6 | -2.7 | +0.16 |
| tr_gr | -6.8 +1.8; +1.20 -0.88 | +0.0 +0.0 +0.0 +0.0 | -0.7 -2.0 -3.6 | -3.26 | +0.0 -0.0 | +0.0 | +0.25 |
| tr_y | -4.7 +4.2; +2.63 -0.58 | +0.0 +0.0 +0.0 +0.0 | -0.6 -1.1 -5.4 | -3.97 | +0.0 -0.0 | +0.0 | +0.30 |
| tr_dc | +0.4 +0.0; +0.04 +0.05 | +0.0 +0.0 +0.0 +0.0 | +0.0 -0.1 +0.0 | +0.10 | -0.0 +0.0 | +0.0 | -0.03 |
| tr_is | +5.8 +0.0; +0.83 +0.85 | - - - - | - -1.9 - | -0.37 | +0.0 +0.0 | +0.0 | -0.02 |
| tr_ise | -8.5 +2.1; +1.18 -1.18 | - - - - | - -0.7 - | -4.32 | -0.0 +0.0 | +0.0 | +0.39 |
| tr_ac | -1.0 +0.0; -0.19 -0.19 | - - - - | - -0.1 - | -0.00 | +0.0 -0.0 | +0.0 | -0.05 |
| fol_ideal | -0.0 +0.0; -0.01 -0.00 | - - - - | -0.4 -0.0 -0.2 | - | - - | - | - |
| fol_gr | +0.0 -0.0; -0.00 +0.00 | - - - - | +0.0 +0.0 +0.0 | - | - - | - | - |
| sch_edge | - -; - - | - - - - | +0.0 -0.0 -0.2 | - | - - | - | - |
| sch_edge_max | - -; - - | - - - - | +0.0 -0.0 -0.2 | - | - - | - | - |
| sch_lo | - -; - - | - - - - | -2.1 +0.1 -3.3 | - | - - | - | - |
| sch_hi | - -; - - | - - - - | -1.2 -0.3 +0.0 | - | - - | - | - |
| sch_wide | - -; - - | - - - - | -3.6 +0.2 -4.8 | - | - - | - | - |
| sch_narrow | - -; - - | - - - - | -2.6 +0.3 -0.2 | - | - - | - | - |
| all | -8.9 +1.6; +0.80 -1.25 | +0.1 +0.1 +1.5 +1.4 | -0.7 -2.0 -3.6 | -3.26 | +0.8 +1.6 | -2.7 | +0.31 |

### The six oscillators by the Schmitt's thresholds (Hz; the cowbell's two held by TM1, TM2)

| Thresholds | VT+, VT- V | Osc 1..6 Hz | Duty % |
|---|---|---|---|
| typ | 2.7, 2.1 | 554.0, 825.0, 522.7, 304.4, 369.6, 205.3 | 48.0 |
| lo | 1.9, 1.6 | 554.0, 825.0, 955.7, 556.6, 675.8, 375.5 | 35.0 |
| hi | 3.4, 3.15 | 554.0, 825.0, 1139.8, 663.8, 806.0, 447.8 | 65.5 |
| wide | 3.1, 2.1 | 554.0, 825.0, 310.9, 181.0, 219.8, 122.1 | 52.1 |
| narrow | 2.65, 2.4 | 554.0, 825.0, 1261.4, 734.6, 892.0, 495.6 | 50.5 |
| narrow_hd | 2.45, 2.35 | 554.0, 825.0, 3151.1, 1835.0, 2228.3, 1237.9 | 48.0 |

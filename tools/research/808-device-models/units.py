"""The fuller transistor and op-amp models against the two real units (the user's choice after the device-model report):
the measures that moved, taken alike on Fischer's and Audiorealism's recordings and on ngspice renders of today's
netlists, the 2SC1815-GR card and the uPC4558 model, all at the units' 4.7 V.

  python tools/research/808-device-models/units.py --work <folder> [--jobs 14]

- Bass drum (C39): Tone full, Fischer's BD1025, BD1050, BD1075 and Audiorealism's TUNE4 DECAY1 to 3. The model at six
  Decay settings, each measure read at the unit's own time to -40 dB. The sigh in semitones over the settled tail (its
  depth at 25 to 35 ms, at 100 ms, its time constant), the body's 2nd and 3rd harmonics.
- Rim shot, claves: the decay to a tenth of a 2 ms rms envelope; Fischer's one hit, Audiorealism's sixteen (median).
- Snare: Snappy 0, Tone 0, 5 and 10; the lower shell's band (140 to 220 Hz) over the upper's (270 to 420 Hz), 2 to 60 ms.
- Clap (C139): the clap-tail test's measure (a 10 ms rms envelope in dB under its peak at ten times); four noise seeds.
Renders are cached in <folder>, as measure.py's.
"""
import argparse
import concurrent.futures
import glob
import json
import math
import os
import sys

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure  # noqa: E402

REF = os.path.join(measure.ROOT, "tools", "references")
FISCHER = os.path.join(REF, "fischer-808")
AR = os.path.join(REF, "audiorealism-808", "TR-808-96kHz-24bit")
VARIANTS = ["base", "tr_gr", "oa_upc"]
U = 4.7
KICK_DECAYS = [0.2, 0.35, 0.5, 0.7, 0.9, 1.15]
CLAP_TIMES = [0.02, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.7, 1.0]


def cases():
    c = []
    for d in KICK_DECAYS:
        c.append(dict(netlist="bassdrum.cir", kind="bd", name="u_bd_{:g}".format(d), tend=2.0,
                      set=dict(amp=U, dec=d, tone=0.99, lev=0.99)))
    for name, rs in (("u_rs", 1), ("u_cl", 0)):
        c.append(dict(netlist="rimclave.cir", kind="rim", name=name, tend=0.1, set=dict(amp=U, rs=rs)))
    for tone in (0, 0.5, 1):
        c.append(dict(netlist="snare.cir", kind="sd", name="u_sd_t{:g}".format(tone), tend=0.15,
                      set=dict(amp=U, tone=tone, snappy=0, wn=0)))
    for seed in (1, 2, 3, 4):
        c.append(dict(netlist="clap.cir", kind="cp", name="u_cp_s{}".format(seed), tend=1.05, set=dict(amp=U, lev=1),
                      noise=(seed, 0.267)))
    return c


def read(path):
    rate, x = wavfile.read(path)
    x = np.asarray(x, dtype=float)
    if x.ndim > 1:
        x = x[:, 0]
    return x / max(np.abs(x).max(), 1e-30), float(rate)


def from_onset(x, fs, frac=0.02):
    on = int(np.argmax(np.abs(x) > frac * np.abs(x).max()))
    return x[on:]


def rendered(work, case, variant):
    level = measure.found(work, case["name"], variant) if variant != "base" else 0
    a = np.load(measure.render_path(work, case["name"], variant, level or 0))
    out = a[1 + measure.nodes_of(case).index("vout")]
    return out - out[0], measure.RATE


# --- the measures ---------------------------------------------------------------------------------------------------

def half_cycles(x, fs):
    """Each half cycle's frequency and middle time, from the zero crossings (interpolated)."""
    s = np.signbit(x)
    idx = np.nonzero(s[:-1] != s[1:])[0]
    t = (idx + x[idx] / (x[idx] - x[idx + 1])) / fs
    return 0.5 * (t[1:] + t[:-1]), 0.5 / np.diff(t)


def kick(x, fs):
    x = from_onset(x, fs)  # the recordings carry no dc and start at the hit; the renders start at rest
    t40 = measure.decay_time(x, fs, 40)
    tm, f = half_cycles(x, fs)
    tail = f[(tm > 0.6 * t40) & (tm < t40)]
    ftail = float(np.median(tail))
    s = 12 * np.log2(f / ftail)
    at = lambda a, b: float(np.mean(s[(tm >= a) & (tm <= b)]))
    sel = (tm > 0.02) & (tm < min(0.2, 0.6 * t40)) & (s > 0.05)
    slope = np.polyfit(tm[sel], np.log(s[sel]), 1)[0] if sel.sum() > 3 else float("nan")
    fbody = float(np.median(f[(tm > 0.02) & (tm < 0.25)]))
    h2, h3 = measure.harmonic(x, fs, fbody, 0.02, 0.25)
    return dict(t40=t40, ftail=ftail, depth=at(0.025, 0.035), s100=at(0.09, 0.11), tau=-1000.0 / slope, h2=h2, h3=h3)


def decay_tenth(x, fs):
    x = from_onset(x, fs)
    env = measure.envelope(x, fs, 0.002)
    k = int(np.argmax(env))
    below = np.nonzero(env[k:] < 0.1 * env[k])[0]
    return 1000.0 * below[0] / fs if below.size else float("nan")


def shells(x, fs):
    x = from_onset(x, fs)
    seg = x[int(0.002 * fs):int(0.06 * fs)]
    seg = (seg - seg.mean()) * np.hanning(len(seg))
    spec = np.abs(np.fft.rfft(seg, 1 << 18)) ** 2
    freqs = np.fft.rfftfreq(1 << 18, 1.0 / fs)
    band = lambda a, b: spec[(freqs >= a) & (freqs < b)].sum()
    return 10 * math.log10(band(140, 220) / band(270, 420))


def clap(x, fs):
    """The clap-tail test's envelopeDb: a 10 ms rms envelope from the onset (2 % of the peak), in dB under its peak."""
    x = from_onset(x, fs)
    k = max(3, int(0.010 * fs))
    p = np.convolve(x * x, np.ones(k) / k, mode="full")[k // 2:k // 2 + len(x)]
    env = np.sqrt(np.maximum(p, 0))
    top = env.max()
    return [20 * math.log10(env[min(len(env) - 1, int(t * fs))] / top + 1e-12) for t in CLAP_TIMES]


# --- the comparison -------------------------------------------------------------------------------------------------

def interp_at(points, t40, key):
    pts = sorted((p["t40"], p[key]) for p in points if p["t40"] == p["t40"] and p[key] == p[key])
    xs, ys = zip(*pts)
    return float(np.interp(t40, xs, ys)) if xs[0] <= t40 <= xs[-1] else float("nan")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--work", required=True)
    p.add_argument("--jobs", type=int, default=14)
    args = p.parse_args()
    os.makedirs(args.work, exist_ok=True)
    cs = cases()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futs = {pool.submit(measure.render, c, v, args.work): (c["name"], v) for c in cs for v in VARIANTS}
        for fut in concurrent.futures.as_completed(futs):
            print("{:>12} {:>7} {}".format(*futs[fut], "FAILED" if fut.result() is None else "ok"), flush=True)
    by = {c["name"]: c for c in cs}
    out = {}

    # Bass drum.
    model = {v: [kick(*rendered(args.work, by["u_bd_{:g}".format(d)], v)) for d in KICK_DECAYS] for v in VARIANTS}
    units = {"Fischer": {m: os.path.join(FISCHER, "BD", "BD10{}.WAV".format(m)) for m in ("25", "50", "75")},
             "Audiorealism": {m: os.path.join(AR, "BD-TUNE4-DECAY{}.wav".format(k)) for m, k in (("25", 1), ("50", 2), ("75", 3))}}
    rows = []
    for unit, files in units.items():
        for mark, path in files.items():
            u = kick(*read(path))
            row = {"unit": unit, "mark": mark, "unit_m": u}
            for v in VARIANTS:
                row[v] = {k: interp_at(model[v], u["t40"], k) for k in ("depth", "s100", "tau", "h2", "h3")}
            rows.append(row)
    out["bd"] = {"rows": rows, "model": model}
    print("\nBass drum, Tone full, read at each unit's time to -40 dB: unit | today | GR | uPC4558")
    for r in rows:
        u = r["unit_m"]
        print(" {} DECAY {}: t40 {:.0f} ms".format(r["unit"], r["mark"], 1000 * u["t40"]))
        for k, unit in (("depth", "st"), ("s100", "st"), ("tau", "ms"), ("h2", "dB"), ("h3", "dB")):
            print("   {:>5} {:7.2f} | {:7.2f} | {:7.2f} | {:7.2f} {}".format(k, u[k], r["base"][k], r["tr_gr"][k], r["oa_upc"][k], unit))

    # Rim shot and claves.
    print("\nDecay to a tenth (ms): unit(s) | today | GR | uPC4558")
    out["decay"] = {}
    for name, fis, pat in (("u_rs", "RS/RS.WAV", "RS-SHOT*.wav"), ("u_cl", "CL/CL.WAV", "CL-SHOT*.wav")):
        f = decay_tenth(*read(os.path.join(FISCHER, fis)))
        a = float(np.median([decay_tenth(*read(q)) for q in sorted(glob.glob(os.path.join(AR, pat)))]))
        m = {v: decay_tenth(*rendered(args.work, by[name], v)) for v in VARIANTS}
        out["decay"][name] = dict(fischer=f, audiorealism=a, **m)
        print(" {:>4}: Fischer {:.2f}, Audiorealism {:.2f} | {:.2f} | {:.2f} | {:.2f}".format(name[2:], f, a, m["base"], m["tr_gr"], m["oa_upc"]))

    # Snare.
    print("\nSnare, Snappy 0: lower shell over upper (dB), 2 to 60 ms: Fischer, Audiorealism | today | GR | uPC4558")
    out["sd"] = {}
    for tone, fn, an in ((0, "SD0000.WAV", 0), (0.5, "SD5000.WAV", 2), (1, "SD1000.WAV", 4)):
        f = shells(*read(os.path.join(FISCHER, "SD", fn)))
        a = shells(*read(os.path.join(AR, "SD-TONE{}-SNAPPY0.wav".format(an))))
        m = {v: shells(*rendered(args.work, by["u_sd_t{:g}".format(tone)], v)) for v in VARIANTS}
        out["sd"][str(tone)] = dict(fischer=f, audiorealism=a, **m)
        print(" TONE {:>4}: {:+.1f}, {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f}".format(tone * 10, f, a, m["base"], m["tr_gr"], m["oa_upc"]))

    # Clap.
    f = clap(*read(os.path.join(FISCHER, "CP", "CP.WAV")))
    a = list(np.median([clap(*read(q)) for q in sorted(glob.glob(os.path.join(AR, "CLAP-SHOT*.wav")))], axis=0))
    print("\nClap, dB under the peak at", " ".join("{:g}".format(1000 * t) for t in CLAP_TIMES), "ms")
    print("   Fischer      ", " ".join("{:6.1f}".format(x) for x in f))
    print("   Audiorealism ", " ".join("{:6.1f}".format(x) for x in a))
    out["cp"] = {"fischer": f, "audiorealism": a}
    for v in VARIANTS:
        seeds = [clap(*rendered(args.work, by["u_cp_s{}".format(s)], v)) for s in (1, 2, 3, 4)]
        m = list(np.median(seeds, axis=0))
        late = [(m[i] - u[i]) ** 2 for i, t in enumerate(CLAP_TIMES) if t >= 0.15 for u in (f, a)]
        bias = np.mean([m[i] - (f[i] + a[i]) / 2 for i, t in enumerate(CLAP_TIMES) if t >= 0.15])
        out["cp"][v] = dict(env=m, rms_late=math.sqrt(np.mean(late)), bias_late=float(bias))
        print("   {:<13}".format(v), " ".join("{:6.1f}".format(x) for x in m),
              "| from 150 ms rms {:.1f} dB, mean {:+.1f} dB".format(math.sqrt(np.mean(late)), bias))
    with open(os.path.join(args.work, "units.json"), "w") as fh:
        json.dump(out, fh, indent=1, default=float)


if __name__ == "__main__":
    main()

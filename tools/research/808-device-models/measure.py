"""How much each simplified device model in the TR-808 reference netlists changes the sound (ngspice only).

Each variant swaps one model in the netlists of external/coolsynth-engine/tools (tr808/*.cir, kick808/bassdrum.cir),
renders the same hits as today's netlists, and compares them with today's renders: rms error per stage, level,
third-octave bands, decay and pitch. The report is docs/research/808-device-models.md.

Usage (numpy needed, so not -I):
  python tools/research/808-device-models/measure.py --work <folder> [--jobs 14] [--only bd,lt] [--variants base,oa_upc]
  python tools/research/808-device-models/measure.py --work <folder> --summary     (the tables, as in results.md)

Renders are cached in <folder> (one .npy each; a run ngspice stops leaves a .log and is tried again with looser
solver settings, its reference alike, as <case>__<variant>@1.npy); the measures go to <folder>/results.json.
ngspice: COOLSYNTH_NGSPICE, else tools/ngspice (D235). TI's RC4558 macromodel (SLOJ053, https://www.ti.com/lit/zip/SLOJ053)
is read from tools/references/rc4558/RC4558.301, outside git like ngspice.
"""
import argparse
import concurrent.futures
import importlib.util
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TOOLS = os.path.join(ROOT, "external", "coolsynth-engine", "tools")
NGSPICE = os.environ.get("COOLSYNTH_NGSPICE") or os.path.join(ROOT, "tools", "ngspice", "Spice64", "bin", "ngspice_con.exe")
TI_MODEL = os.path.join(ROOT, "tools", "references", "rc4558", "RC4558.301")
RATE = 96000.0

_spec = importlib.util.spec_from_file_location("render808", os.path.join(TOOLS, "tr808", "render.py"))
render808 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(render808)

KICK_NODES = ["vout", "nout", "neg", "npulse", "ntop", "vin", "trig"]

# ---------------------------------------------------------------------------------------------------------------------
# The fuller models.

# Kaz-electro's measured 2SC1815 models (https://kaz-electro.jp/lab/2sc1815-spice/, one part per hFE rank, March 2022,
# fitted in Micro-Cap from pulsed measurements). GR (hFE 200 to 400) is the 2SC945(P)'s range (P: 200 to 400).
GP_CARDS = {
    "GR": "IS=16.045f BF=275.022 NF=0.99569 VAF=526.372 IKF=0.293 ISE=157.886f NE=1.62711 BR=4.268 VAR=88 IKR=0.056 "
          "ISC=57.614p NC=1.8 RE=0.546 RB=34 RBM=0.38 IRB=0.0029 RC=0.332 CJE=11.936p VJE=0.637 MJE=0.328 CJC=5.046p "
          "VJC=0.408 MJC=0.302 FC=0.92 TF=451.405p XTF=3 VTF=2.5 ITF=0.134 TR=919.518n XTB=1.9",
    "Y": "IS=9.097f BF=173.744 NF=0.99634 VAF=400.547 IKF=0.558 ISE=51.624f NE=1.66209 BR=4.519 VAR=76 IKR=0.030 "
         "ISC=92.629p NC=1.8 RE=0.539 RB=16 RBM=0.68 IRB=0.002 RC=0.254 CJE=14.686p VJE=0.622 MJE=0.315 CJC=5.387p "
         "VJC=0.393 MJC=0.275 FC=0.9 TF=609.854p XTF=1.1 VTF=2.5 ITF=0.166 TR=1.104u XTB=1.7",
}
TODAY_Q = "IS=10f BF=300 BR=4 CJE=8p CJC=4p"
# Today's card with only the GR part's Early voltage, base resistance and knee added (the dc effects alone).
EARLY_Q = TODAY_Q + " VAF=526.372 VAR=88 RB=34 RBM=0.38 IRB=0.0029 IKF=0.293 IKR=0.056 RE=0.546 RC=0.332"
# The GR card in parts, each onto today's card, to find which one moves a voice: its saturation current and gains;
# its low-current recombination (beta falling at small currents); its charge (junction capacitances, transit and
# storage times).
PART_Q = {
    "tr_is": "IS=16.045f BF=275.022 NF=0.99569 BR=4.268 CJE=8p CJC=4p",
    "tr_ise": TODAY_Q + " ISE=157.886f NE=1.62711 ISC=57.614p NC=1.8",
    "tr_ac": "IS=10f BF=300 BR=4 CJE=11.936p VJE=0.637 MJE=0.328 CJC=5.046p VJC=0.408 MJC=0.302 FC=0.92 TF=451.405p "
             "XTF=3 VTF=2.5 ITF=0.134 TR=919.518n",
}
PART_CASES = {"bd", "bd_acc", "bd_dec1_acc", "rs", "rs_acc", "cl", "sd", "sd_acc", "cp", "ma", "cb", "oh"}

# The emitter followers the engine takes as ideal (nullors): Q44 (bass drum), Q25, Q26, Q31 (metal board).
FOLLOWERS = {"bassdrum.cir": ["Q44"], "metal.cir": ["Q25", "Q26", "Q31"]}

# The HD14584B at 5 V, 25 C (Hitachi's data sheet, tools/references/tr808-metal/HD14584B.pdf, git-ignored): VT+ 1.8 /
# 2.7 / 3.4 V, VT- 1.6 / 2.1 / 3.2 V, VH 0.10 / 0.55 / 1.0 V; tr and tf 100 ns typ, 200 ns max into 50 pF; IOH 0.88 mA
# typ, 0.44 min at 4.6 V. onsemi's MC14584B (https://www.onsemi.com/pdf/datasheet/mc14584b-d.pdf) gives the same but VH
# 0.25 V min and IOH 0.51 mA min; the corners rendered take its 0.25 V, the HD's 0.10 V is in the table only.
SCHMITT = {"typ": (2.7, 2.1), "lo": (1.9, 1.6), "hi": (3.4, 3.15), "wide": (3.1, 2.1), "narrow": (2.65, 2.4)}
SCHMITT_TABLE_ONLY = {"narrow_hd": (2.45, 2.35)}
# Output resistance from IOH at VOH 4.6 V: 0.4 V / 0.88 mA typ, / 0.51 mA (MC14584B's minimum).
EDGES = {"typ": (455.0, 100e-9), "max": (784.0, 200e-9)}


def ti_model(name="RC4558", upc=False):
    """TI's RC4558 macromodel; with upc, retuned to the uPC4558's data sheet (Renesas,
    https://www.renesas.com/en/document/dst/upc258-upc4558-datasheet: SR 1.0 V/us, IB 60 nA, Vom +-14 V into 10k) keeping its 3 MHz gain-bandwidth: IEE 20 uA (SR = IEE / C2), RE1 = RE2 40 ohm (the input
    pair's gm as before), REE 10 MOhm (200 V / IEE, as TI's), BF 167 (IB = IEE / 2 / BF), VC = VE 1.7 V."""
    with open(TI_MODEL, "r", encoding="ascii", errors="replace") as f:
        text = f.read().replace("\x1a", "")  # the 1989 DOS file ends in a Ctrl-Z
    text = text.replace(".SUBCKT RC4558", ".SUBCKT " + name)
    if upc:
        subs = [(r"IEE   3 10 DC 34.28E-6", "IEE   3 10 DC 20.0E-6"), (r"RE1  13 10 1.122E3", "RE1  13 10 40"),
                (r"RE2  14 10 1.122E3", "RE2  14 10 40"), (r"REE  10 99 5.834E6", "REE  10 99 10.0E6"),
                (r"VC    3 53 DC 2.600", "VC    3 53 DC 1.700"), (r"VE   54  4 DC 2.600", "VE   54  4 DC 1.700"),
                (r"BF=121.4", "BF=167")]
        for a, b in subs:
            if a not in text:
                raise SystemExit("TI model: no " + a)
            text = text.replace(a, b)
    return text


# ---------------------------------------------------------------------------------------------------------------------
# Netlist transforms.

def swap_opamps(lines, model):
    """Every inline op-amp (G 1 mS into an inner node, RI, CI, the B follower, clamp diodes) and metal.cir's subcircuit
    oa become the vendor macromodel on the +-15 V supplies."""
    out = list(lines)
    if any(l.lower().startswith(".subckt oa ") for l in out):
        a = next(i for i, l in enumerate(out) if l.lower().startswith(".subckt oa "))
        b = next(i for i in range(a, len(out)) if out[i].lower().startswith(".ends"))
        out[a:b + 1] = [".subckt oa inp inm out vp vn", "XA inp inm vp vn out " + model, ".ends"]
        out = [re.sub(r"^(X\S+ \S+ \S+ \S+) oa$", r"\1 vcc vee oa", l) for l in out]
        return out
    inner = {}
    for l in out:
        m = re.match(r"^G(\S+) 0 (\S+) (\S+) (\S+) 1m$", l)
        if m:
            inner[m.group(2)] = (m.group(1), m.group(3), m.group(4))
    if not inner:
        return out
    outs = {}
    for l in out:
        m = re.match(r"^B\S+ (\S+) 0 V=v\((\S+)\)$", l)
        if m and m.group(2) in inner:
            outs[m.group(2)] = m.group(1)
    new = []
    for l in out:
        m = re.match(r"^G(\S+) 0 (\S+) (\S+) (\S+) 1m$", l)
        if m and m.group(2) in inner:
            name, inp, inm = inner[m.group(2)]
            new.append("X{} {} {} vcc vee {} {}".format(name, inp, inm, outs[m.group(2)], model))
            continue
        tokens = l.split()
        gone = set(inner) | {"V=v({})".format(n) for n in inner}
        if tokens and tokens[0][0] in "RCBD" and any(t in gone for t in tokens[1:]):
            continue
        new.append(l)
    # The clamps' voltage sources are left dangling: drop each V whose first node nothing else uses.
    while True:
        used = {}
        for l in new:
            tokens = l.split()
            if tokens and tokens[0][0].isalpha() and not tokens[0].startswith("."):
                for t in tokens[1:]:
                    used[t] = used.get(t, 0) + 1
        drop = [l for l in new if l.split() and l.split()[0][0] == "V" and used.get(l.split()[1], 0) == 1]
        if not drop:
            break
        new = [l for l in new if l not in drop]
    return new


def swap_transistors(lines, card, only=None):
    """QEM and QPM become `card` (NPN and the same as PNP, as today's pair); with only, just those instances, through a
    model of their own."""
    if only is None:
        return [re.sub(r"^\.model (QEM|QPM) (NPN|PNP)\(.*\)$", lambda m: ".model {} {}({})".format(m.group(1), m.group(2), card), l)
                for l in lines]
    out = []
    for l in lines:
        tokens = l.split()
        if tokens and tokens[0] in only and tokens[-1] == "QEM":
            l = " ".join(tokens[:-1] + ["QFOL"])
        out.append(l)
    return out + [".model QFOL NPN({})".format(card)]


def ideal_followers(lines, only):
    """The followers as the engine's nullors: emitter = base, no base current."""
    out = []
    for l in lines:
        tokens = l.split()
        if tokens and tokens[0] in only:
            _, c, b, e, _ = tokens
            l = "E{} {} 0 {} 0 1".format(tokens[0], e, b)
        out.append(l)
    return out


def oscillator_lines(phases, f1, f2, cs, tend, vtp, vtm, edge=None):
    """render.py's oscillators at other thresholds; with edge (ohms, 10-90 % seconds), each from an ideal step through
    the output resistance into the capacitance that gives that edge. TM1 and TM2 hold the cowbell's two at f1, f2."""
    high_rc, low_rc = math.log((5.0 - vtm) / (5.0 - vtp)), math.log(vtp / vtm)
    lines = []
    for k, (r, c) in enumerate(render808.OSCILLATORS):
        if r is None:
            r = 1.0 / ((f1 if k == 0 else f2) * c * (high_rc + low_rc))
        rc = r * c * cs
        high, low = rc * high_rc, rc * low_rc
        period = high + low
        into = (phases[k] % 1.0) * period
        level = 5.0 if into < high else 0.0
        t = high - into if into < high else period - into
        ramp = 1e-9 if edge else 50e-9
        points = [(0.0, level)]
        while t < tend + period:
            new = 5.0 - level
            points.append((max(t - ramp, points[-1][0] + 1e-12), level))
            points.append((t + ramp, new))
            level = new
            t += high if new > 0.0 else low
        text = " ".join("{:.12g} {:g}".format(a, b) for a, b in points)
        if edge:
            rout, tr = edge
            lines += ["VO{} nos{} 0 PWL({})".format(k + 1, k + 1, text), "ROS{} nos{} no{} {:g}".format(k + 1, k + 1, k + 1, rout),
                      "COS{} no{} 0 {:.6g}".format(k + 1, k + 1, tr / (2.1972 * rout))]
        else:
            lines.append("VO{} no{} 0 PWL({})".format(k + 1, k + 1, text))
    return lines


def schmitt_frequencies(vtp, vtm, f1=554.0, f2=825.0):
    high_rc, low_rc = math.log((5.0 - vtm) / (5.0 - vtp)), math.log(vtp / vtm)
    fs = []
    for k, (r, c) in enumerate(render808.OSCILLATORS):
        fs.append((f1, f2)[k] if r is None else 1.0 / (r * c * (high_rc + low_rc)))
    return fs, high_rc / (high_rc + low_rc)


VARIANTS = {
    "base": "today's netlists",
    "oa_ti": "op-amps: TI's RC4558 macromodel (1.7 V/us, about +-13 V, 25 mA)",
    "oa_upc": "op-amps: the TI model retuned to the uPC4558 (1.0 V/us, +-14 V)",
    "tr_gr": "transistors: 2SC1815-GR (Kaz-electro, measured)",
    "tr_y": "transistors: 2SC1815-Y (Kaz-electro, measured)",
    "tr_dc": "transistors: today's card plus the GR part's VAF, RB, IKF, RE, RC only",
    "tr_is": "transistors: today's card with the GR part's IS, BF, NF, BR only",
    "tr_ise": "transistors: today's card with the GR part's low-current recombination (ISE, NE, ISC, NC) only",
    "tr_ac": "transistors: today's card with the GR part's capacitances, TF and TR only",
    "fol_ideal": "followers as the engine's ideal buffers",
    "fol_gr": "followers only: 2SC1815-GR",
    "sch_edge": "Schmitt: typical thresholds, 455 ohm output, 100 ns edges",
    "sch_edge_max": "Schmitt: typical thresholds, 784 ohm output, 200 ns edges",
    "sch_lo": "Schmitt: VT+ 1.9, VT- 1.6 V (both at their minimum)",
    "sch_hi": "Schmitt: VT+ 3.4, VT- 3.15 V (both at their maximum)",
    "sch_wide": "Schmitt: VT+ 3.1, VT- 2.1 V (1.0 V hysteresis, the maximum)",
    "sch_narrow": "Schmitt: VT+ 2.65, VT- 2.4 V (0.25 V hysteresis, the minimum)",
    "all": "uPC4558 op-amps, 2SC1815-GR transistors and followers, the Schmitt's typical edges",
}
METAL_ONLY = {v for v in VARIANTS if v.startswith("sch_")}


ORIGINAL_OPTIONS = ".options method=trap reltol=1e-5 abstol=1e-12 vntol=1e-7"
# Looser solver settings for the runs ngspice stops ("timestep too small"): a failed variant is rendered again with
# the first that gets through, and so is its reference (cached as <case>__base@<n>), so each pair is compared alike.
ROBUST = [".options method=trap reltol=1e-4 abstol=1e-10 vntol=1e-6 itl4=100",
          ".options method=gear reltol=1e-4 abstol=1e-10 vntol=1e-6 itl4=100"]


def build(case, variant, options=None):
    """The netlist text for one case in one variant (with options, that solver line instead of the netlist's)."""
    netlist = case["netlist"]
    path = os.path.join(TOOLS, "kick808" if netlist == "bassdrum.cir" else "tr808", netlist)
    with open(path, "r", encoding="ascii") as f:
        lines = f.read().splitlines()
    values = dict(render808.PAIRS[case["pair"]]) if case.get("pair") else {}
    values.update({k: str(v) for k, v in case.get("set", {}).items()})
    values["tend"] = str(case["tend"])
    values["tedge"] = "{:.9g}".format(0.5 / RATE)
    body, plain = [], {}
    for line in lines:
        if line.startswith(".param ") and "{" not in line:
            fields = []
            for pair in line[len(".param "):].split():
                name = pair.split("=", 1)[0]
                fields.append("{}={}".format(name, values.pop(name)) if name in values else pair)
                plain[name] = fields[-1].split("=", 1)[1]
            line = ".param " + " ".join(fields)
        elif line.startswith(".tran"):
            step = 1.0 / RATE
            line = ".tran {:.12g} {} 0 {:.12g}".format(step, case["tend"], min(step, 0.5e-6))
        elif line.strip() == ".end":
            continue
        elif line.startswith(".options") and options:
            if line.strip() != ORIGINAL_OPTIONS:
                raise SystemExit("{}: unexpected {}".format(case["name"], line))
            line = options
        body.append(line)
    if values:
        raise SystemExit("{}: unknown parameters {}".format(case["name"], values))
    sv = render808.spice_value
    if "* @TRIGGERS@" in body:
        at = body.index("* @TRIGGERS@")
        body[at:at + 1] = render808.trigger_lines(case["hits"], sv(plain["tedge"]), sv(plain["width"]))
    if "* @OSCILLATORS@" in body:
        phases = [0.1, 0.37, 0.62, 0.05, 0.81, 0.44]
        vtp, vtm = SCHMITT["typ"]
        edge = None
        if variant.startswith("sch_") and variant[4:] in SCHMITT:
            vtp, vtm = SCHMITT[variant[4:]]
        if variant in ("sch_edge", "all"):
            edge = EDGES["typ"]
        if variant == "sch_edge_max":
            edge = EDGES["max"]
        at = body.index("* @OSCILLATORS@")
        if variant == "base" or not (variant.startswith("sch_") or variant == "all"):
            body[at:at + 1] = render808.oscillator_lines(phases, sv(plain["f1"]), sv(plain["f2"]), sv(plain["cs"]), case["tend"])
        else:
            body[at:at + 1] = oscillator_lines(phases, sv(plain["f1"]), sv(plain["f2"]), sv(plain["cs"]), case["tend"], vtp, vtm, edge)
    if case.get("noise"):
        # The clap's white noise bus from a seeded generator (ngspice's TRNOISE cannot be seeded here): Gaussian samples
        # of the given rms every wnstep, joined linearly as TRNOISE joins them, so a variant and its reference share it.
        seed, rms = case["noise"]
        step = sv(plain["wnstep"])
        x = np.random.default_rng(seed).normal(0.0, rms, int(case["tend"] / step) + 2)
        at = next(i for i, l in enumerate(body) if l.startswith("VWC "))
        body[at] = "VWC nwc 0 PWL(" + " ".join("{:.9g} {:.6g}".format(k * step, v) for k, v in enumerate(x)) + ")"
    followers = FOLLOWERS.get(netlist, [])
    extra = []
    if variant in ("oa_ti", "oa_upc", "all"):
        name = "RC4558" if variant == "oa_ti" else "UPC4558"
        body = swap_opamps(body, name)
        extra.append(ti_model(name, upc=name == "UPC4558"))
    if variant in ("tr_gr", "all"):
        body = swap_transistors(body, GP_CARDS["GR"])
    if variant == "tr_y":
        body = swap_transistors(body, GP_CARDS["Y"])
    if variant == "tr_dc":
        body = swap_transistors(body, EARLY_Q)
    if variant in PART_Q:
        body = swap_transistors(body, PART_Q[variant])
    if variant == "fol_ideal" and followers:
        body = ideal_followers(body, followers)
    if variant == "fol_gr" and followers:
        body = swap_transistors(body, GP_CARDS["GR"], only=followers)
    return body + extra


def nodes_of(case):
    if case["netlist"] == "bassdrum.cir":
        return KICK_NODES
    return case.get("nodes") or render808.NODES[case["netlist"]]


def render_path(work, name, variant, level=0):
    return os.path.join(work, "{}__{}{}.npy".format(name, variant, "@{}".format(level) if level else ""))


def render(case, variant, work):
    """Renders a case in a variant, with looser solver settings if ngspice stops; and its reference alike. The level
    used (0 the netlist's own settings), or None."""
    for level in range(len(ROBUST) + 1):
        if run(case, variant, work, level):
            if level and variant != "base" and not run(case, "base", work, level):
                continue
            return level
    return None


def found(work, name, variant):
    """The lowest level at which a case's variant and its reference are both rendered, or None."""
    for level in range(len(ROBUST) + 1):
        if os.path.exists(render_path(work, name, variant, level)) and os.path.exists(render_path(work, name, "base", level)):
            return level
    return None


def run(case, variant, work, level=0):
    """Renders one case in one variant (cached): an array, time first, then each node."""
    out = render_path(work, case["name"], variant, level)
    if os.path.exists(out):
        return out
    if os.path.exists(out[:-4] + ".log"):
        return None  # tried before and stopped
    body = build(case, variant, ROBUST[level - 1] if level else None)
    nodes = nodes_of(case)
    tmp = tempfile.mkdtemp(prefix="dm808-", dir=work)
    data = os.path.join(tmp, "data.txt").replace("\\", "/")
    vectors = " ".join("v({})".format(n) for n in nodes)
    # Only the nodes read are kept (the metal board's 415 nodes at every step took 2 to 5 GB a run). The last time
    # ngspice reached is printed before linearize, which fills a stopped run's remaining time with zeros.
    body += [".save " + vectors, ".control", "set noaskquit", "set wr_singlescale", "set wr_vecnames", "run",
             "let tlast = time[length(time) - 1]", "echo TLAST $&tlast", "linearize " + vectors,
             "wrdata {} {}".format(data, vectors), "quit", ".endc", ".end"]
    netlist = os.path.join(tmp, "run.cir")
    with open(netlist, "w", encoding="ascii") as f:
        f.write("\n".join(body) + "\n")
    result = subprocess.run([NGSPICE, "-b", netlist], capture_output=True, text=True, cwd=tmp)
    m = re.search(r"TLAST (\S+)", result.stdout)
    reached = float(m.group(1)) if m else 0.0
    if reached < case["tend"] * (1.0 - 1e-6):
        with open(out[:-4] + ".log", "w") as f:
            f.write("stopped at {:g} s of {}\n".format(reached, case["tend"]) + result.stdout[-20000:] + "\n" + result.stderr[-5000:])
        return None
    if not os.path.exists(data):
        with open(out[:-4] + ".log", "w") as f:
            f.write(result.stdout[-20000:] + "\n" + result.stderr[-5000:])
        return None
    a = np.loadtxt(data, skiprows=1)
    np.save(out, a.T)
    shutil.rmtree(tmp, ignore_errors=True)
    return out


# ---------------------------------------------------------------------------------------------------------------------
# The cases: unaccented (4.7 V) and full accent (11.5 V, D258's 4.7 + 6.8 V), default knobs and their extremes.

U, A = 4.7, 11.5


def cases():
    c = []
    bd = dict(netlist="bassdrum.cir", tend=0.6, kind="bd")
    c += [dict(bd, name="bd", set=dict(amp=U)), dict(bd, name="bd_acc", set=dict(amp=A)),
          dict(bd, name="bd_dec0", set=dict(amp=U, dec=0.0), tend=0.3),
          dict(bd, name="bd_dec1_acc", set=dict(amp=A, dec=1.0), tend=1.2),
          dict(bd, name="bd_tone0_acc", set=dict(amp=A, tone=0.0))]
    tom = dict(netlist="tom.cir", tend=0.5, kind="tom")
    c += [dict(tom, name="lt", pair="low", set=dict(amp=U)), dict(tom, name="lt_acc", pair="low", set=dict(amp=A)),
          dict(tom, name="lt_t0", pair="low", set=dict(amp=U, tune=0)), dict(tom, name="lt_t1_acc", pair="low", set=dict(amp=A, tune=1)),
          dict(tom, name="mt", pair="mid", set=dict(amp=U)), dict(tom, name="ht_acc_l1", pair="high", set=dict(amp=A, lev=1)),
          dict(tom, name="lc", pair="low", set=dict(amp=U, conga=1)), dict(tom, name="mc_acc", pair="mid", set=dict(amp=A, conga=1)),
          dict(tom, name="hc", pair="high", set=dict(amp=U, conga=1))]
    rim = dict(netlist="rimclave.cir", tend=0.2, kind="rim")
    c += [dict(rim, name="rs", set=dict(amp=U, rs=1)), dict(rim, name="rs_acc", set=dict(amp=A, rs=1)),
          dict(rim, name="cl", set=dict(amp=U, rs=0)), dict(rim, name="cl_acc", set=dict(amp=A, rs=0)),
          dict(rim, name="rs_acc_l1", set=dict(amp=A, rs=1, lev=1))]
    sd = dict(netlist="snare.cir", tend=0.3, kind="sd")
    c += [dict(sd, name="sd", set=dict(amp=U, wn=0.13)), dict(sd, name="sd_acc", set=dict(amp=A, wn=0.13)),
          dict(sd, name="sd_tone0", set=dict(amp=U, wn=0.13, tone=0)), dict(sd, name="sd_tone1_acc", set=dict(amp=A, wn=0.13, tone=1)),
          dict(sd, name="sd_snap0", set=dict(amp=U, wn=0.13, snappy=0)),
          dict(sd, name="sd_snap1_acc_l1", set=dict(amp=A, wn=0.13, snappy=1, lev=1))]
    cp = dict(netlist="clap.cir", tend=0.8, kind="cp")
    c += [dict(cp, name="cp", set=dict(amp=U, wn=0.13)), dict(cp, name="cp_acc", set=dict(amp=A, wn=0.13)),
          dict(cp, name="cp_acc_l1", set=dict(amp=A, wn=0.13, lev=1)),
          dict(cp, name="ma", set=dict(amp=U, wn=0.13, ma=1), tend=0.3),
          dict(cp, name="ma_acc", set=dict(amp=A, wn=0.13, ma=1), tend=0.3)]
    mt = dict(netlist="metal.cir", kind="metal", tend=0.3)
    c += [dict(mt, name="cb", set=dict(amp=U), hits=[("cb", 0.0)], out="vcb"),
          dict(mt, name="cb_acc_l1", set=dict(amp=A, cbl=1), hits=[("cb", 0.0)], out="vcb"),
          dict(mt, name="cy", set=dict(amp=U), hits=[("cy", 0.0)], out="vcy", tend=0.8),
          dict(mt, name="cy_acc", set=dict(amp=A), hits=[("cy", 0.0)], out="vcy", tend=0.8),
          dict(mt, name="cy_t0", set=dict(amp=U, tone=0, cyd=0), hits=[("cy", 0.0)], out="vcy", tend=0.5),
          dict(mt, name="cy_t1_acc_l1", set=dict(amp=A, tone=1, cyd=1, cyl=1), hits=[("cy", 0.0)], out="vcy", tend=1.2),
          dict(mt, name="oh", set=dict(amp=U), hits=[("oh", 0.0)], out="voh", tend=0.6),
          dict(mt, name="oh_acc_l1", set=dict(amp=A, ohl=1), hits=[("oh", 0.0)], out="voh", tend=0.6),
          dict(mt, name="oh_d0", set=dict(amp=U, ohd=0), hits=[("oh", 0.0)], out="voh", tend=0.4),
          dict(mt, name="oh_d1", set=dict(amp=U, ohd=1), hits=[("oh", 0.0)], out="voh", tend=0.9),
          dict(mt, name="ch", set=dict(amp=U), hits=[("ch", 0.0)], out="vch", tend=0.2),
          dict(mt, name="ch_acc_l1", set=dict(amp=A, chl=1), hits=[("ch", 0.0)], out="vch", tend=0.2)]
    return c


# ---------------------------------------------------------------------------------------------------------------------
# The metrics.

def db(x):
    return 20.0 * math.log10(max(x, 1e-30))


def envelope(v, fs, win=0.005):
    n = max(1, int(win * fs))
    p = np.convolve(v * v, np.ones(n) / n, mode="same")
    return np.sqrt(p)


def decay_time(v, fs, drop_db):
    env = envelope(v, fs)
    k = int(np.argmax(env))
    below = np.nonzero(env[k:] < env[k] * 10 ** (-drop_db / 20.0))[0]
    return (k + below[0]) / fs if below.size else float("nan")


def bands(v, fs):
    spec = np.abs(np.fft.rfft(v * np.hanning(len(v)))) ** 2
    freqs = np.fft.rfftfreq(len(v), 1.0 / fs)
    out = []
    for k in range(-16, 14):
        fc = 1000.0 * 2.0 ** (k / 3.0)
        sel = (freqs >= fc * 2 ** (-1 / 6)) & (freqs < fc * 2 ** (1 / 6))
        out.append((fc, float(spec[sel].sum())))
    return out


def peak_freq(v, fs, t0, t1, f0, f1):
    seg = v[int(t0 * fs):int(t1 * fs)]
    if len(seg) < 16:
        return float("nan")
    seg = (seg - seg.mean()) * np.hanning(len(seg))
    n = 1 << 21
    mag = np.abs(np.fft.rfft(seg, n))
    freqs = np.fft.rfftfreq(n, 1.0 / fs)
    sel = np.nonzero((freqs >= f0) & (freqs <= f1))[0]
    i = sel[int(np.argmax(mag[sel]))]
    a, b, c = (math.log(max(mag[j], 1e-30)) for j in (i - 1, i, i + 1))
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return float((i + d) * fs / n)


def zc_freq(v, fs, t0, t1):
    """The mean frequency between t0 and t1 from the upward zero crossings."""
    seg = v[int(t0 * fs):int(t1 * fs)]
    s = np.sign(seg)
    idx = np.nonzero((s[:-1] <= 0) & (s[1:] > 0))[0]
    if idx.size < 2:
        return float("nan")
    t = idx + seg[idx] / (seg[idx] - seg[idx + 1])
    return float((idx.size - 1) / ((t[-1] - t[0]) / fs))


def ring_freq(v, fs, t0, t1, f0, f1):
    """One ring's mean frequency between t0 and t1: the output band-limited to f0..f1, then its zero crossings."""
    spec = np.fft.rfft(v - v.mean())
    freqs = np.fft.rfftfreq(len(v), 1.0 / fs)
    spec[(freqs < f0) | (freqs > f1)] = 0.0
    return zc_freq(np.fft.irfft(spec, len(v)), fs, t0, t1)


def harmonic(v, fs, f, t0, t1):
    seg = v[int(t0 * fs):int(t1 * fs)]
    seg = (seg - seg.mean()) * np.hanning(len(seg))
    n = 1 << 20
    mag = np.abs(np.fft.rfft(seg, n))
    freqs = np.fft.rfftfreq(n, 1.0 / fs)
    def at(x):
        sel = (freqs > x * 0.9) & (freqs < x * 1.1)
        return float(mag[sel].max())
    return db(at(2 * f) / at(f)), db(at(3 * f) / at(f))


def features(case, arr):
    fs = RATE
    nodes = nodes_of(case)
    outn = case.get("out", "vout")
    v = arr[1 + nodes.index(outn)]
    v = v - v[0]
    f = {"peak": float(np.abs(v).max()), "rms": float(np.sqrt(np.mean(v * v))),
         "t20": decay_time(v, fs, 20), "t40": decay_time(v, fs, 40), "bands": bands(v, fs)}
    kind = case["kind"]
    # Pitch is read at each resonator's own node by its zero crossings: at the output a VCA's changing envelope reads
    # as a pitch change in a band-limited or spectral estimate.
    node = lambda n: arr[1 + nodes.index(n)] - arr[1 + nodes.index(n)][0]
    if kind == "bd":
        nout = node("nout")
        f["f_sigh"] = zc_freq(nout, fs, 0.03, 0.1)
        f["f_body"] = zc_freq(nout, fs, 0.1, 0.2)
        f["f_late"] = zc_freq(nout, fs, 0.2, min(0.4, case["tend"] - 0.01))
        f["h2"], f["h3"] = harmonic(v, fs, zc_freq(v, fs, 0.03, 0.2), 0.02, 0.25)
    elif kind == "tom":
        f["f_body"] = zc_freq(node("no1"), fs, 0.01, 0.15)
        f["f_late"] = zc_freq(node("no1"), fs, 0.15, 0.35)
    elif kind == "rim":
        if case["set"].get("rs") != 0:
            f["f_low"] = zc_freq(node("no21"), fs, 0.0005, 0.01)
        f["f_high"] = zc_freq(node("no20b"), fs, 0.0005, 0.01)
    elif kind == "sd":
        f["f_low"] = zc_freq(node("no1"), fs, 0.002, 0.06)
        f["f_high"] = zc_freq(node("no2"), fs, 0.002, 0.06)
    elif kind == "cp":
        f["tail"] = float(np.sqrt(np.mean(v[int(0.15 * fs):] ** 2))) if case["tend"] > 0.3 else float("nan")
    elif kind == "metal":
        f["f_main"] = peak_freq(v, fs, 0.002, min(0.1, case["tend"] - 0.01), 150, 12000)
    return f


def compare(case, base, var):
    fb, fv = features(case, base), features(case, var)
    nodes = nodes_of(case)
    n = min(base.shape[1], var.shape[1])
    # Each stage's rms difference over the reference's rms, both taken about their resting value (the vendor op-amps'
    # bias currents move the rest by tens of millivolts across the 820k to 1M feedback resistors, which the raw error
    # the tests use would count); stages that stay within 1 mV of rest in this case are left out.
    errs, raw = {}, {}
    for k, name in enumerate(nodes):
        b, x = base[1 + k][:n], var[1 + k][:n]
        raw[name] = float(np.sqrt(np.sum((x - b) ** 2) / max(np.sum(b * b), 1e-30)))
        b, x = b - b[0], x - x[0]
        if np.sqrt(np.mean(b * b)) < 1e-3:
            continue
        errs[name] = float(np.sqrt(np.sum((x - b) ** 2) / np.sum(b * b)))
    r = {"err": errs, "err_raw": raw, "rest_shift": {name: float(var[1 + k][0] - base[1 + k][0]) for k, name in enumerate(nodes)},
         "peak_db": db(fv["peak"]) - db(fb["peak"]), "rms_db": db(fv["rms"]) - db(fb["rms"])}
    for key in ("t20", "t40"):
        r[key + "_pct"] = 100.0 * (fv[key] / fb[key] - 1.0) if fb[key] == fb[key] and fv[key] == fv[key] else float("nan")
        r[key + "_base"] = fb[key]
    top = max(e for _, e in fb["bands"])
    diffs = [(fc, 10 * math.log10(max(ev, 1e-60) / eb)) for (fc, eb), (_, ev) in zip(fb["bands"], fv["bands"])
             if eb > top * 1e-3]
    r["band_max_db"] = max(diffs, key=lambda d: abs(d[1])) if diffs else (0, 0)
    for key in ("f_sigh", "f_body", "f_late", "f_low", "f_high", "f_main"):
        if key in fb and fb[key] == fb[key] and fv[key] == fv[key]:
            r[key + "_cents"] = 1200.0 * math.log2(fv[key] / fb[key])
            r[key + "_base"] = fb[key]
    for key in ("h2", "h3"):
        if key in fb:
            r[key + "_db"] = fv[key] - fb[key]
    if "tail" in fb and fb["tail"] == fb["tail"]:
        r["tail_db"] = db(fv["tail"]) - db(fb["tail"])
    r["peak_v"], r["peak_v_base"] = fv["peak"], fb["peak"]
    return r


VOICES = [("Bass drum", "bd"), ("Toms, congas", "tom"), ("Rim shot, claves", "rim"), ("Snare", "sd"),
          ("Clap, maracas", "cp"), ("Cowbell", "cb"), ("Cymbal", "cy"), ("Open hat", "oh"), ("Closed hat", "ch")]
OUT = {"bd": "vout", "tom": "vout", "rim": "vout", "sd": "vout", "cp": "vout", "cb": "vcb", "cy": "vcy", "oh": "voh", "ch": "vch"}


def voice_of(name, kind):
    if kind == "metal":
        return name[:2]
    return kind


def fmt(x, digits=1):
    return "-" if x is None or x != x else "{:+.{}f}".format(x, digits)


def summary(results):
    """Markdown tables: per voice and variant the worst case's numbers; then the fitted parts' own measures."""
    lines = []
    for variant in VARIANTS:
        if variant == "base":
            continue
        rows = []
        for title, key in VOICES:
            worst = {}
            for name, d in results.items():
                if voice_of(name, d["case"]["kind"]) != key or variant not in d:
                    continue
                r = d[variant]
                out = OUT[key]
                cand = {"out": (r["err"].get(out, float("nan")) * 100, name),
                        "stage": max(((e * 100, s) for s, e in r["err"].items() if s != out), default=(float("nan"), "")),
                        "peak": (r["peak_db"], name), "rms": (r["rms_db"], name),
                        "band": (r["band_max_db"][1], "{} {:.0f} Hz".format(name, r["band_max_db"][0])),
                        "t20": (r.get("t20_pct", float("nan")), name),
                        "pitch": max(((r[k], "{} {}".format(name, k[2:-6])) for k in r if k.endswith("_cents")),
                                     key=lambda x: abs(x[0]), default=(float("nan"), ""))}
                for k, (val, where) in cand.items():
                    if val != val:
                        continue
                    if k not in worst or abs(val) > abs(worst[k][0]):
                        worst[k] = (val, where if k != "stage" else "{} {}".format(name, where))
            if not worst:
                continue
            g = lambda k, d=1: fmt(worst.get(k, (float("nan"), ""))[0], d)
            w = lambda k: worst.get(k, (0, ""))[1]
            rows.append("| {} | {} ({}) | {} ({}) | {} | {} | {} ({}) | {} ({}) | {} ({}) |".format(
                title, g("out").lstrip("+"), w("out"), g("stage").lstrip("+"), w("stage"), g("peak", 2), g("rms", 2),
                g("band", 2), w("band"), g("t20"), w("t20"), g("pitch"), w("pitch")))
        if rows:
            lines += ["", "### {}: {}".format(variant, VARIANTS[variant]), "",
                      "| Voice | Output error % | Worst stage % | Peak dB | Rms dB | Worst band dB | Decay t20 % | Pitch cents |",
                      "|---|---|---|---|---|---|---|---|"] + rows
    # The fitted parts.
    def get(case, variant, key):
        return results.get(case, {}).get(variant, {}).get(key, float("nan"))
    lines += ["", "### The fitted parts' measures", "",
              "| Variant | C39: bd sigh, body pitch c; h2, h3 dB | Toms' Rfb: t20 % lt, mt, lc, hc | C60/R134: oh t20 % d0, d.5, d1 | "
              "C139: clap tail dB | R312: ring c rs, cl | R313: cl t20 % | R324: rs over cl dB |", "|---|---|---|---|---|---|---|---|"]
    for variant in VARIANTS:
        if variant == "base":
            continue
        bd = "{} {}; {} {}".format(fmt(get("bd", variant, "f_sigh_cents")), fmt(get("bd", variant, "f_body_cents")),
                                   fmt(get("bd", variant, "h2_db"), 2), fmt(get("bd", variant, "h3_db"), 2))
        toms = " ".join(fmt(get(c, variant, "t20_pct")) for c in ("lt", "mt", "lc", "hc"))
        oh = " ".join(fmt(get(c, variant, "t20_pct")) for c in ("oh_d0", "oh", "oh_d1"))
        tail = fmt(get("cp", variant, "tail_db"), 2)
        r312 = "{} {}".format(fmt(get("rs", variant, "f_high_cents")), fmt(get("cl", variant, "f_high_cents")))
        r313 = fmt(get("cl", variant, "t20_pct"))
        r324 = fmt(get("rs", variant, "peak_db") - get("cl", variant, "peak_db"), 2)
        lines.append("| {} | {} | {} | {} | {} | {} | {} | {} |".format(variant, bd, toms, oh, tail, r312, r313, r324))
    lines += ["", "### The six oscillators by the Schmitt's thresholds (Hz; the cowbell's two held by TM1, TM2)", "",
              "| Thresholds | VT+, VT- V | Osc 1..6 Hz | Duty % |", "|---|---|---|---|"]
    for k, (vtp, vtm) in list(SCHMITT.items()) + list(SCHMITT_TABLE_ONLY.items()):
        fs, duty = schmitt_frequencies(vtp, vtm)
        lines.append("| {} | {}, {} | {} | {:.1f} |".format(k, vtp, vtm, ", ".join("{:.1f}".format(x) for x in fs), 100 * duty))
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--summary", action="store_true", help="print the tables from <work>/results.json")
    p.add_argument("--no-render", action="store_true", help="measure only what is already rendered")
    p.add_argument("--results", default="results.json")
    p.add_argument("--work", required=True)
    p.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    p.add_argument("--only")
    p.add_argument("--variants")
    args = p.parse_args()
    if args.summary:
        with open(os.path.join(args.work, args.results)) as f:
            print(summary(json.load(f)))
        return
    os.makedirs(args.work, exist_ok=True)
    all_cases = cases()
    if args.only:
        keep = set(args.only.split(","))
        all_cases = [c for c in all_cases if c["name"] in keep]
    variants = args.variants.split(",") if args.variants else list(VARIANTS)
    if "base" not in variants:
        variants = ["base"] + variants
    jobs = []
    for c in all_cases:
        for v in variants:
            if v in METAL_ONLY and c["netlist"] != "metal.cir":
                continue
            if v.startswith("fol_") and c["netlist"] not in FOLLOWERS:
                continue
            if v in PART_Q and c["name"] not in PART_CASES:
                continue
            jobs.append((c, v))
    if args.no_render:
        jobs = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(render, c, v, args.work): (c["name"], v) for c, v in jobs}
        for fut in concurrent.futures.as_completed(futures):
            name, v = futures[fut]
            level = fut.result()
            print("{:>16} {:>13} {}".format(name, v, "FAILED" if level is None else "ok" if level == 0 else
                                           "ok with looser solver settings ({})".format(level)), flush=True)
    results = {}
    for c in all_cases:
        bpath = render_path(args.work, c["name"], "base")
        if not os.path.exists(bpath):
            continue
        results[c["name"]] = {"case": {k: c[k] for k in ("netlist", "kind") if k in c},
                              "base": features(c, np.load(bpath))}
        for v in variants:
            level = found(args.work, c["name"], v)
            if v != "base" and level is not None:
                r = compare(c, np.load(render_path(args.work, c["name"], "base", level)),
                            np.load(render_path(args.work, c["name"], v, level)))
                r["solver_level"] = level
                results[c["name"]][v] = r
    for name in results:
        results[name]["base"].pop("bands", None)
    with open(os.path.join(args.work, args.results), "w") as f:
        json.dump(results, f, indent=1)
    print("wrote", os.path.join(args.work, args.results))


if __name__ == "__main__":
    main()

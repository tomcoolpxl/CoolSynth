"""Renders a TR-808 voice netlist (tom.cir, rimclave.cir) with ngspice at a fixed sample rate (D252).

Usage:
  python -I tools/tr808/render.py tom.cir --pair low --set amp=4 --set conga=0 --tend 0.6 --rate 96000 --out out.csv
  python -I tools/tr808/render.py rimclave.cir --set rs=1 --out out.csv

--pair low|mid|high sets the tom/conga pair's values; --set name=value overrides any .param; --nodes names the
voltages written (default: each netlist's own list). Writes a CSV: time, then one column per node. ngspice is looked
for in tools/ngspice (downloaded with the user's OK, not in git).

metal.cir (D254) takes its hits and oscillators from here:
  python -I tools/tr808/render.py metal.cir --hit oh:0 --hit ch:0.05 --phases 0.1,0.37,0.62,0.05,0.81,0.44 --out o.csv
--hit instrument:seconds strikes cb, cy, oh or ch (all at the netlist's amp); --phases gives each oscillator's place
in its cycle at time 0 (0 a rising edge; high for the first 47.98 %), as Tr808MetalBank's setPhases.

snare.cir and clap.cir (D256, D257) take the white noise bus as two test sines (--set wn=0.1) or as random noise
(--set wnrms=0.267); clap.cir plays the maracas with --set ma=1 (their nodes: vout, vt, n135, nb67, nb66, nm3, ne68).
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
NGSPICE = os.path.join(ROOT, "tools", "ngspice", "Spice64", "bin", "ngspice_con.exe")

# The three pairs' values (service notes, p. 9): low (IC15, IC13), mid (IC17, IC16), high (IC18, IC16); rfb fitted (D252).
PAIRS = {
    "low": dict(cin="56n", cout="12n", ctom="47n", cintom="1p", cpulse="6.8n", rbias="4.7k", rconga="1.5k",
                cenv="39n", cn1="47n", cn2="18n", cmix="47n", rfb="865k"),
    "mid": dict(cin="27n", cout="12n", ctom="27n", cintom="8.2n", cpulse="2.7n", rbias="6.8k", rconga="2.2k",
                cenv="33n", cn1="33n", cn2="15n", cmix="39n", rfb="865k"),
    "high": dict(cin="27n", cout="5.6n", ctom="22n", cintom="1p", cpulse="2.7n", rbias="5.6k", rconga="1.5k",
                 cenv="33n", cn1="27n", cn2="10n", cmix="33n", rfb="880k"),
}
NODES = {"tom.cir": ["vout", "vt", "np", "nm", "no1", "no2", "nev", "nq", "nfl"],
         "rimclave.cir": ["vout", "vt", "nenv", "nf", "no21", "no20a", "no20b", "ns", "ne63"],
         "metal.cir": ["vcb", "vcy", "voh", "vch", "nv2", "nsum", "nhb", "nlb", "ncbe", "ncbo", "nj", "nv", "ne1", "nce"],
         "snare.cir": ["vout", "vt", "np1", "np2", "no1", "no2", "ne47", "n52", "nsn", "nc48", "ne49"],
         "clap.cir": ["vout", "vt", "n8", "n139", "n143", "no21", "n138", "nb70", "nc70", "n14", "n1", "n13", "n144", "n2",
                      "n5", "nb72", "nh", "nc72", "np1", "np6"]}

# The HD14584 at 5 V as Werner, Abel and Smith take it (ICMC-SMC 2014): VT+ 2.7 V, VT- 2.1 V, its output 0 or 5 V.
SCHMITT_HIGH = math.log((5.0 - 2.1) / (5.0 - 2.7))  # the high time over RC
SCHMITT_LOW = math.log(2.7 / 2.1)                   # the low time over RC
# The oscillators' R and C (TM1 + R44 with C6, TM2 + R45 with C5, then R43 C4, R42 C3, R41 C2, R40 C1); the first
# two are set from f1 and f2.
OSCILLATORS = [(None, 22e-9), (None, 18e-9), (220e3, 18e-9), (680e3, 10e-9), (560e3, 10e-9), (560e3, 18e-9)]
LINES = {"cb": "jcb", "cy": "jcy", "oh": "joh", "ch": "jch"}


def spice_value(text):
    """A SPICE number (10k, 2.2u, 1Meg) as a float."""
    text = text.strip().lower()
    scale = {"t": 1e12, "g": 1e9, "meg": 1e6, "k": 1e3, "m": 1e-3, "u": 1e-6, "n": 1e-9, "p": 1e-12, "f": 1e-15}
    for suffix in ("meg", "t", "g", "k", "m", "u", "n", "p", "f"):
        if text.endswith(suffix):
            return float(text[: -len(suffix)]) * scale[suffix]
    return float(text)


def oscillator_lines(phases, f1, f2, cs, tend):
    """Each oscillator as a PWL square wave from its RC, the edges 100 ns long."""
    lines = []
    for k, (r, c) in enumerate(OSCILLATORS):
        period_rc = SCHMITT_HIGH + SCHMITT_LOW
        if r is None:
            r = 1.0 / ((f1 if k == 0 else f2) * c * period_rc)
        rc = r * c * cs
        high, low = rc * SCHMITT_HIGH, rc * SCHMITT_LOW
        period = high + low
        into = (phases[k] % 1.0) * period
        level = 5.0 if into < high else 0.0
        t = high - into if into < high else period - into
        points = [(0.0, level)]
        while t < tend + period:
            new = 5.0 - level
            points.append((max(t - 50e-9, points[-1][0] + 1e-12), level))
            points.append((t + 50e-9, new))
            level = new
            t += high if new > 0.0 else low
        text = " ".join("{:.12g} {:g}".format(a, b) for a, b in points)
        lines.append("VO{} no{} 0 PWL({})".format(k + 1, k + 1, text))
    return lines


def trigger_lines(hits, tedge, width):
    """IC6's pulse (low while a hit's pulse stands) and each instrument's data line (high from its hit to 20 us past
    its pulse)."""
    windows = []
    for _, t in sorted(hits, key=lambda h: h[1]):
        start, end = t + tedge, t + tedge + width
        if windows and start <= windows[-1][1] + 2e-6:
            windows[-1][1] = max(windows[-1][1], end)
        else:
            windows.append([start, end])
    points = [(0.0, 5.0)]
    for start, end in windows:
        points += [(start - 0.5e-6, 5.0), (start + 0.5e-6, 0.0), (end - 0.5e-6, 0.0), (end + 0.5e-6, 5.0)]
    lines = ["V6 n6 0 PWL({})".format(" ".join("{:.12g} {:g}".format(a, b) for a, b in points))]
    for name, node in LINES.items():
        times = [t for n, t in hits if n == name]
        if not times:
            lines.append("VJ{} {} 0 0".format(name.upper(), node))
            continue
        pts = [(0.0, 5.0 if times[0] <= 0.0 else 0.0)]
        for t in sorted(times):
            if t > 0.0:
                pts += [(t - 1e-6, 0.0), (t, 5.0)]
            pts += [(t + tedge + width + 20e-6, 5.0), (t + tedge + width + 21e-6, 0.0)]
        lines.append("VJ{} {} 0 PWL({})".format(name.upper(), node, " ".join("{:.12g} {:g}".format(a, b) for a, b in pts)))
    return lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("netlist")
    parser.add_argument("--pair", choices=sorted(PAIRS))
    parser.add_argument("--set", action="append", default=[])
    parser.add_argument("--nodes")
    parser.add_argument("--tend", type=float, default=0.6)
    parser.add_argument("--rate", type=float, default=96000.0)
    parser.add_argument("--out", required=True)
    parser.add_argument("--digits", type=int, default=0, help="significant digits written (0: as ngspice writes them)")
    parser.add_argument("--hit", action="append", default=[], help="metal.cir: instrument:seconds (cb, cy, oh, ch)")
    parser.add_argument("--phases", default="0.1,0.37,0.62,0.05,0.81,0.44", help="metal.cir: the oscillators' phases")
    parser.add_argument("--step", type=float, default=0.5e-6, help="the solver's largest step, seconds")
    args = parser.parse_args()

    path = os.path.join(HERE, args.netlist)
    values = dict(PAIRS[args.pair]) if args.pair else {}
    for item in args.set:
        name, value = item.split("=", 1)
        values[name.strip()] = value.strip()
    values["tend"] = str(args.tend)
    values["tedge"] = "{:.9g}".format(0.5 / args.rate)

    body = []
    plain = {}  # every plain .param as it ends up, for the markers below
    with open(path, "r", encoding="ascii") as f:
        for line in f.read().splitlines():
            if line.startswith(".param ") and "{" not in line:
                fields = []
                for pair in line[len(".param "):].split():
                    name = pair.split("=", 1)[0]
                    fields.append("{}={}".format(name, values.pop(name)) if name in values else pair)
                    plain[name] = fields[-1].split("=", 1)[1]
                line = ".param " + " ".join(fields)
            elif line.startswith(".tran"):
                step = 1.0 / args.rate
                line = ".tran {:.12g} {} 0 {:.12g}".format(step, args.tend, min(step, args.step))
            elif line.strip() == ".end":
                continue
            body.append(line)
    if values:
        sys.exit("unknown parameters: " + ", ".join(values))
    if "* @TRIGGERS@" in body:
        hits = []
        for item in args.hit:
            name, t = item.split(":")
            if name not in LINES:
                sys.exit("unknown instrument: " + name)
            hits.append((name, float(t)))
        at = body.index("* @TRIGGERS@")
        body[at:at + 1] = trigger_lines(hits, spice_value(plain["tedge"]), spice_value(plain["width"]))
    if "* @OSCILLATORS@" in body:
        phases = [float(p) for p in args.phases.split(",")]
        at = body.index("* @OSCILLATORS@")
        body[at:at + 1] = oscillator_lines(phases, spice_value(plain["f1"]), spice_value(plain["f2"]),
                                           spice_value(plain["cs"]), args.tend)

    nodes = args.nodes.split(",") if args.nodes else NODES[os.path.basename(args.netlist)]
    work = tempfile.mkdtemp(prefix="tr808-")
    data = os.path.join(work, "data.txt").replace("\\", "/")
    vectors = " ".join("v({})".format(n) for n in nodes)
    body += [".control", "set noaskquit", "set wr_singlescale", "set wr_vecnames", "run", "linearize " + vectors,
             "wrdata {} {}".format(data, vectors), "quit", ".endc", ".end"]
    netlist = os.path.join(work, "run.cir")
    with open(netlist, "w", encoding="ascii") as f:
        f.write("\n".join(body) + "\n")
    result = subprocess.run([NGSPICE, "-b", netlist], capture_output=True, text=True)
    if not os.path.exists(data):
        sys.stderr.write(result.stdout[-3000:] + "\n" + result.stderr[-3000:] + "\n")
        sys.exit("ngspice wrote no data")
    with open(data, "r", encoding="ascii") as f, open(args.out, "w", encoding="ascii") as out:
        out.write("time," + ",".join(nodes) + "\n")
        header = True
        for line in f:
            fields = line.split()
            if header:
                header = False
                if not fields[0][0].isdigit() and fields[0][0] not in "-.":
                    continue
            if args.digits > 0:
                fields = [fields[0]] + ["{:.{}g}".format(float(v), args.digits) for v in fields[1:]]
            out.write(",".join(fields) + "\n")
    print("wrote", args.out)


if __name__ == "__main__":
    main()

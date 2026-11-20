"""Renders the TR-909 bass drum netlist with ngspice at a fixed sample rate (D237).

Usage:
  python -I tools/kick909/render.py --dac 2.5 --tune 0.5 --decay 0.5 --attack 0.5 --level 0.5 --tend 0.6 \
      --rate 96000 --out out.csv

The knobs are turns, 0..1: TUNE (VR2 100 k A) and DECAY (VR5 1 M A) through the audio taper, ATTACK (VR3 500 B,
fully turned up = 0 ohm) and LEVEL (VR4 100 k B). The op-amps are TI's RC4558 macromodel (SLOJ053, outside git in
tools/references/rc4558), the 909's M5218L being a 4558-class part. Writes a CSV: time and the voltages in NODES.
ngspice is looked for in tools/ngspice (not in git).
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
NGSPICE = os.path.join(ROOT, "tools", "ngspice", "Spice64", "bin", "ngspice_con.exe")
NETLIST = os.path.join(HERE, "bassdrum.cir")
RC4558 = os.path.join(ROOT, "tools", "references", "rc4558", "RC4558.301")
NODES = ["sum", "q12b", "n1", "n4", "n7c", "c9", "cv", "sq", "tri", "cmp", "nx", "c7", "e1", "nk", "c8", "c12", "q8e", "q9e",
         "a2", "nvca", "out", "acc"]
AUDIO_TAPER = 32.1  # 15 % of the track at mid-travel


def audio(turn):
    return (AUDIO_TAPER ** turn - 1.0) / (AUDIO_TAPER - 1.0)


def rc4558_opamp():
    with open(RC4558, "r", encoding="ascii", errors="replace") as f:
        text = f.read().splitlines()
    # The 1989 file ends with a DOS end-of-file byte (^Z), which ngspice will not parse.
    text = ["".join(ch for ch in line if ch >= " " or ch == "\t") for line in text]
    return [line.replace(".SUBCKT RC4558", ".SUBCKT OPAMP") for line in text if line.strip()]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dac", type=float, default=2.5)
    parser.add_argument("--tune", type=float, default=0.5)
    parser.add_argument("--decay", type=float, default=0.5)
    parser.add_argument("--attack", type=float, default=0.5)
    parser.add_argument("--level", type=float, default=0.5)
    parser.add_argument("--ttrig", type=float, default=0.005)
    parser.add_argument("--tend", type=float, default=0.6)
    parser.add_argument("--rate", type=float, default=96000.0)
    parser.add_argument("--extra", default="", help="extra netlist lines, separated by ';'")
    parser.add_argument("--drop", default="", help="elements to leave out, by name, separated by ';' (to rewire with --extra)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    vr2 = max(1.0, 100e3 * audio(args.tune))
    vr5 = max(1.0, 1e6 * audio(args.decay))
    vr3 = max(0.01, 500.0 * (1.0 - args.attack))
    vr4 = max(1.0, 100e3 * args.level)

    with open(NETLIST, "r", encoding="ascii") as f:
        lines = f.read().splitlines()

    step = 1.0 / args.rate
    dropped = {name.strip().upper() for name in args.drop.split(";") if name.strip()}
    body = []
    for line in lines:
        if line.split() and line.split()[0].upper() in dropped:
            continue
        if line.startswith(".param vdac="):
            line = ".param vdac={} vr2={} vr5={} vr3={} vr4={} ttrig={} tend={}".format(
                args.dac, vr2, vr5, vr3, vr4, args.ttrig, args.tend)
        elif line.startswith(".tran"):
            line = ".tran {:.12g} {} 0 {:.12g}".format(step, args.tend, min(step, 2e-6))
        elif line.strip() == ".end":
            continue
        body.append(line)
    body += rc4558_opamp()
    body += [l for l in args.extra.split(";") if l.strip()]

    work = tempfile.mkdtemp(prefix="kick909-")
    data = os.path.join(work, "data.txt").replace("\\", "/")
    vectors = " ".join("v({})".format(n) for n in NODES)
    body += [
        ".options reltol=1e-4 abstol=1e-12 vntol=1e-7",
        ".control",
        "set noaskquit",
        "set wr_singlescale",
        "set wr_vecnames",
        "run",
        "linearize " + vectors,
        "wrdata {} {}".format(data, vectors),
        "quit",
        ".endc",
        ".end",
    ]
    netlist = os.path.join(work, "run.cir")
    with open(netlist, "w", encoding="ascii") as f:
        f.write("\n".join(body) + "\n")

    result = subprocess.run([NGSPICE, "-b", netlist], capture_output=True, text=True)
    if not os.path.exists(data):
        sys.stderr.write(result.stdout[-3000:] + "\n" + result.stderr[-3000:] + "\n")
        sys.exit("ngspice wrote no data")

    with open(data, "r", encoding="ascii") as f, open(args.out, "w", encoding="ascii") as out:
        out.write("time," + ",".join(NODES) + "\n")
        header = True
        for line in f:
            fields = line.split()
            if header:
                header = False
                if not fields[0][0].isdigit() and fields[0][0] not in "-.":
                    continue
            out.write(",".join(fields) + "\n")
    print("wrote", args.out)


if __name__ == "__main__":
    main()

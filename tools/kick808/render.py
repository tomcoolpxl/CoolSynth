"""Renders the TR-808 bass drum netlist with ngspice at a fixed sample rate (D235).

Usage:
  python -I tools/kick808/render.py --amp 4 --dec 0.4 --tone 0.99 --lev 0.99 --tend 0.6 --rate 96000 --out out.csv

Writes a CSV: time, v(vout), v(nout), v(neg), v(npulse), v(ntop). ngspice is looked for in tools/ngspice
(downloaded with the user's OK, not in git); the netlist is tools/kick808/bassdrum.cir.
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
NODES = ["vout", "nout", "neg", "npulse", "ntop"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--amp", type=float, default=4.0)
    parser.add_argument("--dec", type=float, default=0.4)
    parser.add_argument("--tone", type=float, default=0.99)
    parser.add_argument("--lev", type=float, default=0.99)
    parser.add_argument("--tend", type=float, default=0.6)
    parser.add_argument("--rate", type=float, default=96000.0)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    with open(NETLIST, "r", encoding="ascii") as f:
        lines = f.read().splitlines()

    body = []
    for line in lines:
        if line.startswith(".param amp="):
            line = ".param amp={} dec={} tone={} lev={} tend={}".format(args.amp, args.dec, args.tone, args.lev, args.tend)
        elif line.startswith(".tran"):
            step = 1.0 / args.rate
            line = ".tran {:.12g} {} 0 {:.12g}".format(step, args.tend, min(step, 1.417e-6))
        elif line.strip() == ".end":
            continue
        body.append(line)

    work = tempfile.mkdtemp(prefix="kick808-")
    data = os.path.join(work, "data.txt").replace("\\", "/")
    vectors = " ".join("v({})".format(n) for n in NODES)
    body += [
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

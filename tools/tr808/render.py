"""Renders a TR-808 voice netlist (tom.cir, rimclave.cir) with ngspice at a fixed sample rate (D252).

Usage:
  python -I tools/tr808/render.py tom.cir --pair low --set amp=4 --set conga=0 --tend 0.6 --rate 96000 --out out.csv
  python -I tools/tr808/render.py rimclave.cir --set rs=1 --out out.csv

--pair low|mid|high sets the tom/conga pair's values; --set name=value overrides any .param; --nodes names the
voltages written (default: each netlist's own list). Writes a CSV: time, then one column per node. ngspice is looked
for in tools/ngspice (downloaded with the user's OK, not in git).
"""
import argparse
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
         "rimclave.cir": ["vout", "vt", "nenv", "nf", "no21", "no20a", "no20b", "ns", "ne63"]}


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
    args = parser.parse_args()

    path = os.path.join(HERE, args.netlist)
    values = dict(PAIRS[args.pair]) if args.pair else {}
    for item in args.set:
        name, value = item.split("=", 1)
        values[name.strip()] = value.strip()
    values["tend"] = str(args.tend)
    values["tedge"] = "{:.9g}".format(0.5 / args.rate)

    body = []
    with open(path, "r", encoding="ascii") as f:
        for line in f.read().splitlines():
            if line.startswith(".param ") and "{" not in line:
                fields = []
                for pair in line[len(".param "):].split():
                    name = pair.split("=", 1)[0]
                    fields.append("{}={}".format(name, values.pop(name)) if name in values else pair)
                line = ".param " + " ".join(fields)
            elif line.startswith(".tran"):
                step = 1.0 / args.rate
                line = ".tran {:.12g} {} 0 {:.12g}".format(step, args.tend, min(step, 0.5e-6))
            elif line.strip() == ".end":
                continue
            body.append(line)
    if values:
        sys.exit("unknown parameters: " + ", ".join(values))

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

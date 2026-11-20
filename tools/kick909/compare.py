"""Prints two renders' columns side by side at set times (a SPICE render and the model's dump).

Usage: python -I tools/kick909/compare.py spice.csv model.csv column [column ...] [--every ms] [--from ms] [--to ms]
"""
import sys


def load(path):
    with open(path, "r", encoding="ascii") as f:
        names = f.readline().strip().split(",")
        rows = [list(map(float, line.strip().split(","))) for line in f if line.strip()]
    return names, rows


def main():
    args = sys.argv[1:]
    opts = {"--every": 5.0, "--from": 0.0, "--to": 400.0}
    plain = []
    i = 0
    while i < len(args):
        if args[i] in opts:
            opts[args[i]] = float(args[i + 1])
            i += 2
        else:
            plain.append(args[i])
            i += 1
    a_path, b_path, columns = plain[0], plain[1], plain[2:]
    an, ar = load(a_path)
    bn, br = load(b_path)
    rate_a = 1.0 / (ar[1][0] - ar[0][0])
    rate_b = 1.0 / (br[1][0] - br[0][0])
    print("ms      " + "  ".join("{:>21}".format(c) for c in columns))
    t = opts["--from"]
    while t <= opts["--to"]:
        ia = min(len(ar) - 1, int(round(t * 1e-3 * rate_a)))
        ib = min(len(br) - 1, int(round(t * 1e-3 * rate_b)))
        cells = []
        for c in columns:
            cells.append("{:>10.4f}/{:<10.4f}".format(ar[ia][an.index(c)], br[ib][bn.index(c)]))
        print("{:7.2f} ".format(t) + "  ".join(cells))
        t += opts["--every"]


if __name__ == "__main__":
    main()

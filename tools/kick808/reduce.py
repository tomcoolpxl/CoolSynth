"""Cuts a render down to what the tests keep: the first seconds, the stages' columns, six significant digits.

Usage: python -I tools/kick808/reduce.py in.csv out.csv --tend 0.06
"""
import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("target")
    parser.add_argument("--tend", type=float, default=0.06)
    args = parser.parse_args()
    keep = ["time", "vout", "nout", "neg", "npulse"]
    with open(args.source, "r", encoding="ascii") as f, open(args.target, "w", encoding="ascii", newline="\n") as out:
        names = f.readline().strip().split(",")
        index = [names.index(k) for k in keep]
        out.write(",".join(keep) + "\n")
        for line in f:
            fields = line.strip().split(",")
            if float(fields[0]) > args.tend:
                break
            out.write(",".join("{:.6g}".format(float(fields[i])) for i in index) + "\n")


if __name__ == "__main__":
    main()

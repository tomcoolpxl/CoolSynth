"""Cuts a 909 render down to a test reference: from the trigger (5 ms in) for 200 ms, every 4th sample, the columns
the test compares, five significant digits.

Usage: python -I tools/kick909/reduce.py render.csv reference.csv
"""
import sys

COLUMNS = ["n4", "c9", "cv", "tri", "c8", "c12", "q9e", "out"]


def main():
    source, target = sys.argv[1], sys.argv[2]
    with open(source, "r", encoding="ascii") as f:
        names = f.readline().strip().split(",")
        rows = [line.strip().split(",") for line in f if line.strip()]
    rate = 1.0 / (float(rows[1][0]) - float(rows[0][0]))
    first = int(round(0.005 * rate))
    last = first + int(round(0.2 * rate))
    index = [names.index(c) for c in COLUMNS]
    with open(target, "w", encoding="ascii") as out:
        out.write("time," + ",".join(COLUMNS) + "\n")
        for i in range(first, min(last, len(rows)), 4):
            t = float(rows[i][0]) - 0.005
            out.write("{:.7f},".format(t) + ",".join("{:.5g}".format(float(rows[i][k])) for k in index) + "\n")
    print("wrote", target)


if __name__ == "__main__":
    main()

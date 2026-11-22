"""Records every part's current and every node's voltage of the TR-808 bass drum netlist with ngspice, for the
CoolSynth 808 mockup's complete bass drum (D263): docs/design/mockups/coolsynth-808.html plays them back on its
schematic. Run from the repository root:

    python tools/kick808/mockup_currents.py --html docs/design/mockups/coolsynth-808.html

It writes one JSON object into the page's <script id="bdData"> element: the sample times (ms), the series' names and
scales, the series as 16-bit integers in base64 (series by series), and each part's nodes as the netlist has them, so
the page can check its drawing against the netlist. The glide bend (the engine's own) and the netlist's solver aids
(the op-amps' inner parts, the load on nout) are left out, as the drawing leaves them out.
"""
import argparse
import base64
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NGSPICE = os.path.join(ROOT, "tools", "ngspice", "Spice64", "bin", "ngspice_con.exe")
NETLIST = os.path.join(ROOT, "tools", "kick808", "bassdrum.cir")
LEFT_OUT = {"rq", "qg", "dg", "rg", "cg", "rb", "ri1", "ci1", "rl", "ri2", "ci2"}
STEP = 40.0e-6


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--html", required=True)
    parser.add_argument("--amp", type=float, default=8.0)
    parser.add_argument("--dec", type=float, default=0.6)
    parser.add_argument("--tone", type=float, default=0.5)
    parser.add_argument("--lev", type=float, default=0.8)
    parser.add_argument("--tend", type=float, default=0.3)
    args = parser.parse_args()

    with open(NETLIST, "r", encoding="ascii") as f:
        lines = f.read().splitlines()

    parts = {}
    body = []
    for line in lines:
        if line.startswith(".param amp="):
            line = ".param amp={} dec={} tone={} lev={} tend={}".format(args.amp, args.dec, args.tone, args.lev, args.tend)
        elif line.startswith(".tran"):
            line = ".tran {:.9g} {} 0 2u".format(STEP, args.tend)
        elif line.strip() == ".end":
            continue
        fields = line.split()
        if fields and re.match(r"^[RCDQ]\w*$", fields[0]) and fields[0].lower() not in LEFT_OUT:
            count = 3 if fields[0][0] == "Q" else 2
            parts[fields[0].lower()] = [n.lower() for n in fields[1:1 + count]]
        body.append(line)

    nodes = sorted({n for ns in parts.values() for n in ns if n != "0"})
    vectors = ["v({})".format(n) for n in nodes]
    for name in parts:
        kind = name[0]
        if kind == "q":
            vectors += ["@{}[ic]".format(name), "@{}[ib]".format(name), "@{}[ie]".format(name)]
        else:
            vectors.append("@{}[{}]".format(name, "id" if kind == "d" else "i"))

    work = tempfile.mkdtemp(prefix="kick808-currents-")
    data = os.path.join(work, "data.txt").replace("\\", "/")
    body += [
        ".options savecurrents",
        ".control",
        "set noaskquit",
        "set wr_singlescale",
        "set wr_vecnames",
        "run",
        "linearize " + " ".join(vectors),
        "wrdata {} {}".format(data, " ".join(vectors)),
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

    rows = []
    with open(data, "r", encoding="ascii") as f:
        for line in f:
            fields = line.split()
            if not fields or not (fields[0][0].isdigit() or fields[0][0] in "-."):
                continue
            rows.append([float(x) for x in fields])
    if len(rows[0]) != len(vectors) + 1:
        sys.exit("expected {} columns, ngspice wrote {}".format(len(vectors) + 1, len(rows[0])))

    # Every sample of the first 8 ms (the trigger and the attack), then one every 0.52 ms (the ring).
    early = int(round(0.008 / STEP))
    picked = list(range(0, early + 1)) + list(range(early + 13, len(rows), 13))
    times = [round(rows[i][0] * 1000.0, 4) for i in picked]

    names, scales, packed = [], [], bytearray()
    for column, vector in enumerate(vectors, start=1):
        series = [rows[i][column] for i in picked]
        peak = max(abs(x) for x in series) or 1.0
        names.append(vector[2:-1] if vector.startswith("v(") else vector[1:].replace("[", ".").rstrip("]"))
        scales.append(peak / 32767.0)
        packed += struct.pack("<{}h".format(len(series)), *[int(round(x / peak * 32767.0)) for x in series])

    payload = {
        "setting": {"accent_volts": args.amp, "decay": args.dec, "tone": args.tone, "level": args.lev},
        "t": times,
        "names": names,
        "scales": scales,
        "parts": parts,
        "data": base64.b64encode(bytes(packed)).decode("ascii"),
    }
    with open(args.html, "r", encoding="utf-8") as f:
        page = f.read()
    marker = re.compile(r'(<script id="bdData" type="application/json">)(.*?)(</script>)', re.S)
    if not marker.search(page):
        sys.exit("the page has no <script id=\"bdData\"> element")
    page = marker.sub(lambda m: m.group(1) + json.dumps(payload, separators=(",", ":")) + m.group(3), page)
    with open(args.html, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print("wrote {} series of {} samples ({} KB) into {}".format(len(names), len(times), len(payload["data"]) // 1024, args.html))


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""Builds the CoolSynth V3 engine page for GitHub Pages: the engine map and a few words around it.

Writes into site/ at the repository root (published by .github/workflows/pages.yml):
  index.html        the page (inline SVG, always dark: the user finds the map most readable on it)
  engine-map.svg    the diagram alone (fixed dark colours, generic font family)
With --png it also writes engine-map.png, a raster of the drawing in Arial, 3200 px wide (not a browser
rendering), and with --check check-light.png and check-dark.png for looking the layout over; neither is published
unless copied in by hand.

Text is measured with the Windows Arial fonts (Pillow), so the layout is computed on Windows:
    python tools/site/build_engine_map.py
Edit the facts here when the engine changes, run it, and commit site/.
"""
import math
import os
import sys
from xml.sax.saxutils import escape

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "site"))
os.makedirs(OUT, exist_ok=True)
W = 1600
FONT_DIR = r"C:\Windows\Fonts"

LIGHT = {
    "bg": "#EEF1F4", "surface": "#FFFFFF", "panel": "#F6F8FA",
    "ink": "#17212B", "ink2": "#44515E", "ink3": "#5C6976", "line": "#B7C1CB",
    "note-bg": "#E4E9EE", "note-line": "#8693A0",
    "gen": "#875000", "gen-bg": "#FCEED0", "gen-line": "#D49B32",
    "proc": "#0A6A63", "proc-bg": "#D6F0EB", "proc-line": "#45A398",
    "fx": "#5042B5", "fx-bg": "#E8E4FB", "fx-line": "#978BE0",
    "mod": "#AD1F5B", "mod-bg": "#FBE4ED", "mod-line": "#D56C9A",
    "key-white": "#FFFFFF", "key-black": "#17212B",
}
DARK = {
    "bg": "#10151B", "surface": "#1B232D", "panel": "#151C24",
    "ink": "#E8EDF2", "ink2": "#B4C0CB", "ink3": "#8D9AA7", "line": "#3B4753",
    "note-bg": "#232D38", "note-line": "#5E6C7A",
    "gen": "#F2BC5E", "gen-bg": "#34290F", "gen-line": "#94702A",
    "proc": "#62D5C8", "proc-bg": "#0F302C", "proc-line": "#2F8279",
    "fx": "#B7ACFF", "fx-bg": "#27214A", "fx-line": "#685CB8",
    "mod": "#FF93BC", "mod-bg": "#3A1627", "mod-line": "#B5507E",
    "key-white": "#DCE2E8", "key-black": "#0B1015",
}

# ----------------------------------------------------------------- text measuring (Arial)
_measure = {}
_draw = {}


def _font_file(weight, italic):
    if weight >= 600 and italic:
        return "arialbi.ttf"
    if weight >= 600:
        return "arialbd.ttf"
    if italic:
        return "ariali.ttf"
    return "arial.ttf"


def measure(s, size, weight=400, italic=False, ls=0.0):
    key = (weight >= 600, bool(italic))
    if key not in _measure:
        _measure[key] = ImageFont.truetype(os.path.join(FONT_DIR, _font_file(weight, italic)), 200)
    return _measure[key].getlength(s) * size / 200.0 + (ls or 0.0) * len(s)


def R(t, w=None, f=None, s=None, i=None, ls=None, dx=None):
    return {"t": t, "w": w, "f": f, "s": s, "i": i, "ls": ls, "dx": dx}


def num(v):
    s = ("%.2f" % v).rstrip("0").rstrip(".")
    return s if s else "0"


class Canvas:
    def __init__(self):
        self.items = []
        self.warn = []

    # ---- shapes
    def rect(self, x, y, w, h, fill="surface", stroke="line", rx=6, sw=1.2, dash=None):
        self.items.append({"k": "rect", "x": x, "y": y, "w": w, "h": h, "fill": fill, "stroke": stroke,
                           "rx": rx, "sw": sw, "dash": dash})

    def line(self, pts, stroke="ink", sw=1.7, dash=None):
        self.items.append({"k": "line", "pts": list(pts), "stroke": stroke, "sw": sw, "dash": dash})

    def poly(self, pts, fill):
        self.items.append({"k": "poly", "pts": list(pts), "fill": fill})

    def circle(self, cx, cy, r, fill="surface", stroke="ink", sw=1.5):
        self.items.append({"k": "circle", "cx": cx, "cy": cy, "r": r, "fill": fill, "stroke": stroke, "sw": sw})

    def dot(self, cx, cy, r=2.8, fill="ink"):
        self.circle(cx, cy, r, fill=fill, stroke=None, sw=0)

    def arrow(self, pts, stroke="ink", sw=1.7, dash=None, head=8):
        pts = list(pts)
        (x1, y1), (x2, y2) = pts[-2], pts[-1]
        length = math.hypot(x2 - x1, y2 - y1)
        ux, uy = (x2 - x1) / length, (y2 - y1) / length
        end = (x2 - ux * head * 0.85, y2 - uy * head * 0.85)
        self.line(pts[:-1] + [end], stroke=stroke, sw=sw, dash=dash)
        bx, by = x2 - ux * head, y2 - uy * head
        hw = head * 0.46
        self.poly([(x2, y2), (bx - uy * hw, by + ux * hw), (bx + uy * hw, by - ux * hw)], stroke)

    # ---- text
    def text(self, x, y, runs, size=11.5, weight=400, fill="ink2", anchor="start", ls=0.0, italic=False,
             disp=False, maxw=None):
        if isinstance(runs, str):
            runs = [R(runs)]
        width = 0.0
        for r in runs:
            width += measure(r["t"], r["s"] or size, r["w"] or weight, italic if r["i"] is None else r["i"],
                             ls if r["ls"] is None else r["ls"]) + (r["dx"] or 0)
        left = x if anchor == "start" else (x - width / 2 if anchor == "middle" else x - width)
        label = "".join(r["t"] for r in runs)[:48]
        if maxw is not None and width > maxw:
            self.warn.append("too wide by %.0f px (%.0f > %.0f): %s" % (width - maxw, width, maxw, label))
        if left < 8 or left + width > W - 8:
            self.warn.append("leaves the canvas (%.0f..%.0f): %s" % (left, left + width, label))
        self.items.append({"k": "text", "x": x, "y": y, "runs": runs, "size": size, "weight": weight, "fill": fill,
                           "anchor": anchor, "ls": ls, "italic": italic, "disp": disp, "width": width, "left": left})
        return width

    def wrap(self, runs, width, size, weight=400):
        if isinstance(runs, str):
            runs = [R(runs)]
        tokens = []
        for r in runs:
            for word in r["t"].split(" "):
                if word:
                    tokens.append((word, r))
        space = measure(" ", size)
        lines, cur, curw = [], [], 0.0
        for word, r in tokens:
            ww = measure(word, size, r["w"] or weight, bool(r["i"]))
            if cur and curw + space + ww > width:
                lines.append(cur)
                cur, curw = [], 0.0
            if cur:
                curw += space
            cur.append((word, r))
            curw += ww
        if cur:
            lines.append(cur)
        out = []
        for ln in lines:
            merged = []
            for word, r in ln:
                if merged and merged[-1][1] is r:
                    merged[-1][0] += " " + word
                else:
                    merged.append([word, r])
            runs_out = []
            for idx, (t, r) in enumerate(merged):
                t2 = t + (" " if idx < len(merged) - 1 else "")
                runs_out.append(R(t2, w=r["w"], f=r["f"], i=r["i"]))
            out.append(runs_out)
        return out

    def para(self, x, y, width, runs, size=11.5, leading=15.0, weight=400, fill="ink2", italic=False):
        for ln in self.wrap(runs, width, size, weight):
            self.text(x, y, ln, size=size, weight=weight, fill=fill, italic=italic, maxw=width + 2)
            y += leading
        return y


c = Canvas()

# =============================================================== header
c.text(24, 50, [R("CoolSynth V3", w=700, f="ink", s=30), R("engine architecture", w=400, f="ink2", s=20, dx=14)],
       disp=True)
c.text(24, 76, "Signal flow of one patch, left to right. Every slot takes any module of its kind; "
               "the choices are listed underneath.", size=13, fill="ink2")
c.text(1576, 44, "2 parts · 16 voices · 3 generator + 3 processor slots per voice · "
                 "24 modulation routings per part · 118 factory presets",
       size=12.5, weight=600, fill="ink", anchor="end")

legend = [("note", "notes"), ("audio", "audio"), ("mod", "modulation"), ("planned", "planned, not built yet")]
total = sum(30 + 7 + measure(lab, 11.5) for _, lab in legend) + 22 * (len(legend) - 1)
lx, ly = 1576 - total, 72
for kind, lab in legend:
    if kind == "note":
        c.arrow([(lx, ly - 4), (lx + 30, ly - 4)], stroke="ink2", sw=1.4, head=7)
    elif kind == "audio":
        c.arrow([(lx, ly - 4), (lx + 30, ly - 4)], stroke="ink", sw=1.7, head=8)
    elif kind == "mod":
        c.arrow([(lx, ly - 4), (lx + 30, ly - 4)], stroke="mod", sw=1.5, dash="5 4", head=7)
    else:
        c.rect(lx + 2, ly - 11, 26, 14, fill=None, stroke="ink3", rx=3, sw=1.2, dash="4 3")
    lx += 37
    lx += c.text(lx, ly, lab, size=11.5, fill="ink2") + 22

# =============================================================== geometry of the spine
MIDY = 249          # centre line of part A's chain
MBY, MBH = 392, 164  # the modulation band inside part A
PX, PW = 172, 1246  # part panels
PA_TOP, PA_BOTTOM = 104, MBY + MBH + 12
BLY, BLH = PA_BOTTOM + 14, 70   # part B lane
BY = BLY + 45       # centre line of part B's chain
NW = dict(stroke="ink2", sw=1.4, head=7)

# ---- keys
KX, KY, KW, KH = 24, 193, 124, 112
c.rect(KX, KY, KW, KH, "note-bg", "note-line")
c.text(KX + 12, KY + 21, "KEYS · MIDI", size=10.5, weight=700, fill="ink", ls=0.8)
gx, gy = KX + 12, KY + 30
for i in range(9):
    c.rect(gx + i * 11, gy, 11, 20, "key-white", "note-line", rx=1.5, sw=0.9)
for i in (0, 1, 3, 4, 5, 7):
    c.rect(gx + i * 11 + 7.5, gy, 7, 12, "key-black", None, rx=1, sw=0)
for j, s in enumerate(["notes, velocity,", "release velocity,", "wheel, bend, pedal"]):
    c.text(KX + 12, KY + 70 + j * 14.5, s, size=11, fill="ink2", maxw=KW - 18)

c.text(24, 334, "PART MODE", size=10.5, weight=700, fill="ink2", ls=0.8)
c.text(24, 352, "A · Layer · Split · B", size=12, weight=600, fill="ink", maxw=130)
c.para(24, 369, 128, "Split by key range. Each part has its own velocity range and transpose.", size=11,
       leading=14.5)

# ---- part A panel
c.rect(PX, PA_TOP, PW, PA_BOTTOM - PA_TOP, "panel", "line", rx=10)
c.text(PX + 16, 123, [R("PART A", w=700, f="ink", ls=0.8),
                      R("one complete synth: its own patch, note layer, voices, modulation and insert effects",
                        f="ink2", dx=10)], size=11.5)

# ---- note layer
NY, NH = 193, 112


def notebox(x, w, title, lines):
    c.rect(x, NY, w, NH, "note-bg", "note-line")
    c.text(x + 10, NY + 22, title, size=12.5, weight=600, fill="ink", maxw=w - 16)
    for j, s in enumerate(lines):
        c.text(x + 10, NY + 42 + j * 14.5, s, size=11, fill="ink2", maxw=w - 14)


c.text(188, 184, "NOTE LAYER", size=10.5, weight=700, fill="ink2", ls=0.8)
notebox(188, 118, "Chord memory", ["12 chord types", "+ a learned chord", "of up to 6 notes;", "strum up or down"])
notebox(320, 126, "Arpeggiator", ["11 patterns", "Euclidean rhythms", "ratchets ×2 to ×4", "chance, accent,",
                                   "swing, latch"])
notebox(460, 126, "Play mode", ["poly · mono · legato", "unison: 2 to 8 voices", "glide, key priority",
                                 "voice spread: each", "voice has its place"])

c.arrow([(KX + KW, MIDY), (188, MIDY)], **NW)
c.arrow([(160, MIDY), (160, BY), (188, BY)], **NW)
c.dot(160, MIDY, fill="ink2")
c.arrow([(306, MIDY), (320, MIDY)], **NW)
c.arrow([(446, MIDY), (460, MIDY)], **NW)
c.arrow([(586, MIDY), (608, MIDY)], **NW)

# ---- the voice
VX, VY, VW, VH = 608, 140, 598, 212
c.rect(VX + 6, VY - 6, VW, VH, "surface", "line", rx=8)
c.rect(VX + 3, VY - 3, VW, VH, "surface", "line", rx=8)
c.rect(VX, VY, VW, VH, "surface", "line", rx=8, sw=1.3)
c.text(VX + 18, VY + 19, [R("VOICE", w=700, f="ink", ls=0.8),
                          R("up to 16 at once · each one computed at 2× the sample rate", f="ink2", dx=10)],
       size=11.5, maxw=VW - 30)


def caret(x, y, fill):
    c.poly([(x, y), (x + 8, y), (x + 4, y + 5.5)], fill)


GX, GW, GH = 626, 130, 34
GYS = [182, 232, 282]
c.text(GX, 176, "3 GENERATOR SLOTS", size=10.5, weight=700, fill="gen", ls=0.7)
for gy_, name, ex in zip(GYS, ["G1", "G2", "G3"], ["Wavetable", "FM4", "VA"]):
    c.rect(GX, gy_, GW, GH, "gen-bg", "gen-line")
    c.text(GX + 10, gy_ + 22, name, size=13, weight=700, fill="gen")
    c.text(GX + 38, gy_ + 22, ex, size=11.5, fill="ink")
    caret(GX + GW - 18, gy_ + 14, "gen")
for gy_ in GYS[:2]:
    c.arrow([(GX + 18, gy_ + GH), (GX + 18, gy_ + GH + 16)], stroke="gen", sw=1.5, head=6)
    c.text(GX + 30, gy_ + GH + 12, "sync · ring · FM · AM", size=10.5, fill="gen", maxw=GW - 30)

MX = 792
c.arrow([(GX + GW, GYS[0] + 17), (MX, GYS[0] + 17), (MX, MIDY - 11)])
c.arrow([(GX + GW, MIDY), (MX - 11, MIDY)])
c.arrow([(GX + GW, GYS[2] + 17), (MX, GYS[2] + 17), (MX, MIDY + 11)])
c.circle(MX, MIDY, 11)
c.line([(MX - 5, MIDY), (MX + 5, MIDY)], sw=1.5)
c.line([(MX, MIDY - 5), (MX, MIDY + 5)], sw=1.5)
c.arrow([(MX + 11, MIDY), (828, MIDY)])

PXS, PWD, PHT, PYT = [828, 930, 1032], 86, 56, 221
c.text(828, 212, "3 PROCESSOR SLOTS, IN SERIES", size=10.5, weight=700, fill="proc", ls=0.7)
for x, name, ex in zip(PXS, ["P1", "P2", "P3"], ["Ladder", "Comb", "Drive"]):
    c.rect(x, PYT, PWD, PHT, "proc-bg", "proc-line")
    c.text(x + 10, PYT + 23, name, size=13, weight=700, fill="proc")
    c.text(x + 10, PYT + 42, ex, size=11.5, fill="ink")
    caret(x + PWD - 18, PYT + 15, "proc")
c.arrow([(914, MIDY), (930, MIDY)], head=7)
c.arrow([(1016, MIDY), (1032, MIDY)], head=7)
c.arrow([(1118, MIDY), (1134, MIDY)], head=7)

c.rect(1134, PYT, 58, PHT, "surface", "ink", sw=1.3)
c.text(1163, PYT + 25, "AMP", size=12, weight=700, fill="ink", anchor="middle", ls=0.6)
c.text(1163, PYT + 42, "+ pan", size=10.5, fill="ink2", anchor="middle")
SX = 1236
c.arrow([(1192, MIDY), (SX - 11, MIDY)])
c.circle(SX, MIDY, 11)
c.text(SX, MIDY + 4.5, "Σ", size=13, weight=700, fill="ink", anchor="middle")
c.text(SX, MIDY + 27, "2× → 1×", size=10, fill="ink2", anchor="middle")

IXS, IW = [1262, 1315, 1368], 42
c.arrow([(SX + 11, MIDY), (IXS[0], MIDY)], head=7)
c.text(IXS[0], 212, "3 INSERT SLOTS", size=10.5, weight=700, fill="fx", ls=0.7, maxw=150)
for x, name, ex in zip(IXS, ["I1", "I2", "I3"], ["Chorus", "Room", "Delay"]):
    c.rect(x, PYT, IW, PHT, "fx-bg", "fx-line")
    c.text(x + IW / 2, PYT + 23, name, size=12.5, weight=700, fill="fx", anchor="middle")
    c.text(x + IW / 2, PYT + 42, ex, size=10.5, fill="ink", anchor="middle", maxw=IW - 4)
c.arrow([(IXS[0] + IW, MIDY), (IXS[1], MIDY)], head=6)
c.arrow([(IXS[1] + IW, MIDY), (IXS[2], MIDY)], head=6)

# generator 3 into the ladder's cutoff
c.arrow([(700, 316), (700, 330), (852, 330), (852, PYT + PHT)], stroke="gen", sw=1.5, dash="4 3", head=7)
c.text(706, 344, "generator 3 → ladder cutoff, at audio rate", size=10.5, fill="gen")
c.text(868, 300, "Modulation refreshed 3,000 times a second", size=11, fill="ink2", maxw=320)
c.text(868, 315, "(at 48 kHz). Values ramp between updates.", size=11, fill="ink2", maxw=320)

# ---- modulation into the voice
RY = 366
MODW = dict(stroke="mod", sw=1.5, dash="5 4")
c.line([(691, RY), (1163, RY)], **MODW)
for x in (691, 973, 1163):
    c.arrow([(x, RY), (x, VY + VH + 1)], head=7, **MODW)
c.line([(1075, MBY), (1075, RY)], **MODW)
c.text(703, 381, "into any slot value, pitch, level, pan, combine amount, amp level", size=10.5, fill="mod",
       maxw=362)
c.text(1087, 381, "also LFO rates and envelope times", size=10.5, fill="mod", maxw=300)

c.rect(188, MBY, 742, MBH, "surface", "mod-line")
c.text(204, MBY + 21, "MODULATION SOURCES · PER VOICE", size=10.5, weight=700, fill="mod", ls=0.7, maxw=340)
c.text(572, MBY + 21, "PER PART, SHARED BY ITS VOICES", size=10.5, weight=700, fill="mod", ls=0.7, maxw=340)
c.line([(556, MBY + 10), (556, MBY + MBH - 10)], stroke="line", sw=1)


def item(x, y, width, head, body):
    return c.para(x, y, width, [R(head, w=700, f="ink"), R(body, f="ink2")], size=11.5, leading=15) + 5


y = MBY + 42
y = item(204, y, 338, "3 envelopes",
         "delay, attack, hold, decay, sustain, release; a curve per stage; times that shorten up the keyboard; "
         "loop; one-shot")
y = item(204, y, 338, "2 LFOs",
         "8 shapes with sample-and-hold and a 16-step staircase; in Hz or 16 tempo divisions; delay and fade-in")
y = item(204, y, 338, "Per note", "velocity, release velocity, key position, a random value, a slow drift")
if y - 5 > MBY + MBH - 4:
    c.warn.append("per-voice sources overflow their box: %.0f" % y)

y = MBY + 42
y = item(572, y, 346, "2 global LFOs", "one phase for all voices; song-position lock")
y = item(572, y, 346, "Global envelope", "from a phrase’s first key, or from every key")
y = item(572, y, 346, "16-step sequencer", "slide per step, 1 to 16 steps, tempo-locked")
y = item(572, y, 346, "4 macros · chaos · wheel · bend", "(chaos: a slow random walk)")
pl = "planned: aftertouch · MPE pressure and slide · expression pedal"
y += 6
c.rect(566, y - 14, measure(pl, 11, italic=True) + 14, 20, fill=None, stroke="ink3", rx=4, sw=1.1, dash="4 3")
c.text(573, y, pl, size=11, fill="ink3", italic=True, maxw=346)
if y + 6 > MBY + MBH - 6:
    c.warn.append("per-part sources overflow their box: %.0f > %.0f" % (y + 6, MBY + MBH - 6))

c.arrow([(930, 472), (952, 472)], head=7, **MODW)
c.rect(952, MBY, 246, MBH, "mod-bg", "mod-line")
c.text(968, MBY + 23, "MOD MATRIX", size=13, weight=700, fill="mod", ls=0.5)
c.text(968, MBY + 41, "24 routings per part", size=12, weight=600, fill="ink")
c.para(968, MBY + 61, 216, "Each routing: a source, a destination, an amount, a second source that scales it "
                           "(via) and one of 6 curves. One curve snaps pitch to semitones.", size=11.5,
       leading=15, fill="ink")
c.text(968, MBY + MBH - 13, "Over 20 sources, over 90 destinations", size=11.5, weight=600, fill="ink", maxw=220)

c.text(1214, MBY + 21, "ALWAYS CONNECTED", size=10.5, weight=700, fill="ink2", ls=0.7)
for j, s in enumerate(["ENV1 → amp", "ENV2 → P1 cutoff", "velocity → amp and P1 cutoff", "wheel → LFO1 depth",
                       "bend → pitch", "key tracking per slot"]):
    c.text(1214, MBY + 42 + j * 15, s, size=11.5, fill="ink2", maxw=196)

# ---- part B lane
c.rect(PX, BLY, PW, BLH, "panel", "line", rx=10)
c.text(PX + 16, BLY + 20, [R("PART B", w=700, f="ink", ls=0.8),
                           R("the same chain again with its own patch · the two parts share the 16 voices, "
                             "each sure of 8", f="ink2", dx=10)], size=11.5)
my, mh = BY - 13, 26
c.rect(188, my, 398, mh, "note-bg", "note-line", rx=5)
c.text(387, my + 17, "chord memory → arpeggiator (shared settings, on or off per part) → play mode", size=10.5,
       fill="ink2", anchor="middle", maxw=390)
c.arrow([(586, BY), (626, BY)], **NW)
for i, x in enumerate((626, 672, 718)):
    c.rect(x, my, 38, mh, "gen-bg", "gen-line", rx=5)
    c.text(x + 19, my + 17.5, "G%d" % (i + 1), size=11.5, weight=700, fill="gen", anchor="middle")
c.arrow([(756, BY), (828, BY)], head=7)
for i, x in enumerate((828, 874, 920)):
    c.rect(x, my, 38, mh, "proc-bg", "proc-line", rx=5)
    c.text(x + 19, my + 17.5, "P%d" % (i + 1), size=11.5, weight=700, fill="proc", anchor="middle")
c.arrow([(958, BY), (980, BY)], head=7)
c.rect(980, my, 46, mh, "surface", "ink", rx=5, sw=1.2)
c.text(1003, my + 17.5, "AMP", size=11, weight=700, fill="ink", anchor="middle", ls=0.5)
c.arrow([(1026, BY), (SX - 9, BY)], head=7)
c.circle(SX, BY, 9)
c.text(SX, BY + 4, "Σ", size=11.5, weight=700, fill="ink", anchor="middle")
c.arrow([(SX + 9, BY), (IXS[0], BY)], head=7)
for i, x in enumerate(IXS):
    c.rect(x, my, IW, mh, "fx-bg", "fx-line", rx=5)
    c.text(x + IW / 2, my + 17.5, "I%d" % (i + 1), size=11.5, weight=700, fill="fx", anchor="middle")

# ---- mix bus and master column
BUSX = 1432
CX0, CW, CMID = 1446, 130, 1511
c.rect(CX0, PA_TOP, CW, BLY + BLH - PA_TOP, "panel", "line", rx=10)
c.line([(IXS[2] + IW, MIDY), (BUSX, MIDY)])
c.arrow([(IXS[2] + IW, BY), (BUSX, BY), (BUSX, 156), (1460, 156)])
c.dot(BUSX, MIDY)
c.text(1460, 126, "MASTER", size=10.5, weight=700, fill="ink", ls=0.8)
c.rect(1460, 140, 102, 32, "surface", "ink", sw=1.3)
c.text(CMID, 161, "MIX  A + B", size=12, weight=600, fill="ink", anchor="middle")
c.arrow([(CMID, 172), (CMID, 196)], head=7)
for i in range(6):
    fy = 196 + i * 38
    c.rect(1460, fy, 102, 30, "fx-bg", "fx-line", rx=5, sw=1.3)
    c.text(1470, fy + 20, "F%d" % (i + 1), size=12, weight=700, fill="fx")
    c.text(1553, fy + 20, "any effect", size=10.5, fill="ink2", anchor="end")
    if i < 5:
        c.line([(CMID, fy + 30), (CMID, fy + 38)], sw=1.5)
c.arrow([(CMID, 416), (CMID, 438)], head=7)
c.rect(1460, 438, 102, 30, "surface", "ink", sw=1.3)
c.text(CMID, 458, "MASTER GAIN", size=10.5, weight=700, fill="ink", anchor="middle", ls=0.5)
c.arrow([(CMID, 468), (CMID, 484)], head=7)
c.rect(1460, 484, 102, 30, "surface", "ink", sw=1.3)
c.text(CMID, 504, "SOFT CLIPPER", size=10.5, weight=700, fill="ink", anchor="middle", ls=0.5)
c.arrow([(CMID, 514), (CMID, 540)], head=7)
c.rect(1460, 540, 102, 30, "ink", None, rx=15, sw=0)
c.text(CMID, 560, "STEREO OUT", size=10.5, weight=700, fill="bg", anchor="middle", ls=0.6)
for j, s in enumerate(["Any of the 12 effects", "in any master slot,", "in slot order."]):
    c.text(CMID, 594 + j * 14, s, size=10.5, fill="ink2", anchor="middle", maxw=CW - 8)

# =============================================================== the three type lists
MENU_Y = BLY + BLH + 22
HEAD_H = 32
ROW_LEAD = 14.5


def menu_layout(rows, detailw):
    laid, h = [], 0.0
    for name, runs in rows:
        lines = c.wrap(runs, detailw, 11.5)
        rh = max(1, len(lines)) * ROW_LEAD + 11
        laid.append((name, lines, rh))
        h += rh
    return laid, h


def menu_draw(x, y, w, h, kind, head, sub, laid, namew, note_runs, note_italic=False):
    c.rect(x, y, w, HEAD_H, kind + "-bg", None, rx=8, sw=0)
    c.rect(x, y + HEAD_H / 2, w, HEAD_H / 2, kind + "-bg", None, rx=0, sw=0)
    c.rect(x, y + HEAD_H, w, h - HEAD_H, "surface", None, rx=8, sw=0)
    c.rect(x, y + HEAD_H, w, 12, "surface", None, rx=0, sw=0)
    c.rect(x, y, w, h, None, kind + "-line", rx=8, sw=1.3)
    c.text(x + 16, y + 21, [R(head, w=700, f=kind, ls=0.7), R(sub, f="ink", dx=10)], size=11.5, maxw=w - 28)
    ry = y + HEAD_H
    for idx, (name, lines, rh) in enumerate(laid):
        if idx:
            c.line([(x + 12, ry), (x + w - 12, ry)], stroke="line", sw=0.8)
        c.text(x + 16, ry + 18, name, size=12.5, weight=700, fill=kind, maxw=namew - 8)
        for j, ln in enumerate(lines):
            c.text(x + 16 + namew, ry + 18 + j * ROW_LEAD, ln, size=11.5, fill="ink", maxw=w - namew - 26)
        ry += rh
    note_lines = c.wrap(note_runs, w - 32, 11)
    ny = y + h - 12 - (len(note_lines) - 1) * 14
    c.line([(x + 12, ny - 16), (x + w - 12, ny - 16)], stroke="line", sw=0.8)
    for ln in note_lines:
        c.text(x + 16, ny, ln, size=11, fill="ink2", italic=note_italic, maxw=w - 28)
        ny += 14
    return ry, len(note_lines)


GEN_ROWS = [
    ("VA", "Sine, triangle, saw and pulse as one morph, PWM, sub, unison of up to 7 on a JP-8000-style detune "
           "curve. Band-limited edges, also under hard sync: saw aliasing −79 dB at 3.5 kHz."),
    ("Noise", "White, pink, a pitch-tracked band, or the NES noise channel’s 15-bit shift register."),
    ("FM2", "Two sine operators: ratio 0.25 to 16, index up to 10 radians, self-feedback."),
    ("FM4", "Four operators, 6 algorithms, feedback; each operator plays one of the TX81Z’s 8 waves."),
    ("Wavetable", "4 tables of 16 frames with position morph; unison of up to 5. Every frame is kept at 11 band "
                  "limits: aliasing −118 dB at A7."),
    ("String", "Plucked-string model: decay, damping, pick position, a noise or a fingertip pluck, a damper on "
               "release. Stays in tune however dark the damping."),
    ("Modal", "12 struck partials. Material morphs from wood (a marimba bar, 1 : 4 : 10.1 : 19.1, measured on "
              "a recording) through metal and glass to a tine. Mallet hardness, strike position."),
    ("Sample", "Plays the Conservatory’s recorded instruments (16-point sinc read), or 5 factory loops."),
    ("Grain", "A cloud of up to 12 grains per voice from a sample: position, scan (0 freezes it), size, "
              "density, spray, pitch spray."),
]
PROC_ROWS = [
    ("Ladder", "4-pole transistor ladder with saturation in every stage. LP24, LP12, BP, HP12. Self-oscillates "
               "from its own noise floor, within 3.3 cents of the cutoff (250 Hz to 4 kHz). Generator 3 can "
               "modulate the cutoff at audio rate, up to 3 octaves either way."),
    ("SVF", "2-pole state-variable filter: low-pass, high-pass, band-pass, notch. Q from 0.5 to 40, drive."),
    ("Comb", "Feedback comb tuned to the key: tune, damping, either polarity."),
    ("Formant", "Three band-passes on the vowels a, e, i, o, u, with morph and throat size."),
    ("Folder", "Sine wavefolder with up to 30 dB of drive and symmetry; anti-aliased."),
    ("Crusher", "1 to 16 bits, continuous; sample-and-hold down to 1 kHz."),
    ("Drive", "Soft, hard or tube curve with tone; anti-aliased."),
]
FX_ROWS = [
    ("Drive", "saturation, shaped at 2× oversampling"),
    ("Lo-fi", "wow, flutter, hiss, crackle, bandwidth, bit and rate crush"),
    ("Modulation", "phaser · flanger with feedback of either polarity · chorus"),
    ("Delay", "tape or ping-pong; free, or 14 tempo-synced note values"),
    ("Plate reverb", "pre-delay, low cut, modulation; shimmer feeds the tail back an octave up"),
    ("Dynamics, EQ", "compressor · EQ with low shelf, tilt and high shelf · pump: tempo-synced ducking"),
    ("Room", "24 early reflections of a box-shaped room into a 16-line feedback delay network; decay 0.1 to "
             "1.2 s."),
    ("Grain", "replays the last 4 seconds as a cloud of up to 32 grains; freeze."),
]
panels = [
    dict(x=24, w=594, kind="gen", head="GENERATOR SLOT", sub="one of 9 types · any type in any of the 3 slots",
         rows=GEN_ROWS, namew=84,
         note=[R("Planned: your own wavetables and samples; grains from a live generator.")], italic=True),
    dict(x=634, w=462, kind="proc", head="PROCESSOR SLOT", sub="one of 7 types · per voice",
         rows=PROC_ROWS, namew=70,
         note=[R("Three in series, per voice: a chord is filtered, folded and driven note by note, before the "
                 "notes are summed.")],
         italic=False),
    dict(x=1112, w=464, kind="fx", head="EFFECT SLOT", sub="one of 12 effects · 3 slots per part · 6 master slots",
         rows=FX_ROWS, namew=100,
         note=[R("Master section: six slots, F1 to F6, any effect in any order, then gain and a soft clipper. "
                 "Each part sets what share of it passes through them.")],
         italic=False),
]
need = 0.0
for p in panels:
    p["laid"], rows_h = menu_layout(p["rows"], p["w"] - p["namew"] - 28)
    p["note_n"] = len(c.wrap(p["note"], p["w"] - 32, 11))
    p["need"] = HEAD_H + rows_h + 22 + p["note_n"] * 14 + 6
    need = max(need, p["need"])
MENU_H = math.ceil(need)
for p in panels:
    menu_draw(p["x"], MENU_Y, p["w"], MENU_H, p["kind"], p["head"], p["sub"], p["laid"], p["namew"], p["note"],
              p["italic"])

# =============================================================== what is unusual
CARD_Y = MENU_Y + MENU_H + 20


def card_layout(w, paras):
    lines = []
    for runs in paras:
        lines.append(c.wrap(runs, w - 32, 12))
    n = sum(len(x) for x in lines)
    return lines, 38 + n * 16 + (len(paras) - 1) * 6 + 12


def card_draw(x, y, w, h, title, sub, lines):
    c.rect(x, y, w, h, "surface", "ink", rx=8, sw=1.3)
    c.text(x + 16, y + 24, [R(title, w=700, f="ink", ls=0.8), R(sub, f="ink2", dx=10)], size=11.5, maxw=w - 28)
    ty = y + 46
    for para_lines in lines:
        for ln in para_lines:
            c.text(x + 16, ty, ln, size=12, fill="ink2", maxw=w - 28)
            ty += 16
        ty += 6


B = lambda t: R(t, w=700, f="ink")
cards = [
    dict(x=24, w=700, title="THE MUSEUM", sub="classic sounds rebuilt from the instruments’ own numbers", paras=[
        [B("Ten so far:"), R("DX7 E.PIANO 1 and BASS 1 · Prophet-5 Sync II and Brass · Minimoog Fat Bass and "
                             "the Trilogy lead · TX81Z LatelyBass · and the two synths that open a 1981 record, "
                             "rebuilt from the record alone (the second also in a rougher version).")],
        [B("Method:"), R("start from the instrument’s own patch data (DX7 voice data, Prophet-5 factory "
                         "programs, a 1974 Minimoog sound chart, the TX81Z preset table read from its "
                         "firmware), convert the numbers to this engine’s units, and compare with a reference "
                         "recording. A sound is admitted only after a side-by-side listening test.")],
        [B("Measured:"), R("LatelyBass, with nothing tuned by ear, has its first 16 harmonics within 0.9 dB on "
                           "average of a recorded TX81Z note. Fat Bass is typically within 1 dB per band up "
                           "to 2.8 kHz of a recorded Minimoog; Sync II within 1 dB of a recorded Prophet-5.")],
        [B("References:"), R("recordings of the hardware; for the DX7 pair, of an open DX7 emulation."),
         B("The engine grew for it:"), R("oscillator-3 filter FM for the Minimoog, eight operator waves for "
                                         "the TX81Z.")],
    ]),
    dict(x=740, w=440, title="THE CONSERVATORY", sub="40 recorded acoustic instruments", paras=[
        [R("Seven families: keys, strings, plucked, brass, winds, organs, mallets and bells. 76 playing "
           "styles.")],
        [R("2,213 lossless recordings: every recorded pitch, dynamic layer and take, at full length, in "
           "stereo.")],
        [R("They play through the Sample generator, so a grand piano or a cello section sits in a slot like "
           "any oscillator: layer it with FM, send it through the ladder, modulate it from the matrix.")],
        [R("Instruments load in the background; a note never waits for the disk.")],
    ]),
    dict(x=1196, w=380, title="2× VOICES", sub="and FM that does not fold back", paras=[
        [R("Every voice is computed at twice the sample rate and brought down once per part.")],
        [R("FM2 and FM4 also hold the index, note by note, where the first sideband that would fold back into "
           "the audible band stays under −80 dB. Low and middle notes never reach the limit.")],
        [R("Measured: with index 10 asked for on a 2 kHz carrier, the largest alias below 20 kHz sits at "
           "−75 dB.")],
        [R("Changing a slot’s type under a held note crossfades in 5 ms; nothing else in the patch moves.")],
    ]),
]
card_h = 0
for cd in cards:
    cd["lines"], cd["need"] = card_layout(cd["w"], cd["paras"])
    card_h = max(card_h, cd["need"])
for cd in cards:
    card_draw(cd["x"], CARD_Y, cd["w"], card_h, cd["title"], cd["sub"], cd["lines"])

# =============================================================== the rooms
ROOM_Y = CARD_Y + card_h + 30
c.text(24, ROOM_Y, [R("118 FACTORY PRESETS IN 13 ROOMS", w=700, f="ink", ls=0.8),
                    R("Workshop · Bedroom · Arcade · Outrun · Observatory · Greenhouse · Basement · Studio 1982 · "
                      "Main Stage · Engine Room · Clock Tower · Conservatory · Museum", f="ink2", dx=12)],
       size=11.5, maxw=1552)
H = int(ROOM_Y + 24)


# =============================================================== output
def colour_attr(kind, token, mode, classes):
    if token is None:
        return '%s="none"' % kind
    if mode == "html":
        classes.append(("f-" if kind == "fill" else "s-") + token)
        return ""
    return '%s="%s"' % (kind, DARK[token]) # the standalone map is dark like the page


def render_svg(mode):
    out = []
    for it in c.items:
        k = it["k"]
        classes, a = [], []
        if k == "rect":
            a += ['x="%s"' % num(it["x"]), 'y="%s"' % num(it["y"]), 'width="%s"' % num(it["w"]),
                  'height="%s"' % num(it["h"])]
            if it["rx"]:
                a.append('rx="%s"' % num(it["rx"]))
            a.append(colour_attr("fill", it["fill"], mode, classes))
            if it["stroke"]:
                a.append(colour_attr("stroke", it["stroke"], mode, classes))
                a.append('stroke-width="%s"' % num(it["sw"]))
                if it["dash"]:
                    a.append('stroke-dasharray="%s"' % it["dash"])
            tag = "rect"
        elif k == "line":
            a.append('points="%s"' % " ".join("%s,%s" % (num(x), num(y)) for x, y in it["pts"]))
            a.append('fill="none"')
            a.append(colour_attr("stroke", it["stroke"], mode, classes))
            a.append('stroke-width="%s"' % num(it["sw"]))
            a.append('stroke-linejoin="round"')
            if it["dash"]:
                a.append('stroke-dasharray="%s"' % it["dash"])
            else:
                a.append('stroke-linecap="round"')
            tag = "polyline"
        elif k == "poly":
            a.append('points="%s"' % " ".join("%s,%s" % (num(x), num(y)) for x, y in it["pts"]))
            a.append(colour_attr("fill", it["fill"], mode, classes))
            tag = "polygon"
        elif k == "circle":
            a += ['cx="%s"' % num(it["cx"]), 'cy="%s"' % num(it["cy"]), 'r="%s"' % num(it["r"])]
            a.append(colour_attr("fill", it["fill"], mode, classes))
            if it["stroke"]:
                a.append(colour_attr("stroke", it["stroke"], mode, classes))
                a.append('stroke-width="%s"' % num(it["sw"]))
            tag = "circle"
        else:
            a += ['x="%s"' % num(it["x"]), 'y="%s"' % num(it["y"]), 'font-size="%s"' % num(it["size"])]
            if it["weight"] != 400:
                a.append('font-weight="%d"' % it["weight"])
            if it["anchor"] != "start":
                a.append('text-anchor="%s"' % it["anchor"])
            if it["ls"]:
                a.append('letter-spacing="%s"' % num(it["ls"]))
            if it["italic"]:
                a.append('font-style="italic"')
            a.append(colour_attr("fill", it["fill"], mode, classes))
            if it["disp"] and mode == "html":
                classes.append("disp")
            runs = it["runs"]
            plain = len(runs) == 1 and all(runs[0][key] is None for key in ("w", "f", "s", "i", "ls", "dx"))
            if plain:
                inner = escape(runs[0]["t"])
            else:
                parts = []
                for r in runs:
                    ra, rc = [], []
                    if r["w"] is not None:
                        ra.append('font-weight="%d"' % r["w"])
                    if r["s"] is not None:
                        ra.append('font-size="%s"' % num(r["s"]))
                    if r["i"] is not None:
                        ra.append('font-style="%s"' % ("italic" if r["i"] else "normal"))
                    if r["ls"] is not None:
                        ra.append('letter-spacing="%s"' % num(r["ls"]))
                    if r["dx"] is not None:
                        ra.append('dx="%s"' % num(r["dx"]))
                    if r["f"] is not None:
                        ra.append(colour_attr("fill", r["f"], mode, rc))
                    if rc:
                        ra.insert(0, 'class="%s"' % " ".join(rc))
                    parts.append("<tspan %s>%s</tspan>" % (" ".join(x for x in ra if x), escape(r["t"])))
                inner = "".join(parts)
            if classes:
                a.insert(0, 'class="%s"' % " ".join(classes))
            out.append("<text %s>%s</text>" % (" ".join(x for x in a if x), inner))
            continue
        if classes:
            a.insert(0, 'class="%s"' % " ".join(classes))
        out.append("<%s %s/>" % (tag, " ".join(x for x in a if x)))
    return "\n".join(out)


ARIA = ("CoolSynth V3 engine architecture. Keys and MIDI feed a note layer of chord memory, arpeggiator and play "
        "mode. Each of two parts is a complete synth: per voice three generator slots that can sync, ring, FM or "
        "AM one another, three processor slots in series and an amplifier, then three insert effects. Modulation "
        "sources and a 24-routing matrix feed the voice from below. Both parts are mixed into six master effect "
        "slots, gain, soft clipper and stereo out. Three lists give the 9 generator types, 7 processor types "
        "and 12 effects a slot can hold; three notes describe the Museum, the Conservatory and 2x voices.")

svg_open_standalone = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" '
                       'font-family="sans-serif" role="img" aria-label="%s">' % (W, H, W, H, escape(ARIA)))
standalone = ('<?xml version="1.0" encoding="UTF-8"?>\n' + svg_open_standalone +
              "\n<title>CoolSynth V3 engine architecture</title>\n" +
              '<rect x="0" y="0" width="%d" height="%d" fill="%s"/>\n' % (W, H, DARK["bg"]) +
              render_svg("svg") + "\n</svg>\n")
with open(os.path.join(OUT, "engine-map.svg"), "w", encoding="utf-8", newline="\n") as fh:
    fh.write(standalone)


def tokens_css(pal, indent="  "):
    return "\n".join("%s--%s: %s;" % (indent, k, v) for k, v in pal.items())


util = "\n".join(".f-%s{fill:var(--%s)} .s-%s{stroke:var(--%s)}" % (k, k, k, k) for k in LIGHT)

CAPTION = ("The signal path of one CoolSynth V3 patch. Keys pass through the note layer into two parts; each part "
           "is a complete synth whose voices chain three generator slots, three processor slots and an amplifier, "
           "followed by three insert effects, the mix and the master section. Modulation enters the voice from "
           "below. Dashed outlines mark what is designed but not built yet; the user interface comes last and is "
           "not drawn.")

UPDATED = "14 November 2026"
REPO_URL = "https://github.com/tomcoolpxl/CoolSynth"


page ="""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CoolSynth V3 Engine</title>
<meta name="description" content="The engine of CoolSynth, a slot-based software synthesizer: its architecture, what each slot can hold, and where the project stands.">
<link rel="icon" href="data:image/svg+xml,%%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%%3E%%3Crect width='32' height='32' rx='7' fill='%%235042B5'/%%3E%%3Cpath d='M5 20c3-9 5-9 7 0s4 9 7 0 4-9 8 0' stroke='white' stroke-width='3' fill='none' stroke-linecap='round'/%%3E%%3C/svg%%3E">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:ital,wght@0,400;0,600;0,700;1,400&family=Barlow+Semi+Condensed:wght@400;700&display=swap">
<style>
/* Layout: one wide figure first. The signal flow is a left-to-right spine; the type lists and notes hang beneath
   it. A few short sections follow in a reading column. Always dark: the map reads best on it (the user's choice). */
:root {
%(dark)s
  --font-text: "Barlow", "Helvetica Neue", Arial, sans-serif;
  --font-display: "Barlow Semi Condensed", "Barlow", "Arial Narrow", Arial, sans-serif;
  color-scheme: dark;
}
*, *::before, *::after { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font-family: var(--font-text); font-size: 16px; line-height: 1.55; }
.wrap {
  max-width: 1680px; margin: 0 auto; padding-block: 28px 40px;
  padding-left: max(16px, env(safe-area-inset-left, 0px));
  padding-right: max(16px, env(safe-area-inset-right, 0px));
}
header { max-width: 80ch; margin-bottom: 22px; }
.kicker { margin: 0 0 6px; font-family: var(--font-display); font-weight: 700; font-size: 13px; letter-spacing: 1.4px;
          text-transform: uppercase; color: var(--fx); }
h1 { margin: 0 0 10px; font-family: var(--font-display); font-weight: 700; font-size: clamp(30px, 4.2vw, 44px);
     line-height: 1.05; letter-spacing: 0.2px; }
.lead { margin: 0; font-size: 18px; color: var(--ink2); text-wrap: pretty; }
.flux { display: inline-block; margin-top: 14px; padding: 4px 10px; border: 1px dashed var(--note-line);
        border-radius: 999px; font-size: 13.5px; color: var(--ink2); background: var(--surface); }
figure { margin: 0; display: flex; flex-direction: column; gap: 14px; min-width: 0; }
.scroller { overflow-x: auto; max-width: 100%%; min-width: 0; border: 1px solid var(--line); border-radius: 12px; background: var(--bg); }
.scroller:focus-visible { outline: 2px solid var(--fx); outline-offset: 2px; }
.map { display: block; width: 100%%; min-width: 1180px; height: auto; }
.map text { font-family: var(--font-text); font-variant-numeric: tabular-nums; }
.map text.disp { font-family: var(--font-display); }
.hint { display: none; margin: 0; font-size: 13px; color: var(--ink3); }
@media (max-width: 1210px) { .hint { display: block; } }
figcaption { max-width: 80ch; color: var(--ink2); font-size: 14.5px; text-wrap: pretty; }
figcaption a { color: inherit; }
.columns { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%%, 340px), 1fr)); gap: 18px; margin-top: 34px; }
section { min-width: 0; padding: 18px 20px 20px; background: var(--surface); border: 1px solid var(--line); border-radius: 12px; }
section.wide { grid-column: 1 / -1; }
h2 { margin: 0 0 10px; font-family: var(--font-display); font-weight: 700; font-size: 15px; letter-spacing: 1.2px;
     text-transform: uppercase; }
.s-gen h2 { color: var(--gen); } .s-proc h2 { color: var(--proc); } .s-fx h2 { color: var(--fx); } .s-mod h2 { color: var(--mod); }
section p { margin: 0 0 10px; color: var(--ink2); text-wrap: pretty; }
section p:last-child { margin-bottom: 0; }
ul { margin: 0; padding-left: 18px; color: var(--ink2); }
li { margin: 5px 0; text-wrap: pretty; }
li b { color: var(--ink); font-weight: 600; }
a { color: var(--fx); text-underline-offset: 2px; }
footer { margin-top: 32px; padding-top: 14px; border-top: 1px solid var(--line); color: var(--ink3); font-size: 13.5px; }
%(util)s
</style>
</head>
<body>
<div class="wrap">
  <header>
    <p class="kicker">CoolSynth · V3 engine</p>
    <h1>A synthesizer built from slots</h1>
    <p class="lead">CoolSynth is a polyphonic software synthesizer, a VST3 plugin and a standalone app, written in C++
    with JUCE. Its engine is made of slots: any sound source in any of three generator slots, any filter or shaper in
    any of three processor slots, any effect in any effect slot, all of it modulated from one matrix.</p>
    <p class="flux">Work in progress: the engine still changes week to week, and the instrument's own interface comes last.</p>
  </header>

  <figure>
    <p class="hint">The map is wider than this screen: scroll it sideways to follow the signal.</p>
    <div class="scroller" tabindex="0" role="group" aria-label="Engine map; scrolls sideways on narrow screens">
<svg class="map" viewBox="0 0 %(w)d %(h)d" role="img" aria-label="%(aria)s">
<rect x="0" y="0" width="%(w)d" height="%(h)d" class="f-bg"/>
%(svg)s
</svg>
    </div>
    <figcaption>%(caption)s <a href="engine-map.svg">The map alone, as SVG.</a></figcaption>
  </figure>

  <div class="columns">
    <section>
      <h2>A patch</h2>
      <p>Two parts, each a complete synth with its own voices, modulation and effects, played alone, layered or split
      across the keyboard. They share sixteen voices: each is sure of eight and gets more when the other needs fewer.</p>
      <p>Each voice runs three generator slots into three processor slots in series and an amplifier, at twice the
      host's sample rate. Play modes: poly, mono, legato and unison, with glide by time or by rate.</p>
    </section>
    <section class="s-mod">
      <h2>Modulation</h2>
      <p>Per voice: three envelopes with hold, curves and key-scaled times, two LFOs, velocity, key, note random and
      drift. Per part: two global LFOs, a looping global envelope, a 16-step sequencer with slides, four macros and a
      chaos source.</p>
      <p>A 24-slot matrix routes any of them to pitch, level, pan, any slot's values and the envelope times; each
      routing can be scaled by a second source and bent by a curve.</p>
    </section>
    <section>
      <h2>Notes in</h2>
      <p>Chord memory (twelve chord shapes, or one learned from the keys), strum, and an arpeggiator with eleven
      patterns, swing, ratchets, accents and Euclidean rhythms, all before the parts.</p>
      <p>Effects: three slots on each part, then six master slots, each holding any of twelve effects in any order.
      What each slot can hold, generator, processor or effect, is listed under the map.</p>
    </section>
    <section class="wide">
      <h2>Sounds, and how they are checked</h2>
      <p>118 factory presets in 13 rooms. The Conservatory holds 40 recorded acoustic instruments from free (CC0)
      sample libraries, played through the Sample generator. The Museum holds classic synth sounds rebuilt from the
      instruments' own patch data, or from a record when no data exists, and each is admitted only after a
      side-by-side listening test against a recording of the real thing.</p>
      <p>Every module has unit tests, and sounds are measured against recordings (harmonic levels, band levels,
      envelopes, pitch) before they are judged by ear.</p>
    </section>
    <section class="wide">
      <h2>Being built now</h2>
      <ul>
        <li><b>Key pressure, MPE pressure and slide, and the expression pedal</b> as working modulation sources.</li>
        <li><b>Your own samples and wavetables,</b> loaded into the Sample, Grain and Wavetable generators.</li>
        <li><b>Then the interface,</b> designed once the engine has settled.</li>
      </ul>
    </section>
  </div>

  <footer>Updated %(updated)s · <a href="%(repo)s">Source on GitHub</a> · This page and its map are generated by
  <code>tools/site/build_engine_map.py</code>.</footer>
</div>
</body>
</html>
""" % dict(dark=tokens_css(DARK), util=util, w=W, h=H,
           aria=escape(ARIA, {'"': "&quot;"}), svg=render_svg("html"), caption=escape(CAPTION),
           updated=UPDATED, repo=REPO_URL)
with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8", newline="\n") as fh:
    fh.write(page)


# =============================================================== rough raster, for checking the layout
def preview(path, scale=1.5, pal=LIGHT):
    img = Image.new("RGB", (int(W * scale), int(H * scale)), pal["bg"])
    d = ImageDraw.Draw(img)

    def fnt(size, weight, italic):
        key = (round(size * scale, 1), weight >= 600, bool(italic))
        if key not in _draw:
            _draw[key] = ImageFont.truetype(os.path.join(FONT_DIR, _font_file(weight, italic)), size * scale)
        return _draw[key]

    def dashed(pts, colour, width, dash):
        on, off = [float(v) * scale for v in dash.split()]
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            length = math.hypot(x2 - x1, y2 - y1)
            if length == 0:
                continue
            ux, uy = (x2 - x1) / length, (y2 - y1) / length
            pos = 0.0
            while pos < length:
                e = min(pos + on, length)
                d.line([(x1 + ux * pos, y1 + uy * pos), (x1 + ux * e, y1 + uy * e)], fill=colour, width=width)
                pos += on + off

    for it in c.items:
        k = it["k"]
        if k == "rect":
            x0, y0, x1, y1 = it["x"] * scale, it["y"] * scale, (it["x"] + it["w"]) * scale, (it["y"] + it["h"]) * scale
            fill = pal[it["fill"]] if it["fill"] else None
            if it["stroke"] and it["dash"]:
                if fill:
                    d.rounded_rectangle([x0, y0, x1, y1], radius=it["rx"] * scale, fill=fill)
                dashed([(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)], pal[it["stroke"]],
                       max(1, round(it["sw"] * scale)), it["dash"])
            else:
                d.rounded_rectangle([x0, y0, x1, y1], radius=it["rx"] * scale, fill=fill,
                                    outline=pal[it["stroke"]] if it["stroke"] else None,
                                    width=max(1, round(it["sw"] * scale)) if it["stroke"] else 0)
        elif k == "line":
            pts = [(x * scale, y * scale) for x, y in it["pts"]]
            if it["dash"]:
                dashed(pts, pal[it["stroke"]], max(1, round(it["sw"] * scale)), it["dash"])
            else:
                d.line(pts, fill=pal[it["stroke"]], width=max(1, round(it["sw"] * scale)), joint="curve")
        elif k == "poly":
            d.polygon([(x * scale, y * scale) for x, y in it["pts"]], fill=pal[it["fill"]])
        elif k == "circle":
            d.ellipse([(it["cx"] - it["r"]) * scale, (it["cy"] - it["r"]) * scale, (it["cx"] + it["r"]) * scale,
                       (it["cy"] + it["r"]) * scale], fill=pal[it["fill"]] if it["fill"] else None,
                      outline=pal[it["stroke"]] if it["stroke"] else None,
                      width=max(1, round(it["sw"] * scale)) if it["stroke"] else 0)
        else:
            x = it["left"] * scale
            for r in it["runs"]:
                size = r["s"] or it["size"]
                weight = r["w"] or it["weight"]
                italic = it["italic"] if r["i"] is None else r["i"]
                ls = it["ls"] if r["ls"] is None else r["ls"]
                colour = pal[r["f"] or it["fill"]]
                x += (r["dx"] or 0) * scale
                f = fnt(size, weight, italic)
                if ls:
                    for ch in r["t"]:
                        d.text((x, it["y"] * scale), ch, font=f, fill=colour, anchor="ls")
                        x += f.getlength(ch) + ls * scale
                else:
                    d.text((x, it["y"] * scale), r["t"], font=f, fill=colour, anchor="ls")
                    x += f.getlength(r["t"])
    return img


# Drawn at four times the size and reduced to two, so edges come out smooth.
if "--png" in sys.argv or "--check" in sys.argv:
    big = preview(None, scale=4.0)
if "--png" in sys.argv:
    big.resize((W * 2, H * 2), Image.LANCZOS).save(os.path.join(OUT, "engine-map.png"), optimize=True)
if "--check" in sys.argv:
    big.resize((int(W * 1.5), int(H * 1.5)), Image.LANCZOS).save(os.path.join(OUT, "check-light.png"))
    preview(None, scale=3.0, pal=DARK).resize((int(W * 1.5), int(H * 1.5)), Image.LANCZOS).save(
        os.path.join(OUT, "check-dark.png"))
print("canvas %d x %d, menu height %d, card height %d" % (W, H, MENU_H, card_h))
for p in panels:
    print("  panel %-5s needs %.0f" % (p["kind"], p["need"]))
for w in c.warn:
    print("WARN", w)
print("%d warnings" % len(c.warn))

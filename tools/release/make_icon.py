"""Draw CoolSynth's icon (resources/icon/coolsynth-icon.svg, D209) into installer/CoolSynth.ico and a 512 px PNG.

The shapes are the SVG's, drawn with Pillow at 8 times the size and scaled down, so the small sizes stay clean.

usage: python make_icon.py
"""
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
S = 8 * 4  # 120 units drawn at 3840 px, then scaled
DARK, TRACK, TEAL, WHITE = "#131619", "#30353b", "#23c4b5", "#f2f5f8"


def u(v):
    return v * S


def arc(draw, cx, cy, r, start_deg, end_deg, colour, width):
    # Degrees clockwise from 12 o'clock, as the SVG's knob arcs; Pillow counts from 3 o'clock and draws the stroke
    # inside its box, so the box reaches half a stroke past the radius to centre the stroke on it, as SVG does.
    half = width / 2
    box = [u(cx - r - half), u(cy - r - half), u(cx + r + half), u(cy + r + half)]
    draw.arc(box, start_deg - 90, end_deg - 90, fill=colour, width=int(u(width)))
    for deg in (start_deg, end_deg):  # round caps
        x, y = cx + r * math.sin(math.radians(deg)), cy - r * math.cos(math.radians(deg))
        draw.ellipse([u(x - width / 2), u(y - width / 2), u(x + width / 2), u(y + width / 2)], fill=colour)


def line(draw, x1, y1, x2, y2, colour, width):
    draw.line([u(x1), u(y1), u(x2), u(y2)], fill=colour, width=int(u(width)))
    for x, y in ((x1, y1), (x2, y2)):
        draw.ellipse([u(x - width / 2), u(y - width / 2), u(x + width / 2), u(y + width / 2)], fill=colour)


def knob(draw, cx, cy, value_deg):
    arc(draw, cx, cy, 15, -135, 135, TRACK, 5)
    arc(draw, cx, cy, 15, -135, value_deg, TEAL, 5)
    a = math.radians(value_deg)
    line(draw, cx, cy, cx + 8.25 * math.sin(a), cy - 8.25 * math.cos(a), WHITE, 3.5)


image = Image.new("RGBA", (u(120), u(120)), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle([0, 0, u(120) - 1, u(120) - 1], radius=u(26), fill=DARK)
knob(draw, 42, 38, 60)
knob(draw, 78, 38, -20)
draw.rounded_rectangle([u(26), u(73), u(94), u(104)], radius=u(4), fill=WHITE)
for x in (43, 60, 77):
    draw.line([u(x), u(73), u(x), u(104)], fill=DARK, width=int(u(2)))
for x in (37, 54, 71):
    draw.rounded_rectangle([u(x), u(73), u(x + 11), u(91)], radius=u(2), fill=DARK)

os.makedirs(os.path.join(ROOT, "installer"), exist_ok=True)
big = image.resize((512, 512), Image.LANCZOS)
big.save(os.path.join(ROOT, "resources", "icon", "coolsynth-icon-512.png"))
big.save(os.path.join(ROOT, "installer", "CoolSynth.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("icon written")

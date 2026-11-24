"""Writes a 303 mockup's .html from its .src.html with the app's fonts inlined, so the page opens on its own: the
source names them in CoolSynth303's resources/fonts, beside this repository.

    python -I docs/design/mockups/303-redesign/inline_fonts.py [page, default coolsynth-303-redesign] [CoolSynth303 path]
"""
import base64
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOCKUPS = os.path.dirname(HERE)
name = sys.argv[1] if len(sys.argv) > 1 else "coolsynth-303-redesign"
repo303 = sys.argv[2] if len(sys.argv) > 2 else os.path.join(MOCKUPS, "..", "..", "..", "..", "CoolSynth303")
with open(os.path.join(MOCKUPS, name + ".src.html"), encoding="utf-8") as f:
    page = f.read()


def inline(match):
    font_file = match.group(1)
    with open(os.path.join(repo303, "resources", "fonts", font_file), "rb") as font:
        data = base64.b64encode(font.read()).decode("ascii")
    return "url(data:font/ttf;base64," + data + ")"


page, count = re.subn(r"url\(\.\./\.\./\.\./\.\./CoolSynth303/resources/fonts/([\w-]+\.ttf)\)", inline, page)
out = os.path.join(MOCKUPS, name + ".html")
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write(page)
print("written", out, "with", count, "fonts")

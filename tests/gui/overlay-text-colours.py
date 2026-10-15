"""The console's and the report view's colours stand out on their grounds.

docs/HandsOnQueue.md entry 139 (the reporter, 2026-10-15 12:00 and 13:26):
"revise the python console and report view colors on its text to be more
standout in overlay background on both theme ... including classic", "audit
for all colors. for overlay, the same for all modes. If it makes things
simpler, use the same colors on non-overlay panel as well".

The themes' colours are their preference packs' (Dark, Light, Classic), the
same docked and overlaid. Each is measured against the two grounds it is
read on: the panel's (the theme's text-view ground) and, in a see-through
overlay, the line's ground over the 3D view -- the panel's at the overlays'
opacity over a mid-grey view, about #2D2D2D under Dark and #E2E2E2 under
Light and Classic. Claim: every one stands 4.5 to 1 or better (WCAG's ratio
for text) against both.

Reads the packs from the installed resources; needs no window.
"""
import os
import re
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))

NAMES = ["colorText", "colorLogging", "colorWarning", "colorError", "Text", "Keyword", "Comment",
         "Block comment", "Number", "String", "Character", "Class name", "Define name",
         "Operator", "Python output", "Python error"]
GROUNDS = {
    "Dark": [(25, 25, 25), (45, 45, 45)],
    "Light": [(250, 250, 250), (226, 226, 226)],
    "Classic": [(255, 255, 255), (226, 226, 226)],
}


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def luminance(c):
    def channel(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = c
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def ratio(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def run():
    try:
        packs = os.path.join(FreeCAD.getResourceDir(), "Gui", "PreferencePacks")
        for theme, grounds in GROUNDS.items():
            path = os.path.join(packs, theme, theme + ".cfg")
            if not check("the %s pack is there" % theme, os.path.exists(path), path):
                continue
            with open(path, encoding="utf-8") as f:
                text = f.read()
            weak = []
            for name in NAMES:
                m = re.search(r'<FCUInt Name="%s" Value="(\d+)"' % re.escape(name), text)
                if not m:
                    continue
                v = int(m.group(1))
                colour = ((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255)
                worst = min(ratio(colour, g) for g in grounds)
                if worst < 4.5:
                    weak.append("%s #%02X%02X%02X %.2f" % ((name,) + colour + (worst,)))
            check("%s: every colour 4.5 to 1 against the panel's ground and the overlay's"
                  % theme, not weak, weak)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)

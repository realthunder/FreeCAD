"""OK in the preferences, on a profile that never changed anything, changes nothing.

A setting has a default in two places: in its definition (the generated
parameter classes, src/**/*Params.py), which is what the program uses while
the key is not stored, and in the preference page that shows it, which is
what the page displays for a key that is not stored -- and what it WRITES on
OK, since OK saves every page. Where the two differ, a profile behaves one
way until the first time the preferences are confirmed and another way
after, without anybody having changed the setting. Asked by hand
(docs/HandsOnQueue.md entry 23, the audit's side findings).

Claims, on a profile with nothing stored:

  - the preferences dialog opens and OK closes it without a question;
  - every key OK stored that has a definition holds the definition's
    default (a failure names the setting, both values and nothing else);
  - and is stored as the type the definition reads: a colour shown in a
    spin box is written as an integer, under a key nothing reads.

The settings a page cannot show as they are defined are listed in SAME, with
the reason the value written means the same thing.

Also noted, not judged: how many keys OK stored, how long OK took, and how
long the event loop was held afterwards (docs/HandsOnQueue.md entry 26).

Scored against the tree before the change: see the commit message.
"""
import importlib.util
import os
import sys
import time
import traceback
import types

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
SRC = os.environ.get("GT_SRC") or os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src"))
PREFIX = "User parameter:BaseApp/Preferences/"

# (group, key): why the value a page writes is the same setting as the default.
SAME = {
    ("Mod/Mesh", "MeshColor"): "0 means the built-in colour, which is the one the page shows",
    ("Mod/Mesh", "LineColor"): "0 means the built-in colour, which is the one the page shows",
    ("Document", "prefLicenseUrl"): "empty means the address of the chosen license, which the page spells out",
    ("Expression", "EditDialogBGAlpha"): "the default is a macro that differs by platform",
    ("OutputWindow", "colorText"): "0 means the window's text colour, which is the one the page shows",
    ("General", "Language"): "empty means the system's language, which the page spells out",
    ("Macro", "MacroPath"): "empty means the user's macro directory, which the page spells out",
    ("Editor", "Text"): "not set means the window's text colour, which is the one the page shows",
    ("Dialog", "DontUseNativeDialog"): "the default is a macro the build decides",
    ("General", "AutoloadModule"): "empty means the configured start workbench, which the page spells out",
    ("Mod/Part/STEP", "Scheme"): "empty means the kernel's own scheme, which the page spells out",
}
KIND = {"Bool": "Boolean", "Int": "Integer", "Unsigned": "Unsigned Long", "Float": "Float", "ASCII": "String"}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def held(seconds):
    """Spin the event loop; the longest single turn, and the time in turns over 50 ms."""
    end = time.perf_counter() + seconds
    longest = busy = 0.0
    while time.perf_counter() < end:
        t = time.perf_counter()
        QtCore.QCoreApplication.processEvents()
        d = time.perf_counter() - t
        longest = max(longest, d)
        if d > 0.05:
            busy += d
            end = max(end, time.perf_counter() + 1.5)
        time.sleep(0.005)
    return longest, busy


def definitions():
    """(group under Preferences, key) -> (class name, default, kind), from the definition files."""
    sys.path.insert(0, os.path.join(SRC, "Tools"))
    cog = types.ModuleType("cog")
    for name in ("out", "outl", "msg", "error"):
        setattr(cog, name, lambda *a, **k: None)
    cog.inFile = ""
    had = sys.modules.get("cog")
    sys.modules["cog"] = cog
    found = {}
    try:
        for root, dirs, names in os.walk(SRC):
            if "3rdParty" in root:
                dirs[:] = []
                continue
            for n in names:
                if not n.endswith("Params.py"):
                    continue
                path = os.path.join(root, n)
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                if "params_utils" not in text or "ClassName" not in text:
                    continue
                spec = importlib.util.spec_from_file_location("prefdefs_%d" % len(found), path)
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                group = getattr(mod, "ParamPath", "")
                if not group.startswith(PREFIX):
                    continue
                for prm in getattr(mod, "Params", []):
                    # A setting may be kept in a sub-group, under a key that
                    # is not its name (CheckGeometry/AutoRun).
                    where = group[len(PREFIX):]
                    sub = getattr(prm, "subpath", "")
                    if sub.startswith(PREFIX):
                        # ... or in another group altogether (the Sketcher's
                        # colours are in the 3D view's)
                        where = sub[len(PREFIX):]
                    elif sub and not sub.startswith("User parameter:"):
                        where += "/" + sub
                    key = getattr(prm, "param_name", "") or prm.name
                    found[(where, key)] = (mod.ClassName, prm._default, KIND.get(prm.Type))
    finally:
        if had is None:
            del sys.modules["cog"]
        else:
            sys.modules["cog"] = had
    return found


def stored(group="", into=None):
    """Every key stored under Preferences: (group, key, kind) -> value."""
    into = {} if into is None else into
    grp = FreeCAD.ParamGet(PREFIX + group if group else PREFIX[:-1])
    for kind, name, value in grp.GetContents() or []:
        into[(group, name, kind)] = value
    for sub in grp.GetGroups():
        stored(group + "/" + sub if group else sub, into)
    return into


def same(value, default):
    if isinstance(default, bool) or isinstance(value, bool):
        return bool(value) == bool(default)
    if isinstance(default, int) and isinstance(value, int):
        # Whole numbers are the same or they are not. They used to be given
        # the tolerance of the fractions below, which for a packed colour
        # -- a number of ten digits -- let a neighbouring colour pass.
        return value == default
    if isinstance(default, (int, float)) and isinstance(value, (int, float)):
        return abs(float(value) - float(default)) <= 1e-9 * max(1.0, abs(float(default)))
    return str(value) == str(default)


def show(value):
    return "0x%08x" % value if isinstance(value, int) and not isinstance(value, bool) and value > 0xffff else repr(value)


def run():
    try:
        # A module's pages are in the dialog only once the module is loaded.
        for module in ("PartGui", "PartDesignGui", "SketcherGui", "MeshGui", "SpreadsheetGui", "TechDrawGui",
                       "FemGui", "AssemblyGui"):
            try:
                __import__(module)
            except ImportError as e:
                note("not loaded: %s (%s)" % (module, e))
        held(1.0)
        defs = definitions()
        check("the definition files were read", len(defs) > 400, "%d settings" % len(defs))
        before = stored()
        state = {"asked": [], "took": None}

        def sweep():
            box = QtWidgets.QApplication.activeModalWidget()
            if isinstance(box, QtWidgets.QMessageBox):
                state["asked"].append(box.text())
                box.reject()

        sweeper = QtCore.QTimer()
        sweeper.timeout.connect(sweep)
        sweeper.start(400)

        def press():
            dialog = QtWidgets.QApplication.activeModalWidget()
            if dialog is None or isinstance(dialog, QtWidgets.QMessageBox):
                state["error"] = "no preferences dialog"
                return
            state["pages"] = len(dialog.findChildren(QtWidgets.QStackedWidget))
            ok = dialog.findChild(QtWidgets.QDialogButtonBox).button(QtWidgets.QDialogButtonBox.Ok)
            t = time.perf_counter()
            ok.click()
            state["took"] = time.perf_counter() - t

        QtCore.QTimer.singleShot(2500, press)
        FreeCADGui.showPreferences()
        longest, busy = held(3.0)
        sweeper.stop()
        check("the preferences opened and OK closed them", state["took"] is not None, state.get("error", ""))
        check("OK asked nothing", not state["asked"], state["asked"])
        after = stored()
        written = [k for k in after if k not in before]
        changed = [k for k in after if k in before and after[k] != before[k]]
        note("INFO OK stored %d keys that were not stored, changed %d that were (%s); it took %.2f s, and the "
             "event loop was held %.2f s afterwards (longest turn %.2f s)" % (
                 len(written), len(changed), ", ".join("%s/%s" % k[:2] for k in changed[:6]),
                 state["took"] or -1.0, busy, longest))
        differ = []
        mistyped = []
        defined = 0
        for key in sorted(written):
            setting = key[:2]
            if setting not in defs:
                continue
            defined += 1
            cls, default, kind = defs[setting]
            if kind and key[2] != kind:
                mistyped.append("%s.%s: read as %s, OK stored %s %s" % (cls, key[1], kind, key[2], show(after[key])))
            elif setting not in SAME and not same(after[key], default):
                differ.append("%s.%s: defined %s, OK wrote %s" % (cls, key[1], show(default), show(after[key])))
        check("OK wrote keys that have a definition", defined > 50, "%d of the %d stored" % (defined, len(written)))
        for line in differ:
            note("FAIL a page's default is its setting's default | " + line)
        check("every key OK stored holds its setting's default", not differ, "%d differ" % len(differ))
        for line in mistyped:
            note("FAIL a page stores a setting as the type that is read | " + line)
        check("every key OK stored has its setting's type", not mistyped, "%d do not" % len(mistyped))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)

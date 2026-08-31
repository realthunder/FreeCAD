#!/usr/bin/env python3
"""Slice B check: a whole SHEET recompute, native vs routed.

The corpus gate compares one expression at a time through
`FreeCAD.ExpressionSandbox`.  This rig instead builds real sheets, lets
`Sheet::updateProperty` -> `PropertySheet::eval` drive the evaluation,
and compares the CELL VALUES the two paths produce -- so it exercises
the seam the spreadsheet actually uses, python mode included.

`hex(255)` and `sorted([3, 1, 2])` are the discriminators: neither is
an expression function, and the second does not even PARSE outside
python mode, so a run that reports them correct proves the option mask
reached both the parse and the walk inside the image.

Run (note the `-c exec(...)` form -- a script PATH runs nothing and
says nothing):

    SP=<scratchpad>; mkdir -p $SP/fchome
    QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=$SP/fchome \\
    FCX_IMAGE=$PWD/build/wasi-image/fcx_image.wasm \\
    FCX_STDLIB=$HOME/works/sw/cpython-wasi/Python-3.12.13/Lib \\
      .conda/limited.sh .conda/run.sh \\
      build/conda-relwithdebinfo-801/bin/FreeCADCmd -c \\
      "exec(open('scripts/expr-switchover/sheet_routing.py').read())"

Exits non-zero on any divergence, or if nothing crossed (a silent
native run would otherwise report a clean pass).
"""

import os
import sys

import FreeCAD as App

class PrefGuard:
    """Set preferences for the run and put them back afterwards.

    These rigs change GLOBAL preferences (enforcement, routing, the
    image path).  That is fine while they run and poison the box
    afterwards -- a left-behind `Enforce=0` silently disarms the
    permission tests, which then pass for the wrong reason.

    WARNING: FREECAD_USER_HOME is NOT a safety net: Application::getCustomPaths
    CLEARS it when the directory does not exist, without a word, and
    the run writes to the user's real config.  `mkdir -p` it, and
    /// restore anyway.
    """

    def __init__(self):
        self._undo = []

    def set_bool(self, group, key, value):
        params = App.ParamGet(group)
        had = key in params.GetBools()
        prior = params.GetBool(key, False) if had else None
        self._undo.append((group, key, "bool", had, prior))
        params.SetBool(key, value)

    def set_string(self, group, key, value):
        params = App.ParamGet(group)
        had = key in params.GetStrings()
        prior = params.GetString(key, "") if had else None
        self._undo.append((group, key, "string", had, prior))
        params.SetString(key, value)

    def restore(self):
        for group, key, kind, had, prior in reversed(self._undo):
            params = App.ParamGet(group)
            if kind == "bool":
                params.SetBool(key, prior) if had else params.RemBool(key)
            else:
                params.SetString(key, prior) if had else params.RemString(key)
        self._undo = []


PLAIN = [
    ("A1", "=1+2"),
    ("A2", "=A1 * 3"),
    ("A3", "=10mm + 1cm"),
    ("A4", "=<<ab>> + <<cd>>"),
    ("A5", "=min(A1; 100)"),
    ("A6", "=sum(A1:A2)"),
    ("A7", "=<<hello>>"),
]

# python mode only: `hex` and `sorted` resolve from CPython builtins,
# which are visible only on a python-mode call frame, and the comma
# list is python-mode SYNTAX
PYMODE = [
    ("B1", "=hex(255)"),
    ("B2", "=sorted([3, 1, 2])"),
    ("B3", "=sum([1, 2, 3])"),
    ("B4", "=1+2"),
]


def build(name, pymode, cases):
    doc = App.newDocument(name)
    sheet = doc.addObject("Spreadsheet::Sheet", "S")
    sheet.PythonMode = pymode
    for addr, content in cases:
        sheet.set(addr, content)
    doc.recompute()
    values = {}
    for addr, _ in cases:
        try:
            values[addr] = repr(sheet.get(addr))
        except Exception as exc:
            values[addr] = "ERR %s: %s" % (type(exc).__name__, str(exc)[:80])
    return doc, values


def main():
    sandbox = getattr(App, "ExpressionSandbox", None)
    if sandbox is None:
        print("FATAL: this build has no FreeCAD.ExpressionSandbox module")
        return 2
    prefs = PrefGuard()
    sandboxGroup = "User parameter:BaseApp/Preferences/Expression/Sandbox"
    if os.environ.get("FCX_IMAGE"):
        prefs.set_string(sandboxGroup, "ImagePath", os.environ["FCX_IMAGE"])
    if os.environ.get("FCX_STDLIB"):
        prefs.set_string(sandboxGroup, "StdlibPath", os.environ["FCX_STDLIB"])
    prefs.set_bool(sandboxGroup, "Evaluate", False)
    if not sandbox.available():
        prefs.restore()
        print("FATAL: no sandbox image (set FCX_IMAGE / FCX_STDLIB, or the "
              "ImagePath/StdlibPath preferences)")
        return 2

    try:
        return run(sandbox)
    finally:
        prefs.restore()


def run(sandbox):
    failures = 0
    crossed_total = 0
    report = []
    for pymode, cases, tag in ((False, PLAIN, "plain"), (True, PYMODE, "pymode")):
        sandbox.setRouting(False)
        native_doc, native = build("N_" + tag, pymode, cases)
        sandbox.setRouting(True)
        if not sandbox.routed():
            print("FATAL: routing did not switch on")
            return 2
        before = sandbox.evalCount()
        routed_doc, routed = build("R_" + tag, pymode, cases)
        crossed = sandbox.evalCount() - before
        crossed_total += crossed
        sandbox.setRouting(False)

        report.append("=== %s: %d evaluations crossed ===" % (tag, crossed))
        for addr, _ in cases:
            ok = native[addr] == routed[addr]
            if not ok:
                failures += 1
            report.append("  %s %-4s native=%-26s routed=%s"
                          % ("ok " if ok else "DIFF", addr,
                             native[addr], routed[addr]))
        App.closeDocument(native_doc.Name)
        App.closeDocument(routed_doc.Name)

    report.append("")
    report.append("divergences: %d, crossings: %d" % (failures, crossed_total))
    text = "\n".join(report)
    # FreeCADCmd's restore chatter shares stdout and it does not always
    # flush at exit: write the report to a file as well
    with open(os.environ.get("FCX_SHEET_REPORT", "sheet_routing.txt"), "w") as f:
        f.write(text + "\n")
    print(text, flush=True)
    if crossed_total == 0:
        print("FATAL: nothing crossed -- this was a native run", flush=True)
        return 2
    return 1 if failures else 0


if __name__ == "__main__":
    # Flush first: SystemExit out of a FreeCADCmd `-c` skips the
    # shutdown that flushes a block-buffered stdout, and a piped run
    # then prints nothing at all while still exiting 0.
    _code = main()
    sys.stdout.flush()
    sys.exit(_code)

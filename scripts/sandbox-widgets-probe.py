#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Probe B (docs/Sandbox.md 7.3): ipywidgets in the guest, the comm shim,
a form rendered in Qt from a guest script, and what each part costs.

Boots the sandbox with the bundled ipywidgets/traitlets/fcx_widgets
wheels, times the guest's `import ipywidgets`, builds the six-widget
form of the SandboxWidgets gate, shows it in a task panel, then drives
it from both sides and times each event: a Qt slider move (host ->
guest observer -> guest label -> host label, one synchronous round
trip) and a guest-side value write reaching the Qt widget.  Runs
under the GUI, on Xvfb like the gate:

    cd build/conda-relwithdebinfo-801
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb FREECAD_USER_HOME=/tmp/fchome \
      FCX_PYODIDE=$HOME/.local/share/FreeCAD/Pyodide/314.0.6 \
      xvfb-run -a -s "-screen 0 1280x800x24" timeout -k 5 600 \
      ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD ~/works/sw/fcad/scripts/sandbox-widgets-probe.py

One line per fact on stderr and in $SANDBOX_WIDGETS_PROBE_RESULT (default
sandbox-widgets-probe.txt in the user data directory); the last line is
`RESULT OK` or `RESULT FAILED`.
"""

import os
import sys
import time
import traceback

import FreeCAD
import FreeCADGui

GUEST_IMPORT = r'''
import sys, time
t0 = time.perf_counter()
import traitlets
t1 = time.perf_counter()
import ipywidgets
t2 = time.perf_counter()
import IPython, comm
sys.stderr.write("guest: import traitlets %.3f s, ipywidgets %.3f s (%s, comm %s, IPython %s),"
                 " %d modules\n" % (t1 - t0, t2 - t1, ipywidgets.__version__, comm.__version__,
                                    IPython.__version__, len(sys.modules)))
'''

GUEST_FORM = r'''
import time
import ipywidgets as W
import FreeCADGui

t0 = time.perf_counter()
slider = W.IntSlider(value=3, min=0, max=1000, description="Count")
text = W.Text(value="hello", description="Name")
check = W.Checkbox(value=False, description="Enabled")
drop = W.Dropdown(options=["Wall", "Window", "Door"], index=1, description="Kind")
button = W.Button(description="Apply")
label = W.Label(value="count 3")
box = W.VBox([slider, text, check, drop, button, label])
t1 = time.perf_counter()
events = []


def on_slider(change):
    label.value = "count %d" % change["new"]
    events.append(change["new"])


slider.observe(on_slider, names="value")
button.on_click(lambda b: events.append("click"))
build_seconds = t1 - t0
t2 = time.perf_counter()
FreeCADGui.showWidget(box, "Sandbox widgets probe")
show_seconds = time.perf_counter() - t2
'''

lines = []


def out(*a):
    line = " ".join(str(x) for x in a)
    lines.append(line)
    sys.stderr.write(line + "\n")
    sys.stderr.flush()


def timed(label, fn, n=1):
    t0 = time.perf_counter()
    for _ in range(n):
        fn()
    dt = (time.perf_counter() - t0) / n
    out("%-44s %8.3f ms" % (label, dt * 1000.0))
    return dt


def main():
    S = FreeCAD.ExpressionSandbox
    info = S.imageInfo()
    out("host", info["host"], "runtime", info["runtime"])
    S.setRouting(True)
    t0 = time.perf_counter()
    ok = S.available()
    out("boot %.2f s available %s" % (time.perf_counter() - t0, ok))
    if not ok:
        return False
    from freecad import widgets

    M = widgets.manager()

    # the imports, timed in the guest (its own clock)
    S.resetStats()
    t0 = time.perf_counter()
    S.exec(GUEST_IMPORT, "fcx_wprobe_import")
    out("guest import round trip %.3f s" % (time.perf_counter() - t0))

    # the form: comm traffic per widget
    S.resetStats()
    t0 = time.perf_counter()
    S.exec(GUEST_FORM, "fcx_wprobe")
    dt = time.perf_counter() - t0
    ops = S.stats()["ops"]
    opts = S.OptionCallFrame | S.OptionPythonMode
    doc = FreeCAD.newDocument("WidgetsProbe")
    owner = doc.addObject("App::FeaturePython", "Owner")

    def guest(expr):
        return S.evaluate(owner, "import fcx_wprobe as p; " + expr, opts)

    build = guest("p.build_seconds")
    show = guest("p.show_seconds")
    comm_ops = ops.get("gui.comm", 0)
    out("form: 7 widgets built in the guest %.3f s (%d models, %d gui.comm ops, %.1f ms per op),"
        " shown %.3f s, whole exec %.3f s"
        % (build, len(M.models), comm_ops, build * 1000.0 / max(comm_ops, 1), show, dt))
    out("models:", sorted(set(m.name for m in M.models.values())))

    slider = M.find("IntSliderModel", "Count")[0]
    label = M.find("LabelModel")[0]
    button = M.find("ButtonModel", "Apply")[0]
    sv, lv, bv = slider.views[0], label.views[0], button.views[0]
    if not FreeCADGui.Control.activeDialog():
        out("FAILED: no task panel")
        return False

    # host -> guest: one Qt slider step is one proxy call into the guest
    # (the observer runs there) with the label's update nested in it
    counter = [10]

    def step():
        counter[0] += 1
        sv.slider.setValue(counter[0])

    step()
    if lv.widget.text() != "count 11":
        out("FAILED: label after the first step: %r" % lv.widget.text())
        return False
    S.resetStats()
    n = 100
    per = timed("slider step -> observer -> label (round trip)", step, n)
    st = S.stats()
    out("  per step: %.1f gui.comm ops, %.1f proxy calls"
        % (st["ops"].get("gui.comm", 0) / n, st.get("proxy_calls", 0) / n))
    if lv.widget.text() != "count %d" % counter[0]:
        out("FAILED: label after %d steps: %r" % (n, lv.widget.text()))
        return False
    timed("button click (custom event, no reply)", bv.widget.click, n)
    clicks = guest("p.events.count('click')")
    if clicks != n:
        out("FAILED: %d clicks reached the guest, expected %d" % (clicks, n))
        return False

    # guest -> host: a value written in the guest reaches the Qt widget
    # (measured with the exec's own cost, and that cost alone)
    counter2 = [0]

    def write():
        counter2[0] += 1
        S.exec("import fcx_wprobe as p; p.slider.value = %d" % counter2[0], "fcx_wprobe_w")

    S.resetStats()
    per_w = timed("guest write -> Qt slider (exec + update)", write, n)
    st = S.stats()
    out("  per write: %.1f gui.comm ops" % (st["ops"].get("gui.comm", 0) / n))
    if sv.slider.value() != counter2[0]:
        out("FAILED: slider after guest writes: %d, expected %d" % (sv.slider.value(), counter2[0]))
        return False
    per_exec = timed("exec of a no-op module (the floor)",
                     lambda: S.exec("pass", "fcx_wprobe_n"), n)
    out("  guest write net of the exec floor: %.3f ms" % ((per_w - per_exec) * 1000.0))
    out("  host event per step: %.3f ms" % (per * 1000.0))

    # take it down
    FreeCADGui.Control.closeDialog()
    S.exec("import ipywidgets; ipywidgets.Widget.close_all()", "fcx_wprobe_c")
    out("closed: %d models left" % len(M.models))
    FreeCAD.closeDocument(doc.Name)
    return len(M.models) == 0


def finish():
    verdict = "FAILED"
    try:
        if main():
            verdict = "OK"
    except Exception:
        out(traceback.format_exc())
    # nothing may be left to ask about on the way out (an open document
    # would prompt, and under Xvfb a prompt is a hang)
    try:
        if FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    except Exception:
        out(traceback.format_exc())
    path = os.environ.get("SANDBOX_WIDGETS_PROBE_RESULT") or os.path.join(
        FreeCAD.getUserAppDataDir(), "sandbox-widgets-probe.txt")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\nRESULT %s\n" % verdict)
    sys.stderr.write("RESULT %s (%s)\n" % (verdict, path))
    FreeCADGui.getMainWindow().close()


from PySide import QtCore  # noqa: E402

QtCore.QTimer.singleShot(0, finish)

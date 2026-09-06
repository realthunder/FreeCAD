# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Project Association                        *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""The G1 corpus gates under the GUI (docs/Sandbox.md 7.6, 7.9): the Draft
and BIM test documents built in a routed session with a GUI up -- so
the guest's FreeCAD.GuiUp is 1 (ruling 2026-09-06, sec 13) and every
`if App.GuiUp:` branch of the App side runs in the guest, view providers
included -- and compared object by object with a native build: every
scripted Proxy a guest stand-in, nothing invalid, every shape equal or
within a few ULPs.  The headless twins are the `*BuiltRouted` gtests
in tests/src/App/ExpressionImageHost.cpp, where GuiUp stays 0.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, and when the
image cannot boot."""

import hashlib
import importlib
import math
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

CORPORA = (
    # label, generator, the least scripted objects the guest must hold,
    # strict (a corpus whose GUI-side losses are still being listed --
    # docs/Sandbox.md 7.9, the GuiUp flip -- reports invalid objects
    # and shape differences instead of failing on them)
    ("Draft test document", "drafttests.draft_test_objects", 60, True),
    ("BIM test document", "bimtests.bim_test_objects", 50, False),
)


class PrefGuard:
    def __init__(self):
        self._undo = []

    def set_bool(self, key, value):
        params = FreeCAD.ParamGet(PARAMS)
        had = key in params.GetBools()
        prior = params.GetBool(key, False) if had else None
        self._undo.append((key, had, prior))
        params.SetBool(key, value)

    def restore(self):
        params = FreeCAD.ParamGet(PARAMS)
        for key, had, prior in reversed(self._undo):
            if had:
                params.SetBool(key, prior)
            else:
                params.RemBool(key)
        self._undo = []


def settle():
    from PySide import QtCore, QtWidgets

    for _ in range(100):
        QtWidgets.QApplication.processEvents()
        if QtWidgets.QApplication.overrideCursor() is None:
            return
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(20, loop.quit)
        loop.exec()


def sig(o):
    try:
        sh = o.Shape
    except Exception:
        return None
    if sh.isNull():
        return "null"
    return hashlib.sha1(sh.exportBrepToString().encode()).hexdigest()


def verts(o):
    try:
        return sorted((v.X, v.Y, v.Z) for v in o.Shape.Vertexes)
    except Exception:
        return None


def proxy_kind(holder):
    p = getattr(holder, "Proxy", None)
    if p is None:
        return "none"
    return "guest" if FreeCAD.ExpressionSandbox.proxyInfo(p) else "host"


class SandboxCorpusGuiTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        info = FreeCAD.ExpressionSandbox.imageInfo()
        if not info["host"]:
            self.skipTest("this build has no sandbox host")
        self.S = FreeCAD.ExpressionSandbox
        self.prefs = PrefGuard()
        self.prefs.set_bool("Evaluate", True)
        self.S.setRouting(True)
        if not self.S.available():
            self.prefs.restore()
            self.skipTest("the sandbox image did not boot")
        settle()

    def tearDown(self):
        self.prefs.restore()

    def build(self, generator, routed):
        """Build the corpus, recompute it twice more, snapshot it."""
        self.S.setRouting(routed)
        gen = importlib.import_module(generator)
        doc = FreeCAD.newDocument("FcxCorpusGui")
        try:
            gen._create_objects(doc)
            doc.recompute()
            settle()
            invalid = set()
            for _ in range(2):
                for o in doc.Objects:
                    o.touch()
                doc.recompute()
                settle()
                invalid.update(o.Name for o in doc.Objects if "Invalid" in o.State)
            snap = {}
            for o in doc.Objects:
                vp = proxy_kind(o.ViewObject) if o.ViewObject is not None else "none"
                snap[o.Name] = (proxy_kind(o), vp, sig(o), verts(o), o.Name in invalid)
            return snap
        finally:
            FreeCAD.closeDocument(doc.Name)
            settle()

    def check(self, label, generator, min_scripted, strict):
        try:
            importlib.import_module(generator)
        except Exception as e:
            self.skipTest("%s: %s" % (generator, e))
        native = self.build(generator, False)
        self.S.resetStats()
        routed = self.build(generator, True)
        stats = self.S.stats()
        report = dict(objects=len(native), guest=0, host=0, none=0, vp_guest=0, vp_host=0,
                      invalid=[], differ=[], missing=[], hosts=[], nones=[], native_invalid=[])
        for name, (pn, vpn, sn, vn, ninv) in native.items():
            if ninv:
                report["native_invalid"].append(name)
            if name not in routed:
                report["missing"].append(name)
                continue
            pr, vpr, sr, vr, inv = routed[name]
            if pn != "none":
                report[pr] += 1
                if pr == "host":
                    report["hosts"].append(name)
                elif pr == "none":
                    report["nones"].append(name)
            if vpn != "none":
                report["vp_" + vpr if vpr != "none" else "none"] += 1
            if inv and not ninv:
                report["invalid"].append(name)
            if sn is not None and sn != sr:
                d = ulps = None
                if vn and vr and len(vn) == len(vr):
                    d = max(abs(a - b) for pa, pb in zip(vn, vr) for a, b in zip(pa, pb))
                    scale = max(abs(c) for p in vn + vr for c in p)
                    ulps = d / math.ulp(scale) if scale else (0.0 if d == 0 else None)
                report["differ"].append((name, d, ulps))
        FreeCAD.Console.PrintMessage("%s, GUI, routed: %s; proxy calls %s, ops %s\n" % (
            label, {k: v for k, v in report.items() if v}, stats["proxy_calls"], stats["ops"]))
        self.assertEqual(report["hosts"], [], "scripted objects whose Proxy stayed native")
        self.assertGreaterEqual(report["guest"], min_scripted, report)
        if not strict:
            return report
        self.assertEqual(report["missing"], [], report)
        self.assertEqual(report["invalid"], [], "invalid only when routed")
        off = [d for d in report["differ"] if d[2] is None or d[2] > 4]
        self.assertEqual(off, [], "shapes differing beyond 4 ULPs")
        return report

    def test_draft_corpus_gui_routed(self):
        self.check(*CORPORA[0])

    def test_bim_corpus_gui_routed(self):
        self.check(*CORPORA[1])

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

"""The pyodide bootstrap (freecad.pyodide, docs/PyodideHost.md sec 12),
exercised without any network: the runtime is installed from a LOCAL
source -- the pinned distribution a dev tree stages under
<datadir>/Pyodide, or the core tarball named by FCX_PYODIDE_CORE_TARBALL
-- into a scratch user directory, and a package from a local mirror
(the same staged directory holds the numpy wheel the lock names).  Then
the sandbox boots from that install and a python-mode evaluation imports
the package.  Skips on a build without the pyodide host or a box without
the staged runtime."""

import json
import os
import shutil
import tempfile
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"


class PrefGuard:
    """Set string/bool preferences for the test, put them back after.
    These are GLOBAL preferences: a left-behind override would send the
    real sandbox to a deleted scratch directory."""

    def __init__(self):
        self._undo = []

    def set_string(self, key, value):
        params = FreeCAD.ParamGet(PARAMS)
        had = key in params.GetStrings()
        prior = params.GetString(key, "") if had else None
        self._undo.append((key, "string", had, prior))
        params.SetString(key, value)

    def set_bool(self, key, value):
        params = FreeCAD.ParamGet(PARAMS)
        had = key in params.GetBools()
        prior = params.GetBool(key, False) if had else None
        self._undo.append((key, "bool", had, prior))
        params.SetBool(key, value)

    def restore(self):
        params = FreeCAD.ParamGet(PARAMS)
        for key, kind, had, prior in reversed(self._undo):
            if kind == "string":
                if had:
                    params.SetString(key, prior)
                else:
                    params.RemString(key)
            else:
                if had:
                    params.SetBool(key, prior)
                else:
                    params.RemBool(key)
        self._undo = []


def _pyodide_host():
    info = FreeCAD.ExpressionSandbox.imageInfo()
    return info["host"] and info["runtime"] == "pyodide"


class SandboxPyodideTest(unittest.TestCase):
    def setUp(self):
        if not _pyodide_host():
            self.skipTest("this build has no pyodide host, or another runtime is selected")
        import freecad.pyodide as p

        self.p = p
        self.prefs = PrefGuard()
        self.scratch = tempfile.mkdtemp(prefix="fc-pyodide-")
        # the scratch user dir; packages default to <user dir>/packages
        self.prefs.set_string("PyodideUserDir", os.path.join(self.scratch, "Pyodide"))
        self.prefs.set_string("PyodidePackages", "")
        self.prefs.set_string("PyodideDir", "")
        self.prefs.set_string("PyodideWheel", "")
        FreeCAD.ExpressionSandbox.reset()
        self.env_dir = os.environ.pop("FCX_PYODIDE", None)
        self.env_pk = os.environ.pop("FCX_PYODIDE_PACKAGES", None)
        self.env_user = os.environ.pop("FCX_PYODIDE_USER", None)

    def tearDown(self):
        FreeCAD.ExpressionSandbox.reset()
        self.prefs.restore()
        for name, value in (
            ("FCX_PYODIDE", self.env_dir),
            ("FCX_PYODIDE_PACKAGES", self.env_pk),
            ("FCX_PYODIDE_USER", self.env_user),
        ):
            if value is not None:
                os.environ[name] = value
        shutil.rmtree(self.scratch, ignore_errors=True)
        FreeCAD.ExpressionSandbox.reset()

    def _staged_runtime(self):
        """The pinned distribution on this box, or None."""
        info = FreeCAD.ExpressionSandbox.imageInfo()
        stdlib = info["stdlib"]
        if not os.path.isfile(os.path.join(stdlib, "pyodide.asm.wasm")):
            return None
        if FreeCAD.ExpressionSandbox.pyodideVerify(stdlib):
            return None
        return stdlib

    def test_releases_and_layout(self):
        rel = self.p.releases()
        self.assertTrue(rel, "no pinned pyodide release")
        v = self.p.default_version()
        self.assertIn(v, rel)
        self.assertEqual(len(rel[v]["files"]), 6)
        for name, digest in rel[v]["files"].items():
            self.assertEqual(len(digest), 64, name)
        lay = self.p.layout()
        self.assertEqual(lay["user_dir"], os.path.join(self.scratch, "Pyodide"))
        self.assertEqual(lay["packages"], os.path.join(self.scratch, "Pyodide", "packages"))
        self.assertEqual(lay["installed"], [])
        self.assertEqual(lay["current"], "")
        with self.assertRaises(self.p.RuntimeUnsupported):
            self.p.install_runtime("0.0.1", source=self.scratch)

    def test_install_runtime_locally_then_a_package(self):
        source = os.environ.get("FCX_PYODIDE_CORE_TARBALL") or self._staged_runtime()
        if not source:
            self.skipTest("no pinned pyodide distribution on this box")
        mirror = self._staged_runtime()
        version = self.p.default_version()

        # a wrong-hash source is refused before anything moves into place
        bad = os.path.join(self.scratch, "bad-source")
        shutil.copytree(mirror or os.path.dirname(source), bad)
        with open(os.path.join(bad, "pyodide-lock.json"), "ab") as f:
            f.write(b"\n")
        with self.assertRaises(self.p.VerificationError):
            self.p.install_runtime(version, source=bad)
        self.assertEqual(self.p.layout()["installed"], [])

        target = self.p.install_runtime(version, source=source)
        self.assertEqual(target, os.path.join(self.scratch, "Pyodide", version))
        self.assertEqual(FreeCAD.ExpressionSandbox.pyodideVerify(target), "")
        installed, current = self.p.list_runtimes()
        self.assertEqual(installed, [version])
        self.assertEqual(current, version)
        # the host now resolves the installed runtime FIRST
        info = FreeCAD.ExpressionSandbox.imageInfo()
        self.assertEqual(info["stdlib"], target)
        self.assertTrue(FreeCAD.ExpressionSandbox.available(), "the installed runtime did not boot")

        if not mirror:
            self.skipTest("no local package mirror on this box")
        lock = json.load(open(os.path.join(target, "pyodide-lock.json"), "rb"))
        numpy_file = lock["packages"]["numpy"]["file_name"]
        if not os.path.isfile(os.path.join(mirror, numpy_file)):
            self.skipTest("no numpy wheel in the mirror")

        doc = FreeCAD.newDocument("SandboxPyodide")
        try:
            obj = doc.addObject("App::FeaturePython", "Owner")
            self.prefs.set_bool("Evaluate", True)
            opts = (
                FreeCAD.ExpressionSandbox.OptionCallFrame
                | FreeCAD.ExpressionSandbox.OptionPythonMode
            )
            src = "import numpy; float(numpy.sqrt(4.0))"
            # before: the offer, as a ModuleNotFoundError naming the install
            with self.assertRaises(Exception) as cm:
                FreeCAD.ExpressionSandbox.evaluate(obj, src, opts)
            self.assertIn("can install numpy", str(cm.exception))
            pending = [
                r
                for r in FreeCAD.ExpressionSecurity.pending()
                if r["permission"] == "pkg.install" and r["target"] == "numpy"
            ]
            self.assertEqual(len(pending), 1, pending)
            self.assertEqual(pending[0]["object"], "Owner")

            # the install, from the mirror
            order = self.p.install_package("numpy", source=mirror)
            self.assertEqual(order, ["numpy"])
            self.assertEqual(self.p.list_packages(), {"numpy": lock["packages"]["numpy"]["version"]})
            self.assertTrue(
                os.path.isfile(os.path.join(self.p.layout()["packages"], numpy_file))
            )
            m = self.p.manifest()
            self.assertEqual(m["load_order"], ["numpy"])
            self.assertEqual(m["abi"], FreeCAD.ExpressionSandbox.pyodideAbi(target))
            # the install satisfied the request
            self.assertFalse(
                [
                    r
                    for r in FreeCAD.ExpressionSecurity.pending()
                    if r["permission"] == "pkg.install" and r["target"] == "numpy"
                ]
            )
            # already there: nothing more to do
            self.assertEqual(self.p.install_package("numpy", source=mirror), [])

            # after: the value, from a guest that booted with the package
            value = FreeCAD.ExpressionSandbox.evaluate(obj, src, opts)
            self.assertAlmostEqual(float(value), 2.0)

            self.p.remove_package("numpy")
            self.assertEqual(self.p.list_packages(), {})
        finally:
            for r in FreeCAD.ExpressionSecurity.pending():
                if r["permission"] == "pkg.install":
                    FreeCAD.ExpressionSecurity.clearPending(
                        r["principal"], "pkg.install", r["target"]
                    )
            FreeCAD.closeDocument(doc.Name)

        self.p.remove_runtime(version)
        self.assertEqual(self.p.list_runtimes(), ([], ""))

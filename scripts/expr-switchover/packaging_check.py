#!/usr/bin/env python3
"""Packaging check: does a FreeCAD that was told NOTHING find its image?

Every other rig here points the host at an image with FCX_IMAGE /
FCX_STDLIB or the ImagePath / StdlibPath preferences.  A packaged
FreeCAD has neither: the sandbox has to resolve itself from the data
directory the install put it in (<datadir>/Fcx, mirrored into the build
tree by the fcx_image_data target).  This rig removes every hint and
checks what is left:

  1. the wasmtime host is compiled in at all,
  2. the image resolves to the data-dir bundle and loads,
  3. a routed evaluation crosses and answers correctly,
  4. the compiled-module cache lands in the USER cache directory and
     not beside the image -- an installed image sits in a read-only
     directory, and a cache that silently fails to write costs ~600 ms
     of JIT on every start.

Run (note the `-c exec(...)` form -- a script PATH runs nothing and
says nothing):

    SP=<scratchpad>; mkdir -p $SP/fchome
    QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=$SP/fchome \\
      .conda/limited.sh .conda/run.sh \\
      build/conda-relwithdebinfo-801/bin/FreeCADCmd -c \\
      "exec(open('scripts/expr-switchover/packaging_check.py').read())"

FREECAD_USER_HOME must EXIST -- Application::getCustomPaths clears it
without a word otherwise, and the run then writes the real user.cfg.

Exits non-zero on any failure.
"""

import os
import sys

import FreeCAD as App

SANDBOX_GROUP = "User parameter:BaseApp/Preferences/Expression/Sandbox"


class PrefGuard:
    """Set preferences for the run and put them back afterwards."""

    def __init__(self):
        self._undo = []

    def set_bool(self, group, key, value):
        params = App.ParamGet(group)
        had = key in params.GetBools()
        prior = params.GetBool(key, False) if had else None
        self._undo.append((group, key, had, prior))
        params.SetBool(key, value)

    def restore(self):
        for group, key, had, prior in reversed(self._undo):
            params = App.ParamGet(group)
            if had:
                params.SetBool(key, prior)
            else:
                params.RemBool(key)
        self._undo = []


def fail(report, message):
    report.append("FAIL " + message)


def main():
    # The host reads these lazily, on first use, so dropping them here
    # is enough to make the run answer the packaged question even if the
    # caller's shell had them set.
    for var in ("FCX_IMAGE", "FCX_STDLIB", "FCX_REPO"):
        os.environ.pop(var, None)

    sandbox = getattr(App, "ExpressionSandbox", None)
    if sandbox is None:
        print("FATAL: this build has no FreeCAD.ExpressionSandbox module")
        return 2

    params = App.ParamGet(SANDBOX_GROUP)
    for key in ("ImagePath", "StdlibPath"):
        if key in params.GetStrings():
            print("FATAL: the %s preference is set (%r); this rig answers "
                  "what happens with NOTHING set" % (key, params.GetString(key)))
            return 2

    report = []
    info = sandbox.imageInfo()
    if not info["host"]:
        print("FATAL: built without the wasmtime host "
              "(BUILD_EXPR_IMAGE_HOST was off, or wasmtime was not found)")
        return 2

    resources = App.getResourceDir()
    expected = os.path.join(resources, "Fcx", "fcx_image.wasm")
    if os.path.normpath(info["image"]) != os.path.normpath(expected):
        fail(report, "image resolved to %s, not the data-dir bundle %s"
             % (info["image"], expected))
    if not os.path.isfile(info["image"]):
        fail(report, "no image at %s -- configure the host build with "
             "FREECAD_EXPR_IMAGE_DIR" % info["image"])
    if not os.path.isdir(info["stdlib"]):
        fail(report, "no stdlib slice at %s" % info["stdlib"])

    cache_root = os.path.normpath(App.getUserCachePath())
    if os.path.normpath(info["cache"]).find(cache_root) != 0:
        fail(report, "cache %s is not under the user cache dir %s"
             % (info["cache"], cache_root))
    beside = info["image"] + ".cwasm"
    if os.path.exists(beside):
        fail(report, "a cache file sits beside the image (%s): the install "
             "directory is written to" % beside)

    if report:
        print("\n".join(report))
        return 1

    if not sandbox.available():
        print("FAIL image found but did not load: %s" % info["image"])
        return 1

    prefs = PrefGuard()
    prefs.set_bool(SANDBOX_GROUP, "Evaluate", True)
    try:
        if not sandbox.routed():
            print("FAIL routing did not switch on with an available image")
            return 1
        doc = App.newDocument("PackagingCheck")
        obj = doc.addObject("App::FeaturePython", "Probe")
        before = sandbox.evalCount()
        value = sandbox.evaluate(obj, "2 * (3 + 4)")
        crossed = sandbox.evalCount() - before
        App.closeDocument(doc.Name)
        if crossed != 1:
            print("FAIL %d evaluations crossed, expected 1" % crossed)
            return 1
        if value != 14:
            print("FAIL routed evaluation returned %r, expected 14" % (value,))
            return 1
    finally:
        prefs.restore()

    if not os.path.isfile(info["cache"]):
        print("FAIL no compiled-module cache written at %s" % info["cache"])
        return 1

    print("OK image %s" % info["image"])
    print("OK stdlib %s" % info["stdlib"])
    print("OK cache %s (%.1f MB)"
          % (info["cache"], os.path.getsize(info["cache"]) / 1e6))
    print("OK routed evaluation crossed and answered 14")
    return 0


# Flush BEFORE exiting: SystemExit out of a FreeCADCmd `-c` skips the
# interpreter shutdown that would flush a block-buffered stdout, so a run
# whose output is redirected or piped prints NOTHING and exits 0 -- a
# silent pass that says nothing about what was checked.
_code = main()
sys.stdout.flush()
sys.exit(_code)

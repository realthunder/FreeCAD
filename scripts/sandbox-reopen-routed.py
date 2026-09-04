#!/usr/bin/env python3
"""The G1d harness (docs/Sandbox.md 7.6): does a document recompute the
same with its scripted objects' Proxies in the sandbox guest?

For each document: reopen it NATIVELY, touch every object, recompute
and snapshot (the honest baseline -- a re-execute drifts on its own,
Draft's Fillet does); then set the routing preference, reopen it
through PropertyPythonObject's Restore route (every saved
<Python module=".." class=".."> allocated in the guest, the property
holding a stand-in), touch, recompute, snapshot; print one row per
object: how the Proxy came back (GUEST stand-in / host instance /
none), whether the shape hash equals the native one (with the max
vertex delta when it does not), and the recompute state.  With no
argument it builds the Draft test document (drafttests.
draft_test_objects) first.

Run under the test rig (docs/Testing.md sec 1):

    cd build/conda-relwithdebinfo-801
    QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome \\
      ../../.conda/limited.sh ../../.conda/run.sh \\
      ./bin/FreeCADCmd ../../scripts/sandbox-reopen-routed.py [doc.FCStd ...]

The routing preference is left as it was found.
"""
import hashlib
import math
import os
import sys
import tempfile
import traceback

import FreeCAD as App

S = App.ExpressionSandbox


def sig(o):
    try:
        sh = o.Shape
    except Exception:
        return None
    try:
        if sh.isNull():
            return "null"
        return hashlib.sha1(sh.exportBrepToString().encode()).hexdigest()[:12]
    except Exception as e:
        return "ERR:" + type(e).__name__


def verts(o):
    try:
        vs = o.Shape.Vertexes
        if len(vs) > 400:
            return None
        return sorted((v.X, v.Y, v.Z) for v in vs)
    except Exception:
        return None


def proxy_desc(o):
    try:
        p = o.Proxy
    except AttributeError:
        return "-"
    if p is None:
        return "none"
    info = S.proxyInfo(p)
    if info:
        return "GUEST %s.%s" % (info["module"], info["class"])
    return "host %s.%s" % (type(p).__module__, type(p).__name__)


def snapshot(doc):
    return {
        o.Name: dict(
            type=o.TypeId,
            proxy=proxy_desc(o),
            sig=sig(o),
            verts=verts(o),
            invalid="Invalid" in o.State,
            touched="Touched" in o.State,
        )
        for o in doc.Objects
    }


def vdelta(a, b):
    """The largest vertex coordinate difference, and how many ULPs of
    the object's largest coordinate that is: the guest's wasm libm
    rounds a rare cos/sin one ULP from glibc, so a last-bit
    difference reads as ~1 ULP; more is a real divergence."""
    if not a or not b or len(a) != len(b):
        return "n/a (%s vs %s vertices)" % (a and len(a), b and len(b))
    d = max(abs(x - y) for pa, pb in zip(a, b) for x, y in zip(pa, pb))
    scale = max(abs(c) for p in a + b for c in p)
    ulps = d / math.ulp(scale) if scale else 0.0
    return "max vertex delta %.3e = %.1f ULP of %.6g" % (d, ulps, scale)


def reopen(path, routed):
    S.setRouting(routed)
    doc = App.openDocument(path)
    restored = {o.Name: proxy_desc(o) for o in doc.Objects}
    for o in doc.Objects:
        o.touch()
    S.resetStats()
    doc.recompute()
    stats = S.stats()
    snap = snapshot(doc)
    App.closeDocument(doc.Name)
    return restored, snap, stats


def report(title, native, routed, restored_proxy, stats):
    print("\n==== %s ====" % title)
    print("recompute traffic (routed):", stats)
    print(
        "%-28s %-30s %-40s %-40s %-6s %s"
        % ("object", "type", "proxy native", "proxy routed", "shape", "state")
    )
    counts = dict(guest=0, host=0, none=0, equal=0, differ=0, invalid=0, noshape=0)
    for name in native:
        n = native[name]
        r = routed.get(name)
        if r is None:
            print("%-28s %-30s %-40s %s" % (name, n["type"], n["proxy"], "MISSING after reopen"))
            continue
        rp = restored_proxy.get(name, "?")
        if rp.startswith("GUEST"):
            counts["guest"] += 1
        elif rp.startswith("host"):
            counts["host"] += 1
        elif n["proxy"] not in ("-", "none"):
            counts["none"] += 1
        if n["sig"] is None:
            shape = "-"
            counts["noshape"] += 1
        elif n["sig"] == r["sig"]:
            shape = "EQUAL"
            counts["equal"] += 1
        else:
            shape = "DIFFER"
            counts["differ"] += 1
            print("   ", name, vdelta(n["verts"], r["verts"]))
        state = "INVALID" if r["invalid"] else ("touched" if r["touched"] else "ok")
        if r["invalid"]:
            counts["invalid"] += 1
        print(
            "%-28s %-30s %-40s %-40s %-6s %s"
            % (name, n["type"][:30], n["proxy"][:40], rp[:40], shape, state)
        )
    print("COUNTS", counts)
    return counts


def run_file(path):
    _, native, _ = reopen(path, False)
    restored, routed, stats = reopen(path, True)
    return report(os.path.basename(path), native, routed, restored, stats)


def draft_test_document(out_dir):
    S.setRouting(False)
    doc = App.newDocument("SandboxDraft")
    from drafttests import draft_test_objects as dto

    # no font, no hatch pattern: those two objects need files the rig
    # may not have, and the generator warns and skips them
    dto._create_objects(doc)
    doc.recompute()
    path = os.path.join(out_dir, "sandbox_draft_objects.FCStd")
    doc.saveAs(path)
    App.closeDocument(doc.Name)
    return path


def main(argv):
    was = App.ParamGet("User parameter:BaseApp/Preferences/Expression/Sandbox").GetBool(
        "Evaluate", False
    )
    try:
        paths = [os.path.abspath(a) for a in argv if a.endswith(".FCStd")]
        if not paths:
            paths = [draft_test_document(tempfile.mkdtemp(prefix="fcx-g1d-"))]
        for p in paths:
            try:
                run_file(p)
            except Exception:
                traceback.print_exc()
    finally:
        S.setRouting(was)
    sys.stdout.flush()


main(sys.argv[1:])

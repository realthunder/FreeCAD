"""The sandbox guest's files are served next to the scene.

docs/Sandbox.md 7.20, C1. A browser guest boots from the FreeCAD that
serves the document: /pyodide/boot.json names what the desktop runtime would
boot with, and the runtime, the fcx_image wheel, the bundled wheels and the
package set are served under /pyodide/ (src/Gui/SandboxServe.cpp). This checks
the endpoints over HTTP against a headless serve with a token grant as the
door, with no browser -- tests/gui/sandbox-console-browser.py is the browser
leg.

What is asserted:

  - boot.json is behind the door: 403 without the token; with it, 200,
    never cached, naming the runtime's version and the fcx_image wheel that
    ExpressionSandbox.imageInfo() resolves;
  - every runtime file the browser's loader fetches is served WITHOUT the
    token, byte for byte the file on disk, typed the way a module loader
    insists (text/javascript for .mjs, application/wasm), under a URL that
    carries the version -- and another version is 404;
  - the fcx_image wheel likewise, and never cached (it keeps its name across
    rebuilds); every bundled wheel boot.json lists is served;
  - nothing else is: a path with "..", the runtime directory itself, a name
    with a slash in it, the package manifest, a POST to a served file.

On a box with no pyodide runtime or no wheel, boot.json answers 503 with the
reason; that is checked, the file checks are noted as skipped, and no file is
served.
"""

import http.client
import json
import os
import socket
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SandboxConsoleServe"
TOKEN = "c1-serve-token"

RUNTIME_FILES = {
    "pyodide.mjs": "text/javascript",
    "pyodide.asm.mjs": "text/javascript",
    "pyodide.asm.wasm": "application/wasm",
    "python_stdlib.zip": "application/zip",
    "pyodide-lock.json": "application/json",
}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def fetch(port, path, method="GET", body=None):
    """(status, content type, cache control, body), the path sent as is."""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
    try:
        conn.request(method, path, body=body)
        r = conn.getresponse()
        return (
            r.status,
            r.getheader("Content-Type") or "",
            r.getheader("Cache-Control") or "",
            r.read(),
        )
    finally:
        conn.close()


def sandbox():
    sb = getattr(FreeCAD, "ExpressionSandbox", None)
    if sb is None:
        import ExpressionSandbox as sb
    return sb


def run():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
        "Type", "bgfx - OpenGL"
    )
    doc = FreeCAD.newDocument(DOC, hidden=True)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    port = free_port()
    if not check(
        "the document is served headless", FreeCADGui.serveDocument(doc, port), "port %d" % port
    ):
        return
    FreeCADGui.serveSetGrants([{"token": TOKEN}])

    status = fetch(port, "/pyodide/boot.json")[0]
    check("boot.json is behind the door", status == 403, status)
    status, ctype, cache, body = fetch(port, "/pyodide/boot.json?token=" + TOKEN)
    info = sandbox().imageInfo()
    note("NOTE imageInfo " + json.dumps(info))

    if status == 503:
        reason = json.loads(body.decode()).get("error", "")
        check("with nothing to serve, boot.json says why", bool(reason), reason)
        note("SKIP the file checks: " + reason)
        status = fetch(port, "/pyodide/runtime/0/pyodide.mjs?token=" + TOKEN)[0]
        check("and no file is served", status == 404, status)
        return
    if not check(
        "boot.json answers with the token",
        status == 200 and ctype.startswith("application/json"),
        (status, ctype),
    ):
        return
    boot = json.loads(body.decode())
    note("NOTE boot.json " + json.dumps(boot))
    check("boot.json is never cached", cache == "no-store", cache)

    stdlib = os.path.realpath(info["stdlib"])
    wheel = os.path.realpath(info["image"])
    with open(os.path.join(stdlib, "package.json")) as f:
        version = json.load(f)["version"]
    check(
        "boot.json names the runtime's version",
        boot["version"] == version,
        (boot["version"], version),
    )
    check(
        "the runtime URL carries the version",
        boot["runtime"] == "runtime/%s/" % version,
        boot["runtime"],
    )
    check(
        "boot.json names the desktop's fcx_image wheel",
        boot["wheel"] == "wheels/" + os.path.basename(wheel),
        (boot["wheel"], wheel),
    )

    for name, want in RUNTIME_FILES.items():
        status, ctype, cache, body = fetch(port, "/pyodide/" + boot["runtime"] + name)
        with open(os.path.join(stdlib, name), "rb") as f:
            disk = f.read()
        check(
            "runtime %s is served ahead of the door, byte for byte" % name,
            status == 200 and ctype == want and body == disk,
            (status, ctype, len(body), len(disk)),
        )
        check("runtime %s may be cached" % name, "max-age" in cache, cache)
    # What the ungated mount declines falls through to the door: without
    # the token that is a 403, with it the 404 of a path nothing serves.
    status = fetch(port, "/pyodide/runtime/0.0.0/pyodide.mjs")[0]
    check("another runtime version goes to the door", status == 403, status)
    status = fetch(port, "/pyodide/runtime/0.0.0/pyodide.mjs?token=" + TOKEN)[0]
    check("and is 404 past it", status == 404, status)

    status, ctype, cache, body = fetch(port, "/pyodide/" + boot["wheel"])
    with open(wheel, "rb") as f:
        disk = f.read()
    check(
        "the fcx_image wheel is served ahead of the door, byte for byte",
        status == 200 and body == disk,
        (status, len(body), len(disk)),
    )
    check("the fcx_image wheel is never cached", cache == "no-store", cache)
    for w in boot["bundled"]:
        status, ctype, cache, body = fetch(port, "/pyodide/" + w)
        check("bundled %s is served" % w, status == 200 and body[:2] == b"PK", (status, len(body)))

    refused = [
        "/pyodide/runtime/%s/../%s/pyodide.mjs" % (version, version),
        "/pyodide/" + boot["runtime"],
        "/pyodide/wheels//" + os.path.basename(wheel),
        "/pyodide/packages/manifest.json",
        "/pyodide/runtime/%s/pyodide.mjs/" % version,
    ]
    for path in refused:
        status = fetch(port, path)[0]
        check("not served: %s" % path, status != 200, status)
    status = fetch(port, "/pyodide/%s?token=%s" % (boot["wheel"], TOKEN), "POST", b"x")[0]
    check("a POST to a served file is not served", status != 200, status)


def main():
    try:
        run()
    except Exception:
        note("FAIL run:\n" + traceback.format_exc())
    try:
        FreeCADGui.serveStop()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, main)

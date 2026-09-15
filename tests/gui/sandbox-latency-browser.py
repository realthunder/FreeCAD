"""What a console statement costs over a LAN or a tunnel, and the page's memory.

docs/Sandbox.md 7.20, C5. Serves a document headless (Box, 48 more boxes and
Poly, a 100-edge polygon) with a token grant as the door, then for each RTT puts
scripts/delay-proxy.js between Chrome and the server and has
scripts/console-drive.js open web/latency-test.html through it. The page boots
the guest over the proxied socket and times a fixed set of statements; its rows
come back here as NOTE lines, one table per RTT, with the boot times and the
page's processes' memory at three marks. RTT 0 is no proxy at all. Each RTT
runs with the host's prefetch of sibling reads on and off (FC_SANDBOX_PREFETCH,
read when the connection's endpoint is made); SANDBOX_LATENCY_MODES narrows
that to "on" or "off".

A C5 measurement, not a gate: it checks only that every statement ran in every
run, made the same number of bridge ops at each RTT of one mode, and cost at
least ops x RTT.
The figures go to $GT_OUT/latency.json. SANDBOX_LATENCY_RTTS overrides the list
of RTTs in ms. Not registered, for the reason docs/Testing.md gives; the
environment is the one tests/gui/sandbox-console-browser.py names:

  PUPPETEER_PATH=~/works/sw/fcad-probes/node_modules/puppeteer-core \\
  CHROME=$(ls ~/.cache/puppeteer/chrome/linux-*/chrome-linux64/chrome) \\
  CHROME_LIBS=~/.cache/puppeteer/lib \\
  NODE=~/works/sw/emsdk-5.0.3/node/24.19.0_64bit/bin/node \\
  scripts/gui-test.sh tests/gui/sandbox-latency-browser.py /tmp/latency-web \\
      --timeout 1800
"""

import json
import os
import shutil
import socket
import subprocess
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
PAGE = os.path.join(WASM, "web", "latency-test.html")
DRIVER = os.path.join(REPO, "scripts", "console-drive.js")
PROXY = os.path.join(REPO, "scripts", "delay-proxy.js")
DOC = "SandboxLatencyBrowser"
TOKEN = "c5-latency-token"
RTTS = [int(x) for x in os.environ.get("SANDBOX_LATENCY_RTTS", "0,2,10,30,100").split(",")]
MODES = os.environ.get("SANDBOX_LATENCY_MODES", "on,off").split(",")
RUNS = [(rtt, mode) for mode in MODES for rtt in RTTS]
RUN_WAIT_S = 600

os.environ["FC_BGFX_VIEWER_BUILD"] = WASM

state = {"done": False, "runs": [], "i": 0, "port": 0, "node": None}


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


def missing():
    node = os.environ.get("NODE") or shutil.which("node")
    if not node:
        return "node (NODE or PATH)", None
    pp = os.environ.get("PUPPETEER_PATH", "")
    if not pp or not os.path.isdir(pp):
        return "a puppeteer-core install (PUPPETEER_PATH)", None
    chrome = os.environ.get("CHROME", "")
    if not chrome or not os.path.isfile(chrome):
        return "a Chrome binary (CHROME)", None
    if not os.path.isfile(PAGE):
        return "the web bundle (%s)" % PAGE, None
    return None, node


def build():
    try:
        why, node = missing()
        if why:
            note("SKIP the browser leg needs " + why)
            finish()
            return
        state["node"] = node
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        doc.addObject("Part::Box", "Box")
        for i in range(48):
            b = doc.addObject("Part::Box", "B%02d" % i)
            b.Placement.Base.x = 20 * (i + 1)
        poly = doc.addObject("Part::Feature", "Poly")
        poly.Shape = Part.makePolygon([FreeCAD.Vector(i, 3 * (i % 2), 0) for i in range(101)])
        doc.recompute()
        port = free_port()
        if not check(
            "the document is served headless", FreeCADGui.serveDocument(doc, port), "port %d" % port
        ):
            finish()
            return
        FreeCADGui.serveSetGrants([{"token": TOKEN}])
        state["port"] = port
        QtCore.QTimer.singleShot(0, start_run)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def start_run():
    try:
        rtt, mode = RUNS[state["i"]]
        os.environ["FC_SANDBOX_PREFETCH"] = "1" if mode == "on" else "0"
        run = {"rtt": rtt, "mode": mode, "proxy": None, "lines": [], "t0": time.perf_counter()}
        state["runs"].append(run)
        port = state["port"]
        if rtt > 0:
            run["proxy"] = subprocess.Popen(
                [state["node"], PROXY, "127.0.0.1:%d" % port, str(rtt)],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            first = run["proxy"].stdout.readline().split()
            if len(first) != 2 or first[0] != "listening":
                check("the delay proxy for RTT %d ms listens" % rtt, False, first)
                finish()
                return
            port = int(first[1])
        reps = 3 if rtt < 30 else 1
        url = "http://127.0.0.1:%d/web/latency-test.html?token=%s&doc=%s&reps=%d" % (
            port,
            TOKEN,
            DOC,
            reps,
        )
        note("NOTE RTT %d ms, prefetch %s: %s" % (rtt, mode, url))
        log = open(os.path.join(OUT, "drive-rtt%d-%s.log" % (rtt, mode)), "w")
        run["proc"] = subprocess.Popen(
            [state["node"], DRIVER, url, "fcxLatency", str((RUN_WAIT_S - 30) * 1000)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        def pump():
            for line in run["proc"].stdout:
                log.write(line)
                log.flush()
                run["lines"].append(line.rstrip("\n"))
            log.close()

        run["thread"] = threading.Thread(target=pump, daemon=True)
        run["thread"].start()
        QtCore.QTimer.singleShot(200, poll)
    except Exception:
        note("FAIL start_run:\n" + traceback.format_exc())
        finish()


def poll():
    run = state["runs"][-1]
    if run["proc"].poll() is None:
        if time.perf_counter() - run["t0"] > RUN_WAIT_S:
            check(
                "RTT %d ms, prefetch %s: the page reported" % (run["rtt"], run["mode"]),
                False,
                "timed out",
            )
            run["proc"].kill()
            finish()
            return
        QtCore.QTimer.singleShot(200, poll)
        return
    run["thread"].join(10)
    if run["proxy"]:
        run["proxy"].kill()
    collect(run)
    state["i"] += 1
    if state["i"] < len(RUNS):
        QtCore.QTimer.singleShot(0, start_run)
    else:
        summarize()


def collect(run):
    rtt = "%d ms, prefetch %s:" % (run["rtt"], run["mode"])
    report = None
    for line in run["lines"]:
        if line.startswith("REPORT "):
            try:
                report = json.loads(line[len("REPORT ") :])
            except ValueError:
                pass
    run["report"] = report
    if not check(
        "RTT %s the page reported" % rtt,
        report is not None,
        "exit %s" % run["proc"].returncode,
    ):
        return
    check("RTT %s the guest booted" % rtt, not report.get("error"), report.get("error", ""))
    note(
        "NOTE RTT %d ms: connect %s ms, runtime %s ms, wheels %s ms"
        % (rtt, report.get("connectMs"), report.get("runtimeMs"), report.get("wheelsMs"))
    )
    for row in report.get("rows", []):
        best = min(row["ms"]) if row["ms"] else -1
        check(
            "RTT %s %s" % (rtt, row["name"]),
            row["ok"],
            "%d ops, best %.1f ms, bridge %.1f ms | %s"
            % (row["ops"], best, min(row["bridgeMs"] or [-1]), row["detail"]),
        )
    for label, m in report.get("memory", {}).items():
        by_type = {}
        for p in m.get("processes") or []:
            by_type.setdefault(p["type"], [0.0, 0.0])
            by_type[p["type"]][0] += p["pssMB"]
            by_type[p["type"]][1] += p["rssMB"]
        note(
            "NOTE RTT %d ms: memory at %s: wasm %s MB, JS heap %s MB, PSS/RSS by process type %s"
            % (
                rtt,
                label,
                None if m.get("wasmMB") is None else round(m["wasmMB"], 1),
                None if m.get("jsHeapUsedMB") is None else round(m["jsHeapUsedMB"], 1),
                json.dumps({k: [round(v[0], 1), round(v[1], 1)] for k, v in by_type.items()}),
            )
        )


def summarize():
    for mode in MODES:
        runs = [r for r in state["runs"] if r["mode"] == mode]
        base = runs[0].get("report") if runs else None
        if not base:
            continue
        for run in runs[1:]:
            rep = run.get("report")
            if not rep:
                continue
            what = "RTT %d ms, prefetch %s:" % (run["rtt"], mode)
            same = [r["ops"] for r in rep["rows"]] == [r["ops"] for r in base["rows"]]
            check(what + " every statement made the ops it made direct", same)
            floor = all(min(r["ms"]) >= 0.9 * r["ops"] * run["rtt"] for r in rep["rows"] if r["ok"])
            check(what + " every statement cost at least ops x RTT", floor)
        # One table per mode: statement, ops, then best ms per RTT.
        note("NOTE prefetch %s" % mode)
        head = "%-44s %5s" % ("statement", "ops") + "".join(
            " %9s" % ("%d ms" % run["rtt"]) for run in runs
        )
        note("NOTE " + head)
        for k, row in enumerate(base["rows"]):
            cells = []
            for run in runs:
                rep = run.get("report")
                rows = rep["rows"] if rep else []
                cells.append(" %9.1f" % min(rows[k]["ms"]) if k < len(rows) else " %9s" % "-")
            note("NOTE %-44s %5d" % (row["name"], row["ops"]) + "".join(cells))
    with open(os.path.join(OUT, "latency.json"), "w") as f:
        json.dump(
            [
                {"rtt": r["rtt"], "mode": r["mode"], "report": r.get("report")}
                for r in state["runs"]
            ],
            f,
            indent=1,
        )
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for run in state["runs"]:
        for key in ("proc", "proxy"):
            p = run.get(key)
            if p and p.poll() is None:
                p.kill()
    try:
        FreeCADGui.serveStop()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)

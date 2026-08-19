"""Reduce a pbr_cost_probe.py run to the per-fragment cost of PBR.

Reads the FreeCAD --log-file the probe wrote and the JSON it left
beside it, attributes every once-a-second `render frame:` line to the
cell whose PBRCOST markers bracket it, and reports each leg's frame
cost against the covered-pixel count the cell was staged at.

The per-fragment slope is the answer being sought: PBR's cost is per
fragment, so what matters is not "PBR frames cost N ms" (which is a
statement about one scene at one zoom) but nanoseconds per covered
pixel, which carries to any scene.

Usage: python3 scripts/pbr_cost_report.py <log> <json>
"""
import json
import re
import sys

FRAME_RE = re.compile(
    r"render frame: frames:(?P<frames>\d+) "
    r"(?P<w>\d+)x(?P<h>\d+) "
    r"frame (?P<frame>[\d.]+)ms "
    r"submit (?P<submit>[\d.]+)ms "
    r"gpu (?P<gpu>n/a|[\d.]+ms\(n=\d+\)).*?"
    r"draws (?P<draws>[\d.]+) prims (?P<prims>[\d.]+).*?"
    r"cpu ours (?P<ours>[\d.]+)ms \(bgfx::frame (?P<bgfx>[\d.]+)ms\) "
    r"outside (?P<outside>[\d.]+)ms")
BEGIN_RE = re.compile(
    r"PBRCOST BEGIN leg=(?P<leg>\S+) height=(?P<height>[\d.]+)"
    r"(?: layers=(?P<layers>\d+))?")
END_RE = re.compile(r"PBRCOST END ")


def parse(logpath):
    """Cells in run order, each with the frame lines that fell inside it."""
    cells = []
    current = None
    with open(logpath, "r", errors="replace") as fp:
        for line in fp:
            begin = BEGIN_RE.search(line)
            if begin:
                current = {"leg": begin.group("leg"),
                           "height": float(begin.group("height")),
                           "layers": int(begin.group("layers") or 0),
                           "lines": []}
                cells.append(current)
                continue
            if END_RE.search(line):
                current = None
                continue
            if current is None:
                continue
            m = FRAME_RE.search(line)
            if m:
                current["lines"].append(m.groupdict())
    return cells


def reduce_cell(cell):
    """Frame-weighted means. The first line of a window is dropped: its
    accumulation started before the marker, so it averages in frames
    that belong to the previous cell's settings."""
    rows = cell["lines"][1:] or cell["lines"]
    total = sum(int(r["frames"]) for r in rows)
    if not total:
        return None
    out = {"samples": len(rows), "frames": total}
    for key in ("frame", "submit", "ours", "bgfx", "outside", "draws", "prims"):
        out[key] = sum(float(r[key]) * int(r["frames"]) for r in rows) / total
    gpus = [r["gpu"] for r in rows if r["gpu"] != "n/a"]
    out["gpu"] = gpus[0] if gpus else "n/a"
    out["res"] = "%sx%s" % (rows[0]["w"], rows[0]["h"])
    return out


def fit(points):
    """Least-squares slope/intercept of ms against covered pixels."""
    n = len(points)
    if n < 2:
        return None, None
    sx = sum(p[0] for p in points)
    sy = sum(p[1] for p in points)
    sxx = sum(p[0] * p[0] for p in points)
    sxy = sum(p[0] * p[1] for p in points)
    denom = n * sxx - sx * sx
    if denom == 0:
        return None, None
    slope = (n * sxy - sx * sy) / denom
    return slope, (sy - slope * sx) / n


def main():
    logpath, jsonpath = sys.argv[1], sys.argv[2]
    cells = parse(logpath)
    with open(jsonpath) as fp:
        meta = json.load(fp)["cells"]

    rows = []
    for cell, info in zip(cells, meta):
        red = reduce_cell(cell)
        if not red:
            continue
        assert cell["leg"] == info["leg"], "log and json disagree on cell order"
        red.update(leg=info["leg"], height=info["height"],
                   layers=info.get("layers", 0),
                   sync=info.get("syncMs"),
                   pixels=info["geometryPixels"],
                   staged=info.get("staged", True))
        rows.append(red)

    print("%-9s %7s %8s %9s %9s %8s %8s %8s %7s %s"
          % ("leg", "layers", "px", "frame ms", "sync ms", "ours", "submit",
             "draws", "frames", "staged"))
    for r in rows:
        print("%-9s %7d %8d %9.3f %9.3f %8.3f %8.3f %8.0f %7d %s"
              % (r["leg"], r["layers"], r["pixels"], r["frame"],
                 r["sync"] if r["sync"] is not None else float("nan"),
                 r["ours"], r["submit"], r["draws"], r["frames"],
                 "" if r["staged"] else "STALE"))

    print()
    flag_warmup(rows)
    swept = len({r["layers"] for r in rows}) > 1
    if swept:
        report_layers(rows)
    else:
        report_coverage(rows)


def flag_warmup(rows):
    """Drop cells still paying startup costs.

    A probe can burn a warm-up cell and the FIRST measured cell can
    still come back contaminated -- shader compiles and the first real
    frames land wherever they land. It is not detectable from the
    timing being measured (that is the thing in question) but it is
    obvious in the submission cost, which is a property of the scene
    and should barely move across a run. A cell submitting several
    times the run's median is not comparable to the rest, and leaving
    it in bends whichever leg it lands on -- always the first, so
    always the same leg, which is worse than noise.
    """
    submits = sorted(r["submit"] for r in rows)
    median = submits[len(submits) // 2]
    for r in rows:
        if median > 0 and r["submit"] > 2.0 * median:
            r["staged"] = False
            print("  excluded: %s layers=%d -- submit %.2fms is %.1fx the "
                  "run median %.2fms (still warming up)"
                  % (r["leg"], r["layers"], r["submit"],
                     r["submit"] / median, median))


def report_layers(rows):
    """Slope against overdraw layers = the cost of one full-screen
    shading pass. Reported off BOTH clocks: the free-running frame,
    where the CPU hides most of it, and the readback-serialized frame,
    where it cannot hide."""
    px = max(r["pixels"] for r in rows)
    for field, label in (("sync", "readback-serialized frame"),
                         ("frame", "free-running frame")):
        if any(r.get(field) is None for r in rows):
            continue
        print("cost of one full-screen shading pass, %s" % label)
        print("(%s ms against layers, %d px covered):" % (field, px))
        base = summarize(rows, field, px)
        verdict(base, px)
        print()


def summarize(rows, field, px):
    base = {}
    for leg in ("phong", "pbr", "pbr-nofs"):
        pts = [(r["layers"], r[field]) for r in rows
               if r["leg"] == leg and r["staged"]]
        slope, intercept = fit(pts)
        if slope is None:
            continue
        base[leg] = slope
        print("  %-9s %8.4f ms/pass = %6.3f ns/px   fixed %7.3f ms   (n=%d)"
              % (leg, slope, slope * 1e6 / px, intercept, len(pts)))
    return base


def verdict(base, px):
    if not base.get("phong"):
        return
    if base.get("phong", 0) <= 0.0:
        print("  !! Phong's own slope is <= 0: the overdraw did not reach")
        print("     the shader (early-z rejected it), so no slope here is")
        print("     a shading cost. Nothing below is to be believed.")
        return
    if "pbr" in base:
        ratio = base["pbr"] / base["phong"]
        print("  PBR shading costs %.2fx Phong per full-screen pass"
              % ratio)
        print("  = %+.4f ms per full-screen pass, %+.3f ns/px"
              % (base["pbr"] - base["phong"],
                 (base["pbr"] - base["phong"]) * 1e6 / px))
        for w, h, label in ((1920, 1080, "1080p"), (2560, 1440, "1440p")):
            print("    %-6s one full-coverage frame: %+.3f ms"
                  % (label, (base["pbr"] - base["phong"]) * w * h / px))
    if "pbr-nofs" in base and "pbr" in base:
        print("  of which fcBaseFromSpecular: %+.4f ms/pass (%.2fx)"
              % (base["pbr"] - base["pbr-nofs"],
                 base["pbr"] / base["pbr-nofs"] if base["pbr-nofs"] else 0))


def report_coverage(rows):
    print("per-fragment cost (frame ms against covered pixels):")
    base = {}
    for leg in ("phong", "pbr", "pbr-nofs"):
        pts = [(r["pixels"], r["frame"]) for r in rows
               if r["leg"] == leg and r["staged"]]
        slope, intercept = fit(pts)
        if slope is None:
            continue
        base[leg] = (slope, intercept)
        print("  %-9s %7.3f ns/px   fixed %6.3f ms   (n=%d)"
              % (leg, slope * 1e6, intercept, len(pts)))
    if "phong" in base and "pbr" in base:
        dp = (base["pbr"][0] - base["phong"][0]) * 1e6
        print()
        print("  PBR costs %+.3f ns per covered pixel over Phong" % dp)


if __name__ == "__main__":
    main()

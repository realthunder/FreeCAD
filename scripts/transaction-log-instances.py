# T4 of docs/TransactionLog.md sec 27.52, with real modeling (sec 27.66): the
# log over a long run of copy-on-change link instances. Run with FreeCADCmd:
#
#   T4_FILE=scanner.FCStd T4_STEPS=1000 T4_OUT=result.json \
#   FreeCADCmd scripts/transaction-log-instances.py
#
# It copies the file, opens it with the log on (embedded mode) and takes
# T4_STEPS steps, each a transaction and a recompute, with a fixed seed:
#   add    -- a copy-on-change App::Link (Owned) to T4_BODY with dimensions
#             never used before; the link gets a private copy of the body;
#   edit   -- an instance's dimensions (new geometry, no new names);
#   delete -- an instance, which takes its copy with it.
# A save every T4_SAVE_EVERY steps and after the last; branch `side` at T4_BRANCH_AT, a switch
# every T4_SWITCH_EVERY after it; a trim of the current branch every
# T4_TRIM_EVERY. The last step switches and trims nothing, so the end runs on
# the branch the steps before it made. At the end: open the oldest version,
# restore to it, undo that, undo T4_UNDO steps (the hot window, then cold),
# reopen, compactFileState(); every instance's stored state -- its copy, its
# mode, its Config_* values, every copy object's element map -- is compared
# with what it was at each of those points. The link's own shape, made on
# demand, is compared too and reported apart.
#
# T4_LOG=0 runs the same steps with the log off (T2 of sec 27.52): no
# branches, trims, versions or store; the recompute creep and the saves.
# Prints one line per save and writes everything to T4_OUT as JSON.

import hashlib
import json
import os
import random
import re
import shutil
import sqlite3
import tempfile
import time

import FreeCAD
import Part

env = os.environ
source = env["T4_FILE"]
bodyName = env.get("T4_BODY", "Body002")
steps = int(env.get("T4_STEPS", "1000"))
saveEvery = int(env.get("T4_SAVE_EVERY", "10"))
branchAt = int(env.get("T4_BRANCH_AT", "50"))
switchEvery = int(env.get("T4_SWITCH_EVERY", "50"))
trimEvery = int(env.get("T4_TRIM_EVERY", "200"))
undoSteps = int(env.get("T4_UNDO", "20"))
maxInstances = int(env.get("T4_MAX_INSTANCES", "40"))
seed = int(env.get("T4_SEED", "27"))
logOn = env.get("T4_LOG", "1") == "1"
out = env.get("T4_OUT", "")
# 1: keep every element map whole, and report how the ones that differ do.
dump = env.get("T4_DUMP", "0") == "1"
fullMaps = {}

# T4_LOGLEVEL=Log shows, among much else, a switch that read a version whole.
if env.get("T4_LOGLEVEL"):
    FreeCAD.setLogLevel("App", env["T4_LOGLEVEL"])
params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
params.SetInt("TransactionLog", 2 if logOn else 0)

work = tempfile.mkdtemp(prefix="fc-t4-")
path = os.path.join(work, os.path.basename(source))
shutil.copy(source, path)
rng = random.Random(seed)
result = {
    "file": os.path.basename(source),
    "log": logOn,
    "steps": steps,
    "seed": seed,
    "save_every": saveEvery,
    "saves": [],
    "switches": [],
    "trims": [],
    "errors": [],
    "trace": [],
}


def timed(fn):
    start = time.perf_counter()
    value = fn()
    return value, time.perf_counter() - start


def memory():
    """(resident, peak) bytes of this process."""
    rss = hwm = 0
    with open("/proc/self/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                rss = int(line.split()[1]) * 1024
            elif line.startswith("VmHWM:"):
                hwm = int(line.split()[1]) * 1024
    return rss, hwm


def storeStats(doc):
    db = os.path.join(doc.TransientDir, "history", "log.db")
    wal = db + "-wal"
    con = sqlite3.connect("file:%s?mode=ro" % db, uri=True)
    try:
        count = lambda table: con.execute("SELECT count(*) FROM %s" % table).fetchone()[0]
        try:
            tables = dict(
                con.execute("SELECT name, sum(pgsize) FROM dbstat GROUP BY name").fetchall()
            )
        except sqlite3.Error:
            tables = {}
        stats = {
            "db_bytes": os.path.getsize(db),
            "wal_bytes": os.path.getsize(wal) if os.path.exists(wal) else 0,
            "free_pages": con.execute("PRAGMA freelist_count").fetchone()[0],
            "tables": tables,
            "txns": count("txn"),
            "ops": count("op"),
            "versions": count("version"),
            "entities": count("entity"),
            "strtable_rows": count("strtable"),
            "strref_rows": count("strref"),
            "strref_bytes": con.execute(
                "SELECT coalesce(sum(length(owner) + length(ranges)), 0) FROM strref"
            ).fetchone()[0],
        }
    finally:
        con.close()
    return stats


def instances(doc):
    return [o for o in doc.Objects if o.TypeId == "App::Link" and o.Name.startswith("Inst")]


def elementMaps(doc):
    """{instance: (digest of its stored state, digest of its link's shape)}.

    The stored state is what the document holds for the instance: its copy's
    name, LinkCopyOnChange, the Config_* values, and every copy object's
    stored element map. The link's shape is made on demand (Part.getShape)
    and is reported apart: its element tags are minted when it is made."""
    maps = {}
    for link in instances(doc):
        try:
            linked = link.getLinkedObject(False)
            objs = [linked] + list(getattr(linked, "Group", []))
            stored = {
                "copy": linked.Name,
                "mode": link.LinkCopyOnChange,
                "config": sorted(
                    (n, round(getattr(link, n).Value, 9))
                    for n in link.PropertiesList
                    if n.startswith("Config")
                ),
                "maps": {
                    o.Name: sorted(o.Shape.ElementMap.items()) for o in objs if hasattr(o, "Shape")
                },
            }
            digest = hashlib.sha1(repr(sorted(stored.items())).encode()).hexdigest()[:16]
            shape = Part.getShape(link, needSubElement=False, retType=0)
            derived = hashlib.sha1(repr(sorted(shape.ElementMap.items())).encode()).hexdigest()[:16]
            maps[link.Name] = (digest, derived)
            if dump:
                fullMaps[digest] = {
                    "%s %s" % (o, n): e for o, items in stored["maps"].items() for n, e in items
                }
        except Exception as e:
            maps[link.Name] = ("error: %s" % e, "")
    return maps


def describe(doc, name):
    link = doc.getObject(name)
    if not link:
        return None
    linked = link.getLinkedObject(False)
    return {
        "linked": linked.Name if linked else None,
        "copy_on_change": link.LinkCopyOnChange,
        "config": {n: str(getattr(link, n)) for n in link.PropertiesList if n.startswith("Config")},
    }


def strings(doc, name):
    """The hasher strings a mapped name refers to, id -> text."""
    found = {}
    for m in re.finditer(r":H([0-9a-f]+)", name or ""):
        try:
            sid = doc.Hasher.getID(int(m.group(1), 16))
            found[m.group(1)] = sid.Data if sid else None
        except Exception as e:
            found[m.group(1)] = "error: %s" % e
    return found


def compare(label, doc, expected):
    both = elementMaps(doc)
    now = {k: v[0] for k, v in both.items()}
    want = {k: v[0] for k, v in expected.items()}
    differ = sorted(k for k in set(now) | set(want) if now.get(k) != want.get(k))
    derived = sorted(
        k for k in set(both) & set(expected) if both[k][1] != expected[k][1] and k not in differ
    )
    entry = {
        "at": label,
        "instances": len(now),
        "differ": len(differ),
        "names": differ[:10],
        "link_shapes_differ": len(derived),
    }
    entry["detail"] = {
        k: {"now": now.get(k), "expected": want.get(k), "state": describe(doc, k)}
        for k in differ[:10]
    }
    for k in differ[:3] if dump else []:
        a, b = fullMaps.get(now.get(k)), fullMaps.get(want.get(k))
        if a is None or b is None:
            continue
        # By element: the name it had, and the name it has.
        was = {v: n for n, v in b.items()}
        has = {v: n for n, v in a.items()}
        diff = [
            (e, was.get(e), has.get(e))
            for e in sorted(set(was) | set(has))
            if was.get(e) != has.get(e)
        ]
        entry["detail"][k]["diff"] = {
            "entries": [len(b), len(a)],
            "count": len(diff),
            "sample": [(e, x, y, strings(doc, x), strings(doc, y)) for e, x, y in diff[:4]],
        }
    result.setdefault("element_maps", []).append(entry)
    print(
        "T4 element maps %s: %d instances, %d differ %s (link shapes only: %d)"
        % (label, len(now), len(differ), differ[:5], len(derived))
    )
    return entry


def dimensions():
    # Values never used before: rng.uniform does not repeat in practice.
    inner = rng.uniform(8.0, 12.0)
    return {
        "Config_ID": inner,
        "Config_OD": inner + rng.uniform(4.0, 8.0),
        "Config_Height": rng.uniform(2.0, 6.0),
    }


def setDimensions(link, dims):
    for name, value in dims.items():
        setattr(link, name, value)


def branchName(doc):
    for b in doc.getTransactionBranches():
        if b["current"]:
            return b["name"]
    return ""


doc, t = timed(lambda: FreeCAD.openDocument(path))
result["open"] = t
doc.UndoMode = 1
if doc.SaveSchemaVersion < 5:
    doc.SaveSchemaVersion = 5
body = doc.getObject(bodyName)
# The file's element maps are older than this build's: the first recompute
# rebuilds them all, which is not what the run measures.
_, result["first_recompute"] = timed(doc.recompute)
_, result["first_save"] = timed(doc.save)

window = {"recompute": 0.0, "commit": 0.0, "drain": 0.0, "minted": 0, "kinds": {}}
# Each branch's states in step order: an undo walks the steps its branch took
# since the document was opened (a switch brings the other branch's back).
current = "main"
chains = {current: [elementMaps(doc)]}
hasher = doc.Hasher
for step in range(1, steps + 1):
    live = instances(doc)
    kind = rng.choices(("add", "edit", "delete"), (0.35, 0.45, 0.2))[0]
    if not live:
        kind = "add"
    elif kind == "add" and len(live) >= maxInstances:
        kind = "delete"
    size0 = hasher.Size
    target = name = None
    doc.openTransaction("%s %d" % (kind, step))
    try:
        if kind == "add":
            link = doc.addObject("App::Link", "Inst")
            link.LinkedObject = body
            name = link.Name
            link.LinkCopyOnChange = "Owned"
            setDimensions(link, dimensions())
        elif kind == "edit":
            target = rng.choice(live)
            name = target.Name
            setDimensions(target, dimensions())
        else:
            target = rng.choice(live)
            name = target.Name
            doc.removeObject(name)
    except Exception as e:
        entry = {"step": step, "kind": kind, "error": str(e)}
        if target is not None and target.isAttachedToDocument():
            linked = target.getLinkedObject(False)
            entry.update(
                {
                    "instance": target.Name,
                    "linked": linked.Name if linked else None,
                    "copy_on_change": target.LinkCopyOnChange,
                    "config": [n for n in target.PropertiesList if n.startswith("Config")],
                }
            )
        result["errors"].append(entry)
    _, dt = timed(doc.recompute)
    window["recompute"] += dt
    _, dt = timed(doc.commitTransaction)
    window["commit"] += dt
    if logOn:
        _, dt = timed(doc.resolveTransactionLog)
        window["drain"] += dt
    result["trace"].append([step, current, kind, name, hasher.Size - size0])
    window["minted"] += hasher.Size - size0
    window["kinds"][kind] = window["kinds"].get(kind, 0) + 1
    chains[current].append(elementMaps(doc))

    if step % saveEvery and step != steps:
        continue
    size0 = hasher.Size
    _, dt = timed(doc.save)
    rss, hwm = memory()
    stats = storeStats(doc) if logOn else {}
    stats.update(
        {
            "step": step,
            "save": dt,
            "file_bytes": os.path.getsize(path),
            "objects": len(doc.Objects),
            "instances": len(instances(doc)),
            "hasher": hasher.Size,
            "minted": window["minted"],
            "dropped": size0 - hasher.Size,
            "rss": rss,
            "hwm": hwm,
            "recompute": window["recompute"],
            "commit": window["commit"],
            "drain": window["drain"],
            "kinds": window["kinds"],
            "branch": branchName(doc) if logOn else "",
        }
    )
    result["saves"].append(stats)
    window = {"recompute": 0.0, "commit": 0.0, "drain": 0.0, "minted": 0, "kinds": {}}
    print(
        "T4 step %d [%s]: recompute %.2fs, save %.2fs, file %.1f MB, store %.1f MB, objects %d"
        " (%d instances), strings %d (+%d -%d), rss %.0f MB"
        % (
            step,
            stats["branch"],
            stats["recompute"],
            dt,
            stats["file_bytes"] / 1e6,
            stats.get("db_bytes", 0) / 1e6,
            stats["objects"],
            stats["instances"],
            stats["hasher"],
            stats["minted"],
            stats["dropped"],
            rss / 1e6,
        )
    )

    if not logOn or step == steps:
        continue
    if step == branchAt:
        _, dt = timed(lambda: doc.createTransactionBranch("side"))
        chains["side"] = list(chains[current])
        current = "side"
        result["switches"].append({"step": step, "to": "side", "create": True, "seconds": dt})
    elif step > branchAt and (step - branchAt) % switchEvery == 0:
        target = "main" if branchName(doc) == "side" else "side"
        before = storeStats(doc)["db_bytes"]
        ok, dt = timed(lambda: doc.switchTransactionBranch(target))
        current = target
        compare("switched to %s at %d" % (target, step), doc, chains[current][-1])
        result["switches"].append(
            {
                "step": step,
                "to": target,
                "ok": ok,
                "seconds": dt,
                "db_before": before,
                "db_after": storeStats(doc)["db_bytes"],
                "objects": len(doc.Objects),
            }
        )
        print("T4 switch to %s: %.2fs, %d objects" % (target, dt, len(doc.Objects)))
    if step % trimEvery == 0:
        name = branchName(doc)
        before = storeStats(doc)
        removed, dt = timed(lambda: doc.trimTransactionBranch(name))
        after = storeStats(doc)
        result["trims"].append(
            {
                "step": step,
                "branch": name,
                "rows": removed,
                "seconds": dt,
                "db_before": before["db_bytes"],
                "db_after": after["db_bytes"],
                "versions_before": before["versions"],
                "versions_after": after["versions"],
                "txns_before": before["txns"],
                "txns_after": after["txns"],
            }
        )
        print(
            "T4 trim %s: %d rows, %.2fs, store %.1f -> %.1f MB"
            % (name, removed, dt, before["db_bytes"] / 1e6, after["db_bytes"] / 1e6)
        )

chain = chains[current]
final = chain[-1]
if logOn:
    versions = sorted(v["num"] for v in doc.getTransactionVersions())
    oldest = versions[0]
    result["versions_live"] = len(versions)
    v, t = timed(lambda: doc.openTransactionVersion(oldest, False))
    result["open_oldest_version"] = t
    FreeCAD.closeDocument(v.Name)
    _, t = timed(lambda: doc.restoreTransactionVersion(oldest))
    result["restore_oldest"] = t
    compare("restored (vs the last step)", doc, final)
    _, t = timed(doc.undo)
    result["undo_restore"] = t
    compare("restore undone", doc, final)

# Back through the steps: the hot window first, then the stubs past it.
undos = []
for n in range(1, doc.UndoCount + 1):
    if n > undoSteps + 19:
        break
    _, t = timed(doc.undo)
    undos.append(t)
    e = compare("undo %d" % n, doc, chain[-1 - n])
    e["seconds"] = t
result["undo_seconds"] = undos
if undos:
    _, t = timed(doc.recompute)
    result["recompute_after_undo"] = t
    compare("undo %d, recomputed" % len(undos), doc, chain[-1 - len(undos)])
FreeCAD.closeDocument(doc.Name)

doc, t = timed(lambda: FreeCAD.openDocument(path))
result["reopen"] = t
compare("reopened", doc, final)
if logOn:
    result["versions_carried"] = len(doc.getTransactionVersions())
    c, t = timed(doc.compactFileState)
    result["compact"] = t
    result["compacted"] = c
    compare("compacted", doc, final)
print(
    "T4 end: open oldest %.2fs, restore %.2fs, undo it %.2fs, undos %s, reopen %.2fs,"
    " compact %.3fs %s, errors %d"
    % (
        result.get("open_oldest_version", 0),
        result.get("restore_oldest", 0),
        result.get("undo_restore", 0),
        " ".join("%.2f" % u for u in undos),
        result["reopen"],
        result.get("compact", 0),
        result.get("compacted", ""),
        len(result["errors"]),
    )
)
FreeCAD.closeDocument(doc.Name)
if out:
    with open(out, "w") as f:
        json.dump(result, f, indent=1)
shutil.rmtree(work, ignore_errors=True)

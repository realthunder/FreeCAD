# How the transaction log scales on a real model (docs/TransactionLog.md
# sec 27.52). Run with FreeCADCmd:
#
#   SCALE_FILE=model.FCStd SCALE_OBJECT=Pad SCALE_PROPERTY=Length \
#   SCALE_EDITS=40 SCALE_SAVE_EVERY=5 SCALE_OUT=result.json \
#   FreeCADCmd scripts/transaction-log-scale.py
#
# It copies the file, opens it with the log on (embedded mode), and edits one
# property SCALE_EDITS times -- each edit a transaction and a recompute -- with
# a save every SCALE_SAVE_EVERY edits. After every save it records the file's
# size, the log store's size, and the store's entities by tier and encoding.
# At the end it reopens the file and times the history's operations. Prints
# one line per save and writes everything to SCALE_OUT as JSON.

import json
import os
import shutil
import sqlite3
import tempfile
import time

import FreeCAD

env = os.environ
source = env["SCALE_FILE"]
objectName = env.get("SCALE_OBJECT", "Pad")
propertyName = env.get("SCALE_PROPERTY", "Length")
edits = int(env.get("SCALE_EDITS", "40"))
saveEvery = int(env.get("SCALE_SAVE_EVERY", "5"))
out = env.get("SCALE_OUT", "")
# 1: every edit a value never set before, so no shape repeats and content
# addressing cannot share one; 0: seven values in turn.
unique = env.get("SCALE_UNIQUE", "1") == "1"

params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
params.SetInt("TransactionLog", 2)

work = tempfile.mkdtemp(prefix="fc-scale-")
path = os.path.join(work, os.path.basename(source))
shutil.copy(source, path)
result = {"file": os.path.basename(source), "edits": edits, "save_every": saveEvery, "saves": []}


def timed(fn):
    start = time.perf_counter()
    value = fn()
    return value, time.perf_counter() - start


def dirSize(folder):
    total = 0
    for root, _, files in os.walk(folder):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return total


def storeStats(doc):
    db = os.path.join(doc.TransientDir, "history", "log.db")
    size = os.path.getsize(db)
    wal = os.path.getsize(db + "-wal") if os.path.exists(db + "-wal") else 0
    blobs = {}
    for root, _, files in os.walk(os.path.join(doc.TransientDir, "blobs")):
        for f in files:
            ext = os.path.splitext(f)[1] or f
            n, b = blobs.get(ext, (0, 0))
            try:
                blobs[ext] = (n + 1, b + os.path.getsize(os.path.join(root, f)))
            except OSError:
                pass
    con = sqlite3.connect("file:%s?mode=ro" % db, uri=True)
    try:
        rows = con.execute(
            "SELECT tier, enc, kind, count(*), sum(size), sum(length(data)) FROM entity"
            " GROUP BY tier, enc, kind"
        ).fetchall()
        txns = con.execute("SELECT count(*) FROM txn").fetchone()[0]
        ops = con.execute("SELECT count(*) FROM op").fetchone()[0]
        versions = con.execute("SELECT count(*) FROM version").fetchone()[0]
        refRoles = con.execute(
            "SELECT role, count(*), sum(length(entity) + length(target) + length(name) + length(role))"
            " FROM ref GROUP BY role"
        ).fetchall()
        txnKinds = con.execute(
            "SELECT kind, count(*), sum(length(script)) FROM txn GROUP BY kind"
        ).fetchall()
        pageSize = con.execute("PRAGMA page_size").fetchone()[0]
        pages = con.execute("PRAGMA page_count").fetchone()[0]
        free = con.execute("PRAGMA freelist_count").fetchone()[0]
        try:
            tables = dict(
                con.execute(
                    "SELECT name, sum(pgsize) FROM dbstat GROUP BY name ORDER BY 2 DESC"
                ).fetchall()
            )
        except sqlite3.Error:
            tables = {}
    finally:
        con.close()
    entities = [
        {"tier": t, "enc": e, "kind": k, "count": n, "full": full or 0, "stored": stored or 0}
        for t, e, k, n, full, stored in rows
    ]
    return {
        "db_bytes": size,
        "wal_bytes": wal,
        "page_size": pageSize,
        "pages": pages,
        "free_pages": free,
        "tables": tables,
        "ref_roles": refRoles,
        "txn_kinds": txnKinds,
        "blobs_by_ext": blobs,
        "blob_dir_bytes": dirSize(os.path.join(doc.TransientDir, "blobs")),
        "txns": txns,
        "ops": ops,
        "versions": versions,
        "entities": entities,
    }


doc, t = timed(lambda: FreeCAD.openDocument(path))
result["open"] = t
doc.UndoMode = 1
obj = doc.getObject(objectName)
base = getattr(obj, propertyName)
base = float(getattr(base, "Value", base))
recompute = commit = drain = 0.0
for i in range(edits):
    doc.openTransaction("edit %d" % i)
    setattr(obj, propertyName, base + (0.01 * (i + 1) if unique else 0.5 * (i % 7 + 1)))
    _, dt = timed(doc.recompute)
    recompute += dt
    _, dt = timed(doc.commitTransaction)
    commit += dt
    _, dt = timed(doc.resolveTransactionLog)
    drain += dt
    if (i + 1) % saveEvery == 0:
        _, dt = timed(doc.save)
        stats = storeStats(doc)
        stats.update(
            {
                "edit": i + 1,
                "save": dt,
                "file_bytes": os.path.getsize(path),
                "recompute": recompute,
                "commit": commit,
                "drain": drain,
            }
        )
        recompute = commit = drain = 0.0
        result["saves"].append(stats)
        stored = sum(e["stored"] for e in stats["entities"])
        print(
            "SCALE edit %d: save %.2fs, file %.1f MB, store %.1f MB + wal %.1f MB (entities %.1f MB),"
            " blobs %.1f MB, versions %d, ops %d"
            % (
                i + 1,
                dt,
                stats["file_bytes"] / 1e6,
                stats["db_bytes"] / 1e6,
                stats["wal_bytes"] / 1e6,
                stored / 1e6,
                stats["blob_dir_bytes"] / 1e6,
                stats["versions"],
                stats["ops"],
            )
        )

versions = sorted(v["num"] for v in doc.getTransactionVersions())
FreeCAD.closeDocument(doc.Name)

doc, t = timed(lambda: FreeCAD.openDocument(path))
result["reopen"] = t
_, t = timed(doc.compactFileState)
result["compact"] = t
oldest = versions[0]
v, t = timed(lambda: doc.openTransactionVersion(oldest, False))
result["open_oldest_version"] = t
FreeCAD.closeDocument(v.Name)
_, t = timed(lambda: doc.restoreTransactionVersion(oldest))
result["restore_oldest"] = t
print(
    "SCALE reopen %.2fs, compact %.3fs, open oldest version %.2fs, restore to it %.2fs"
    % (result["reopen"], result["compact"], result["open_oldest_version"], result["restore_oldest"])
)
FreeCAD.closeDocument(doc.Name)
if out:
    with open(out, "w") as f:
        json.dump(result, f, indent=1)
shutil.rmtree(work, ignore_errors=True)

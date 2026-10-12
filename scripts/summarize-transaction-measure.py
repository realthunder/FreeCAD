#!/usr/bin/env python3
"""Summarise the CSV written by App.startTransactionMeasure (scripts/measure-transactions.py).

    python3 scripts/summarize-transaction-measure.py txn-measure.csv [--top N]

Rows are grouped by the marker row that precedes them. For each group the
table gives what the log writer would have stored and what it would have
cost, and the per-transaction commit overhead. Byte columns count only
values not already seen in the session (content addressing), split by the
derived rule of docs/TransactionLog.md sec 10 so that the three policies
(none / cache / full) read straight off the same rows.
"""

import csv
import statistics
import sys


def num(s):
    if s in ("", None):
        return None
    try:
        return float(s)
    except ValueError:
        return None


def main():
    path = sys.argv[1]
    top = 8
    if "--top" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1])

    groups = []
    cur = None
    header = None
    with open(path, newline="") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if header is None:
                header = row
                continue
            kind = row[0]
            if kind == "mark":
                cur = {"label": row[3], "ops": [], "txns": []}
                groups.append(cur)
                continue
            if cur is None:
                cur = {"label": "(before first mark)", "ops": [], "txns": []}
                groups.append(cur)
            if kind == "txn":
                cur["txns"].append({"seq": row[1], "id": row[2], "name": row[3], "ops": int(row[4]),
                                    "new_values": int(row[5]), "new_bytes": int(row[6]),
                                    "t_us": float(row[7])})
            elif kind == "op":
                cur["ops"].append(dict(zip(header, row)))

    def fmt_bytes(n):
        if n is None:
            return "-"
        if n >= 1 << 20:
            return "%.1fM" % (n / (1 << 20))
        if n >= 1 << 10:
            return "%.1fK" % (n / (1 << 10))
        return "%d" % n

    cols = ["scenario", "txns", "ops", "noop", "new vals", "input B", "derived B",
            "zlib", "zstd3", "zstd1", "patch", "cdc new", "t/txn med ms", "t/txn max ms",
            "ser ms", "hash ms", "zlib ms", "zstd3 ms", "patch ms"]
    rows = []
    biggest = []
    for g in groups:
        ops = g["ops"]
        txns = g["txns"]
        if not ops and not txns:
            continue
        noop = 0
        input_b = 0
        derived_b = 0
        raw_c = 0
        zlib_c = 0
        zstd3_c = 0
        zstd1_c = 0
        patch_raw = 0
        patch_c = 0
        cdc_raw = 0
        cdc_new = 0
        t_ser = t_hash = t_zlib = t_zstd3 = t_patch = 0.0
        for op in ops:
            if op["op"] == "set" and op["before_hash"] == op["after_hash"]:
                noop += 1
                continue
            derived = op["derived"] == "1"
            for side in ("before", "after"):
                b = num(op[side + "_bytes"])
                seen = op[side + "_seen"] == "1"
                if b is None or seen or not op[side + "_hash"]:
                    continue
                if derived:
                    derived_b += b
                else:
                    input_b += b
                biggest.append((b, g["label"], op["op"], op["object"], op["prop"], op["proptype"],
                                derived, side))
            t_ser += num(op["t_ser_us"]) or 0
            t_hash += num(op["t_hash_us"]) or 0
            z = num(op["zlib_bytes"])
            if z is not None:
                stored = num(op["after_bytes"]) if op["after_hash"] else num(op["before_bytes"])
                raw_c += stored
                zlib_c += z
                zstd3_c += num(op["zstd3_bytes"]) or 0
                zstd1_c += num(op["zstd1_bytes"]) or 0
                t_zlib += num(op["t_zlib_us"]) or 0
                t_zstd3 += num(op["t_zstd3_us"]) or 0
            p = num(op["patch_bytes"])
            if p is not None:
                patch_raw += num(op["after_bytes"])
                patch_c += p
                t_patch += num(op["t_patch_us"]) or 0
            cn = num(op["cdc_new_bytes"])
            if cn is not None:
                cdc_raw += num(op["after_bytes"])
                cdc_new += cn

        def ratio(c, r):
            return "-" if not r else "%.2f" % (c / r)

        t_list = [t["t_us"] / 1000.0 for t in txns]
        rows.append([
            g["label"], len(txns), len(ops), noop,
            sum(t["new_values"] for t in txns),
            fmt_bytes(input_b), fmt_bytes(derived_b),
            ratio(zlib_c, raw_c), ratio(zstd3_c, raw_c), ratio(zstd1_c, raw_c),
            ratio(patch_c, patch_raw), ratio(cdc_new, cdc_raw),
            "%.2f" % statistics.median(t_list) if t_list else "-",
            "%.2f" % max(t_list) if t_list else "-",
            "%.1f" % (t_ser / 1000), "%.1f" % (t_hash / 1000), "%.1f" % (t_zlib / 1000),
            "%.1f" % (t_zstd3 / 1000), "%.1f" % (t_patch / 1000),
        ])

    widths = [max(len(str(r[i])) for r in [cols] + rows) for i in range(len(cols))]
    line = "| " + " | ".join(str(c).ljust(widths[i]) for i, c in enumerate(cols)) + " |"
    print(line)
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for r in rows:
        print("| " + " | ".join(str(c).ljust(widths[i]) for i, c in enumerate(r)) + " |")

    print()
    print("Largest values stored (bytes, scenario, op, object, property, type, derived, side):")
    biggest.sort(reverse=True)
    for b, label, op, obj, prop, ptype, derived, side in biggest[:top]:
        kind = "derived" if derived else "input"
        print(
            "  %10s  %-22s %-7s %-14s %-12s %-28s %s %s"
            % (fmt_bytes(b), label, op, obj, prop, ptype, kind, side)
        )

    print()
    print("Columns: noop = set ops whose before and after hash the same (dropped by the writer);")
    print("input B / derived B = bytes of values not seen before, by the derived rule (policy none")
    print("stores input only, cache/full store both); zlib/zstd/patch/cdc = compressed size")
    print("over raw")
    print("for the stored values (patch = after compressed with before as zstd prefix; cdc = bytes")
    print("of 4 KB content-defined chunks of after absent from before); t/txn = the measurement")
    print("walk")
    print("per commit, which is serialise + hash + compress + delta, i.e. an upper bound on the")
    print("synchronous cost of a log write before any store I/O.")


if __name__ == "__main__":
    main()

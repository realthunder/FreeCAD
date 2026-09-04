#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Pack a pure-Python wheel for the expression sandbox's pyodide guest
(docs/Sandbox.md sec 5.6).

A workbench's App side -- the Python that runs in an object's execute()
and never touches Qt -- goes into the guest UNMODIFIED as a wheel that
boots with the guest (a "bundled" wheel under <datadir>/Pyodide/wheels).
This script builds such a wheel from source files listed by CMake, plus
whatever they import that the host has and the guest does not: the
vendored lazy_loader, freecad.deprecation, and the guest shims under
src/App/ExpressionImage/shims (PySide's three names, the resource
modules).

    packSandboxWheel.py --name fcx_draft --version 0.22.0 --out-dir DIR
        --root src/Mod/Draft [--exclude REL ...] [--add SRC=DEST ...]
        REL ...

  REL       a file relative to --root, stored under the same path
  --exclude a REL to leave out (a module the guest gets as a facade)
  --add     SRC=DEST: a file or directory outside --root, stored at
            DEST (a directory is walked for its .py files; DEST "."
            merges at the root)
  --add-data SRC=DEST: the same keeping every file (presets, JSON)

The wheel is deterministic: entries sorted, timestamps fixed, so a
rebuild that changes nothing leaves a byte-identical file and CMake's
copy_if_different holds.  Only python3 is needed; no setuptools.
"""

import argparse
import base64
import hashlib
import io
import os
import sys
import zipfile

WHEEL_TAG = "py3-none-any"
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def record_hash(data):
    digest = hashlib.sha256(data).digest()
    return "sha256=" + base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")


def walk(src, dest, data=False):
    """(archive path, file path) pairs for a file or a directory: the
    Python files of a directory, or every file with `data`."""
    if os.path.isfile(src):
        yield dest, src
        return
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = sorted(d for d in dirnames if d != "__pycache__")
        for fn in sorted(filenames):
            if not data and not fn.endswith(".py"):
                continue
            rel = os.path.relpath(os.path.join(dirpath, fn), src)
            arc = rel if dest in ("", ".") else dest + "/" + rel
            yield arc.replace(os.sep, "/"), os.path.join(dirpath, fn)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True, help="distribution name, e.g. fcx_draft")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--root", required=True, help="the directory the REL files are relative to")
    ap.add_argument("--exclude", action="append", default=[], metavar="REL")
    ap.add_argument("--add", action="append", default=[], metavar="SRC=DEST")
    ap.add_argument("--add-data", action="append", default=[], metavar="SRC=DEST",
                    help="like --add, keeping every file of a directory (presets, JSON)")
    ap.add_argument("--summary", metavar="FILE",
                    help="write the archive member list there (the test reads it)")
    ap.add_argument("files", nargs="*", metavar="REL")
    args = ap.parse_args()

    excluded = {e.replace(os.sep, "/") for e in args.exclude}
    members = {}
    for rel in args.files:
        arc = rel.replace(os.sep, "/")
        if arc in excluded or not arc.endswith(".py"):
            continue
        members[arc] = os.path.join(args.root, rel)
    for spec, data in [(a, False) for a in args.add] + [(a, True) for a in args.add_data]:
        if "=" not in spec:
            sys.exit("--add wants SRC=DEST, got %r" % spec)
        src, dest = spec.split("=", 1)
        if not os.path.exists(src):
            sys.exit("--add: no such path %s" % src)
        for arc, path in walk(src, dest.replace(os.sep, "/").strip("/"), data):
            if arc in excluded:
                continue
            members[arc] = path
    if not members:
        sys.exit("packSandboxWheel: nothing to pack")

    dist_info = "%s-%s.dist-info" % (args.name, args.version)
    metadata = (
        "Metadata-Version: 2.1\n"
        "Name: %s\n"
        "Version: %s\n"
        "Summary: %s, packed for the FreeCAD expression sandbox guest\n"
        "Requires-Python: >=3.12\n" % (args.name, args.version, args.name)
    )
    wheel_meta = (
        "Wheel-Version: 1.0\n"
        "Generator: packSandboxWheel.py\n"
        "Root-Is-Purelib: true\n"
        "Tag: %s\n" % WHEEL_TAG
    )

    entries = []  # (arc, bytes)
    for arc in sorted(members):
        with open(members[arc], "rb") as fp:
            entries.append((arc, fp.read()))
    entries.append((dist_info + "/METADATA", metadata.encode("utf-8")))
    entries.append((dist_info + "/WHEEL", wheel_meta.encode("utf-8")))
    record = io.StringIO()
    for arc, data in entries:
        record.write("%s,%s,%d\n" % (arc, record_hash(data), len(data)))
    record.write("%s/RECORD,,\n" % dist_info)
    entries.append((dist_info + "/RECORD", record.getvalue().encode("utf-8")))

    os.makedirs(args.out_dir, exist_ok=True)
    wheel_name = "%s-%s-%s.whl" % (args.name, args.version, WHEEL_TAG)
    out = os.path.join(args.out_dir, wheel_name)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for arc, data in entries:
            info = zipfile.ZipInfo(arc, date_time=FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, data)
    data = buf.getvalue()
    current = None
    if os.path.exists(out):
        with open(out, "rb") as fp:
            current = fp.read()
    if current != data:
        tmp = out + ".tmp"
        with open(tmp, "wb") as fp:
            fp.write(data)
        os.replace(tmp, out)
    if args.summary:
        with open(args.summary, "w") as fp:
            fp.write("\n".join(arc for arc, _ in entries) + "\n")
    print("packSandboxWheel: %s, %d files, %d bytes" % (wheel_name, len(members), len(data)))


if __name__ == "__main__":
    main()

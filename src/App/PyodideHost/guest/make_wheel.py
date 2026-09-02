#!/usr/bin/env python3
"""Wrap wasm32-emscripten extension modules into a wheel pyodide can load.

pyodide's loadPackage() takes a wheel path (read here through the host's
scoped reader), unpacks it into site-packages and links every .so in it
as an emscripten side module.  A wheel is a zip with a dist-info
directory; this writes exactly that, with the tag pyodide expects for the
ABI it was built with.

    make_wheel.py --name fcx_image --version 0.1 --out dist/ \
        --abi cp314 --platform pyodide_2026_0_wasm32 file.so [pkg/...]

Every positional argument is copied into the wheel root: a .so lands as a
top-level extension module, a directory as a package.
"""
import argparse
import base64
import hashlib
import os
import sys
import zipfile


def record_line(name, data):
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    return f"{name},sha256={digest},{len(data)}\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, help="directory to write the wheel into")
    ap.add_argument("--abi", default="cp314")
    ap.add_argument("--platform", default="pyodide_2026_0_wasm32")
    ap.add_argument("--summary", default="")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()

    tag = f"{args.abi}-{args.abi}-{args.platform}"
    wheel_name = f"{args.name}-{args.version}-{tag}.whl"
    dist_info = f"{args.name}-{args.version}.dist-info"
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, wheel_name)

    entries = []  # (arcname, bytes)
    for f in args.files:
        if os.path.isdir(f):
            base = os.path.basename(f.rstrip("/"))
            for root, _dirs, files in os.walk(f):
                for fn in files:
                    if fn.endswith(".pyc"):
                        continue
                    full = os.path.join(root, fn)
                    arc = os.path.join(base, os.path.relpath(full, f))
                    entries.append((arc, open(full, "rb").read()))
        else:
            entries.append((os.path.basename(f), open(f, "rb").read()))

    metadata = (
        "Metadata-Version: 2.1\n"
        f"Name: {args.name}\n"
        f"Version: {args.version}\n"
        f"Summary: {args.summary}\n"
    ).encode()
    wheel = (
        "Wheel-Version: 1.0\n"
        "Generator: make_wheel.py\n"
        "Root-Is-Purelib: false\n"
        f"Tag: {tag}\n"
    ).encode()
    entries.append((f"{dist_info}/METADATA", metadata))
    entries.append((f"{dist_info}/WHEEL", wheel))
    entries.append((f"{dist_info}/top_level.txt",
                    "\n".join(sorted({e[0].split("/")[0].split(".")[0] for e in entries
                                      if not e[0].startswith(dist_info)})).encode() + b"\n"))
    record = "".join(record_line(n, d) for n, d in entries) + f"{dist_info}/RECORD,,\n"

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for n, d in entries:
            z.writestr(n, d)
        z.writestr(f"{dist_info}/RECORD", record)
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())

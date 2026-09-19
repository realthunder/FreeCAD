#!/usr/bin/env python3
"""Pack the sandbox image for shipping (docs/ExpressionImage.md).

A DEVELOPMENT box hands wasmtime the image as built and preopens the whole
CPython Lib directory.  Neither is shippable: the image carries ~24 MB of
DWARF that only a native debugger attached to wasmtime reads, and the stdlib
directory is 51 MB of which the image opens sixteen files.  This tool
produces the two bundles that ARE shipped, from one measured slice:

  --out          the browser bundle, fetched over HTTP
                   fcx_image.min.wasm   stripped image
                   lib/...              the CPython files the image opens
                   fcx.json             the manifest the browser host reads
  --desktop-out  the desktop bundle, installed as <datadir>/Fcx
                   fcx_image.wasm       the same stripped image
                   Lib/...              the same files, under the name the
                                        host preopens them as

Stripping is done here rather than with llvm-objcopy/wasm-opt so the packer
has no toolchain dependency beyond python3, and because the operation is
exactly "drop custom sections", which is four lines of the binary format.

The stdlib slice is EXPLICIT rather than discovered.  What the image needs was
measured by asking a running image for the __file__ of everything in
sys.modules after exercising every Ring 0 pseudo-module: almost the whole
stdlib is frozen into the interpreter, and only these files are read from the
preopen.  A list can go stale, so it is not trusted on faith -- the browser
acceptance page (web/public/sandbox-test.html) evaluates through every
pseudo-module, and a missing file fails it loudly.
"""

import argparse
import json
import os
import shutil
import sys

# Measured, not guessed: see the module docstring.  encodings/* is what
# CPython's startup needs; the rest is the import closure of the `re` and
# `collections` modules behind the _re / _coll pseudo-modules.
STDLIB_SLICE = [
    "encodings/__init__.py",
    "encodings/aliases.py",
    "encodings/ascii.py",
    "copyreg.py",
    "enum.py",
    "functools.py",
    "keyword.py",
    "operator.py",
    "reprlib.py",
    "types.py",
    "collections/__init__.py",
    "re/__init__.py",
    "re/_casefix.py",
    "re/_compiler.py",
    "re/_constants.py",
    "re/_parser.py",
]

# Custom sections the browser has no use for.  "name" only feeds developer
# tooling; the DWARF is for a native debugger attached to wasmtime.
DROP_SECTIONS = (".debug", "name")


def _uleb(data, i):
    result = 0
    shift = 0
    while True:
        byte = data[i]
        i += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, i
        shift += 7


def strip(wasm):
    """Return `wasm` without the debug and name custom sections."""
    if wasm[:4] != b"\0asm":
        raise SystemExit("not a wasm module")
    out = bytearray(wasm[:8])
    i = 8
    dropped = []
    while i < len(wasm):
        start = i
        section_id = wasm[i]
        i += 1
        size, i = _uleb(wasm, i)
        body = i
        i += size
        if section_id == 0:
            n, j = _uleb(wasm, body)
            name = wasm[j:j + n].decode("utf8", "replace")
            if name.startswith(".debug") or name == "name":
                dropped.append((name, size))
                continue
        out += wasm[start:i]
    return bytes(out), dropped


def copy_slice(stdlib, lib_out):
    """Copy the measured stdlib slice into `lib_out`; return its byte size."""
    if os.path.isdir(lib_out):
        shutil.rmtree(lib_out)
    lib_bytes = 0
    for rel in STDLIB_SLICE:
        src = os.path.join(stdlib, rel)
        if not os.path.isfile(src):
            raise SystemExit("stdlib slice is stale: %s not in %s"
                             % (rel, stdlib))
        dst = os.path.join(lib_out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        lib_bytes += os.path.getsize(dst)
    return lib_bytes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", required=True, help="fcx_image.wasm as built")
    ap.add_argument("--stdlib", required=True, help="CPython Lib directory")
    ap.add_argument("--out", help="browser bundle directory to write")
    ap.add_argument("--desktop-out", help="desktop bundle directory to write")
    args = ap.parse_args()
    if not args.out and not args.desktop_out:
        raise SystemExit("nothing to do: pass --out and/or --desktop-out")

    with open(args.image, "rb") as f:
        wasm = f.read()
    stripped, dropped = strip(wasm)

    lib_bytes = 0
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        image_name = "fcx_image.min.wasm"
        with open(os.path.join(args.out, image_name), "wb") as f:
            f.write(stripped)
        lib_bytes = copy_slice(args.stdlib, os.path.join(args.out, "lib"))
        manifest = {
            "image": image_name,
            "libDir": "lib",
            "lib": STDLIB_SLICE,
            "imageBytes": len(stripped),
            "libBytes": lib_bytes,
        }
        with open(os.path.join(args.out, "fcx.json"), "w") as f:
            json.dump(manifest, f, indent=1)

    if args.desktop_out:
        # The host preopens this directory as /Lib and reads the image by
        # the name it was built under, so the desktop bundle keeps both
        # names -- an installed tree is then a drop-in for a build tree.
        os.makedirs(args.desktop_out, exist_ok=True)
        with open(os.path.join(args.desktop_out, "fcx_image.wasm"), "wb") as f:
            f.write(stripped)
        lib_bytes = copy_slice(args.stdlib,
                               os.path.join(args.desktop_out, "Lib"))

    print("image %.1f MB -> %.1f MB (dropped %s), stdlib %d files %.0f KB"
          % (len(wasm) / 1e6, len(stripped) / 1e6,
             ", ".join(n for n, _ in dropped),
             len(STDLIB_SLICE), lib_bytes / 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())

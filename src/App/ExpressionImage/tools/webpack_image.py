#!/usr/bin/env python3
"""Pack the sandbox image for the browser tier (docs/ExpressionImage.md).

The desktop host hands wasmtime the image as built and preopens the whole
CPython Lib directory.  A browser cannot do either cheaply: the image carries
~28 MB of DWARF that no browser reads, and a 51 MB stdlib is not a download.
This tool produces the browser bundle instead:

  fcx_image.min.wasm   the image with .debug_* and the name section dropped
  lib/...              the CPython files the image actually opens
  fcx.json             the manifest the browser host fetches first

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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", required=True, help="fcx_image.wasm as built")
    ap.add_argument("--stdlib", required=True, help="CPython Lib directory")
    ap.add_argument("--out", required=True, help="bundle directory to write")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    lib_out = os.path.join(args.out, "lib")
    if os.path.isdir(lib_out):
        shutil.rmtree(lib_out)

    with open(args.image, "rb") as f:
        wasm = f.read()
    stripped, dropped = strip(wasm)
    image_name = "fcx_image.min.wasm"
    with open(os.path.join(args.out, image_name), "wb") as f:
        f.write(stripped)

    lib_bytes = 0
    for rel in STDLIB_SLICE:
        src = os.path.join(args.stdlib, rel)
        if not os.path.isfile(src):
            raise SystemExit("stdlib slice is stale: %s not in %s"
                             % (rel, args.stdlib))
        dst = os.path.join(lib_out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        lib_bytes += os.path.getsize(dst)

    manifest = {
        "image": image_name,
        "libDir": "lib",
        "lib": STDLIB_SLICE,
        "imageBytes": len(stripped),
        "libBytes": lib_bytes,
    }
    with open(os.path.join(args.out, "fcx.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    print("image %.1f MB -> %.1f MB (dropped %s), stdlib %d files %.0f KB"
          % (len(wasm) / 1e6, len(stripped) / 1e6,
             ", ".join(n for n, _ in dropped),
             len(STDLIB_SLICE), lib_bytes / 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())

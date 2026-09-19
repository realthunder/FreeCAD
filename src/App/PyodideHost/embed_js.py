#!/usr/bin/env python3
"""Embed a text file into a C++ translation unit as a byte array.

    embed_js.py <input> <output.inc> <identifier>

The pyodide runtime compiles its JavaScript (the Web shim and the glue)
INTO libFreeCADApp rather than reading it from the data directory: the
shim is the security policy, and a policy that can be edited beside the
program is not one.  A byte array rather than a string literal so the
content is never subject to escaping, trigraphs or a delimiter clash.
"""
import sys


def main():
    src, out, ident = sys.argv[1:4]
    data = open(src, "rb").read()
    lines = [f"// generated from {src} by embed_js.py; do not edit",
             f"static const unsigned char {ident}[] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("    0x00")
    lines.append("};")
    lines.append(f"static const unsigned long {ident}_len = {len(data)}UL;")
    open(out, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

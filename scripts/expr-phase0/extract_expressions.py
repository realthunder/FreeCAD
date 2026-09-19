#!/usr/bin/env python3
"""Extract every stored expression from .FCStd files under given roots.

A .FCStd is a zip; expressions appear in Document.xml (and GuiDocument.xml):
  - <Expression path="..." expression="..."/>  (PropertyExpressionEngine)
  - <Cell address="..." content="=..." .../>   (Spreadsheet cells)
  - alias/other attributes are ignored.
Output: JSONL lines {file, src, kind, path, expr}
"""
import sys, os, json, zipfile, re
import xml.etree.ElementTree as ET

roots = sys.argv[1:] or [os.path.expanduser("~/works")]
out = []
seen_files = 0
bad = []

def scan_xml(data, fname, member):
    res = []
    # tolerate malformed xml via regex fallback
    try:
        root = ET.fromstring(data)
        it = root.iter()
    except ET.ParseError:
        it = None
    if it is not None:
        for el in it:
            if el.tag == "Expression" and "expression" in el.attrib:
                res.append(("engine", el.attrib.get("path", ""), el.attrib["expression"]))
            elif el.tag == "Cell":
                c = el.attrib.get("content", "")
                if c.startswith("="):
                    res.append(("cell", el.attrib.get("address", ""), c[1:]))
                a = el.attrib.get("alias")
    else:
        for m in re.finditer(r'<Expression\s[^>]*expression="([^"]*)"', data):
            res.append(("engine", "", m.group(1)))
        for m in re.finditer(r'<Cell\s[^>]*content="=([^"]*)"', data):
            res.append(("cell", "", m.group(1)))
    return res

for rootdir in roots:
    for dirpath, dirnames, filenames in os.walk(rootdir):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "install", "node_modules", ".conda")]
        for fn in filenames:
            if not fn.endswith(".FCStd"):
                continue
            full = os.path.join(dirpath, fn)
            seen_files += 1
            try:
                with zipfile.ZipFile(full) as z:
                    for member in z.namelist():
                        if not member.endswith(".xml"):
                            continue
                        try:
                            data = z.read(member).decode("utf-8", "replace")
                        except Exception as e:
                            continue
                        if "<Expression" not in data and "content=\"=" not in data:
                            continue
                        for kind, path, expr in scan_xml(data, full, member):
                            out.append({"file": full, "src": member, "kind": kind,
                                        "path": path, "expr": expr})
            except Exception as e:
                bad.append((full, str(e)))

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus.jsonl"), "w") as f:
    for rec in out:
        f.write(json.dumps(rec) + "\n")
print(f"files scanned: {seen_files}, expressions: {len(out)}, unreadable: {len(bad)}")
for b in bad[:10]:
    print("BAD:", b)
files = {}
for rec in out:
    files[rec["file"]] = files.get(rec["file"], 0) + 1
for f2, n in sorted(files.items(), key=lambda kv: -kv[1])[:20]:
    print(f"{n:6d}  {f2}")

#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Fetch the third-party pure-Python wheels the sandbox's forms bundle
(docs/Sandbox.md 7.3): ipywidgets from PyPI and traitlets from the
pyodide distribution, pinned by version and sha256, into a directory
that FREECAD_BUNDLED_WHEELS then names:

    python3 scripts/sandbox-fetch-wheels.py build/sandbox-wheels
    cmake -DFREECAD_BUNDLED_WHEELS="<dir>/ipywidgets-8.1.9-py3-none-any.whl;\
<dir>/traitlets-5.14.3-py3-none-any.whl" ...

(the script prints the exact value on its last line)

Needs python3 only.  A wheel already present with the right hash is
left alone; a wrong hash is an error, never a silent overwrite.  The
traitlets pin is the pyodide 314.0.6 lock's (the same file the package
set would install); ipywidgets is not in that lock, hence PyPI.  Neither
wheel's declared dependencies are fetched: ipywidgets imports comm and
IPython, both of which the fcx_widgets wheel carries as guest shims, and
widgetsnbextension/jupyterlab_widgets are browser assets it never
imports.
"""

import hashlib
import os
import sys
import urllib.request

PYODIDE_VERSION = "314.0.6"
PYODIDE_CDN = "https://cdn.jsdelivr.net/pyodide/v%s/full/" % PYODIDE_VERSION

WHEELS = [
    ("ipywidgets-8.1.9-py3-none-any.whl",
     "https://files.pythonhosted.org/packages/py3/i/ipywidgets/ipywidgets-8.1.9-py3-none-any.whl",
     "f2b8cbcaae10252b809fbe4d7470db75c09b769a32cbf816d20e5ca6d3c5a79d"),
    ("traitlets-5.14.3-py3-none-any.whl",
     PYODIDE_CDN + "traitlets-5.14.3-py3-none-any.whl",
     None),  # filled from the lock below
]


def lock_sha256(name):
    """The sha256 the pyodide lock pins for `file_name`, from the CDN."""
    import json

    with urllib.request.urlopen(PYODIDE_CDN + "pyodide-lock.json", timeout=60) as r:
        lock = json.load(r)
    for pkg in lock["packages"].values():
        if pkg["file_name"] == name:
            return pkg["sha256"]
    raise SystemExit("%s is not in the pyodide %s lock" % (name, PYODIDE_VERSION))


def main(argv):
    if len(argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    out = argv[1]
    os.makedirs(out, exist_ok=True)
    paths = []
    for name, url, sha in WHEELS:
        if sha is None:
            sha = lock_sha256(name)
        path = os.path.join(out, name)
        if os.path.exists(path):
            have = hashlib.sha256(open(path, "rb").read()).hexdigest()
            if have == sha:
                print("kept   ", path)
                paths.append(path)
                continue
            raise SystemExit("%s exists with sha256 %s, expected %s" % (path, have, sha))
        with urllib.request.urlopen(url, timeout=120) as r:
            data = r.read()
        have = hashlib.sha256(data).hexdigest()
        if have != sha:
            raise SystemExit("%s: sha256 %s, expected %s" % (url, have, sha))
        tmp = path + ".part"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
        print("fetched", path, len(data), "bytes")
        paths.append(path)
    print("FREECAD_BUNDLED_WHEELS=" + ";".join(os.path.abspath(p) for p in paths))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

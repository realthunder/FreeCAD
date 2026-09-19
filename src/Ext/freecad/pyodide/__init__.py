# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Project Association                        *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""The pyodide bootstrap: installing the sandbox runtime and its packages.

FreeCAD's expression sandbox runs Python inside pyodide (CPython built
for WebAssembly) on a bare V8; see docs/PyodideHost.md.  The runtime is
not part of the FreeCAD package: this module fetches a pinned release
into the user's data directory on an explicit request, verifies every
file against the sha256 table compiled into the binary, and keeps the
user's chosen packages beside it with a manifest the runtime loads at
boot (docs/PyodideHost.md sec 12, docs/SandboxNetwork.md sec 9).

This is HOST infrastructure, not sandboxed code: it runs on the native
interpreter, and when a GUI is up its downloads go through the Addon
Manager's network manager (proxies, certificates), else through urllib.
Nothing here ever runs at startup; every entry point is a user action
(the status-bar padlock, the permissions panel, or a call from the
console).

    import freecad.pyodide as p
    p.install_runtime()              # the newest pinned version, from GitHub
    p.install_runtime("314.0.6", source="jsdelivr")
    p.install_runtime(source="/path/to/pyodide-core-314.0.6.tar.bz2")
    p.install_package("numpy")       # from the pyodide index, per the lock
    p.install_package("scipy", source="/path/to/mirror")   # air-gapped
    p.list_runtimes(), p.list_packages(), p.remove_package("scipy")
"""

import hashlib
import json
import os
import re
import shutil
import tarfile
import tempfile
import time

import FreeCAD

__all__ = [
    "releases",
    "layout",
    "default_version",
    "install_runtime",
    "list_runtimes",
    "remove_runtime",
    "set_current",
    "install_package",
    "remove_package",
    "list_packages",
    "manifest",
    "RuntimeUnsupported",
    "DownloadError",
    "VerificationError",
]

GITHUB_RELEASE = "https://github.com/pyodide/pyodide/releases/download/{version}/{asset}"
JSDELIVR_NPM = "https://cdn.jsdelivr.net/npm/pyodide@{version}/{name}"
# pyodide's own package CDN, the one its loader defaults to
PYODIDE_CDN = "https://cdn.jsdelivr.net/pyodide/v{version}/full/{name}"

_PACKAGE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
_MANIFEST_VERSION = 1


class RuntimeUnsupported(RuntimeError):
    """A pyodide version this FreeCAD was not built to run."""


class DownloadError(RuntimeError):
    """A fetch that did not complete."""


class VerificationError(RuntimeError):
    """A file whose sha256 is not the pinned one."""


# ---------------------------------------------------------------- facts


def _sandbox():
    return FreeCAD.ExpressionSandbox


def releases():
    """The pyodide versions this binary agrees to run: a dict
    version -> {abi, python, files: {name: sha256}, core_tarball,
    core_sha256}."""
    return {r["version"]: r for r in _sandbox().pyodideReleases()}


def default_version():
    """The newest pinned version (numeric order of the version parts)."""
    versions = list(releases())
    if not versions:
        raise RuntimeUnsupported("this FreeCAD was built without the pyodide host")
    return max(versions, key=lambda v: [int(x) for x in v.split(".")])


def layout():
    """Where the bootstrap keeps things (FreeCAD.ExpressionSandbox.pyodideLayout)."""
    return _sandbox().pyodideLayout()


def _release(version):
    rel = releases().get(version)
    if rel is None:
        raise RuntimeUnsupported(
            "pyodide %s is not a version this FreeCAD supports (pinned: %s)"
            % (version, ", ".join(sorted(releases())) or "none")
        )
    return rel


# ------------------------------------------------------------ progress


class _Progress:
    """Base.ProgressIndicator when there is one to show (the status bar
    in the GUI, silence headless); a user cancel raises FreeCADAbort
    through it, which unwinds the install cleanly."""

    def __init__(self, callback=None):
        self.callback = callback
        self.indicator = None
        self.text = ""

    def start(self, text, steps):
        self.text = text
        if self.callback:
            self.callback(text, 0, steps)
            return
        try:
            self.indicator = FreeCAD.Base.ProgressIndicator()
            self.indicator.start(text, steps)
        except Exception:
            self.indicator = None

    def step(self, done=None, total=None):
        if self.callback:
            self.callback(self.text, done, total)
        elif self.indicator:
            self.indicator.next()

    def stop(self):
        if self.indicator:
            try:
                self.indicator.stop()
            except Exception:
                pass
            self.indicator = None


# ------------------------------------------------------------- fetching


def _fetch(url, progress=None):
    """GET a URL into memory.  With a GUI up the Addon Manager's network
    manager does it (its proxy and certificate handling apply); else
    urllib.  Raises DownloadError."""
    FreeCAD.Console.PrintLog("pyodide bootstrap: fetching %s\n" % url)
    if FreeCAD.GuiUp:
        try:
            import NetworkManager

            if NetworkManager.AM_NETWORK_MANAGER is None:
                NetworkManager.InitializeNetworkManager()
            data = NetworkManager.AM_NETWORK_MANAGER.blocking_get(url)
            if data is None:
                raise DownloadError("download failed: %s" % url)
            return bytes(data)
        except ImportError:
            pass
    import urllib.request
    import urllib.error

    try:
        req = urllib.request.Request(url, headers={"User-Agent": "FreeCAD pyodide bootstrap"})
        with urllib.request.urlopen(req, timeout=120) as reply:
            total = reply.headers.get("Content-Length")
            total = int(total) if total else None
            chunks = []
            done = 0
            while True:
                chunk = reply.read(1 << 18)
                if not chunk:
                    break
                chunks.append(chunk)
                done += len(chunk)
                if progress:
                    progress.step(done, total)
            return b"".join(chunks)
    except (urllib.error.URLError, OSError) as e:
        raise DownloadError("download failed: %s (%s)" % (url, e)) from e


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _verify(name, data, expected):
    got = _sha256(data)
    if got != expected:
        raise VerificationError(
            "%s: sha256 %s does not match the pinned %s" % (name, got, expected)
        )


def _write_atomic(path, data):
    """Write bytes beside the target and rename into place."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".part-", dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(data)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


# ------------------------------------------------------------ runtimes


def _runtime_dir(version):
    return os.path.join(layout()["user_dir"], version)


def _files_from_tarball(path, rel):
    """The pinned files (plus package.json) out of a pyodide-core tarball.
    Members are read by name; nothing else in the archive is touched."""
    wanted = set(rel["files"]) | {"package.json"}
    found = {}
    with tarfile.open(path, "r:bz2") as tar:
        for member in tar:
            if not member.isfile():
                continue
            base = os.path.basename(member.name)
            if base in wanted and member.name.replace("\\", "/").count("/") == 1:
                found[base] = tar.extractfile(member).read()
    missing = wanted - set(found)
    if missing:
        raise VerificationError(
            "%s lacks %s" % (os.path.basename(path), ", ".join(sorted(missing)))
        )
    return found


def _files_from_directory(path, rel):
    wanted = set(rel["files"]) | {"package.json"}
    found = {}
    for name in wanted:
        p = os.path.join(path, name)
        if not os.path.isfile(p):
            raise VerificationError("%s lacks %s" % (path, name))
        with open(p, "rb") as f:
            found[name] = f.read()
    return found


def install_runtime(version=None, source="github", progress=None, make_current=True):
    """Install a pinned pyodide runtime under the user's data directory.

    version: a pinned version (default: the newest).
    source: "github" (the release's pyodide-core tarball, ~7 MB),
            "jsdelivr" (the npm package, file by file), or a LOCAL path
            to such a tarball or to a directory holding the files
            (air-gapped boxes).
    progress: optional callable(text, done, total).

    Every file is verified against the table compiled into FreeCAD
    before anything is moved into place; the move is one rename.  The
    fcx_image wheel FreeCAD ships must match the runtime's ABI, or the
    install is refused up front.  Returns the runtime directory.
    """
    version = version or default_version()
    rel = _release(version)
    wheels = layout()["wheels"]
    if rel["abi"] not in wheels:
        raise RuntimeUnsupported(
            "no fcx_image wheel for pyodide ABI %s (have: %s); this FreeCAD cannot "
            "run pyodide %s" % (rel["abi"], ", ".join(sorted(wheels)) or "none", version)
        )

    prog = _Progress(progress)
    files = {}
    try:
        if source == "github":
            asset = rel["core_tarball"]
            url = GITHUB_RELEASE.format(version=version, asset=asset)
            prog.start("Downloading %s" % asset, 1)
            data = _fetch(url, prog)
            _verify(asset, data, rel["core_sha256"])
            fd, tmp = tempfile.mkstemp(prefix="pyodide-core-", suffix=".tar.bz2")
            try:
                with os.fdopen(fd, "wb") as out:
                    out.write(data)
                files = _files_from_tarball(tmp, rel)
            finally:
                os.remove(tmp)
        elif source == "jsdelivr":
            names = list(rel["files"]) + ["package.json"]
            prog.start("Downloading pyodide %s" % version, len(names))
            for name in names:
                files[name] = _fetch(JSDELIVR_NPM.format(version=version, name=name))
                prog.step()
        elif os.path.isdir(source):
            files = _files_from_directory(source, rel)
        elif os.path.isfile(source):
            files = _files_from_tarball(source, rel)
        else:
            raise DownloadError("no such source: %s" % source)
    finally:
        prog.stop()

    for name, expected in rel["files"].items():
        _verify(name, files[name], expected)
    claimed = json.loads(files["package.json"].decode("utf-8")).get("version")
    if claimed != version:
        raise VerificationError(
            "package.json says pyodide %s, expected %s" % (claimed, version)
        )

    user_dir = layout()["user_dir"]
    os.makedirs(user_dir, exist_ok=True)
    staging = tempfile.mkdtemp(prefix=".staging-%s-" % version, dir=user_dir)
    try:
        for name, data in files.items():
            with open(os.path.join(staging, name), "wb") as out:
                out.write(data)
        target = _runtime_dir(version)
        if os.path.isdir(target):
            shutil.rmtree(target)
        os.replace(staging, target)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    # the binary's own check, on the files as they now sit
    reason = _sandbox().pyodideVerify(target)
    if reason:
        shutil.rmtree(target, ignore_errors=True)
        raise VerificationError(reason)
    if make_current:
        set_current(version)
    FreeCAD.Console.PrintLog("pyodide bootstrap: installed %s at %s\n" % (version, target))
    return target


def list_runtimes():
    """Installed versions, and which one is current: (versions, current)."""
    l = layout()
    return list(l["installed"]), l["current"]


def set_current(version):
    """Make an installed version the one the runtime boots; the live
    sandbox instance is dropped so the next evaluation uses it."""
    if version not in layout()["installed"]:
        raise RuntimeUnsupported("pyodide %s is not installed" % version)
    user_dir = layout()["user_dir"]
    _write_atomic(os.path.join(user_dir, "current"), (version + "\n").encode("ascii"))
    _sandbox().reset()


def remove_runtime(version):
    """Delete an installed version.  The `current` marker is cleared if
    it named it; packages are left alone (they are per ABI, not per
    version)."""
    l = layout()
    if version not in l["installed"]:
        raise RuntimeUnsupported("pyodide %s is not installed" % version)
    if l["current"] == version:
        _sandbox().reset()
        try:
            os.remove(os.path.join(l["user_dir"], "current"))
        except OSError:
            pass
    shutil.rmtree(os.path.join(l["user_dir"], version))


# ------------------------------------------------------------ packages


def _active_runtime():
    """The runtime directory the host resolves (docs/PyodideHost.md sec
    12 resolve order), and its version and ABI."""
    info = _sandbox().imageInfo()
    if info.get("runtime") != "pyodide":
        raise RuntimeUnsupported("the selected sandbox runtime is %r, not pyodide" % info.get("runtime"))
    runtime = info["stdlib"]
    if not os.path.isfile(os.path.join(runtime, "pyodide-lock.json")):
        raise RuntimeUnsupported(
            "no pyodide runtime is installed (looked at %s); install_runtime() first" % runtime
        )
    abi = _sandbox().pyodideAbi(runtime)
    with open(os.path.join(runtime, "package.json"), "rb") as f:
        version = json.load(f).get("version", "")
    return runtime, version, abi


def _lock(runtime):
    with open(os.path.join(runtime, "pyodide-lock.json"), "rb") as f:
        return json.load(f)


def manifest():
    """The user's package manifest as a dict (empty when none)."""
    path = layout()["manifest"]
    if not os.path.isfile(path):
        return {}
    with open(path, "rb") as f:
        try:
            return json.load(f)
        except ValueError:
            return {}


def _write_manifest(data):
    _write_atomic(layout()["manifest"], (json.dumps(data, indent=1, sort_keys=True) + "\n").encode("utf-8"))


def _resolve(lock, name, have, order, seen):
    """Depth-first over the lock's `depends`: dependencies before the
    package, packages already installed left out."""
    if name in seen:
        return
    seen.add(name)
    entry = lock["packages"].get(name)
    if entry is None:
        raise RuntimeUnsupported(
            "%s is not in the pyodide index of this runtime (PyPI packages are not supported yet)" % name
        )
    for dep in entry.get("depends", []):
        _resolve(lock, dep, have, order, seen)
    if name not in have:
        order.append(name)


def install_package(name, source="index", progress=None, requested_by="session"):
    """Install a package from the pyodide index into the user's set, with
    its dependencies, for the ACTIVE runtime.

    source: "index" (pyodide's CDN, per the lock file's hashes) or a
    LOCAL directory holding the wheels under their lock-file names.
    Returns the names installed (dependencies included), in load order.
    The live sandbox instance is dropped so the next evaluation boots
    with the package.  Nothing is fetched that the lock does not name,
    and nothing is kept whose sha256 is not the lock's.
    """
    if not _PACKAGE_NAME.match(name):
        raise ValueError("not a package name: %r" % name)
    runtime, version, abi = _active_runtime()
    lock = _lock(runtime)
    lock_abi = lock.get("info", {}).get("abi_version", abi)
    m = manifest()
    if m and m.get("abi") and m.get("abi") != lock_abi:
        FreeCAD.Console.PrintWarning(
            "pyodide bootstrap: the package set was installed for ABI %s, the runtime is %s; "
            "starting a new set\n" % (m.get("abi"), lock_abi)
        )
        m = {}
    packages = m.get("packages", {})
    order = []
    _resolve(lock, name, set(packages), order, set())
    if not order:
        return []

    pkg_dir = layout()["packages"]
    os.makedirs(pkg_dir, exist_ok=True)
    prog = _Progress(progress)
    prog.start("Installing %s for the sandbox" % name, len(order))
    try:
        for pkg in order:
            entry = lock["packages"][pkg]
            file_name = entry["file_name"]
            if source == "index":
                data = _fetch(PYODIDE_CDN.format(version=version, name=file_name))
            elif os.path.isdir(source):
                path = os.path.join(source, file_name)
                if not os.path.isfile(path):
                    raise DownloadError("%s is not in the mirror %s" % (file_name, source))
                with open(path, "rb") as f:
                    data = f.read()
            else:
                raise DownloadError("no such source: %s" % source)
            _verify(file_name, data, entry["sha256"])
            _write_atomic(os.path.join(pkg_dir, file_name), data)
            packages[pkg] = {
                "version": entry.get("version", ""),
                "file_name": file_name,
                "sha256": entry["sha256"],
                "depends": list(entry.get("depends", [])),
                "imports": list(entry.get("imports", [])),
                "package_type": entry.get("package_type", "package"),
                "source": source,
                "requested_by": requested_by if pkg == name else "dependency of %s" % name,
                "installed_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            }
            prog.step()
    finally:
        prog.stop()

    load_order = [p for p in m.get("load_order", []) if p in packages]
    load_order += [p for p in order if p not in load_order]
    _write_manifest(
        {
            "version": _MANIFEST_VERSION,
            "runtime": version,
            "abi": lock_abi,
            "packages": packages,
            "load_order": load_order,
        }
    )
    _sandbox().reset()
    # the requests this install satisfies, whoever made them
    security = FreeCAD.ExpressionSecurity
    for pkg in order:
        for req in security.pending():
            if req["permission"] == "pkg.install" and req["target"] == pkg:
                security.clearPending(req["principal"], "pkg.install", pkg)
    FreeCAD.Console.PrintLog(
        "pyodide bootstrap: installed %s into %s\n" % (", ".join(order), pkg_dir)
    )
    return order


def remove_package(name):
    """Remove one package (not its dependencies) from the set."""
    m = manifest()
    packages = m.get("packages", {})
    entry = packages.pop(name, None)
    if entry is None:
        raise RuntimeUnsupported("%s is not installed for the sandbox" % name)
    try:
        os.remove(os.path.join(layout()["packages"], entry["file_name"]))
    except OSError:
        pass
    m["packages"] = packages
    m["load_order"] = [p for p in m.get("load_order", []) if p != name]
    _write_manifest(m)
    _sandbox().reset()


def list_packages():
    """name -> version of every package in the set."""
    return {n: e.get("version", "") for n, e in manifest().get("packages", {}).items()}

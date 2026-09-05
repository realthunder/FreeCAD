/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef APP_EXPRESSION_PYODIDE_H
#define APP_EXPRESSION_PYODIDE_H

/* The pyodide runtime's bootstrap facts (docs/PyodideHost.md sec 12):
 * the pinned release table, the per-user layout the installer fills,
 * the path scoping the guest's reader is confined by, and the package
 * set the guest boots with.  The runtime (ExpressionPyodideRuntime.cpp)
 * reads these; the host Python installer (freecad.pyodide) gets them
 * through FreeCAD.ExpressionSandbox; the bridge answers `pkg.missing`
 * from them.  Host-only, internal to the App library, compiled with
 * BUILD_EXPR_PYODIDE_HOST.
 */

#include <optional>
#include <string>
#include <vector>

#include <FCConfig.h>

namespace App
{
namespace ExpressionSandbox
{
namespace Pyodide
{

/** One pyodide release this binary agrees to run.  The release has no
 * checksum asset and the npm files carry none, so the sha256 of every
 * runtime file is pinned HERE, per version; the table is therefore also
 * the supported-version list, widened only after the shim is re-audited
 * against the new release (docs/PyodideHost.md sec 5).  Package wheels
 * are verified against pyodide-lock.json instead, which does carry
 * hashes.
 */
struct Release
{
    std::string version;  ///< "314.0.6" -- the npm version, the release tag
    std::string abi;      ///< pyodide-lock.json info.abi_version, "2026_0"
    std::string python;   ///< the CPython inside, "3.14.2"
    struct File
    {
        std::string name;
        std::string sha256;
    };
    /// The runtime files, every one required; identical in the GitHub
    /// core tarball and on the npm CDN (verified 2026-09-03).
    std::vector<File> files;
    /// The GitHub release asset holding exactly those files (plus a few
    /// the installer ignores), and its sha256.
    std::string coreTarball;
    std::string coreSha256;
};

AppExport const std::vector<Release>& releases();
/// The pinned entry for a version, or nullptr when unsupported.
AppExport const Release* releaseFor(const std::string& version);

/** Where the bootstrap keeps things.  Runtimes: <userDir>/<version>/;
 * the user's chosen packages: <packages>/ with a manifest; the marker
 * <userDir>/current names the version the runtime boots.  The fcx_image
 * wheel is FreeCAD's own and ships with it under <datadir>/Pyodide/
 * wheels/ (one per ABI tag), never under the user directory.
 */
struct Layout
{
    std::string userDir;   ///< <user app data>/Pyodide (FCX_PYODIDE_USER / PyodideUserDir override)
    std::string packages;  ///< <userDir>/packages (FCX_PYODIDE_PACKAGES / PyodidePackages override)
    std::string manifest;  ///< <packages>/manifest.json
    std::string wheelDir;  ///< <datadir>/Pyodide/wheels
    std::string current;   ///< the version <userDir>/current names, if that runtime exists
    std::vector<std::string> installed;  ///< versions present under userDir
    /// ABI tag -> fcx_image wheel path, over every place a wheel may be
    /// (wheelDir, then <datadir>/Pyodide itself for a dev tree).
    std::vector<std::pair<std::string, std::string>> wheels;
    /// Bundled pure-Python wheels (`*-py3-none-any.whl` in the same two
    /// places): FreeCAD's own workbench code packed for the guest
    /// (fcx_draft, ...), loaded at boot after fcx_image and before the
    /// user's package set.  Sorted by file name.
    std::vector<std::string> bundled;
    /// Bundled COMPILED wheels (`*-cp3xx-cp3xx-pyodide_<abi>_wasm32.whl`
    /// other than fcx_image, in the same two places): ABI tag -> path;
    /// pivy (docs/Sandbox.md 7.10).  Loaded after the pure ones, only
    /// those of the running fcx_image's ABI.
    std::vector<std::pair<std::string, std::string>> bundledCompiled;
};
AppExport Layout layout();

/// The version a runtime directory claims (its package.json); empty
/// when there is none.
AppExport std::string directoryVersion(const std::string& dir);

/// The ABI tag of a runtime directory: from the pinned table by version,
/// else from its pyodide-lock.json; empty when neither answers.
AppExport std::string directoryAbi(const std::string& dir);

/// The fcx_image wheel for an ABI tag from `layout().wheels`, or empty.
AppExport std::string wheelForAbi(const std::string& abi);

/** Check a runtime directory against the pinned table: the version
 * must be pinned and every pinned file must be present with its
 * sha256.  Empty on success, else the reason (for the log and the
 * installer).  `pinned` reports whether the version was in the table
 * at all.
 */
AppExport std::string verifyDirectory(const std::string& dir, bool* pinned = nullptr);

/** Path scoping for the guest's reader and module loader.  `asked` is
 * what the guest named (a `file://` URL, an absolute path, or a
 * relative one); the result is its canonical form when it lies under
 * one of `roots` (each already canonical, no trailing separator), else
 * empty.  Symlinks and `..` cannot leave a root: the test is on the
 * canonical form, component by component (so a root never matches a
 * sibling that merely shares its prefix), case-insensitively on
 * Windows.  `file:///C:/x` is understood as `C:/x`.
 *
 * A relative name resolves against `base`, and when nothing is there,
 * against each of `fallbacks` in turn: pyodide's shell-mode loader
 * drops the base URL and names a lock-file wheel by its bare file name
 * (its resolvePath is the identity there), so the package set has to
 * be reachable that way.  Only existing entries are taken from a
 * fallback; the base form is the answer otherwise.
 */
AppExport std::string scopePath(const std::vector<std::string>& roots,
                                const std::string& asked,
                                const std::string& base,
                                const std::vector<std::string>& fallbacks = {});
/// The URL-to-path step of scopePath alone (tested by construction on
/// every platform): strips `file://` and the slash before a drive letter.
AppExport std::string askedToPath(const std::string& asked);

// ------------------------------------------------------------ packages

/// One package as pyodide-lock.json describes it.
struct PackageInfo
{
    std::string name;
    std::string version;
    std::string fileName;
    std::string sha256;
    std::string type;  ///< "package" | "shared_library" | ...
    std::vector<std::string> depends;
    std::vector<std::string> imports;  ///< top-level import names it provides
};

/// The lock entry providing an import name (top-level module), from the
/// lock at <runtimeDir>/pyodide-lock.json; nullopt when unknown there.
AppExport std::optional<PackageInfo> lookupImport(const std::string& runtimeDir,
                                                  const std::string& importName);
/// The lock entry of a package by name.
AppExport std::optional<PackageInfo> lookupPackage(const std::string& runtimeDir,
                                                   const std::string& name);

/** The packages the manifest says are installed, in load order
 * (dependencies first), for a runtime of ABI `abi`.  A manifest written
 * for another ABI yields nothing (and a log line): wheels are per ABI,
 * and the installer rewrites the manifest when the runtime changes.
 */
AppExport std::vector<std::string> manifestPackages(const std::string& packagesDir,
                                                    const std::string& abi);

/** The `pkg.missing` bridge answer for an import the guest could not
 * satisfy (docs/SandboxNetwork.md sec 9.3).  Empty = unknown: not in
 * the lock, so the ordinary ModuleNotFoundError follows.  Otherwise the
 * message the guest's finder raises: an OFFER (recorded as a pending
 * pkg.install request for the current principal, which the permissions
 * panel turns into the install) or INSTALLED (on disk but not in this
 * guest instance -- the host schedules a fresh instance, which loads it
 * at boot).
 */
AppExport std::string missingImport(const std::string& importName);

}  // namespace Pyodide
}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_PYODIDE_H

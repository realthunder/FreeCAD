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

#include "PreCompiled.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include <QByteArray>
#include <QCryptographicHash>

#include <nlohmann/json.hpp>

#include <Base/Console.h>

#include "Application.h"
#include "ExpressionImageHost.h"
#include "ExpressionPyodide.h"
#include "ExpressionSecurityRuntime.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

FC_LOG_LEVEL_INIT("ExpressionImage", true, true)

namespace App
{
namespace ExpressionSandbox
{
namespace Pyodide
{

namespace
{

std::string envPath(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : std::string();
}

ParameterGrp::handle prefs()
{
    return GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Expression/Sandbox");
}

bool readFile(const fs::path& p, std::string& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string sha256Of(const fs::path& p)
{
    std::string data;
    if (!readFile(p, data))
        return {};
    QByteArray digest = QCryptographicHash::hash(
            QByteArray::fromRawData(data.data(), static_cast<int>(data.size())),
            QCryptographicHash::Sha256);
    return digest.toHex().toStdString();
}

bool hasRuntimeFiles(const fs::path& dir)
{
    std::error_code ec;
    return fs::is_regular_file(dir / "pyodide.js", ec)
        && fs::is_regular_file(dir / "pyodide.asm.wasm", ec)
        && fs::is_regular_file(dir / "python_stdlib.zip", ec);
}

/// A canonical directory path with no trailing separator (a trailing
/// one would leave an empty last component for the scope comparison).
fs::path canonicalDir(const fs::path& p)
{
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    if (ec)
        c = p.lexically_normal();
    if (c.filename().empty() && c.has_parent_path())
        c = c.parent_path();
    return c;
}

bool sameComponent(const fs::path& a, const fs::path& b)
{
#ifdef FC_OS_WIN32
    const std::string sa = a.string();
    const std::string sb = b.string();
    if (sa.size() != sb.size())
        return false;
    for (size_t i = 0; i < sa.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(sa[i]))
                != std::tolower(static_cast<unsigned char>(sb[i])))
            return false;
    return true;
#else
    return a == b;
#endif
}

/// The wheel tag pyodide checks at load, from the ABI in the lock.
std::string wheelTag(const std::string& abi)
{
    return "pyodide_" + abi + "_wasm32";
}

/// The lock file of a runtime directory, parsed once per directory.
const json* lockOf(const std::string& runtimeDir)
{
    static std::mutex mutex;
    static std::map<std::string, json> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(runtimeDir);
    if (it != cache.end())
        return it->second.is_null() ? nullptr : &it->second;
    json& slot = cache[runtimeDir];
    std::string text;
    if (readFile(fs::path(runtimeDir) / "pyodide-lock.json", text)) {
        try {
            slot = json::parse(text);
        }
        catch (const json::exception& e) {
            FC_ERR("unreadable pyodide-lock.json under " << runtimeDir << ": " << e.what());
            slot = json();
        }
    }
    return slot.is_null() ? nullptr : &slot;
}

std::optional<PackageInfo> packageFromLock(const json& entry, const std::string& name)
{
    if (!entry.is_object())
        return std::nullopt;
    PackageInfo info;
    info.name = entry.value("name", name);
    info.version = entry.value("version", "");
    info.fileName = entry.value("file_name", "");
    info.sha256 = entry.value("sha256", "");
    info.type = entry.value("package_type", "package");
    auto dep = entry.find("depends");
    if (dep != entry.end() && dep->is_array())
        for (const auto& d : *dep)
            if (d.is_string())
                info.depends.push_back(d.get<std::string>());
    auto imp = entry.find("imports");
    if (imp != entry.end() && imp->is_array())
        for (const auto& i : *imp)
            if (i.is_string())
                info.imports.push_back(i.get<std::string>());
    return info;
}

}  // namespace

// ------------------------------------------------------------ the table

const std::vector<Release>& releases()
{
    // Every hash below was taken on 2026-09-03 from both the npm package
    // (cdn.jsdelivr.net/npm/pyodide@314.0.6) and the GitHub core tarball,
    // which agree file for file.  A new version is added here only after
    // the shim (src/App/PyodideHost/host_shim.js) has been checked
    // against what the new release's glue reads.
    static const std::vector<Release> table = {
        {"314.0.6",
         "2026_0",
         "3.14.2",
         {
             {"pyodide.js", "8d653521230f8e92f2e2f587e03f29d3abcdfb1c1744442d06fd8235007adb6b"},
             {"pyodide.mjs", "69e3f6ccec3e14b465df60be577ca62f536251406b9a00cce019eac5252a2495"},
             {"pyodide.asm.mjs", "2ac5eba365ec12839c75c03b39b3be1dd63b798852cc460b014b52238be042f7"},
             {"pyodide.asm.wasm", "3a0a00dfeaa348ac20f9ef09904233d32d33f644339662d4af368f8a2010f37a"},
             {"python_stdlib.zip", "80c5be6babfe03297069703410c3c29404dcf2525d2b128746bae5536f94831f"},
             {"pyodide-lock.json", "3fdaef09e9e365c85e002737720f8d0ab8f278c1c244a2dde6a37663cf488ad4"},
         },
         "pyodide-core-314.0.6.tar.bz2",
         "1016c31e39ce3764d9a418cbb491a392c802c1b86ccc1367f009f5c59bf8f5fd"},
    };
    return table;
}

const Release* releaseFor(const std::string& version)
{
    for (const auto& r : releases())
        if (r.version == version)
            return &r;
    return nullptr;
}

// ------------------------------------------------------------- the layout

std::string directoryVersion(const std::string& dir)
{
    std::string text;
    if (!readFile(fs::path(dir) / "package.json", text))
        return {};
    try {
        json pkg = json::parse(text);
        return pkg.value("version", "");
    }
    catch (const json::exception&) {
        return {};
    }
}

std::string directoryAbi(const std::string& dir)
{
    if (const Release* r = releaseFor(directoryVersion(dir)))
        return r->abi;
    if (const json* lock = lockOf(dir)) {
        auto info = lock->find("info");
        if (info != lock->end() && info->is_object())
            return info->value("abi_version", "");
    }
    return {};
}

Layout layout()
{
    Layout l;
    auto hGrp = prefs();
    std::string userDir = hGrp->GetASCII("PyodideUserDir", "");
    if (userDir.empty())
        userDir = envPath("FCX_PYODIDE_USER");
    if (userDir.empty())
        userDir = (fs::path(App::Application::getUserAppDataDir()) / "Pyodide").string();
    l.userDir = userDir;

    std::string packages = hGrp->GetASCII("PyodidePackages", "");
    if (packages.empty())
        packages = envPath("FCX_PYODIDE_PACKAGES");
    if (packages.empty())
        packages = (fs::path(userDir) / "packages").string();
    l.packages = packages;
    l.manifest = (fs::path(packages) / "manifest.json").string();

    const std::string dataDir = App::Application::getResourceDir() + "Pyodide";
    l.wheelDir = (fs::path(dataDir) / "wheels").string();

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(userDir, ec)) {
        if (entry.is_directory(ec) && hasRuntimeFiles(entry.path()))
            l.installed.push_back(entry.path().filename().string());
    }
    std::sort(l.installed.begin(), l.installed.end());

    std::string marker;
    if (readFile(fs::path(userDir) / "current", marker)) {
        while (!marker.empty() && std::isspace(static_cast<unsigned char>(marker.back())))
            marker.pop_back();
        while (!marker.empty() && std::isspace(static_cast<unsigned char>(marker.front())))
            marker.erase(marker.begin());
        if (!marker.empty() && marker.find_first_of("/\\") == std::string::npos
                && hasRuntimeFiles(fs::path(userDir) / marker))
            l.current = marker;
    }

    // Wheels: <datadir>/Pyodide/wheels/ first (the shipped place), then
    // <datadir>/Pyodide itself (a dev tree mirrors the npm directory with
    // the wheel dropped in beside the runtime).  First match per tag wins.
    auto scan = [&l](const fs::path& dir) {
        std::error_code e;
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(dir, e))
            names.push_back(entry.path().filename().string());
        std::sort(names.begin(), names.end());
        for (const auto& fn : names) {
            if (fn.size() < 5 || fn.compare(fn.size() - 4, 4, ".whl") != 0)
                continue;
            // a pure-Python wheel is bundled workbench code, one per
            // distribution name (the first place scanned wins)
            static const std::string pureTag = "-py3-none-any.whl";
            if (fn.size() > pureTag.size()
                    && fn.compare(fn.size() - pureTag.size(), pureTag.size(), pureTag) == 0) {
                const std::string dist = fn.substr(0, fn.find('-'));
                bool have = false;
                for (const auto& b : l.bundled)
                    have = have || fs::path(b).filename().string().substr(0, dist.size() + 1)
                        == dist + "-";
                if (!have)
                    l.bundled.push_back((dir / fn).string());
                continue;
            }
            // ...-cp314-cp314-pyodide_2026_0_wasm32.whl -> "2026_0"
            std::string abi;
            auto at = fn.find("-pyodide_");
            auto end = fn.find("_wasm32.whl");
            if (at != std::string::npos && end != std::string::npos && end > at + 9)
                abi = fn.substr(at + 9, end - (at + 9));
            if (fn.rfind("fcx_image-", 0) != 0) {
                // a COMPILED bundled wheel (pivy, docs/Sandbox.md 7.10):
                // one per distribution name and ABI, loaded only with an
                // fcx_image of the same ABI (the runtime filters)
                if (abi.empty())
                    continue;
                const std::string dist = fn.substr(0, fn.find('-'));
                bool have = false;
                for (const auto& b : l.bundledCompiled)
                    have = have || (b.first == abi
                                    && fs::path(b.second).filename().string().substr(0, dist.size() + 1)
                                        == dist + "-");
                if (!have)
                    l.bundledCompiled.emplace_back(abi, (dir / fn).string());
                continue;
            }
            bool have = false;
            for (const auto& w : l.wheels)
                have = have || w.first == abi;
            if (!have)
                l.wheels.emplace_back(abi, (dir / fn).string());
        }
    };
    scan(l.wheelDir);
    scan(dataDir);
    return l;
}

std::string wheelForAbi(const std::string& abi)
{
    for (const auto& w : layout().wheels)
        if (w.first == abi)
            return w.second;
    return {};
}

std::string verifyDirectory(const std::string& dir, bool* pinned)
{
    if (pinned)
        *pinned = false;
    const std::string version = directoryVersion(dir);
    if (version.empty())
        return "no package.json names a pyodide version under " + dir;
    const Release* r = releaseFor(version);
    if (!r)
        return "pyodide " + version + " is not a version this FreeCAD supports";
    if (pinned)
        *pinned = true;
    for (const auto& f : r->files) {
        const fs::path p = fs::path(dir) / f.name;
        const std::string got = sha256Of(p);
        if (got.empty())
            return "missing runtime file " + p.string();
        if (got != f.sha256)
            return "sha256 mismatch on " + p.string() + " (expected " + f.sha256
                + ", found " + got + ")";
    }
    return {};
}

// --------------------------------------------------------------- scoping

std::string askedToPath(const std::string& asked)
{
    std::string p = asked;
    if (p.rfind("file://", 0) == 0)
        p = p.substr(7);
    // file:///C:/x arrives as /C:/x
    if (p.size() >= 3 && p[0] == '/' && std::isalpha(static_cast<unsigned char>(p[1]))
            && p[2] == ':')
        p = p.substr(1);
    return p;
}

std::string scopePath(const std::vector<std::string>& roots,
                      const std::string& asked,
                      const std::string& base,
                      const std::vector<std::string>& fallbacks)
{
    const std::string p = askedToPath(asked);
    if (p.empty())
        return {};
    std::error_code ec;
    fs::path candidate;
    if (fs::path(p).is_absolute())
        candidate = fs::path(p);
    else {
        candidate = fs::path(base) / p;
        if (!fs::exists(candidate, ec)) {
            for (const auto& dir : fallbacks) {
                if (dir.empty())
                    continue;
                fs::path alt = fs::path(dir) / p;
                if (fs::exists(alt, ec)) {
                    candidate = alt;
                    break;
                }
            }
        }
    }
    fs::path canon = fs::weakly_canonical(candidate, ec);
    if (ec)
        return {};
    for (const auto& rootStr : roots) {
        if (rootStr.empty())
            continue;
        const fs::path root(rootStr);
        auto ri = root.begin();
        auto ci = canon.begin();
        for (; ri != root.end() && ci != canon.end(); ++ri, ++ci) {
            if (!sameComponent(*ri, *ci))
                break;
        }
        if (ri == root.end())
            return canon.string();
    }
    return {};
}

// -------------------------------------------------------------- packages

std::optional<PackageInfo> lookupPackage(const std::string& runtimeDir, const std::string& name)
{
    const json* lock = lockOf(runtimeDir);
    if (!lock)
        return std::nullopt;
    auto pkgs = lock->find("packages");
    if (pkgs == lock->end() || !pkgs->is_object())
        return std::nullopt;
    auto it = pkgs->find(name);
    if (it == pkgs->end())
        return std::nullopt;
    return packageFromLock(*it, name);
}

std::optional<PackageInfo> lookupImport(const std::string& runtimeDir,
                                        const std::string& importName)
{
    const json* lock = lockOf(runtimeDir);
    if (!lock)
        return std::nullopt;
    auto pkgs = lock->find("packages");
    if (pkgs == lock->end() || !pkgs->is_object())
        return std::nullopt;
    for (auto it = pkgs->begin(); it != pkgs->end(); ++it) {
        auto imp = it.value().find("imports");
        if (imp == it.value().end() || !imp->is_array())
            continue;
        for (const auto& i : *imp)
            if (i.is_string() && i.get_ref<const std::string&>() == importName)
                return packageFromLock(it.value(), it.key());
    }
    // a package whose lock entry lists no imports is importable by its
    // own name
    auto direct = pkgs->find(importName);
    if (direct != pkgs->end())
        return packageFromLock(*direct, importName);
    return std::nullopt;
}

std::vector<std::string> manifestPackages(const std::string& packagesDir, const std::string& abi)
{
    std::vector<std::string> names;
    std::string text;
    if (!readFile(fs::path(packagesDir) / "manifest.json", text))
        return names;
    json manifest;
    try {
        manifest = json::parse(text);
    }
    catch (const json::exception& e) {
        FC_ERR("unreadable sandbox package manifest under " << packagesDir << ": " << e.what());
        return names;
    }
    if (!manifest.is_object())
        return names;
    const std::string manifestAbi = manifest.value("abi", "");
    if (!abi.empty() && !manifestAbi.empty() && manifestAbi != abi) {
        FC_WARN("sandbox packages under " << packagesDir << " were installed for pyodide ABI "
                << manifestAbi << ", the runtime is " << abi << "; none loaded until reinstalled");
        return names;
    }
    auto order = manifest.find("load_order");
    if (order != manifest.end() && order->is_array()) {
        for (const auto& n : *order)
            if (n.is_string())
                names.push_back(n.get<std::string>());
        return names;
    }
    auto pkgs = manifest.find("packages");
    if (pkgs != manifest.end() && pkgs->is_object())
        for (auto it = pkgs->begin(); it != pkgs->end(); ++it)
            names.push_back(it.key());
    return names;
}

std::string missingImport(const std::string& importName)
{
    ImageHost::Location loc = ImageHost::instance().location();
    if (loc.stdlib.empty())
        return {};
    std::string top = importName;
    auto dot = top.find('.');
    if (dot != std::string::npos)
        top = top.substr(0, dot);
    std::optional<PackageInfo> info = lookupImport(loc.stdlib, top);
    if (!info)
        return {};
    const std::vector<std::string> have = manifestPackages(loc.packages, directoryAbi(loc.stdlib));
    if (std::find(have.begin(), have.end(), info->name) != have.end()) {
        // On disk, not in this instance: a fresh one loads it at boot.
        ImageHost::instance().scheduleReset();
        return info->name + " " + info->version
            + " is installed for the sandbox and becomes available at the next evaluation";
    }
    ExpressionSecurity::Runtime::instance().requestPending(
            ExpressionSecurity::Permission::PkgInstall, info->name);
    return "FreeCAD can install " + info->name + " " + info->version
        + " for the sandbox: allow pkg.install:" + info->name
        + " in the Document permissions panel";
}

}  // namespace Pyodide
}  // namespace ExpressionSandbox
}  // namespace App

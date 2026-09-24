/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <App/Application.h>
#include <Base/Console.h>
#ifdef FC_EXPR_PYODIDE_HOST
#include <App/ExpressionImageHost.h>
#include <App/ExpressionPyodide.h>
#endif

#include "Renderer/SceneServer.h"
#include "SandboxServe.h"

namespace fs = std::filesystem;
#ifdef FC_EXPR_PYODIDE_HOST
namespace Pyodide = App::ExpressionSandbox::Pyodide;
#endif

namespace
{

/// What the mount may answer: resolved once per install, read-only after.
struct Served
{
    /// Why nothing is served; boot.json answers it with a 503.
    std::string error;
    std::string version;
    fs::path runtime;
    std::set<std::string> runtimeFiles;
    std::map<std::string, fs::path> wheels;    ///< file name -> path
    std::map<std::string, fs::path> packages;  ///< file name -> path
    std::vector<uint8_t> boot;                 ///< the boot.json body
};

/// The types a browser insists on: a module script must be JavaScript,
/// and instantiateStreaming wants application/wasm.
const char* typeOf(const std::string& name)
{
    auto ends = [&](const char* ext) {
        const size_t n = std::char_traits<char>::length(ext);
        return name.size() >= n && name.compare(name.size() - n, n, ext) == 0;
    };
    if (ends(".mjs") || ends(".js"))
        return "text/javascript";
    if (ends(".wasm"))
        return "application/wasm";
    if (ends(".json"))
        return "application/json";
    if (ends(".zip"))
        return "application/zip";
    return "application/octet-stream";
}

bool readInto(const fs::path& path, Render::SceneHttpFile& file, const char* cache)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    const std::streamsize size = in.tellg();
    if (size < 0)
        return false;
    file.body.resize(static_cast<size_t>(size));
    in.seekg(0);
    if (size > 0 && !in.read(reinterpret_cast<char*>(file.body.data()), size))
        return false;
    file.contentType = typeOf(path.filename().string());
    file.cacheControl = cache;
    return true;
}

std::vector<uint8_t> jsonBytes(const QJsonObject& obj)
{
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

/// Resolve what the desktop runtime would boot with, the way it does
/// (ExpressionPyodideRuntime.cpp, resolve and initialize).
std::shared_ptr<Served> resolve()
{
    auto s = std::make_shared<Served>();
#ifndef FC_EXPR_PYODIDE_HOST
    s->error = "this FreeCAD was built without the pyodide sandbox host";
    return s;
#else
    auto& host = App::ExpressionSandbox::ImageHost::instance();
    const std::string runtimeName = host.runtime();
    if (runtimeName != "pyodide") {
        s->error = "the sandbox runtime here is " + runtimeName + ", not pyodide";
        return s;
    }
    const auto where = host.location();
    std::error_code ec;
    s->runtime = fs::weakly_canonical(where.stdlib, ec);
    if (ec || !fs::is_regular_file(s->runtime / "pyodide.mjs")) {
        s->error = "no pyodide runtime at " + where.stdlib;
        return s;
    }
    const std::string dir = s->runtime.string();
    s->version = Pyodide::directoryVersion(dir);
    const std::string abi = Pyodide::directoryAbi(dir);

    // The desktop's allowlist rule, with its override: only a pinned
    // release, only with the files it was pinned with.
    const std::string reason = Pyodide::verifyDirectory(dir);
    if (!reason.empty()) {
        auto hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Expression/Sandbox");
        const char* env = std::getenv("FCX_PYODIDE_UNPINNED");
        if (!hGrp->GetBool("PyodideUnpinned", false) && !(env && *env)) {
            s->error = "pyodide runtime refused: " + reason;
            return s;
        }
    }
    if (s->version.empty() || s->version.find('/') != std::string::npos) {
        s->error = "the pyodide runtime at " + dir + " names no version";
        return s;
    }
    if (const auto* release = Pyodide::releaseFor(s->version)) {
        for (const auto& f : release->files)
            s->runtimeFiles.insert(f.name);
    }
    else {
        // Unpinned (development only): what the browser loader fetches.
        s->runtimeFiles = {"pyodide.mjs", "pyodide.asm.mjs", "pyodide.asm.wasm",
                           "python_stdlib.zip", "pyodide-lock.json", "package.json"};
    }

    fs::path wheel = fs::weakly_canonical(where.image, ec);
    if (ec || !fs::is_regular_file(wheel)) {
        s->error = "no fcx_image wheel at " + where.image;
        return s;
    }
    const std::string wheelName = wheel.filename().string();
    if (!abi.empty() && wheelName.find("pyodide_" + abi + "_wasm32") == std::string::npos) {
        s->error = "fcx_image wheel " + wheelName + " does not match the pyodide ABI " + abi;
        return s;
    }
    s->wheels[wheelName] = wheel;

    // Bundled wheels, pure then compiled of this ABI, as initialize()
    // loads them.
    const Pyodide::Layout layout = Pyodide::layout();
    std::vector<std::string> all = layout.bundled;
    for (const auto& b : layout.bundledCompiled)
        if (b.first == abi)
            all.push_back(b.second);
    QJsonArray bundled;
    for (const auto& b : all) {
        fs::path w = fs::weakly_canonical(b, ec);
        if (ec || !fs::is_regular_file(w))
            continue;
        const std::string name = w.filename().string();
        if (s->wheels.count(name))
            continue;
        s->wheels[name] = w;
        bundled.append(QString::fromStdString("wheels/" + name));
    }

    // The user's package set by lock-file name, and every file the
    // browser's loader will fetch for it: the named packages and their
    // dependencies, closed over the lock.
    QJsonArray packages;
    if (!where.packages.empty() && fs::is_directory(where.packages, ec)) {
        const fs::path pdir = fs::weakly_canonical(where.packages, ec);
        std::vector<std::string> todo = Pyodide::manifestPackages(pdir.string(), abi);
        for (const auto& n : todo)
            packages.append(QString::fromStdString(n));
        std::set<std::string> seen;
        while (!todo.empty()) {
            const std::string name = todo.back();
            todo.pop_back();
            if (!seen.insert(name).second)
                continue;
            const auto info = Pyodide::lookupPackage(dir, name);
            if (!info)
                continue;
            const fs::path f = pdir / info->fileName;
            if (info->fileName.find('/') == std::string::npos && fs::is_regular_file(f, ec))
                s->packages[info->fileName] = f;
            for (const auto& d : info->depends)
                todo.push_back(d);
        }
    }

    QJsonObject boot;
    boot[QLatin1String("version")] = QString::fromStdString(s->version);
    boot[QLatin1String("abi")] = QString::fromStdString(abi);
    boot[QLatin1String("runtime")] = QString::fromStdString("runtime/" + s->version + "/");
    boot[QLatin1String("wheel")] = QString::fromStdString("wheels/" + wheelName);
    boot[QLatin1String("bundled")] = bundled;
    boot[QLatin1String("packageBase")] = QLatin1String("packages/");
    boot[QLatin1String("packages")] = packages;
    s->boot = jsonBytes(boot);
    return s;
#endif
}

}  // namespace

void Gui::SandboxServe::install()
{
    std::shared_ptr<const Served> served = resolve();
    if (!served->error.empty())
        Base::Console().Log("SandboxServe: no guest files served: %s\n", served->error.c_str());

    auto& server = Render::SceneStreamServer::instance();
    server.setHttpMount(
        "/pyodide/boot.json",
        [served](const std::string& rest, Render::SceneHttpFile& file) {
            if (!rest.empty())
                return false;
            if (!served->error.empty()) {
                QJsonObject err;
                err[QLatin1String("error")] = QString::fromStdString(served->error);
                file.status = 503;
                file.body = jsonBytes(err);
            }
            else {
                file.body = served->boot;
            }
            file.contentType = "application/json";
            file.cacheControl = "no-store";
            return true;
        },
        true);
    server.setHttpMount(
        "/pyodide/",
        [served](const std::string& rest, Render::SceneHttpFile& file) {
            if (!served->error.empty())
                return false;
            const auto slash = rest.find('/');
            if (slash == std::string::npos)
                return false;
            const std::string area = rest.substr(0, slash);
            std::string name = rest.substr(slash + 1);
            if (area == "runtime") {
                // The version is in the URL, so a runtime file can be
                // cached: a different runtime is a different URL.
                const std::string prefix = served->version + "/";
                if (name.compare(0, prefix.size(), prefix) != 0)
                    return false;
                name = name.substr(prefix.size());
                return served->runtimeFiles.count(name)
                    && readInto(served->runtime / name, file, "public, max-age=86400");
            }
            // fcx_image keeps its file name across rebuilds: never cached.
            // A package file name carries its version.
            const bool wheels = area == "wheels";
            if (!wheels && area != "packages")
                return false;
            const auto& table = wheels ? served->wheels : served->packages;
            const auto it = table.find(name);
            return it != table.end()
                && readInto(it->second, file, wheels ? "no-store" : "public, max-age=86400");
        },
        false);
}

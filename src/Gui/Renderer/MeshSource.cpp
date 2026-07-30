/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#include "MeshSource.h"
#include "SceneDump.h"

#include <cstdio>
#include <cstdlib>

using namespace Render;

namespace {
bool debugOn()
{
    static const bool on = std::getenv("FC_DEBUG_MESH_SOURCE") != nullptr;
    return on;
}
} // namespace

MeshSourceRegistry &MeshSourceRegistry::instance()
{
    static MeshSourceRegistry reg;
    return reg;
}

void MeshSourceRegistry::add(const void *tag, Generator gen,
                             float publishedError)
{
    if (!tag || !gen)
        return;
    std::lock_guard<std::mutex> guard(mutex);
    Source &src = sources[tag];
    src.gen = std::make_shared<Generator>(std::move(gen));
    src.publishedError = publishedError;
    src.canonicalKey.clear();
    if (debugOn())
        std::fprintf(stderr, "mesh source: add tag=%p err=%g (%zu sources)\n",
                     tag, double(publishedError), sources.size());
}

void MeshSourceRegistry::remove(const void *tag)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (!sources.erase(tag))
        return;
    for (auto it = keys.begin(); it != keys.end();) {
        if (it->second == tag)
            it = keys.erase(it);
        else
            ++it;
    }
}

void MeshSourceRegistry::associate(const std::string &key, const void *tag)
{
    if (key.empty() || !tag)
        return;
    std::lock_guard<std::mutex> guard(mutex);
    auto it = sources.find(tag);
    if (it == sources.end()) {
        if (debugOn())
            std::fprintf(stderr,
                         "mesh source: associate %s tag=%p UNREGISTERED\n",
                         key.c_str(), tag);
        return;
    }
    keys[key] = tag;
    it->second.canonicalKey = key;
    if (debugOn())
        std::fprintf(stderr, "mesh source: associate %s tag=%p (%zu keys)\n",
                     key.c_str(), tag, keys.size());
}

std::string MeshSourceRegistry::canonical(const std::string &key)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto it = keys.find(key);
    if (it == keys.end())
        return key;
    auto src = sources.find(it->second);
    if (src == sources.end() || src->second.canonicalKey.empty())
        return key;
    return src->second.canonicalKey;
}

float MeshSourceRegistry::publishedError(const void *tag)
{
    if (!tag)
        return 0.0f;
    std::lock_guard<std::mutex> guard(mutex);
    auto it = sources.find(tag);
    return it == sources.end() ? 0.0f : it->second.publishedError;
}

bool MeshSourceRegistry::generate(const std::string &key, uint32_t level,
                                  const void *sourceChunk, size_t sourceSize,
                                  std::vector<uint8_t> &out)
{
    // Copy the generator out under the lock: the closure owns its
    // geometry (refcounted), so it stays valid through the call even
    // if the source is removed while it runs.
    std::shared_ptr<Generator> gen;
    const void *tag = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = keys.find(key);
        if (it == keys.end()) {
            if (debugOn())
                std::fprintf(stderr,
                             "mesh source: generate %s: no owner "
                             "(%zu keys, %zu sources)\n",
                             key.c_str(), keys.size(), sources.size());
            return false;
        }
        auto src = sources.find(it->second);
        if (src == sources.end())
            return false;
        tag = it->second;
        gen = src->second.gen;
    }
    bool ok = (*gen)(level, sourceChunk, sourceSize, out);
    if (debugOn())
        std::fprintf(stderr, "mesh source: generate %s level %u -> %s\n",
                     key.c_str(), level, ok ? "ok" : "refused");
    if (ok && !out.empty()) {
        // The built level's key names this source too, non-canonically:
        // a later request may reach the server holding only this rung.
        std::string outKey = sha1Hex(out.data(), out.size());
        std::lock_guard<std::mutex> guard(mutex);
        if (sources.count(tag))
            keys[outKey] = tag;
    }
    return ok;
}

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

void MeshSourceRegistry::add(const void *tag, Generator gen)
{
    if (!tag || !gen)
        return;
    std::lock_guard<std::mutex> guard(mutex);
    sources[tag] = std::make_shared<Generator>(std::move(gen));
    if (debugOn())
        std::fprintf(stderr, "mesh source: add tag=%p (%zu sources)\n", tag,
                     sources.size());
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
    if (!sources.count(tag)) {
        if (debugOn())
            std::fprintf(stderr,
                         "mesh source: associate %s tag=%p UNREGISTERED\n",
                         key.c_str(), tag);
        return;
    }
    keys[key] = tag;
    if (debugOn())
        std::fprintf(stderr, "mesh source: associate %s tag=%p (%zu keys)\n",
                     key.c_str(), tag, keys.size());
}

bool MeshSourceRegistry::generate(const std::string &key, uint32_t level,
                                  const void *sourceChunk, size_t sourceSize,
                                  std::vector<uint8_t> &out)
{
    // Copy the generator out under the lock: the closure owns its
    // geometry (refcounted), so it stays valid through the call even
    // if the source is removed while it runs.
    std::shared_ptr<Generator> gen;
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
        gen = src->second;
    }
    bool ok = (*gen)(level, sourceChunk, sourceSize, out);
    if (debugOn())
        std::fprintf(stderr, "mesh source: generate %s level %u -> %s\n",
                     key.c_str(), level, ok ? "ok" : "refused");
    return ok;
}

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

#ifndef RENDERER_MESH_SOURCE_H
#define RENDERER_MESH_SOURCE_H

/// The seam between a published mesh chunk and the geometry that
/// produced it (docs/SceneStreaming.md §7). A decimated level is made
/// from the chunk's own bytes, but a *re-tessellated* level needs the
/// source shape — knowledge the render-cache feed drops on its way
/// down. This registry carries it back: the tessellating layer
/// (PartGui) registers a generator per geometry it feeds the renderer,
/// the publisher associates each minted chunk key with that geometry's
/// tag, and the scene server's level worker asks here first, falling
/// back to decimation when nobody claims the chunk.
///
/// The registry knows nothing of shapes, nodes, or documents — a tag
/// is an opaque address never dereferenced, and a generator is a
/// closure owning whatever it needs (a refcounted shape handle, so a
/// registered source keeps its geometry alive and a generation racing
/// a document change stays memory-safe). Generators run on the level
/// worker's thread; they must not touch the GUI.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "Renderer.h"

namespace Render {

class RendererExport MeshSourceRegistry {
public:
    /// Build the bytes of declared level \a level of the mesh whose
    /// exact chunk is \a sourceChunk. The source bytes are provided so
    /// the generator can honor the exact mesh's element tables index
    /// for index — and refuse (return false) when it cannot, which
    /// hands the job to the decimation fallback.
    using Generator = std::function<bool(uint32_t level,
                                         const void *sourceChunk,
                                         size_t sourceSize,
                                         std::vector<uint8_t> &out)>;

    static MeshSourceRegistry &instance();

    /// Register (or replace) the generator for \a tag. Called by the
    /// layer that tessellates — the tag is the identity the feed
    /// already carries per mesh (MeshData::sourceTag).
    /// \a publishedError states what the display tessellation itself
    /// is: 0 for the exact mesh, or the level error (relative to the
    /// shape diagonal) when the producer tessellated coarse-first —
    /// which is what tells the serializer to declare the exact mesh as
    /// an unbuilt rung above it.
    void add(const void *tag, Generator gen, float publishedError = 0.0f);
    /// Drop \a tag and every chunk-key association pointing at it.
    /// Call before the geometry behind the tag dies; the tag's address
    /// may be reused.
    void remove(const void *tag);

    /// The chunk stored under \a key was fed by \a tag's geometry.
    /// Called by the publisher when it minted (or re-announced) a mesh
    /// key; that key becomes the source's *canonical* one — the name
    /// its level jobs are memoized under. A tag nobody registered is
    /// not recorded — only shapes with a generator behind them are
    /// worth remembering. (Generated levels associate themselves on
    /// the way out of generate(), non-canonically: any built rung of a
    /// ladder can then name the source in a request.)
    void associate(const std::string &key, const void *tag);

    /// The canonical (publisher-associated) key of the source \a key
    /// belongs to, or \a key itself when nothing claims it. What lets
    /// a request naming any built rung and the publisher's
    /// announcement lookup agree on one job identity.
    std::string canonical(const std::string &key);

    /// The registered publishedError of the source behind \a tag, or 0.
    float publishedError(const void *tag);

    /// Try the shape-backed generator for \a key. False when no source
    /// claims the key or its generator refuses — decimation's turn.
    bool generate(const std::string &key, uint32_t level,
                  const void *sourceChunk, size_t sourceSize,
                  std::vector<uint8_t> &out);

private:
    struct Source {
        std::shared_ptr<Generator> gen;
        float publishedError = 0.0f;
        /// The key the publisher last associated — the job identity.
        std::string canonicalKey;
    };
    std::mutex mutex;
    std::map<const void *, Source> sources;
    std::unordered_map<std::string, const void *> keys;
};

} // namespace Render

#endif // RENDERER_MESH_SOURCE_H

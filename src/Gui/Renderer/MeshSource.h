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

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "Renderer.h"

class QTimer;

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
    /// \a refine is the desktop tier's climb back to exact
    /// (docs/SceneStreaming.md §13): when the level plan finds this
    /// source's coarse tessellation too wrong on screen, requestRefine
    /// fires it and the producer builds and applies the exact mesh
    /// behind it. \a cancelRefine is the way back down while nothing
    /// is built yet (§13 step 4): a plan that no longer wants the
    /// refine un-asks it, and the producer drops the job if it has not
    /// run — a tessellation is *work*, not a fetch to ignore
    /// (RungProvider::cancel's rationale, at this tier's granularity).
    /// Empty means nothing to climb: the source is already exact, or a
    /// serving process whose viewers drive the exact rung themselves.
    /// \a demote is the way back DOWN under memory pressure (§13
    /// step 3): a refined source registers it beside \a demoteError —
    /// the error of the coarse rung it would stand back on — and
    /// requestDemote fires it when an observed ceiling makes the plan
    /// want the bytes back. Empty = nothing resident to fall back to.
    void add(const void *tag, Generator gen, float publishedError = 0.0f,
             std::function<void()> refine = {},
             std::function<void()> cancelRefine = {},
             std::function<void()> demote = {},
             float demoteError = 0.0f);
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

    /// The level plan wants \a tag's exact tessellation: fire the
    /// source's refine callback. Idempotent while the ask stands — a
    /// plan that keeps wanting an unbuilt refine re-asks every settle
    /// and only the first fires; cancelRefine (or a re-registration)
    /// is what re-arms. No-op for unregistered tags and sources
    /// without a callback, so a plan pass may ask blindly.
    void requestRefine(const void *tag);
    /// The level plan stopped wanting \a tag's refine (the camera
    /// moved away before it built): fire the source's cancel callback
    /// and re-arm the ask. Only fires when an ask actually stands, so
    /// a plan pass may cancel blindly too — its de-wanted set includes
    /// every coarse source it never asked for.
    void cancelRefine(const void *tag);

    /// The plan wants \a tag's exact rung dropped (memory pressure,
    /// §13 step 3): fire the source's demote callback, consumed like a
    /// registration — the demotion re-registers the source coarse,
    /// which arms everything afresh. No-op without a callback.
    void requestDemote(const void *tag);
    /// The coarse-rung error \a tag would fall back to; 0 = not
    /// demotable. What planMeshDemotes prices a demotion by.
    float demoteError(const void *tag);

    /// A memory ceiling was observed: an exact build failed to
    /// allocate (bad_alloc / Standard_OutOfMemory), or the worker's
    /// pre-build estimate found available system memory under the
    /// floor. Sticky by design, like MemoryBudget's ceiling — memory
    /// that failed once is not un-failed by a later success — so every
    /// level plan from then on also demotes what the camera would not
    /// miss. The epoch lets the planner replan promptly on a new
    /// observation.
    void observeMemoryCeiling();
    uint64_t memoryCeilingEpoch() const { return ceilingEpoch; }

private:
    struct Source {
        std::shared_ptr<Generator> gen;
        float publishedError = 0.0f;
        /// The key the publisher last associated — the job identity.
        std::string canonicalKey;
        /// The desktop refine trigger and its retraction; `asked` is
        /// the standing ask requestRefine sets and cancelRefine clears.
        std::function<void()> refine;
        std::function<void()> cancelRefine;
        bool asked = false;
        /// The way back down and what it costs the screen (§13 step 3).
        std::function<void()> demote;
        float demoteErr = 0.0f;
    };
    std::mutex mutex;
    std::map<const void *, Source> sources;
    std::unordered_map<std::string, const void *> keys;
    std::atomic<uint64_t> ceilingEpoch {0};
};

/// The desktop tier's plan *events* (docs/SceneStreaming.md §13 step 2):
/// when to run a level plan, owned by the backend that has the camera.
///
/// The policy half — which sources err too much on screen — is the pure
/// planMeshRefines (SceneLadder.h); this class only decides when it is
/// worth asking: the camera has stopped somewhere the last plan did not
/// see, or the scene feed changed under a still camera. Feed observe()
/// from every rendered frame; ~300 ms after the camera settles the
/// stored plan callback runs once, on the GUI thread. The debounce is a
/// QTimer, so the last frame of a drag is enough — no further frames
/// need to arrive for the plan to fire, which matters on a desktop that
/// only renders on demand.
class RendererExport MeshLevelPlanner {
public:
    MeshLevelPlanner();
    ~MeshLevelPlanner();

    /// Tolerance for the plan pass, from the per-frame config feed
    /// (Renderer::setLevelTolerance). A change marks the plan stale so
    /// the new value applies without waiting for a camera move.
    void setTolerance(float px);
    float tolerance() const { return m_tolerance; }

    /// One rendered frame's camera (GL-layout 4x4, the matrices
    /// Renderer::render receives). Schedules \a planFn when a plan is
    /// due; the callback must stay valid until this planner dies —
    /// capture the owning backend, which owns this.
    void observe(const float *viewMatrix, const float *projMatrix,
                 std::function<void()> planFn);

    /// The scene feed changed: replan even with a still camera.
    void markDirty() { m_dirty = true; }

    /// The camera the pending/last plan is for — what a plan callback
    /// should pass to planMeshRefines (stable while the callback runs,
    /// unlike whatever pointer the render loop had).
    const float *viewMatrix() const { return m_view; }
    const float *projMatrix() const { return m_proj; }

private:
    bool moved(const float *view, const float *proj) const;

    std::unique_ptr<QTimer> m_timer;
    std::function<void()> m_planFn;
    float m_tolerance = 2.0f;
    float m_view[16] {};
    float m_proj[16] {};
    /// The camera the last *fired* plan saw; a settle on the same
    /// camera with a clean scene is not worth a pass.
    float m_viewPlanned[16] {};
    float m_projPlanned[16] {};
    bool m_haveObserved = false;
    bool m_havePlanned = false;
    bool m_dirty = false;
};

} // namespace Render

#endif // RENDERER_MESH_SOURCE_H

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

/// The desktop tier's per-source callbacks (docs/SceneStreaming.md
/// §13), all optional and all consumed/armed per registration:
/// - `refine`: the climb — the level plan finds the displayed coarse
///   rung too wrong on screen; requestRefine fires it (a standing
///   ask, idempotent while it stands) and the producer
///   builds/activates the exact mesh behind it.
/// - `cancelRefine`: the retraction (step 4) — a plan that no longer
///   wants an unbuilt refine un-asks it, and the producer drops the
///   job: a tessellation is *work*, not a fetch to ignore
///   (RungProvider::cancel's rationale).
/// - `demote`: drop the exact rung from CPU RAM (step 3), fired only
///   against an observed memory ceiling.
/// - `downgrade`: display the coarse rung but KEEP the exact one in
///   CPU RAM (step 3's GPU half) — fired when the GPU budget wants
///   upload bytes back; the way back up is then an instant
///   re-activation through an ordinary refine.
/// - `fallbackError`: the error of the rung a DEMOTE lands on -- what
///   the plan prices the drop by.
/// - `downgradeFallbackError`: the same for the downgrade, when the two
///   directions land somewhere different. They usually do not: dropping
///   the exact rung and merely un-displaying it both leave the coarse
///   one showing. But a source already displaying its coarse rung
///   descends by re-tessellating COARSER, and then the demote (drop a
///   hidden finer rung, nothing on screen changes) and the downgrade
///   (show a coarser mesh) commit quite different error. 0 falls back
///   to `fallbackError`, which is every pre-existing caller.
///
/// At namespace scope rather than nested, because a nested class with
/// default member initializers cannot be a default argument of its
/// enclosing class's members (incomplete-class context).
struct LevelHooks {
    std::function<void()> refine;
    std::function<void()> cancelRefine;
    std::function<void()> demote;
    std::function<void()> downgrade;
    float fallbackError = 0.0f;
    float downgradeFallbackError = 0.0f;
    /// Whether `demote` drops a HIDDEN finer rung -- a move that changes
    /// nothing on screen -- as against the dynamic-scale descent, which
    /// re-tessellates the DISPLAYED mesh coarser and visibly. The two
    /// arrive through the same slot, and only the arming site knows
    /// which it holds; dropHiddenLevels()'s contract ("nothing on
    /// screen changes") is only honest for the first kind, so it fires
    /// nothing without this. A visible descent is planMeshDemotes'
    /// business: priced against a camera, never taken blind.
    bool demoteDropsHiddenRung = false;
};

/// What demoteError/downgradeError answer for a tag the registry has
/// never heard of, as against 0 for one it knows but that armed no way
/// down.
///
/// The two look identical to a plan that only asks "is there a rung"
/// and they are completely different defects: 0 is a source whose
/// registration declined to arm a descent, while this is a drawn mesh
/// whose source is not registered AT ALL -- never registered, or
/// unregistered while its draw lives on. Counting them together
/// reported "no fallback rung" for both and sent the previous
/// investigation looking for a missing coarse build where the real
/// question was who owns the tag.
constexpr float kTagUnknown = -1.0f;

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

    /// The per-source callbacks (see Render::LevelHooks above).
    using LevelHooks = Render::LevelHooks;

    /// Register (or replace) the generator for \a tag. Called by the
    /// layer that tessellates — the tag is the identity the feed
    /// already carries per mesh (MeshData::sourceTag).
    /// \a publishedError states what the display tessellation itself
    /// is: 0 for the exact mesh, or the level error (relative to the
    /// shape diagonal) when the producer tessellated coarse-first —
    /// which is what tells the serializer to declare the exact mesh as
    /// an unbuilt rung above it.
    /// \a origin names the registration site, for the tally below. It
    /// is a literal owned by the caller, compared by pointer never by
    /// content, and used for nothing else.
    void add(const void *tag, Generator gen, float publishedError = 0.0f,
             LevelHooks hooks = LevelHooks(), const char *origin = nullptr);

    /// How many live sources each registration site contributed, and
    /// how many of those armed a downgrade hook.
    ///
    /// Without a downgrade hook a source can never come back down:
    /// downgradeError() returns 0 for it and the plan skips it as
    /// having no rung to fall back to. Whether that is most of a scene
    /// decides whether a GPU budget can be honoured at all, and the
    /// answer differs per site -- so counting by site is what turns
    /// "the plan refused everything" into a defect with an address.
    struct OriginTally {
        const char *origin = nullptr;
        uint32_t sources = 0;
        uint32_t withDowngrade = 0;
        uint32_t withDemote = 0;
    };
    std::vector<OriginTally> originTally() const;
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

    /// Restate what the DISPLAY tessellation of \a tag now is, without
    /// touching its generator or hooks. False when nothing is
    /// registered under the tag.
    ///
    /// For the producer that coarsened a source in place rather than by
    /// re-registering it -- the decimation rung of
    /// docs/SceneStreaming.md #13c rewrites its display nodes and keeps
    /// everything else. Leaving the old figure standing is not a
    /// cosmetic staleness: the refine pass wants a source when
    /// `levelError * diagPx > tolerancePx`, so an error that understates
    /// how coarse the object actually became is one that may never ask
    /// for it back, and the object stays visibly decimated after the
    /// pressure that decimated it has gone. It also prices the next
    /// descent step from a rung the object is no longer standing on.
    ///
    /// Bumps generation() like add(), so a cached publishedError()
    /// answer is re-asked.
    bool setPublishedError(const void *tag, float publishedError);

    /// Whether \a tag names a registered source at all.
    ///
    /// publishedError() answers 0 both for the exact rung and for a tag
    /// nobody owns, so a mesh whose source was never registered is
    /// published as exact and enters the level plan looking like a
    /// source standing at the top of its ladder. Distinguishing the two
    /// is what says whether the ladder can reach a drawn mesh; measured
    /// on the rack model, 1197 of 1569 apparently-exact sources were
    /// this.
    bool knows(const void *tag) const;

    /// Bumped whenever a registration changes, and so whenever any
    /// tag's publishedError could have moved. A caller holding an
    /// answer from an earlier call can compare this instead of asking
    /// again: publishedError() takes the registry lock, and a scene
    /// publish asks it once per mesh, which measured 4-5ms of a
    /// 6000-object publish spent proving nothing had changed
    /// (docs/IncrementalPublish.md §4d-ii). Lock-free by design --
    /// reading it must not cost what it is there to avoid.
    uint32_t generation() const
    {
        return registryGen.load(std::memory_order_acquire);
    }

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

    /// The plan wants \a tag's exact rung dropped from CPU RAM
    /// (memory pressure, §13 step 3): fire the source's demote
    /// callback, consumed like a registration — the demotion
    /// re-registers the source, which arms everything afresh. No-op
    /// without a callback.
    void requestDemote(const void *tag);
    /// The coarse-rung error \a tag would fall back to; 0 = not
    /// demotable, kTagUnknown = no such source. What planMeshDemotes
    /// prices a demotion by.
    float demoteError(const void *tag);

    /// The GPU budget wants \a tag's upload bytes back (§13 step 3):
    /// fire the source's downgrade callback (consumed) — the display
    /// drops to the coarse rung, the exact one stays in CPU RAM.
    void requestDowngrade(const void *tag);
    /// Like demoteError, for the downgrade sweep; 0 = not downgradable.
    float downgradeError(const void *tag);

    /// A CPU memory ceiling stands: drop every *hidden* exact rung —
    /// sources displaying their coarse rung while still holding the
    /// exact one (a demote hook that declares demoteDropsHiddenRung,
    /// beside a non-zero publishedError). Dropping those consults no
    /// camera: nothing on screen changes. A coarse source whose demote
    /// is the visible dynamic-scale descent is deliberately NOT fired
    /// here -- see LevelHooks::demoteDropsHiddenRung.
    void dropHiddenLevels();

    /// A memory ceiling was observed: an exact build failed to
    /// allocate (bad_alloc / Standard_OutOfMemory), or the worker's
    /// pre-build estimate found available system memory under the
    /// floor. Sticky by design, like MemoryBudget's ceiling — memory
    /// that failed once is not un-failed by a later success — so every
    /// level plan from then on also demotes what the camera would not
    /// miss. The epoch lets the planner replan promptly on a new
    /// observation.
    ///
    /// \a shortfallBytes is how much memory the observer wanted back --
    /// the floor less what the system had free. It is what lets the
    /// plan trade *visible* error for memory, and only as much of it as
    /// the shortfall needs (planMeshDemotes' priced tier); a ceiling
    /// observed without a quantity behind it (a bad_alloc says only
    /// "no") passes 0, and the plan stays inside its free tier.
    /// Freshest observation wins rather than the largest: the shortfall
    /// is a statement about memory now, and a stale one would keep
    /// demoting against pressure that has passed.
    void observeMemoryCeiling(size_t shortfallBytes = 0);
    uint64_t memoryCeilingEpoch() const { return ceilingEpoch; }
    size_t memoryShortfall() const { return ceilingShortfall; }

    /// Plan-ordered descent jobs (worker-side coarsenings: the
    /// dynamic-scale re-tessellation and the decimation rung) currently
    /// queued or running. The producer notes every enqueue and every
    /// settlement -- landed, refused, or canceled; the pair must
    /// balance. What the downgrade ledger holds its write-off horizon
    /// open on: an order's bytes cannot land before its job does, so
    /// expiring the credit on a frame count while the job still queues
    /// re-orders the same memory from other sources -- the storm.
    ///
    /// The optional GENERATION scopes that statement to one sweep's
    /// order. A descent is no longer one job: a downgrade's hook body
    /// queues a worker build, whose landing queues a pooled visual
    /// fill, whose apply is when the bytes actually move -- and the
    /// aggregate settle counter reaches any target long before the
    /// tails of those chains do (measured as the ladder overshooting
    /// a 64MB budget down to 44.7MB: credit written off early, the
    /// sweep re-ordering from other sources while the first order's
    /// fills were still in flight). Every enqueue made while a
    /// generation is current -- the plan's order loop opens one, and
    /// the landing pump re-enters the job's own while running it --
    /// inherits it, so a generation drains exactly when the order's
    /// transitive chain has.
    void noteDescentQueued(uint64_t gen = 0)
    {
        descentJobs.fetch_add(1, std::memory_order_relaxed);
        if (gen) {
            std::lock_guard<std::mutex> lock(genMutex);
            ++genOutstanding[gen];
        }
    }
    void noteDescentSettled(uint64_t gen = 0)
    {
        descentJobs.fetch_sub(1, std::memory_order_relaxed);
        descentSettles.fetch_add(1, std::memory_order_relaxed);
        if (gen) {
            std::lock_guard<std::mutex> lock(genMutex);
            auto it = genOutstanding.find(gen);
            if (it != genOutstanding.end() && --it->second == 0)
                genOutstanding.erase(it);
        }
    }
    uint32_t descentInFlight() const
    {
        return descentJobs.load(std::memory_order_relaxed);
    }
    /// Monotonic count of settled descent jobs (landed, refused, or
    /// purged), kept for the narration lines.
    uint64_t descentSettleCount() const
    {
        return descentSettles.load(std::memory_order_relaxed);
    }
    /// A fresh generation for one sweep's orders (never 0).
    uint64_t openDescentGeneration()
    {
        return descentGenCounter.fetch_add(1, std::memory_order_relaxed)
            + 1;
    }
    /// Whether \a gen's transitive chain of jobs has drained. Gen 0 --
    /// work queued outside any order -- reads as settled.
    bool descentGenerationSettled(uint64_t gen) const
    {
        if (!gen)
            return true;
        std::lock_guard<std::mutex> lock(genMutex);
        return genOutstanding.find(gen) == genOutstanding.end();
    }
    /// The generation new descent work inherits on this thread; the
    /// producers read it at enqueue. GUI thread for all writers today,
    /// thread-local so a worker-side settle cannot see a stale scope.
    static uint64_t &currentDescentGeneration()
    {
        static thread_local uint64_t gen = 0;
        return gen;
    }
    /// Scoped set/restore of currentDescentGeneration: the plan wraps
    /// its order loop, the landing pump wraps each item it runs.
    class DescentGenScope
    {
        uint64_t prev;

    public:
        explicit DescentGenScope(uint64_t gen)
            : prev(currentDescentGeneration())
        {
            currentDescentGeneration() = gen;
        }
        ~DescentGenScope() { currentDescentGeneration() = prev; }
    };

private:
    struct Source {
        std::shared_ptr<Generator> gen;
        float publishedError = 0.0f;
        /// The key the publisher last associated — the job identity.
        std::string canonicalKey;
        /// The desktop tier's callbacks; `asked` is the standing ask
        /// requestRefine sets and cancelRefine clears.
        LevelHooks hooks;
        bool asked = false;
        /// Registration site, a caller-owned literal (see add()).
        const char *origin = nullptr;
    };
    mutable std::mutex mutex;
    std::map<const void *, Source> sources;
    std::unordered_map<std::string, const void *> keys;
    std::atomic<uint64_t> ceilingEpoch {0};
    std::atomic<size_t> ceilingShortfall {0};
    std::atomic<uint32_t> descentJobs {0};
    std::atomic<uint64_t> descentSettles {0};
    std::atomic<uint64_t> descentGenCounter {0};
    /// Per-generation outstanding job counts; an absent key is a
    /// drained (or never-used) generation. Guarded by its own mutex:
    /// settles come off the refine workers too.
    mutable std::mutex genMutex;
    std::unordered_map<uint64_t, uint32_t> genOutstanding;
    /// See generation(). Bumped by every add() and remove(), which are
    /// the only things that can change what publishedError() answers.
    std::atomic<uint32_t> registryGen {0};
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

    /// Whether the plan now running is for a camera the last one did
    /// not see -- valid inside the plan callback, and only there.
    ///
    /// What a rung costs on screen is a function of where the camera
    /// is, so anything a plan LEARNED about this scene under a budget
    /// (PressureTolerance::floorPx) is evidence about this camera and
    /// stops applying when it moves.
    bool cameraMoved() const { return m_cameraMoved; }

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
    bool m_cameraMoved = true;
};

} // namespace Render

#endif // RENDERER_MESH_SOURCE_H

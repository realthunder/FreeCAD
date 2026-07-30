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

#ifndef RENDERER_SCENE_LADDER_H
#define RENDERER_SCENE_LADDER_H

/// Which rung of docs/SceneStreaming.md §6's ladder each object is
/// drawn at, and what that costs — the policy half of progressive
/// rendering, with none of the transport.
///
/// The split this header exists to make is between **what to have** and
/// **how to get it**. Ranking a payload by what the camera can see of
/// the objects that want it, and giving one back when the budget is
/// full, are the same decisions whether the rung arrives over a socket
/// or comes out of a tessellator: both are a queue of known-size
/// payloads, ordered by a camera, bounded by memory. Only the
/// acquisition differs, and that is behind RungProvider.
///
/// So nothing here knows about fetches, IndexedDB, emscripten or bgfx,
/// and the arithmetic is plain float rather than a math library's
/// vectors — the two tiers should not have to agree on a vector type to
/// share a policy.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "SceneDump.h"

namespace Render {

/// The camera as one round of ordering sees it.
///
/// \a right may be either the camera's right axis or its negation: the
/// only test that uses it is symmetric about the view centre, so the
/// sign cannot matter, and requiring a convention of two callers that
/// disagree would only invite a bug that never shows.
struct LadderView {
    float eye[3] {0.0f, 0.0f, 0.0f};
    float at[3] {0.0f, 0.0f, 0.0f};
    float right[3] {1.0f, 0.0f, 0.0f};
    float up[3] {0.0f, 1.0f, 0.0f};
    /// Vertical field of view in degrees.
    float fovY = 45.0f;
    /// Width over height. Zero or negative is read as 1.
    float aspect = 1.0f;
};

/// Where an object is, by the key its chunks name it with.
///
/// A lookup rather than a map, because only the consumer knows where
/// bounds live: the viewer has to look in both the objects it has
/// applied and the ones a staged publish has only announced, and the
/// desktop has the view providers. Returning null means the bounds are
/// not known — which scores zero and sorts last, rather than failing.
using LadderBounds = std::function<const float *(uint64_t objectKey)>;

/// How strongly a payload's size counts against it, for the two
/// decisions that rank chunks. 0 ignores size and ranks purely by what
/// the camera sees; 1 ranks strictly per byte; above 1 discounts size
/// steeper than per byte, and is honored rather than clamped — a tuning
/// knob that silently saturates lies to the person turning it.
///
/// **Acquiring leans on size, keeping does not.** What to get next is a
/// question about a rate — the appearance layer costs a thousandth of
/// the geometry and lifts every object a whole rung, so discounting by
/// size is what puts a model in its real colours in a fraction of a
/// second. What to keep is a question about a stock, and there size is
/// beside the point: the near, detailed object is the one worth its
/// memory even though it is the expensive one.
///
/// The acquire default is a *square root* rather than the full per-byte
/// discount it started as. Per byte, a payload a thousand times smaller
/// was a thousand times preferred, which is far more than "colour
/// first" needs and left a large near mesh queued behind every trivial
/// distant one — visible as boxes in the foreground of a half-loaded
/// model. At 0.5 the same payload is thirty times preferred: the
/// appearance layer still arrives first by a wide margin, and geometry
/// is ordered much more by where it is.
struct LadderWeights {
    float acquire = 0.5f;
    float keep = 0.0f;
};

/// The camera, and the priority of each object it has already had to
/// work out.
///
/// Both halves are memoization and both are needed. The camera frame
/// costs four trigonometric functions, and a chunk shared by every
/// object in the scene carries every one of those objects — so scoring
/// a queue of a few hundred chunks against a few hundred owners apiece
/// re-derived the same camera millions of times per load. Measured, it
/// was the whole cost of the fetch: a load that should have been
/// network-bound spent its time in `sin`.
class RendererExport RungRanker {
public:
    RungRanker(const LadderView &view, LadderBounds bounds,
               const LadderWeights &weights = LadderWeights());

    /// Priority of one object: larger is wanted sooner.
    ///
    /// Projected size, which is distance and extent in one number, and
    /// is what "get what matters" means on a screen: a near wall and a
    /// far building can be equally worth having. Off-screen is a
    /// penalty rather than an exclusion — the object is still acquired,
    /// just behind everything visible, so a viewer that never moves
    /// still ends up holding the whole scene, and one that turns around
    /// finds the work already started.
    float owner(uint64_t objectKey);

    /// What a chunk is worth, discounted by what it costs: **the best
    /// of the objects that need it**, never the first of them, over its
    /// payload size raised to \a sizeWeight.
    ///
    /// The best owner, because content addressing means one material
    /// can back a whole scene and one mesh every instance of a part,
    /// and such a chunk is named by whichever manifest happened to be
    /// read first — which may be the smallest object in the far
    /// distance. Taken at that object's priority it holds up every
    /// object that shares it, and since an object is only drawn once
    /// its whole appearance is resident, one starved material is a
    /// scene that never leaves its boxes.
    ///
    /// Note it keeps the rule the deferred list is built on: what a
    /// consumer does with a chunk follows from its size and never from
    /// what is inside it (SceneDump.h). Nothing here knows a material
    /// from a mesh — it does not need to, because the appearance layer
    /// *is* the small one.
    float value(const SceneSnapshot::DeferredChunk &chunk, float sizeWeight);

    /// What to ask for next: value per byte spent, because bandwidth —
    /// or a tessellator's throughput — is a rate, and the question is
    /// what to spend the next byte on.
    float acquisition(const SceneSnapshot::DeferredChunk &chunk);

    /// What to keep, which is a different question: memory is a stock,
    /// and what belongs in it is what the camera is looking at.
    ///
    /// Measured on the 200-object scene under an 8 MB budget, keeping
    /// per byte gave 494 resident chunks averaging 16 KB against 146
    /// refused averaging 146 KB — the refused set was precisely the
    /// detailed geometry, wherever it was, so the foreground kept boxes
    /// while the distance was fully modelled. Ignoring size instead:
    /// 234 resident averaging 35 KB against 406 refused averaging
    /// 52 KB, and distance decides membership.
    float residency(const SceneSnapshot::DeferredChunk &chunk);

    const LadderView &view() const { return m_view; }

private:
    /// Projected size of a bounding box, penalised if it is off-screen.
    float project(const float *bbox) const;

    LadderView m_view;
    LadderBounds m_bounds;
    LadderWeights m_weights;
    /// Forward axis, normalised, derived once from eye and at.
    float m_fwd[3] {0.0f, 0.0f, 1.0f};
    /// Tangent of the half vertical field of view.
    float m_tanHalfFov = 0.0f;
    std::map<uint64_t, float> m_memo;
};

/// Which rung of a mesh's declared ladder to have, and which to ask
/// for — the two are different whenever the wanted one is declared but
/// unbuilt (docs/SceneStreaming.md §7, phase 5d).
struct LevelChoice {
    /// The rung the camera warrants: the coarsest level whose stated
    /// error the viewer could not tell from the exact mesh.
    size_t desired = 0;
    /// The rung to acquire now: the built level nearest \a desired,
    /// preferring the coarser side — a cheap rung that shows the
    /// object beats a large one that shows it slightly better, which
    /// is the whole progressive argument, and the exact mesh always
    /// closes the ladder so there is always something to fetch.
    size_t fetch = 0;
    /// True when \a desired is declared but has no key yet: worth a
    /// RungProvider::generate, whose answer arrives as a publish.
    bool generate = false;
};

/// Pick a level from what the camera can actually resolve.
///
/// A level's error is stated relative to the mesh's own diagonal
/// (v36), so multiplying by the owner's projected size on screen turns
/// it into pixels, and the choice is a comparison: the coarsest level
/// whose error lands under \a tolerancePx is indistinguishable from
/// the exact mesh to within that many pixels. The *best* owner
/// decides, as everywhere on this ladder — a mesh shared by a near
/// object and a far one must be fine enough for the near one.
///
/// Owners with unknown bounds, a non-positive tolerance (the off
/// switch), or a ladder of one rung all answer "the exact mesh", which
/// is the behavior selection replaced.
RendererExport LevelChoice chooseLevel(
    RungRanker &ranker, const SceneSnapshot::DeferredChunk &entry,
    float tolerancePx, float viewportPx);

/// A rung that may not exist yet, named by what would *produce* it
/// rather than by what it will contain.
///
/// This is the awkward part of generating levels on demand, and it does
/// not go away by being ignored. A chunk's key is the SHA-1 of its
/// bytes (docs/SceneStreaming.md §9, invariant 1), so a level nobody
/// has tessellated has no bytes, therefore no key, therefore cannot be
/// asked for the way every other payload is. A request for work is not
/// a request for bytes and cannot share the channel.
///
/// So a level is named by its source and its index instead: stable
/// across publishes, identical for every viewer that wants it, and
/// derived from content rather than from who is asking — which is what
/// keeps the server free of per-viewer state (invariant 7) even once it
/// has a queue of work. What comes back is the key, and from there it
/// is an ordinary chunk with an ordinary content address.
struct LevelRequest {
    /// Content identity of the geometry the level would be generated
    /// from — any *built* level's key of the same ladder (the producer
    /// canonicalizes, so siblings name the same job). Not the key of
    /// the level — that is what this asks for.
    std::string source;
    /// Which rung, coarsest first. What a level *means* — a tessellation
    /// deviation, a decimation ratio — is the producer's business, and
    /// deliberately not encoded here.
    uint32_t level = 0;
};

/// The level index that names the *exact* mesh (§7, coarse-first
/// publish): a producer that published a coarse tessellation declares
/// the exact one at error 0, unbuilt, and this is how it is asked for.
/// A sentinel rather than a ladder index, because ladder positions of
/// the coarser rungs double as generator grid levels and the exact
/// mesh is not on that grid — its deviation is the display formula's.
constexpr uint32_t kExactMeshLevel = 255;

/// How a rung is actually obtained. The ladder decides *what* to
/// acquire and what to give back; this is the tier's answer to *how*.
///
/// The two implementations differ in everything except their shape. The
/// browser asks a server for bytes over a link whose cost is a round
/// trip; the desktop asks a tessellator for a mesh at a deviation,
/// where the cost is CPU and the bytes never travel. Both are
/// asynchronous, both answer with a payload the ladder can rebuild
/// from, and both have a sensible limit on how much may be in flight —
/// which is the entire reason one policy can drive them.
///
/// Nothing here returns a payload: acquisition completes by the chunk's
/// own fill running, so a provider that answers immediately and one
/// that answers in a second are the same to the caller.
class RendererExport RungProvider {
public:
    virtual ~RungProvider() = default;

    /// Begin acquiring the payload with this content key. Called only
    /// for keys a manifest already names.
    virtual void request(const std::string &key, uint32_t size) = 0;

    /// Requests begun and not yet answered.
    virtual size_t outstanding() const = 0;

    /// How many may be outstanding at once. A window is what makes an
    /// order an order: acquire everything the moment it is named and
    /// the ranking decides nothing, because it is all issued in the
    /// same tick and answered in whatever order it happens to finish.
    virtual size_t window() const = 0;

    /// The end of a round: issue whatever has been accumulated but not
    /// yet sent. A provider that batches must not wait for the next
    /// round to flush a partial batch — left to a timer that cost a
    /// 4.6 s load 58 s (§6 phase 4a).
    virtual void flush() {}

    /// Ask for a level that may not exist yet. False — the default —
    /// means this tier can only fetch what already exists, which is
    /// every provider until the producer-side work of §7 lands.
    virtual bool generate(const LevelRequest &request);

    /// The acquisition for \a key is no longer wanted — the camera
    /// moved, the rung was displaced, the publish it belonged to was
    /// superseded. Advisory, and a no-op by default: a provider whose
    /// acquisition is a download may let it complete and land through
    /// the ordinary path, because a fetch already in flight costs
    /// nothing to ignore. A provider whose acquisition is *work* — a
    /// tessellation at a deviation — should stop the job, because CPU
    /// spent on a level nobody wants anymore is not free the way an
    /// ignored payload is. Cancelling changes no bookkeeping: the chunk
    /// stays outstanding until its fill runs or the caller's timeout
    /// presumes it lost, so a provider that ignores this is merely
    /// slower, never wrong.
    ///
    /// Declared before any caller exists, deliberately: the seam is the
    /// part that fossilizes, and the desktop tier's cost model needs
    /// this expressible from its first implementation.
    virtual void cancel(const std::string &key) { (void)key; }
};

/// What the scene may hold, and how that number is arrived at on a
/// machine nobody measured.
///
/// The budget is stated in the **payload bytes every chunk already
/// declares** (SceneDump.h), because that is what is known before a
/// rung is acquired and what both tiers can rank by. What actually
/// runs a device out of memory is the expanded arrays those payloads
/// become, which is a different number by a factor nobody can name in
/// advance: it depends on the compression the wire happened to get and
/// on what the backend keeps beside each buffer.
///
/// So rather than assume that factor, this measures it. Both numbers
/// are already sampled on a heartbeat — what the scene says it holds
/// and what the heap actually is — and the ratio between them is the
/// expansion. A heap ceiling then converts into a payload budget by
/// dividing, and a device the author never saw gets a budget fitted to
/// what payloads cost *on it*.
///
/// This is deliberately not `navigator.deviceMemory` and friends. Those
/// exist on Chrome and Android, are absent on Safari — where the
/// ceiling matters most — and are rounded to powers of two and capped
/// at 8 GB besides. They are worth a starting guess and nothing more:
/// what a tab may hold is decided by the device, the other fifty tabs
/// and the OS, and the only honest way to learn it is to watch.
class RendererExport MemoryBudget {
public:
    /// The largest heap this process could ever have, as the platform
    /// states it: the wasm growth cap in the browser, physical memory
    /// on a desktop. 0 when nothing will say.
    static size_t systemMemory();

    /// A device hint where one exists (navigator.deviceMemory), in
    /// bytes. 0 when the browser does not answer or this is not one.
    static size_t deviceHint();

    /// Start adapting from a guess for this machine. \a explicitBytes
    /// non-zero pins the budget instead and stops all adaptation — the
    /// `?membudget=` case, where the point is to say what it is.
    void reset(size_t explicitBytes = 0);

    /// One heartbeat: \a payloadBytes is the resident geometry by the
    /// scene's own accounting, \a rawBytes everything else the stream
    /// is holding that the budget does not bound — the local payload
    /// cache — and \a heapBytes what the process actually occupies.
    ///
    /// \a rawBytes is separated out because it is held roughly one for
    /// one and would otherwise be charged to geometry as expansion.
    /// Measured on the 200-object scene, folding it in reported a
    /// factor of six and falling where the real steady-state ratio was
    /// about one: the estimate was tracking the download cache, and the
    /// budget it produced was wrong by that much.
    ///
    /// Cheap enough to call as often as the heap is sampled, and it has
    /// to be: the expansion is only learnable while the scene is
    /// growing, which is exactly when nothing wants to spend time
    /// measuring itself.
    void observe(size_t payloadBytes, size_t rawBytes, size_t heapBytes);

    /// A hard ceiling observation: the process just *failed* to obtain
    /// memory with the heap at \a heapBytes, so the wall is not where
    /// the platform said — it is here. The ceiling drops to a share
    /// below the observed wall (monotonically: a second observation can
    /// only lower it further) and the budget is cut against it at once,
    /// so eviction starts making room before the next allocation walks
    /// into the same wall.
    ///
    /// No caller exists in the wasm tier yet, honestly: with aborting
    /// malloc there is nothing left to call it from. The desktop tier
    /// runs with exceptions, where a caught bad_alloc is exactly this
    /// observation — which is why the seam is here and not in a
    /// platform file.
    void observeCeiling(size_t heapBytes);

    /// The budget as it currently stands, in payload bytes.
    size_t value() const { return m_budget; }

    /// Heap bytes observed per payload byte, as measured. 0 before
    /// enough of a scene has arrived to tell.
    float expansion() const { return m_expansion; }

    /// False once a budget has been pinned explicitly.
    bool adaptive() const { return !m_pinned; }

    /// The heap this budget is trying to keep the process under — what
    /// the adaptation is solving for, reported so that "the viewer is
    /// holding too much" and "something else is" stay distinguishable.
    size_t ceiling() const { return m_ceiling; }

private:
    size_t m_budget = 0;
    size_t m_ceiling = 0;
    /// The heap before the scene paid for anything: everything that is
    /// not geometry — the backend, the runtime, the canvas. Subtracted
    /// before dividing, or the expansion absorbs it and the budget
    /// shrinks on a machine that merely has a large baseline.
    size_t m_baseHeap = 0;
    /// The previous sample the slope is measured against: resident
    /// payload, and the heap net of the raw cache.
    size_t m_prevPayload = 0;
    size_t m_prevNet = 0;
    /// Decayed sums of payload growth and net heap growth — the slope
    /// is their ratio, so a heap that grows in slabs between samples
    /// averages out instead of biasing the estimate (see observe()).
    double m_sumPayload = 0.0;
    double m_sumNet = 0.0;
    float m_expansion = 0.0f;
    bool m_pinned = false;
    bool m_havePrev = false;
};

/// How much better, per byte, an incoming payload must be than the one
/// it displaces. Strictly better is enough to make the resident set
/// converge, but not enough to make it *settle*: two payloads a
/// fraction of a percent apart swap places on every rounding of the
/// projection, and each swap costs an acquisition and a rung. A margin
/// says what counts as a difference worth acting on.
RendererExport extern const float kEvictMargin;

/// The resident payloads a round may give back, worst first: scored
/// once, spent as the round issues requests.
///
/// Once per round rather than once per candidate, which is what the
/// first cut did — and with a few hundred chunks outstanding, sorting
/// the resident set for each of them was most of what a load spent its
/// time on.
class RendererExport Evictor {
public:
    /// Whether a payload is currently held, by key.
    using ResidentTest = std::function<bool(const std::string &key)>;
    /// Walk one payload back down the ladder, at the score it was worth
    /// when it was let go. The caller owns what that means — the entry
    /// is left as it was before the payload arrived, so the ordinary
    /// acquisition path can climb it again if the camera comes back.
    using Release = std::function<void(SceneSnapshot::DeferredChunk &entry,
                                       float score)>;
    /// Where to say why room could not be made. Unset says nothing.
    using Trace = std::function<void(const std::string &message)>;

    Evictor(SceneSnapshot &snap, RungRanker &ranker,
            ResidentTest isResident, Release release);

    /// Free \a need bytes for a payload worth \a incoming per byte, or
    /// change nothing and answer false.
    ///
    /// Planned before it is carried out, because a half-done eviction
    /// is the worst of both: geometry given back and nothing acquired
    /// with the room it made. So the prefix of victims cheap enough to
    /// displace is measured first, and released only if it is enough.
    ///
    /// False means the budget cannot accommodate this payload — it is
    /// worth less than what holding it would cost. It stays
    /// outstanding, and a camera move reconsiders it for free.
    ///
    /// \a starved, when given, reports *why* a false is false: true
    /// means the walk ran out of victims before any was too dear —
    /// nothing resident stood against this payload, the budget being
    /// consumed by requests still in flight or by payloads that cannot
    /// be given back. That refusal is not a comparison and must not be
    /// memoized against the payload: the first issue round pledges the
    /// whole budget before anything is resident, and a "no" recorded
    /// there froze the scene at whatever the first round happened to
    /// ask for — near objects coarse for good, with the evictor never
    /// once running. False with \a starved false is the real verdict:
    /// a victim was met that the margin refused to displace.
    bool makeRoom(float incoming, size_t need, bool *starved = nullptr);

    void setTrace(Trace trace) { m_trace = std::move(trace); }

private:
    void build();

    SceneSnapshot &m_snap;
    RungRanker &m_ranker;
    ResidentTest m_isResident;
    Release m_release;
    Trace m_trace;
    /// Ascending by residency score: the worst to keep comes first.
    std::vector<std::pair<float, size_t>> m_victims;
    size_t m_next = 0;
    bool m_built = false;
};

}  // namespace Render

#endif  // RENDERER_SCENE_LADDER_H

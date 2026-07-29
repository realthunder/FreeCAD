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
/// the camera sees; 1 ranks strictly per byte.
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
    bool makeRoom(float incoming, size_t need);

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

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

#ifndef RENDERER_SCENE_DUMP_H
#define RENDERER_SCENE_DUMP_H

/// Binary snapshot of a complete backend scene feed — the draw calls
/// with their mesh/texture data plus every per-frame config — written
/// by the desktop bridge (FC_BGFX_DUMP_SCENE=<path>, captured on the
/// first non-empty render) and replayed by the standalone/WebAssembly
/// viewer. Little-endian, versioned; both sides of a transfer must be
/// built from the same serializer version.

#include <functional>
#include <map>
#include <tuple>

#include "Renderer.h"

namespace Render {

/// One geometry ladder's mesh objects, one per rung, keyed by the
/// rung's CONTENT key (docs/SceneStreaming.md §7, "one rung per
/// instance, not per content"). Rung indices shuffle when a delta
/// re-declares a ladder's levels; content keys never do, so the store
/// of arrays is keyed by what cannot move. Each rung's mesh object has
/// its own cacheId derived from its key — to the GPU cache a rung is
/// an ordinary immutable-ish mesh, and a refinement is a DIFFERENT
/// object rather than an in-place overwrite (MeshData::generation now
/// only moves on release-and-refill of the same rung).
///
/// Held by the DeferredChunk as a shared_ptr: closures and carried
/// entries share the one store, and the draws parsed against the
/// ladder hold the identity object below, so the arrays live exactly
/// as long as something can still draw them.
struct LevelMeshes {
    virtual ~LevelMeshes() = default;
    /// The parse-time mesh object every draw of this ladder holds —
    /// the slot of the ladder's finest built rung when it was first
    /// read. What a consumer with no level selection fills and draws.
    virtual std::shared_ptr<const MeshData> identity() const = 0;
    /// The rung stored under \a key, or null while nothing was ever
    /// filled there. May be empty (released) — check its arrays.
    virtual std::shared_ptr<const MeshData> at(
        const std::string &key) const = 0;
    /// Parse \a data as the rung stored under \a key, creating the
    /// mesh object on first use. False = the bytes would not parse
    /// (the arrays are left empty, never half-read).
    virtual bool fill(const std::string &key, const void *data,
                      size_t size) = 0;
    /// Empty the rung stored under \a key, keeping the object (draws
    /// may still name it; the GPU cache sees the generation move).
    virtual void release(const std::string &key) = 0;
    /// State the relative error the rung under \a key was built at.
    /// The ladder knows it, but nothing downstream of the rung binder
    /// can see the ladder -- every consumer reads the mesh it was
    /// handed (MeshData::levelError) -- so an unstamped coarse rung
    /// reads as the exact tessellation. The element gate's
    /// coarse-faces rule asks exactly that question.
    virtual void stampError(const std::string &key, float error) = 0;
};

struct SceneSnapshot {
    DrawCallList scene;
    /// Selection feeds keyed by selection id (SelIdBits) and the
    /// preselection highlight, as fed through addSelection()/
    /// setHighlight() (v2; empty on v1 snapshots).
    std::vector<std::pair<int, DrawCallList>> selections;
    DrawCallList highlight;
    bool highlightWholeOnTop = false;
    /// Overlay feeds as fed through setOverlay() (v3; empty on older
    /// snapshots): viewport-anchored content — the foreground
    /// superimposition, the corner axis cross — replayed by the
    /// standalone/WASM viewer with its own viewport and camera.
    struct Overlay {
        int id = 0;
        OverlayAnchor anchor;
        DrawCallList draws;
    };
    std::vector<Overlay> overlays;
    Background background;
    HiddenLineConfig hlconfig;
    SectionConfig secconf;
    AOConfig aoconf;
    CavityConfig cavityconf;    ///< v42; defaulted on older snapshots
    MatcapConfig matcapconf;    ///< v43; defaulted on older snapshots
    PBRConfig pbrconf;
    BumpConfig bumpconf;
    LightConfig lightconf;
    /// v52; `fed` false on older snapshots, which is exactly what makes
    /// them keep the fixed headlight they were rendered with.
    ViewLightConfig viewlightconf;
    VolumetricConfig volconf;
    WaterConfig waterconf;
    BloomConfig bloomconf;      ///< v18; defaulted on older snapshots
    /// v60; None on older snapshots, which is what makes them keep
    /// being drawn the way they were written -- unencoded.
    OutputConfig outconf;
    RenderDebugConfig debugconf; ///< v20; defaulted on older snapshots
    /// User shaders (docs/RenderDebug.md §6; v23; empty on older
    /// snapshots): the scene-level post-stage list. "material"-stage
    /// programs ride the draw materials (Material::usershader) through
    /// a deduplicated shader table in the stream. Serialization
    /// includes the server-compiled viewer binaries
    /// (UserShader::compiled) so the compiler-less viewer tiers can
    /// load the programs.
    UserShaderConfig usershaderconf;
    /// Save-side hook: called once per unique user shader written, on
    /// the COPY that travels, to make it whole for the viewer tiers --
    /// append the server-compiled binary variants beyond whatever the
    /// shader already carries, and resolve a MaterialX document's image
    /// layout (UserShader::Image::layer, imageSampler, imageUnit)
    /// against the producer's generator, which those tiers do not have
    /// (docs/MaterialStorage.md sec 17.23). Unset when loading (and in
    /// tiers with no compiler).
    std::function<void(UserShader &shipped)> shipShader;

    /// Save-side hook enabling out-of-band texture payloads (v26): when
    /// set, a texture is written as its header plus its content key,
    /// and the pixels are handed here instead of into the stream. The
    /// streaming transport uses it so a republish — which happens on
    /// every feed change, including a selection pick — no longer
    /// re-sends every embedded image; the viewer fetches each key once
    /// and caches it. Unset for a bundled snapshot, which must stay
    /// self-contained.
    typedef std::function<void(const std::string &key,
                               std::vector<uint8_t> &&pixels)> TextureBlobSink;
    TextureBlobSink textureBlobs;

    /// Save-side hook for the chunks the manifest layout is built from
    /// (v33): the draw-group manifests themselves, the materials and
    /// the user shaders. **Setting it is what selects that layout** —
    /// a bundled snapshot leaves it unset and stays one self-contained
    /// document with global tables, which is the only thing a
    /// `.fcsd` capture can be.
    ///
    /// It supersedes v31's single material-table blob. That table was
    /// referenced by a global index, and a global index is exactly the
    /// false invalidation the manifest tree exists to avoid: inserting
    /// one material renumbers every group manifest that follows it.
    /// So a material is keyed individually, like a mesh.
    typedef std::function<void(const std::string &key,
                               std::vector<uint8_t> &&bytes)> ChunkBlobSink;
    ChunkBlobSink chunkBlobs;

    /// Save-side hook enabling out-of-band mesh payloads (v28) — the
    /// mesh counterpart of textureBlobs, with two differences that
    /// follow from meshes being many and small rather than few and
    /// large. They are pulled in batches packed to a byte budget
    /// rather than one request per mesh, and the content key is
    /// memoized on
    /// `MeshData::cacheId` instead of recomputed: a cacheId is unique
    /// per content within the process (Renderer.h), so a hit needs no
    /// re-hash, while a re-tessellation mints a new id, misses, and is
    /// hashed once. That is what keeps the hashing cost proportional
    /// to changed geometry rather than to publishes.
    ///
    /// The identity is deliberately not part of the hashed bytes:
    /// cacheId is a bare counter, so a re-tessellation producing
    /// identical geometry would otherwise mint a new key for unchanged
    /// content and defeat the caching entirely.
    struct MeshBlobSink {
        /// The content key already stored for this cacheId, retained
        /// for the publish in flight, with the size of the chunk it
        /// names; empty when the chunk has to be serialized, hashed
        /// and stored.
        std::function<std::string(uint64_t cacheId, uint32_t &size)> reuse;
        /// Take over a freshly serialized chunk under its content key.
        std::function<void(uint64_t cacheId, const std::string &key,
                           std::vector<uint8_t> &&chunk)> store;
        /// The key of a generated level of the mesh whose exact chunk
        /// is \a source, or empty while nobody has built it (§7, phase
        /// 5c). Consulted per declared level, so a level finished since
        /// the last publish is announced by this publish as a keyed
        /// entry — the ordinary path, no side channel. Optional: unset
        /// means every declared level is written unbuilt, which is
        /// exactly what v36 wrote.
        std::function<std::string(const std::string &source, uint32_t level,
                                  uint32_t &size)> built;
        explicit operator bool() const { return bool(reuse) && bool(store); }
    };
    MeshBlobSink meshBlobs;

    /// Load-side counterpart of every out-of-band payload but a
    /// payload: a group manifest, a mesh, a material, a shader, a
    /// texture. `fill` parses the fetched bytes into storage the loader
    /// owns (its types are private to the serializer), and **may name
    /// further deferrals of its own** — a group names its meshes and
    /// materials, a material names its textures — so a consumer
    /// resolves in rounds until nothing is outstanding rather than in
    /// one pass. It takes the snapshot at call time: a staged snapshot
    /// is moved before it is applied, which a captured pointer would
    /// not survive.
    ///
    /// **One list, no payload kinds.** A consumer decides how to fetch
    /// an entry from its `size` alone and never from what is inside it:
    /// a large payload is worth a request of its own, a small one is
    /// worth batching with its neighbours, and that is as true of a
    /// texture as of a mesh. Nothing here says which is which.
    ///
    /// A snapshot must not be fed to a backend until every entry is
    /// filled and finalize() has run.
    struct DeferredChunk {
        std::string key;
        /// Payload size in bytes, always known before the fetch: it is
        /// what the fetch policy is chosen from, and what lets a batch
        /// be packed to a byte budget. A payload larger than the budget
        /// is not a special case — it simply ends up alone.
        uint32_t size = 0;
        /// Which objects this payload is needed by — the reverse index
        /// of docs/SceneStreaming.md §6, recorded as the references are
        /// read rather than reconstructed later. Empty means no object
        /// needs it: the root's own sections and the overlay feeds,
        /// which belong to the scene rather than to anything in it.
        ///
        /// It is what lets a consumer put a fetch order on chunks it
        /// cannot otherwise tell apart. A mesh key says nothing about
        /// where the mesh is, but the entry of an object that wants it
        /// carries a bounding box, so this turns a flat list of
        /// outstanding payloads into one that can be sorted by the
        /// camera.
        ///
        /// **A list, because content addressing means one chunk serves
        /// many objects, and the fetch order has to follow the most
        /// important of them.** Taking the first — the object whose
        /// manifest happened to name a shared material first — starves
        /// every other object that needs it behind whatever that one
        /// object is worth. With dedup that is not a corner case: one
        /// material can back a whole scene, and one mesh backs every
        /// instance of a part.
        ///
        /// **A chunk deferred by another chunk's fill inherits that
        /// chunk's owners**, and the consumer that runs the fills is
        /// what propagates them: a material is parsed long after the
        /// group that asked for it, so the textures it names have no
        /// other way to say who wanted them.
        std::vector<uint64_t> owners;
        /// The ladder of levels this payload exists at, coarsest first
        /// and ending with the exact content (v36, mesh entries under
        /// the manifest layout; docs/SceneStreaming.md §7). `error` is
        /// relative to the mesh's own diagonal; an **empty key is a
        /// level declared possible but not generated** — it cannot be
        /// fetched, only asked to be built (RungProvider::generate),
        /// because a chunk with no bytes has no content address. The
        /// entry's own key and size always name the finest *built*
        /// level; choosing a coarser one is level selection, phase 5
        /// slice 4, and until then the list is carried, not consulted.
        struct Level {
            float error = 0.0f;
            std::string key;
            uint32_t size = 0;
        };
        std::vector<Level> levels;
        /// Give this payload back — the ladder's downward step
        /// (docs/SceneStreaming.md §6, phase 4b). Set only where a
        /// lower rung exists: a mesh, whose draws fall back to the box
        /// on their bounds, so releasing it costs fidelity and not the
        /// object. Empty means the payload cannot be given up — a
        /// manifest, a material, a texture — and eviction passes over
        /// it, which is also why this is a capability the entry states
        /// rather than a payload kind the consumer tests for.
        ///
        /// The entry stays exactly as it was before the payload
        /// arrived, so \a fill re-run puts it back: eviction is the
        /// reverse of arrival and not a state of its own. What it
        /// costs to undo is one fetch, and the payload is in the local
        /// store, so usually not even a download.
        std::function<void()> release;
        /// \a data null means the payload could not be obtained at
        /// all. Whether that is survivable is the entry's own business,
        /// not the consumer's: a texture clears its deferred flag and
        /// returns true, because the draw renders untextured rather
        /// than not at all, while geometry returns false and the
        /// snapshot is not applied.
        std::function<bool(SceneSnapshot &snap,
                           const void *data, size_t size)> fill;
        /// The payload is a texture's pixels (or its encoded file):
        /// \a fill writes that one texture object and nothing of the
        /// snapshot it was born in, so it may run against any
        /// snapshot -- which is what lets a consumer carry it across
        /// a publish that superseded the one naming it. A group's or
        /// a material's fill writes into its own snapshot's tables and
        /// may not.
        bool texture = false;
        /// The rung the current plan targets (docs/SceneStreaming.md
        /// §7, "Selection is a plan, not a reaction"): an index into
        /// \a levels, `kPlanBox` for the box below the ladder, or
        /// `kPlanUnset` while no plan has looked at this entry — read
        /// as "the finest built rung", which is what a consumer did
        /// before there was a plan. Written by Render::planLevels,
        /// consumed by Render::planStep; the producer never sets it.
        static constexpr int16_t kPlanUnset = -2;
        static constexpr int16_t kPlanBox = -1;
        int16_t plan = kPlanUnset;
        /// The parse closure, kept for the ladder's lifetime. \a fill
        /// is consumed — cleared once run, cleared wholesale when a
        /// publish gives up — but geometry is re-fillable by design
        /// (release and climb again, fetch a different rung), and the
        /// closure that parsed one rung parses them all: it captures
        /// the mesh object it writes, never the parse it was born in.
        /// Owning it here is what lets residency survive a re-key: the
        /// old key-indexed refill map lost its entries whenever a
        /// ladder was renamed, which the journal reported as "no
        /// refill" — a ladder below plan that could never be fetched.
        std::function<bool(SceneSnapshot &snap,
                           const void *data, size_t size)> refill;
        /// The rungs this ladder holds filled, bit i = rung i in
        /// planRungs() space (docs/SceneStreaming.md §7, "the ladder
        /// owns its fetch state"). This is the residency truth — the
        /// store's key-indexed books are accounting, not authority.
        /// A SET because instances share a ladder and stand at
        /// different rungs ("one rung per instance, not per
        /// content"): the near owner's exact and the far owner's
        /// coarse rung are resident at once, each in its own mesh
        /// object (LevelMeshes). Set by the consumer when a rung's
        /// bytes fill, cleared per rung by release; a carried entry
        /// keeps it. A ladder refresh (a delta re-declaring levels
        /// under the same identity) re-maps the bits by key, so
        /// resident geometry stays resident under a renamed ladder.
        uint16_t residentMask = 0;
        /// Per-rung mesh objects, shared with the parse closures and
        /// every carried copy of this entry. Geometry only.
        std::shared_ptr<LevelMeshes> levelMeshes;
        /// The rung a fetch is out for, -1 for none: the "one request
        /// per ladder" rule reads its own field instead of scanning
        /// every rung's key against the download table. Cleared when
        /// the request resolves, fails, or is presumed lost.
        int16_t asked = -1;
        /// The payload itself, riding in the root that named it (v37,
        /// docs/SceneStreaming.md §5). Only a delta's group manifests
        /// do this: they are new by definition — an object is in the
        /// delta exactly because its manifest key changed — so a cache
        /// can never answer for them, and fetching a kilobyte by round
        /// trip is what made every delta a chain of waits. A consumer
        /// ingests the bytes under `key` (store included) and then
        /// treats the entry like any other; empty means fetch as
        /// usual. Leaves (meshes, materials, textures) and full roots
        /// stay by reference — their keys usually ARE cached.
        std::vector<uint8_t> inlineData;
    };
    std::vector<DeferredChunk> deferredChunks;

    /// Textures by content key ACROSS publishes, installed by the
    /// consumer before a load (docs/MaterialStorage.md sec 17.23). A
    /// republish re-parses every chunk it names into fresh objects,
    /// but the draws an unchanged object keeps still hold the objects
    /// of the publish that carried them -- and a texture whose pixels
    /// were in flight when the next publish arrived was filled into an
    /// object nothing drew, or into none at all. With the memo the
    /// re-parse hands back the object the draws already hold, so a
    /// fill from any publish lands where it is looked at. Weak, so a
    /// texture nothing holds any more is simply read again. Unset
    /// (the default) is one publish at a time, which is what a
    /// bundled capture is.
    typedef std::map<std::string, std::weak_ptr<TextureImage>> TextureMemo;
    std::shared_ptr<TextureMemo> textureMemo;

    //////////////////////////////////////////////////////////////////
    // Delta publishing (v34, docs/SceneStreaming.md §5)

    /// One object as the root manifest names it: its identity, the box
    /// that lets a consumer act on it before its geometry arrives, and
    /// the key of the manifest describing it.
    struct ObjectEntry {
        uint64_t objectKey = 0;
        float bbox[6] = {0, 0, 0, 0, 0, 0};
        std::string key;    ///< the group manifest's content key
        uint32_t size = 0;
        /// Document identity of the object behind the key (v38,
        /// docs/ThinClient.md §4.1), resolved by the producer via
        /// Renderer::setObjectInfo. Empty when the producer could not
        /// name the draws (merged caches, overlays). Deliberately NOT
        /// part of the group manifest chunk: it must not disturb the
        /// content keys, and a rename should not re-key geometry.
        ObjectInfo info;
        /// Whether the producer held part of this object back when it
        /// published (v55, DrawCall::objectIncomplete): a companion
        /// draw whose capture the publish budget deferred is LATE, not
        /// absent. The element contract reads it to tell a late
        /// companion from a display mode that genuinely draws points or
        /// edges alone (docs/SceneStreaming.md #13b) -- without it a
        /// consumer grants the Points/Wireframe exemption to an object
        /// whose faces are merely still coming, which is the dots-first
        /// load storm reproduced one tier further out.
        ///
        /// Beside `info` and for the same reason: it is a property of
        /// the publish, not of the geometry, and it flips as the
        /// producer's capture backlog drains. Inside a content key it
        /// would retire an object's cached chunks for a state bit.
        bool incomplete = false;
    };

    /// Which publish this one is, and which it is encoded against.
    /// `baseVersion` 0 means the object list is complete; otherwise it
    /// carries only what changed since that version, and a consumer
    /// holding anything else has to be given a full root instead.
    uint64_t manifestVersion = 0;
    uint64_t baseVersion = 0;

    /// Which run of the backend numbered those versions (v35). A
    /// restarted server begins counting again, so a version alone does
    /// not say what a consumer holds: one carried across the restart
    /// names a publish that never happened. A consumer whose session
    /// differs must treat its version as 0.
    ///
    /// What it does *not* invalidate is the consumer's chunk store.
    /// Keys are content hashes, so a restarted backend republishes the
    /// same bytes under the same keys, and a reconnect after a restart
    /// is a full object list over an almost-warm cache.
    uint64_t sessionId = 0;

    /// Save side. `baseObjects` is the object list of `baseVersion` —
    /// set it, with a non-zero baseVersion, to publish a delta. The
    /// writer records this publish's list in `objectEntries` for the
    /// next one to be encoded against, which is why the publisher keeps
    /// it rather than the serializer: only the publisher knows which
    /// versions a consumer might still hold.
    std::vector<ObjectEntry> baseObjects;
    std::vector<ObjectEntry> *objectEntries = nullptr;

    /// Save side (v38): the producer's objectKey → document identity
    /// map (Renderer::setObjectInfo). The writer stamps each object
    /// entry's `info` from it; null = entries stay unnamed.
    const ObjectInfoMap *objectInfo = nullptr;
    /// Save side: the labels those identities carry to a viewer
    /// (Renderer::setObjectMeta), joined onto each entry by document and
    /// object name. Null or missing = the entry goes out with identity
    /// and no label, which is what a viewer's fallback expects — it
    /// shows the internal name, which is what identifies the object
    /// anyway.
    const ObjectMetaMap *objectMeta = nullptr;

    /// Where the parts of a root that may later be rewritten ended up
    /// in the payload, recorded on save when non-null.
    ///
    /// A server holds published bytes, not the scene they came from, so
    /// it cannot re-serialize a root to suit one viewer. What it can do
    /// is replace the object list in a payload it already has, which is
    /// all that separates a full root from a delta — see
    /// spliceObjectDelta() and docs/SceneStreaming.md §5. Meaningful
    /// only for the in-memory (streaming) save.
    struct RootSpans {
        size_t baseVersionAt = 0;   ///< the baseVersion field
        size_t listBegin = 0;       ///< the object-list section
        size_t listEnd = 0;
    };
    RootSpans *rootSpans = nullptr;

    /// Load side: what this publish says about the object list — the
    /// entries it carries (all of them for a full root, the changed
    /// ones for a delta) and, for a delta, the objects it retires.
    /// `group` indexes `groups` for the draws.
    struct ObjectUpdate {
        ObjectEntry entry;
        size_t group = 0;
    };
    std::vector<ObjectUpdate> objectUpdates;
    std::vector<uint64_t> objectsRemoved;

    /// Load-side staging for the manifest layout. The draws of each
    /// group land in `groups` at the index the root named them at —
    /// not appended as they arrive — so the feed order a backend sees
    /// does not depend on the order chunks came back in. `materials`
    /// is the same idea for the materials draws reference by
    /// `DrawCall::materialIndex`. `finalize` stitches both into the
    /// feeds and is run once, after the last chunk.
    std::vector<DrawCallList> groups;
    /// Which entries of `groups` hold draws that have arrived and not
    /// yet been taken. Assembly happens by moving, so without this it
    /// could only run once — and it has to run every time a chunk
    /// lands, because a scene is drawn while it is still arriving
    /// (docs/SceneStreaming.md §6). An empty group is a real answer (a
    /// feed that went empty), which is why arrival is a flag rather
    /// than a count.
    std::vector<uint8_t> groupFilled;
    /// The draws no object owns, kept apart from `scene` so that the
    /// feed can be rebuilt from the model as often as needed instead of
    /// being the thing that accumulates.
    DrawCallList keyless;
    std::vector<Material> materials;
    /// Which entries of `materials` have had their chunk parsed. A
    /// draw is assembled from its group manifest, which can be in hand
    /// well before the materials it references, and the table entry
    /// until then is a default-constructed Material that would be
    /// baked into the draw and never revisited. So an object's draws
    /// are taken only once the appearance they name is real
    /// (docs/SceneStreaming.md §6).
    std::vector<uint8_t> materialFilled;
    std::function<void(SceneSnapshot &snap)> finalize;
    float autozoomScale = 1.0f;
    /// Resolution scale (0.25-1.0) of the expensive screen-space effect
    /// passes (reflection re-render, SSAO resolve); 1.0 = full resolution.
    float effectResolution = 1.0f;
    float ssaoResolution = 1.0f;

    /// Preselection (hover) and selection highlight styling from ViewParams
    /// (v10). The standalone/WASM viewer builds both highlights locally (no
    /// round trip), so it reads these to fill or only outline the hovered /
    /// selected face like the backend would (color, outline width, etc.).
    PreselHighlightConfig preselconf;
    PreselHighlightConfig selconf;

    /// Section-cap hatch image, RGBA8 (the renderer stores it
    /// pre-expanded); null = none. A texture-table entry since v27, so
    /// that it shares the content key and the out-of-band payload path
    /// above — as a raw blob outside the table it was re-sent whole on
    /// every publish, and it is the single largest item in the stream.
    std::shared_ptr<const TextureImage> hatch;

    /// Camera and viewport at capture time (GL-layout matrices).
    float viewMatrix[16];
    float projMatrix[16];
    int width = 0;
    int height = 0;
    /// Clear/background color at capture, packed 0xRRGGBBAA.
    uint32_t clearColor = 0x333333ff;
};

/// A consumer's view of the objects across publishes. A delta names
/// only what changed, so somebody has to remember the rest — and it
/// cannot be the snapshot, which is one publish. Keyed by objectKey and
/// therefore in a fixed order, so the draw order a backend sees is the
/// same however the objects arrived: as one full root, or as a full
/// root and a chain of deltas.
struct SceneObjectModel {
    struct Object {
        SceneSnapshot::ObjectEntry entry;
        DrawCallList draws;
        /// The group manifest the draws came from. Equal to
        /// `entry.key` once the geometry the current entry names is in
        /// hand; different — or empty — while it is still arriving, in
        /// which case the draws are the rung this object was last
        /// drawn at (docs/SceneStreaming.md §6). A key rather than a
        /// flag because an object with no draws is not the same as one
        /// still waiting: an empty feed is a real answer.
        std::string drawsKey;
        bool resolved() const { return drawsKey == entry.key; }
    };
    std::map<uint64_t, Object> objects;
    /// The consumer's per-instance rung binding (docs/SceneStreaming.md
    /// §7, "one rung per instance"): given a draw, the mesh object of
    /// the rung ITS OWNER should stand on, or null to fall through to
    /// the default (the parse-time identity mesh, the bridges, the
    /// box). Unset = every instance draws the ladder's identity mesh,
    /// the pre-Stage-B behavior; the desktop resolve and the tests
    /// leave it unset.
    std::function<std::shared_ptr<const MeshData>(const DrawCall &)>
        rungBinder;
    /// The version the model holds, i.e. what a delta must be based on.
    uint64_t version = 0;
    /// The last live arrays seen per content-addressed mesh id
    /// (meshIdFromKey sets the top bit; producer-counter ids never
    /// enter). A publish re-parses its manifests into fresh, empty
    /// payload objects under the same content identity, and without
    /// this every re-described object dropped to its box for the
    /// length of a re-read of bytes the consumer was already drawing —
    /// the "all meshes flash to boxes when the scene settles" artifact.
    /// Assembly bridges the gap with the copy still in hand
    /// (appendAtBestRung); the fresh object takes over as soon as its
    /// fill runs, so upgrades stay visible. Weak pointers: a mesh
    /// nothing draws costs nothing here, and a *released* mesh empties
    /// in place and stops qualifying — eviction still shows the box it
    /// decided on.
    std::map<uint64_t, std::weak_ptr<const MeshData>> lastGood;

    /// The last live geometry per stable ladder identity: objectKey ×
    /// role, the role being a draw's primitive type and its ordinal
    /// among the object's same-typed geometry draws. `lastGood` above
    /// bridges a re-parse of the SAME content; this bridges a
    /// **re-key** — an edit gives the object new content keys, so the
    /// content-addressed bridge cannot answer by construction, and the
    /// edited object dropped to its box for the length of two fetches
    /// (measured: 600 → 598 draws on a one-object edit). Identity
    /// survives the re-key, so the object keeps showing what it was
    /// until the new geometry lands. The whole draw rides, not just
    /// the mesh: old arrays are only correct under the old index
    /// ranges and the old placement. Weak mesh, the same rules as
    /// lastGood — a released mesh empties in place and stops
    /// qualifying, so eviction still shows the box it decided on.
    /// (First step of the ladder-identity refactor: the id that will
    /// own {resident, target, in-flight} outlives any content key.)
    struct RoleDraw {
        std::weak_ptr<const MeshData> mesh;
        /// The draw as last emitted, its mesh pointer cleared (the
        /// weak one above decides liveness).
        DrawCall draw;
    };
    std::map<std::tuple<uint64_t, int, uint32_t>, RoleDraw> lastRole;

    /// How many objects the root named whose geometry has not arrived.
    size_t unresolved() const;
    /// The union of the bounding boxes the root named, whether or not
    /// the geometry inside them has arrived. This is what the initial
    /// camera fit wants: it frames the model correctly before the
    /// first triangle exists, and does not lurch as geometry streams
    /// in (§6). False when the model names nothing.
    bool boundBox(float *min3, float *max3) const;
};

/// The unit box every coarse stand-in is drawn with: the corners of
/// [0,1]³, one normal per face, shared by every stand-in in the session
/// (docs/SceneStreaming.md §6). The per-mesh variation is the model
/// matrix that puts it on the bounding box, which the draw record
/// carries anyway — so the bottom rung of the ladder costs one GPU
/// upload however much of the model is still in flight.
RendererExport const std::shared_ptr<const MeshData> &standInMesh();

/// Carry resident geometry from a superseded snapshot into the one
/// replacing it, matched by rung content key (docs/SceneStreaming.md
/// §7: "rung indices shuffle on re-declaration; content keys never
/// do"). A publish that re-describes an object replaces its ladder
/// with a freshly parsed one — empty stores, residentMask zero — and
/// without this every announcement in a chain dumped the whole
/// resident set back onto the network: measured as the same rungs
/// re-asked every ~200 ms for as long as a cold backend kept building
/// levels. Fills the fresh ladders' stores from the old ones and sets
/// the matching residentMask bits; returns how many rungs moved. Call
/// before rebooking residency (the consumer's held-payload ledger),
/// which is derived from the masks this writes.
RendererExport size_t carryResidentRungs(SceneSnapshot &fresh,
                                         const SceneSnapshot &old);

/// Merge a loaded publish into \a model and rebuild `snap.scene` from
/// the result. Returns false when the publish is a delta against a
/// version the model does not hold — the caller has to ask for a full
/// root, and must not apply the snapshot.
///
/// Call after finalize(), and call it again on every payload that
/// lands afterwards: a scene is drawn while it is still arriving, so
/// this assembles what is in hand rather than waiting for the last
/// chunk (§6). Re-running it for a publish the model already holds is
/// how refinement happens, not an error.
RendererExport bool applySceneObjects(SceneSnapshot &snap,
                                      SceneObjectModel &model);

/// The difference between two object lists, both ordered by objectKey:
/// the entries of \a to that \a from does not already have, and the
/// keys \a from has that \a to does not. An object counts as unchanged
/// exactly when its group manifest key is unchanged, since that key
/// covers the whole group — bounding box included.
RendererExport void diffObjectLists(
        const std::vector<SceneSnapshot::ObjectEntry> &from,
        const std::vector<SceneSnapshot::ObjectEntry> &to,
        std::vector<SceneSnapshot::ObjectEntry> &changed,
        std::vector<uint64_t> &removed);

/// Rewrite \a full — a serialized root carrying a complete object list
/// — into one carrying only \a changed and \a removed, as a delta
/// against \a baseVersion. Nothing else about the payload moves: the
/// inline state, the draws of the objects that ride inline, and every
/// chunk key it names are the bytes of \a full.
///
/// This is how a server serves a viewer that is behind without holding
/// the scene: it knows what that viewer is missing, it has the latest
/// published payload, and the only difference between what it has and
/// what the viewer needs is which objects the list names. \a spans must
/// be the ones recorded when \a full was saved.
///
/// The result is applicable by a consumer holding exactly
/// \a baseVersion — the same contract as a natively serialized delta.
///
/// \a bytesFor answers a group manifest's content key with its stored
/// bytes, or null; what it answers rides INLINE in the delta (v37,
/// DeferredChunk::inlineData), so the consumer never round-trips for a
/// manifest that is new by definition. Empty means every entry goes by
/// reference — still a valid v37 delta, just a slower one.
typedef std::function<const std::vector<uint8_t> *(const std::string &)>
    ChunkBytesFor;
RendererExport bool spliceObjectDelta(
        const std::vector<uint8_t> &full,
        const SceneSnapshot::RootSpans &spans,
        uint64_t baseVersion,
        const std::vector<SceneSnapshot::ObjectEntry> &changed,
        const std::vector<uint64_t> &removed,
        std::vector<uint8_t> &out,
        const ChunkBytesFor &bytesFor = {});

/// The object-list section codec, exported for payload formats that
/// ride the server's splice path with their own root layout
/// (docs/TechDrawPortAndSection.md sec 24 -- the 2D page). The bytes
/// are exactly what a scene root's `[listBegin, listEnd)` span holds,
/// which is the admission price of spliceObjectDelta: the server
/// rewrites that span with this encoding and copies everything else
/// verbatim.
///
/// write: \a bytesFor non-null marks the DELTA form -- every entry is
/// followed by a flag byte and, when the provider answers its key, the
/// chunk's own bytes inline (v37). The section itself cannot say which
/// form it is; the payload's baseVersion field does, so the caller
/// must keep the two consistent.
RendererExport bool writeObjectSection(
        std::vector<uint8_t> &out,
        const std::vector<uint64_t> &removed,
        const std::vector<SceneSnapshot::ObjectEntry> &entries,
        const ChunkBytesFor *bytesFor = nullptr);

/// One parsed entry: the reference, and the chunk bytes when the delta
/// carried them inline.
struct ObjectSectionEntry {
    SceneSnapshot::ObjectEntry entry;
    bool hasInline = false;
    std::vector<uint8_t> inlineData;
};

/// Parse a section written by writeObjectSection, advancing \a p; \a
/// deltaForm must repeat what the payload's baseVersion said (nonzero
/// = delta), because the flag byte's presence is decided there. False
/// leaves \a p unspecified.
RendererExport bool readObjectSection(
        const uint8_t *&p, const uint8_t *end,
        bool deltaForm,
        std::vector<uint64_t> &removed,
        std::vector<ObjectSectionEntry> &entries);

RendererExport bool saveSceneSnapshot(const char *path,
                                      const SceneSnapshot &snap);
RendererExport bool loadSceneSnapshot(const char *path,
                                      SceneSnapshot &snap);

/// In-memory variants (the live-streaming transport).
RendererExport bool saveSceneSnapshot(std::vector<uint8_t> &out,
                                      const SceneSnapshot &snap);
RendererExport bool loadSceneSnapshot(const void *data, size_t size,
                                      SceneSnapshot &snap);

/// The serializer format version this build writes (and the newest it
/// can read) — the version-handshake token of the viewer control
/// channel (docs/RenderDebug.md §4.4).
RendererExport uint32_t sceneDumpVersion();

/// SHA-1 of a byte range as 40 lowercase hex characters — the content
/// key an out-of-band payload is addressed by. Exposed so a consumer
/// can verify a payload it got from somewhere it does not control (a
/// browser's IndexedDB store) actually is the bytes that key names.
RendererExport std::string sha1Hex(const void *data, size_t size);

/// A material's identity as one value — the bucket a far-field proxy is
/// generated per (docs/FarFieldProxies.md §5.1, ProxyHierarchy.h).
///
/// Equality is the serialized bytes, the same rule the snapshot's own
/// material table dedups by, so a field added to the format cannot
/// silently drop out of the bucket and merge two materials that render
/// differently. Textures and shaders enter by pointer identity.
RendererExport uint64_t materialIdentity(const Material &m);

/// Build the bytes of a declared level from the bytes of the exact
/// mesh chunk it was declared on (§7, phase 5c): parse, decimate on
/// the level's grid (MeshSimplify), re-serialize. The output is an
/// ordinary mesh chunk — same layout, its own content key — and the
/// element tables and refinement subsets carry over, so the level
/// picks like the mesh it stands in for. False when the chunk does
/// not parse or the level would not simplify anything, in which case
/// the level simply stays unbuilt: a rung that saves nothing is not
/// worth a chunk.
RendererExport bool generateMeshLevel(const void *chunk, size_t size,
                                      uint32_t level,
                                      std::vector<uint8_t> &out);

/// A mesh chunk parsed back into a MeshData that owns its arrays — the
/// producer-side counterpart of the consumer's deferred-chunk parse.
/// What a shape-backed level generator (MeshSource.h) reads the source
/// chunk through: the part tables and flags of the exact mesh are the
/// contract its output has to honor index for index.
struct ParsedMeshChunk : MeshData {
    std::vector<float> posStore;
    std::vector<float> normStore;
    std::vector<uint8_t> colorStore;
    std::vector<uint8_t> matStore;
    std::vector<float> uvStore;
    std::vector<int32_t> triStore;
    std::vector<int32_t> lineStore;
    std::vector<int32_t> pointStore;
    std::vector<int32_t> noSeamStore;
};

/// Parse a serialized mesh chunk (this build's layout — chunks come
/// from this build's own store, no compatibility question). False when
/// the bytes are not a mesh chunk.
RendererExport bool parseMeshChunk(const void *chunk, size_t size,
                                   ParsedMeshChunk &out);

/// Serialize \a m as an ordinary mesh chunk — the exact encoding
/// writeMesh uses, so a generated level is indistinguishable from a
/// published mesh. The cacheId is deliberately not part of the bytes.
RendererExport bool encodeMeshChunk(const MeshData &m,
                                    std::vector<uint8_t> &out);
/// Peek the format version of a serialized snapshot (the payload
/// without the 8-byte stream-version prefix); 0 when it is not a
/// snapshot. A viewer receiving a payload newer than its own
/// sceneDumpVersion() reloads itself.
RendererExport uint32_t sceneSnapshotVersion(const void *data, size_t size);

} // namespace Render

#endif // RENDERER_SCENE_DUMP_H

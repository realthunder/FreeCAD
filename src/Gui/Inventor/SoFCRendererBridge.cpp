/****************************************************************************
 *   Copyright (c) 2026 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
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
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#include <fstream>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include <QFile>
#include <QImage>

#include <App/Application.h>
#include <App/Document.h>
#include <App/PropertyFile.h>
#include <App/PropertyGeo.h>
#include <App/PropertyStandard.h>
// PropertyLength: the ground's explicit extents are lengths, as the
// Coin quad reads them.
#include <App/PropertyUnits.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/fields/SoSFBool.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoBumpMap.h>
#include "SoFCRenderMaterial.h"
#include "../Renderer/MeshSource.h"
#include "../Renderer/ProxyHierarchy.h"
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoShaderObject.h>
#include <Inventor/nodes/SoVertexShader.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoShaderParameter.h>

#include "SoAutoZoomTranslation.h"
#include "../Application.h"
#include "../Document.h"
#include "SoFCRendererBridge.h"
#include "SoFCDisplayModeElement.h"
#include "SoFCRenderCacheManager.h"
#include "SoFCRenderer.h"
#include "SoFCVertexCache.h"
#include "../ViewParams.h"
#include "../RenderParams.h"
#include "../View3DInventor.h"
#include "../Renderer/ImageDecode.h"
#include "../Renderer/MaterialXSupport.h"

FC_LOG_LEVEL_INIT("Renderer", true, true)

using namespace Gui;

typedef SoFCRenderCache::Material CoinMaterial;
typedef SoFCRenderCache::VertexCacheEntry VertexCacheEntry;

namespace {

// Read-only lookup of a per-view dynamic property override
// (<group>_<name>, e.g. Render_SSAO or Shadow_ShowGround). Unlike
// View3DInventor::getProperty this never creates the property - the
// per-frame config feed must not mutate the view; the Render_* properties
// are materialized when the renderer is selected
// (View3DInventorViewer::setRendererType) and the Shadow_* ones by the
// Shadow draw style.
template<class PropT>
const PropT * viewPropOverride(App::PropertyContainer * view,
                               const char * group,
                               const char * name)
{
    if (!view)
        return nullptr;
    char propname[128];
    snprintf(propname, sizeof(propname)-1, "%s_%s", group, name);
    auto prop = view->getPropertyByName(propname);
    if (!prop || !prop->isDerivedFrom(PropT::getClassTypeId()))
        return nullptr;
    return static_cast<const PropT*>(prop);
}

template<class PropT, class ValueT>
ValueT viewParamOverride(App::PropertyContainer * view,
                         const char * group,
                         const char * name,
                         const ValueT & def)
{
    if (auto prop = viewPropOverride<PropT>(view, group, name))
        return ValueT(prop->getValue());
    return def;
}

// MeshData that keeps its SoFCVertexCache alive, plus a token pinning
// the exposed CPU arrays' current storage generation. The cache alone
// is not enough: the arrays are copy-on-write and shared across caches
// (the variant-cache array dedup), so a later write — e.g. an
// equality-shared index array detaching under sort_triangles — frees
// the storage the raw pointers below were captured from.
struct CacheMeshData : Render::MeshData {
    Gui::CoinPtr<SoFCVertexCache> holder;
    std::shared_ptr<const void> arrayRefs;
    /// The source registry's generation when levelError was read, so a
    /// later publish can tell that nothing has been registered since
    /// without taking the registry's lock (MeshSource.h generation()).
    uint32_t sourceGen = 0;
    // Compacted index subsets of partial caches (see below); the base
    // struct's index pointers alias these when filled.
    std::vector<int32_t> partialTriangles;
    std::vector<int32_t> partialLines;
};

/// Drawn meshes whose source tag no registration claims, split by
/// where the tag came from (see translateCache). Counted only while
/// Render_LevelDebug is on, and reported at the end of translate().
size_t s_unownedProtoTags = 0;
size_t s_unownedOwnTags = 0;
/// ...and by the node class behind them, with the bytes standing
/// behind each. A count says how much the ladder cannot reach; the
/// class says who to go and ask; the bytes say whether it is worth
/// going -- the same population was once dismissed at 18.7MB from a
/// counter that had no bytes column, and later stood behind most of
/// an unreachable 74MB.
struct UnownedClassTally {
    size_t count = 0;
    uint64_t bytes = 0;
    int maxVertices = 0;
};
std::map<std::string, UnownedClassTally> s_unownedByType;
/// The largest single unowned translations of the publish, so the
/// report can name individuals: which shapes, how many vertices their
/// arrays carry, and how few primitives those arrays are drawn as.
struct UnownedSample {
    std::string type;
    uint64_t cacheId = 0;
    uint32_t bytes = 0;
    int numVertices = 0;
    int points = 0;
    int lines = 0;
    int triangles = 0;
};
std::vector<UnownedSample> s_unownedTop;

std::shared_ptr<CacheMeshData>
translateCache(SoFCVertexCache * cache)
{
    // The node class of an unclaimed source tag, remembered until the
    // mesh's arrays are filled in so the tally below can price it.
    // Points at an SbName's storage, which outlives the type system.
    const char *unownedType = nullptr;
    auto mesh = std::make_shared<CacheMeshData>();
    mesh->holder = cache;
    mesh->arrayRefs = cache->copyArrayRefs();
    mesh->cacheId = cache->getCacheId();
    if (SoNode *node = cache->getNode()) {
        // Proto node preferred: color variants of one geometry carry
        // the same source tag as their base, so a shape-backed level
        // generator registered on the base claims them all.
        SoNode *proto = SoFCVertexCache::getProtoNode(node);
        mesh->sourceTag = proto ? proto : node;
        // Whether the display may suppress this drawable under memory
        // pressure -- every vertex sits on an edge, or every edge
        // bounds a face (docs/SceneStreaming.md #13b). Read by NAME,
        // like protoNode above: the producer of the answer is PartGui
        // (only OCCT topology can say) and Gui must not depend on it.
        // Absent field = absent classification = always draws, which is
        // the safe direction: a drawable nobody has judged is one
        // nothing else on screen may be standing in for.
        static const SbName attachedField("attachedOnly");
        // Off the PROTO when there is one, same as the source tag: a
        // color variant copies its base's geometry arrays but no
        // producer ever classifies the variant node itself, so its own
        // field is the constructor default forever. The base's answer
        // is the variant's answer -- identical geometry.
        SoNode *fieldNode = proto ? proto : node;
        const SoField *f = fieldNode->getField(attachedField);
        if (f && f->isOfType(SoSFBool::getClassTypeId()))
            mesh->attachedOnly = static_cast<const SoSFBool *>(f)->getValue();
        if (Gui::RenderParams::getLevelDebug()) {
            // Which of THREE states this drawable arrives in, named
            // once per (node type, state), in log order so the
            // sequence is readable: no field at all, field saying
            // unattached, field saying attached. The first two are
            // identical downstream -- both end as attachedOnly false,
            // both bypass every gate -- and they have opposite fixes,
            // so only this side can tell them apart.
            //
            // WARNING: the first version of this probe was gated on
            // getenv("FC_LEVEL_DEBUG"), which the harness never sets
            // (it sets the PARAMETER), so it printed nothing and that
            // nothing was read as "the field is always present". A
            // null from a probe nobody validated is not evidence.
            static std::map<std::string, size_t> seen;
            std::string key =
                std::string(node->getTypeId().getName().getString())
                + (!f ? " -- NO attachedOnly FIELD"
                      : mesh->attachedOnly ? " -- attached"
                                           : " -- unattached/unjudged");
            // On the first, and then on each doubling: the magnitude is
            // the question -- one stray node of a type is a curiosity,
            // one per object is the population that covered the screen.
            const size_t n = ++seen[key];
            if ((n & (n - 1)) == 0)
                Base::Console().Message(
                    "render levels: element class: %s (x%zu)\n",
                    key.c_str(), n);
        }
        // A producer running coarse-first registered what the display
        // tessellation itself is; the serializer places the mesh on
        // its ladder by this and declares the exact rung above it.
        //
        // Read before the answer, not after: a registration landing
        // between the two then leaves a generation this mesh does not
        // carry, and the next publish looks again. The other order
        // could stamp an answer as current that already was not.
        auto & registry = Render::MeshSourceRegistry::instance();
        mesh->sourceGen = registry.generation();
        mesh->levelError = registry.publishedError(mesh->sourceTag);
        // Who the level plan cannot reach, and why (Render_LevelDebug).
        //
        // An unregistered tag publishes at error 0 -- publishedError
        // cannot say "unknown" -- so such a mesh enters the plan
        // indistinguishable from one standing at its exact rung, and no
        // climb or descent can ever touch it. Measured on the rack
        // model, that was 1197 of 1569 apparently-exact sources. The
        // split that matters is whether the tag came from a PROTO node:
        // the bridge prefers the proto so colour variants share one
        // source, while PartGui registers the view provider's own
        // faceset/lineset, and where those are not the same node the
        // registration cannot be found by the tag the mesh carries.
        if (Gui::RenderParams::getLevelDebug()
                && !registry.knows(mesh->sourceTag)) {
            ++(proto ? s_unownedProtoTags : s_unownedOwnTags);
            const SoNode *tagged = proto ? proto : node;
            unownedType = tagged->getTypeId().getName().getString();
        }
    }

    mesh->numVertices = cache->getNumVertices();
    mesh->positions = reinterpret_cast<const float *>(cache->getVertexArray());
    mesh->normals = reinterpret_cast<const float *>(cache->getNormalArray());
    mesh->colors = cache->getColorArray();
    static_assert(Render::MeshData::MaterialStride
                      == SoFCVertexCache::MaterialStride,
                  "material stream stride mismatch");
    mesh->materials = cache->getMaterialArray();

    static_assert(sizeof(GLint) == sizeof(int32_t), "GLint size mismatch");
    mesh->numTriangleIndices = cache->getNumTriangleIndices();
    if (mesh->numTriangleIndices > 0)
        mesh->triangleIndices =
            reinterpret_cast<const int32_t *>(cache->getTriangleIndices());
    mesh->numLineIndices = cache->getNumLineIndices();
    if (mesh->numLineIndices > 0)
        mesh->lineIndices =
            reinterpret_cast<const int32_t *>(cache->getLineIndices());
    mesh->numPointIndices = cache->getNumPointIndices();
    if (mesh->numPointIndices > 0)
        mesh->pointIndices =
            reinterpret_cast<const int32_t *>(cache->getPointIndices());

    mesh->hasTransparency = cache->hasTransparency();
    mesh->hasOpaqueParts = cache->hasOpaqueParts();

    // Once, not once per cache: this runs for every cache a publish
    // translates, and the lookup cost 5.8% of a 2000-object publish.
    static const bool debugfeed = (getenv("FC_BGFX_DEBUG_FEED") != nullptr);
    if (debugfeed)
        fprintf(stderr,
                "bridge cache=%llx nv=%d nti=%d tri=%p pos=%p\n",
                (unsigned long long)mesh->cacheId, mesh->numVertices,
                mesh->numTriangleIndices,
                (const void *)mesh->triangleIndices,
                (const void *)mesh->positions);

    mesh->texCoords =
        reinterpret_cast<const float *>(cache->getTexCoordArray());

    // Partial subset caches (e.g. the single-edge copy of a partial
    // selection) keep the parent's FULL index array and record the
    // restriction as a part list — the GL renderer draws it part by
    // part (SoFCVertexArrayIndexer::partialindices). Compact those
    // parts into a real index subset so backends (which consume the
    // arrays verbatim) draw only the selected parts. Point subsets
    // rebuild their index array at copy time and never get here.
    const auto &triParts = cache->getPartialTriangleParts();
    if (!triParts.empty() && mesh->triangleIndices) {
        for (int p : triParts) {
            int start = 0, count = 0;
            if (cache->getTrianglePartRange(p, start, count) && count > 0)
                mesh->partialTriangles.insert(mesh->partialTriangles.end(),
                        mesh->triangleIndices + start,
                        mesh->triangleIndices + start + count);
        }
        mesh->triangleIndices = mesh->partialTriangles.data();
        mesh->numTriangleIndices = int(mesh->partialTriangles.size());
    }
    const auto &lineParts = cache->getPartialLineParts();
    if (!lineParts.empty() && mesh->lineIndices) {
        for (int p : lineParts) {
            int start = 0, count = 0;
            if (cache->getLinePartRange(p, start, count) && count > 0)
                mesh->partialLines.insert(mesh->partialLines.end(),
                        mesh->lineIndices + start,
                        mesh->lineIndices + start + count);
        }
        mesh->lineIndices = mesh->partialLines.data();
        mesh->numLineIndices = int(mesh->partialLines.size());
    }
    if (unownedType) {
        // Priced by the arrays as translated -- what an upload of this
        // mesh costs -- not by what it draws: a cache may carry a
        // full-size vertex array behind a handful of point indices,
        // and the array is the memory.
        const uint32_t bytes = Render::meshResidentBytes(mesh.get());
        auto &tally = s_unownedByType[unownedType];
        ++tally.count;
        tally.bytes += bytes;
        tally.maxVertices = std::max(tally.maxVertices, mesh->numVertices);
        const size_t kTop = 8;
        if (s_unownedTop.size() < kTop
                || bytes > s_unownedTop.back().bytes) {
            UnownedSample sample;
            sample.type = unownedType;
            sample.cacheId = mesh->cacheId;
            sample.bytes = bytes;
            sample.numVertices = mesh->numVertices;
            sample.points = mesh->numPointIndices;
            sample.lines = mesh->numLineIndices / 2;
            sample.triangles = mesh->numTriangleIndices / 3;
            s_unownedTop.push_back(std::move(sample));
            std::sort(s_unownedTop.begin(), s_unownedTop.end(),
                      [](const UnownedSample &a, const UnownedSample &b) {
                          return a.bytes > b.bytes;
                      });
            if (s_unownedTop.size() > kTop)
                s_unownedTop.resize(kTop);
        }
    }
    return mesh;
}

/** Meshes already translated, by the id of the cache they came from.
 *
 * Translating a vertex cache is very nearly the same work every
 * publish: a cache is built once, closed once when its shape's
 * traversal ends, and a shape whose geometry changes is given a *new*
 * cache with a new id rather than having this one rewritten. On a large
 * assembly that repetition was the single biggest cost of a publish
 * (docs/IncrementalPublish.md §4d).
 *
 * Very nearly, not exactly -- which is what meshMatchesCache() below is
 * for. The memo hands back a translation only after checking it still
 * describes the cache.
 *
 * Held weakly, which is what keeps the memo honest about lifetime. A
 * mesh lives for as long as some draw list still refers to it -- the
 * backend holds the previous publish's list while the next one is being
 * translated, which is exactly when the memo is read -- and the entry
 * dies with it. So nothing here keeps a vertex cache alive, and the
 * memo never has to be reconciled against the scene.
 */
std::unordered_map<uint64_t, std::weak_ptr<CacheMeshData>> &
meshMemo()
{
    static std::unordered_map<uint64_t, std::weak_ptr<CacheMeshData>> memo;
    return memo;
}

/** Whether \a mesh still describes what a fresh translation of \a cache
 * would produce.
 *
 * A mesh is a set of raw pointers into the cache's attribute and index
 * arrays, and those are not as immutable as the cache object is. Copies
 * of one shape's geometry start out sharing arrays with the cache they
 * were seeded from, and a cache's own handle can be moved onto shared
 * storage after the cache was already translated -- measured here as
 * every one of 200 identical boxes converging on a single vertex array
 * some publishes after each had its own.
 *
 * So the memo checks rather than assumes: it compares what it pinned
 * against what the cache holds now, and a mismatch simply translates
 * again. This is a dozen inline pointer reads against the two
 * allocations and the array-ref copy that a translation costs, and it
 * makes the reuse correct by construction instead of by an argument
 * about who may touch a closed cache.
 *
 * Pointer equality is identity here, not a guess: the mesh holds a
 * reference to the storage it pinned (copyArrayRefs), so that storage
 * cannot have been freed and reallocated at the same address while the
 * mesh is alive.
 */
bool meshMatchesCache(CacheMeshData & mesh, SoFCVertexCache * cache)
{
    if (mesh.holder != cache || mesh.numVertices != cache->getNumVertices())
        return false;

    if (mesh.positions != reinterpret_cast<const float *>(cache->getVertexArray())
            || mesh.normals != reinterpret_cast<const float *>(cache->getNormalArray())
            || mesh.colors != cache->getColorArray()
            || mesh.materials != cache->getMaterialArray()
            || mesh.texCoords
                   != reinterpret_cast<const float *>(cache->getTexCoordArray()))
        return false;

    if (mesh.numTriangleIndices != cache->getNumTriangleIndices()
            || mesh.numLineIndices != cache->getNumLineIndices()
            || mesh.numPointIndices != cache->getNumPointIndices())
        return false;
    if (mesh.numTriangleIndices > 0
            && mesh.triangleIndices
                   != reinterpret_cast<const int32_t *>(cache->getTriangleIndices()))
        return false;
    if (mesh.numLineIndices > 0
            && mesh.lineIndices
                   != reinterpret_cast<const int32_t *>(cache->getLineIndices()))
        return false;
    if (mesh.numPointIndices > 0
            && mesh.pointIndices
                   != reinterpret_cast<const int32_t *>(cache->getPointIndices()))
        return false;

    // The one thing a mesh holds that is not a property of its cache:
    // which rung of its level ladder the source registry has published
    // since (coarse-first tessellation, docs/SceneStreaming.md §7).
    //
    // Asked of the registry's generation rather than of the registry.
    // publishedError() takes the registry lock, and asking it once per
    // mesh per publish cost 4-5ms of a 6000-object publish to be told
    // every time that nothing had been registered at all.
    if (mesh.sourceTag) {
        auto & registry = Render::MeshSourceRegistry::instance();
        const uint32_t now = registry.generation();
        if (now != mesh.sourceGen) {
            if (mesh.levelError != registry.publishedError(mesh.sourceTag))
                return false;
            // Something was registered, but not for this mesh's source.
            mesh.sourceGen = now;
        }
    }

    return true;
}

/// Whether a translation of \a cache can be described by the checks
/// above at all. A partial cache -- the single-face or single-edge
/// subset a selection makes -- has its index arrays compacted into the
/// mesh itself rather than pointed at the cache, so there is nothing
/// there to compare against. They are few, and they are rebuilt as
/// selection moves anyway.
bool meshIsReusable(SoFCVertexCache * cache)
{
    return cache->getPartialTriangleParts().empty()
        && cache->getPartialLineParts().empty();
}

/// Report anything a reused mesh holds that a fresh translation of the
/// same cache would not. RenderCacheMeshReuse 2 (§7): the failure mode
/// of reuse is a mesh that describes geometry the cache no longer has,
/// and it is invisible in the result until someone looks at that shape.
void verifyMeshReuse(const CacheMeshData & kept, SoFCVertexCache * cache)
{
    auto fresh = translateCache(cache);
    const char * bad = nullptr;
    if (kept.cacheId != fresh->cacheId)                     bad = "cache id";
    else if (kept.sourceTag != fresh->sourceTag)            bad = "source tag";
    else if (kept.levelError != fresh->levelError)          bad = "level error";
    else if (kept.numVertices != fresh->numVertices)        bad = "vertex count";
    else if (kept.positions != fresh->positions)            bad = "positions";
    else if (kept.normals != fresh->normals)                bad = "normals";
    else if (kept.colors != fresh->colors)                  bad = "colors";
    else if (kept.materials != fresh->materials)            bad = "materials";
    else if (kept.texCoords != fresh->texCoords)            bad = "texture coordinates";
    else if (kept.numTriangleIndices != fresh->numTriangleIndices
             || kept.triangleIndices != fresh->triangleIndices)
        bad = "triangle indices";
    else if (kept.numLineIndices != fresh->numLineIndices
             || kept.lineIndices != fresh->lineIndices)
        bad = "line indices";
    else if (kept.numPointIndices != fresh->numPointIndices
             || kept.pointIndices != fresh->pointIndices)
        bad = "point indices";
    else if (kept.hasTransparency != fresh->hasTransparency)  bad = "transparency";
    else if (kept.hasOpaqueParts != fresh->hasOpaqueParts)    bad = "opaque parts";
    if (bad) {
        // Capped: a mismatch is systematic, not incidental, so the first
        // few say everything and the rest would only cost the run.
        static int shown = 0;
        if (++shown <= 8)
            FC_ERR("mesh reuse: cache " << kept.cacheId << " kept a mesh whose "
                   << bad << " a fresh translation disagrees with");
    }
}

/// The mesh for \a cache, translated only if this is the first publish
/// to ask for it (or the first since its level changed).
std::shared_ptr<CacheMeshData>
acquireMesh(SoFCVertexCache * cache)
{
    const long reuse = ViewParams::getRenderCacheMeshReuse();
    if (reuse <= 0 || !meshIsReusable(cache))
        return translateCache(cache);

    auto & memo = meshMemo();
    const uint64_t id = cache->getCacheId();
    auto it = memo.find(id);
    if (it != memo.end()) {
        if (auto mesh = it->second.lock()) {
            if (meshMatchesCache(*mesh, cache)) {
                if (reuse > 1)
                    verifyMeshReuse(*mesh, cache);
                return mesh;
            }
        }
    }

    auto mesh = translateCache(cache);

    // Entries whose mesh has gone are the caches the scene has dropped.
    // Swept when the memo has grown well past what the last sweep left,
    // so the sweep itself stays a small fraction of what it cleans up.
    static size_t sweepat = 1024;
    if (memo.size() >= sweepat) {
        for (auto i = memo.begin(); i != memo.end();)
            i = i->second.expired() ? memo.erase(i) : std::next(i);
        sweepat = std::max<size_t>(1024, memo.size() * 2);
    }

    memo[id] = mesh;
    return mesh;
}

// Shares one TextureImage among all materials referring to the same
// texture node within a translate() call; the pixel copy dedups across
// feeds through the backend's textureId keying (Coin node ids are unique
// per node content revision).
typedef std::unordered_map<const SoNode *,
                           std::shared_ptr<const Render::TextureImage>>
    TextureImageMap;

// Shares one TexturePalette among all materials built from the same set
// of face-texture nodes within a translate() call. Pointer identity is
// what the backend batches on and what it caches the uploaded array
// texture under, so two draws off one appearance must come out holding
// the same palette, not two equal ones.
typedef std::map<std::vector<const SoNode *>,
                 std::shared_ptr<const Render::TexturePalette>>
    TexturePaletteMap;

std::shared_ptr<const Render::TextureImage>
translateTexture(const SoFCRenderCache::TextureInfo & info,
                 TextureImageMap & texmap)
{
    if (!info.texture
            || !info.texture->isOfType(SoTexture2::getClassTypeId()))
        return nullptr;

    auto & res = texmap[info.texture.get()];
    if (res)
        return res;

    auto node = static_cast<const SoTexture2 *>(info.texture.get());
    SbVec2s size;
    int nc = 0;
    const unsigned char * pixels = node->image.getValue(size, nc);
    if (!pixels || size[0] <= 0 || size[1] <= 0 || nc <= 0 || nc > 4)
        return nullptr;

    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = node->getNodeId();
    tex->width = size[0];
    tex->height = size[1];
    tex->numComponents = nc;
    tex->pixels.assign(pixels,
                       pixels + size_t(size[0]) * size[1] * nc);
    tex->wrapS = node->wrapS.getValue() == SoTexture2::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    tex->wrapT = node->wrapT.getValue() == SoTexture2::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    switch (node->model.getValue()) {
    case SoTexture2::DECAL:
        tex->model = Render::TextureImage::Decal; break;
    case SoTexture2::BLEND:
        tex->model = Render::TextureImage::Blend; break;
    case SoTexture2::REPLACE:
        tex->model = Render::TextureImage::Replace; break;
    default:
        tex->model = Render::TextureImage::Modulate; break;
    }
    tex->blendColor = node->blendColor.getValue().getPackedValue(0.0f);
    res = tex;
    return res;
}

std::shared_ptr<const Render::TextureImage>
translateBumpMap(const SoFCRenderCache::TextureInfo & info,
                 TextureImageMap & texmap)
{
    if (!info.texture
            || !info.texture->isOfType(SoBumpMap::getClassTypeId()))
        return nullptr;

    auto & res = texmap[info.texture.get()];
    if (res)
        return res;

    auto node = static_cast<const SoBumpMap *>(info.texture.get());
    SbVec2s size;
    int nc = 0;
    const unsigned char * pixels = node->image.getValue(size, nc);
    if (!pixels || size[0] <= 0 || size[1] <= 0 || nc <= 0 || nc > 4)
        return nullptr;

    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = node->getNodeId();
    tex->width = size[0];
    tex->height = size[1];
    tex->numComponents = nc;
    tex->pixels.assign(pixels,
                       pixels + size_t(size[0]) * size[1] * nc);
    tex->wrapS = node->wrapS.getValue() == SoBumpMap::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    tex->wrapT = node->wrapT.getValue() == SoBumpMap::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    res = tex;
    return res;
}

std::shared_ptr<const Render::TextureImage>
translateRenderTexture(const SoFCRenderCache::TextureInfo & info,
                       TextureImageMap & texmap)
{
    if (!info.texture
            || !info.texture->isOfType(
                    Gui::SoFCRenderTexture::getClassTypeId()))
        return nullptr;

    auto & res = texmap[info.texture.get()];
    if (res)
        return res;

    auto node = static_cast<const Gui::SoFCRenderTexture *>(
            info.texture.get());
    SbVec2s size;
    int nc = 0;
    const unsigned char * pixels = node->image.getValue(size, nc);
    if (!pixels || size[0] <= 0 || size[1] <= 0 || nc <= 0 || nc > 4)
        return nullptr;

    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = node->getNodeId();
    tex->width = size[0];
    tex->height = size[1];
    tex->numComponents = nc;
    tex->pixels.assign(pixels,
                       pixels + size_t(size[0]) * size[1] * nc);
    tex->wrapS = node->wrapS.getValue() == Gui::SoFCRenderTexture::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    tex->wrapT = node->wrapT.getValue() == Gui::SoFCRenderTexture::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    res = tex;
    return res;
}

// The white 1x1 image a palette layer nobody filled is given.
//
// A gap has to be FILLED rather than closed up: the layers are named by
// index, so dropping one would shift every image after it and repaint
// the faces that named those. White is the honest filler -- the images
// modulate, so a face pointing at one renders exactly as it would
// untextured. The id is one no Coin node can produce, since the backend
// keys its uploads on it.
std::shared_ptr<const Render::TextureImage>
whiteFaceTexture()
{
    static const std::shared_ptr<const Render::TextureImage> white = [] {
        auto tex = std::make_shared<Render::TextureImage>();
        tex->textureId = ~uint64_t(0);
        tex->width = 1;
        tex->height = 1;
        tex->numComponents = 3;
        tex->pixels.assign(3, uint8_t(255));
        return tex;
    }();
    return white;
}

// The per-face texture palette: the images the draw's faces are painted
// with, as the layers of one array texture. Entry i is layer i + 1 --
// layer 0 is the untextured face and has no image.
std::shared_ptr<const Render::TexturePalette>
translateFaceTextures(const SoFCRenderCache::Material & m,
                      TextureImageMap & texmap, TexturePaletteMap & palmap)
{
    if (!m.facetextures.getNum())
        return nullptr;

    int maxlayer = 0;
    for (const auto & v : m.facetextures.getData())
        maxlayer = std::max(maxlayer, v.first);
    if (maxlayer <= 0)
        return nullptr;

    std::vector<const SoNode *> key(std::size_t(maxlayer), nullptr);
    for (const auto & v : m.facetextures.getData()) {
        if (v.first > 0 && v.first <= maxlayer)
            key[std::size_t(v.first - 1)] = v.second.texture.get();
    }
    auto & res = palmap[key];
    if (res)
        return res;

    auto palette = std::make_shared<Render::TexturePalette>();
    palette->entries.reserve(std::size_t(maxlayer));
    for (int layer = 1; layer <= maxlayer; ++layer) {
        std::shared_ptr<const Render::TextureImage> image;
        if (const auto * info = m.facetextures.get(layer))
            image = translateRenderTexture(*info, texmap);
        if (!image)
            image = whiteFaceTexture();
        palette->entries.push_back(std::move(image));
    }
    res = palette;
    return res;
}

// Whether a line/point material of this feed renders with GL's
// RenderPassHighlight (selection thickening). Mirrors the bucket routing
// in SoFCRendererP::updateSelection: partial-element selections and full
// (non-partialhighlight) whole-object selections thicken; the mixed
// "partial highlight" whole-object lines (selsontop) do not. Non-on-top
// selections (id < 0) and the preselection highlight always thicken.
//
// The uncolored whole-on-top companions a partial selection carries
// (FLAG_TRANSPARENCY-stamped by addWholeOnTop) and the implicit
// whole-object ids stay thin: in GL the surviving whole-object lines are
// the thin selsontop copies, and the thin + dimmed rendering is what
// keeps the hidden-edge depth cue of a shown-on-top object readable
// next to the thick full-alpha element highlight.
bool
useHighlightPass(const CoinMaterial & m, int selId, bool highlight)
{
    if (m.type == CoinMaterial::Triangle)
        return false;
    if (highlight || selId < 0)
        return true;
    if (selId & SoFCRenderer::SelIdPartial)
        return !m.overrideflags.test(CoinMaterial::FLAG_TRANSPARENCY);
    if (selId & SoFCRenderer::SelIdImplicit)
        return false;
    return (selId & SoFCRenderer::SelIdFull) && !m.partialhighlight;
}

Render::Material
translateMaterial(const CoinMaterial & m, int selId, bool highlight,
                  TextureImageMap & texmap, TexturePaletteMap & palmap,
                  const RendererBridge::SectionOnTop & sectionOnTop)
{
    Render::Material res;

    switch (m.type) {
    case CoinMaterial::Line:
        res.type = Render::Material::Line;
        break;
    case CoinMaterial::Point:
        res.type = Render::Material::Point;
        break;
    default:
        res.type = Render::Material::Triangle;
        break;
    }

    res.diffuse = m.diffuse;
    res.emissive = m.emissive;
    res.specular = m.specular;
    res.ambient = m.ambient;
    res.linecolor = m.linecolor;
    res.shininess = m.shininess;
    res.linewidth = m.linewidth;
    res.pointsize = m.pointsize;

    // Hidden-line draw style material: the backend outlines whole-cache
    // triangle draws and applies the per-frame HiddenLineConfig rules.
    res.outline = m.outline;

    // Solid shape hint, one half of the section-cap eligibility test
    // (GL: renderSection checks shapetype and cache->hasSolid()).
    res.solidshape = m.shapetype == SoShapeHintsElement::SOLID;

    // Selection line/point thickening (GL: applyMaterial ~552 under
    // RenderPassHighlight). Applied at translate time so backends see the
    // effective width. Deviation from GL: the dimmed (hidden) pass of
    // whole-on-top preselect lines thickens too, where GL leaves it thin.
    if (useHighlightPass(m, selId, highlight)) {
        // Never true for triangles: useHighlightPass rejects them first.
        res.highlightline = true;
        float scale = float(ViewParams::getSelectionLineThicken());
        if (scale < 1.0f)
            scale = 1.0f;
        float w = res.linewidth * scale;
        if (ViewParams::getSelectionLineMaxWidth() > 1.0)
            w = std::min<float>(w, std::max<float>(
                    res.linewidth,
                    float(ViewParams::getSelectionLineMaxWidth())));
        res.linewidth = w;

        float pscale = float(ViewParams::getSelectionPointScale());
        if (pscale < 1.0f)
            pscale = scale;
        w = res.pointsize * pscale;
        if (ViewParams::getSelectionPointMaxSize() > 1.0)
            w = std::min<float>(w, std::max<float>(
                    res.pointsize,
                    float(ViewParams::getSelectionPointMaxSize())));
        res.pointsize = w;
    }

    res.pervertexcolor = m.pervertexcolor;
    res.lighting = m.lightmodel != SoLazyElement::BASE_COLOR;
    res.twoside = m.twoside;
    res.culling = m.culling;
    res.ccw = m.vertexordering != SoLazyElement::CW;

    res.depthtest = m.depthtest;
    res.depthwrite = m.depthwrite;
    switch (m.depthfunc) {
    case SoDepthBufferElement::NEVER:
        res.depthfunc = Render::Material::Never; break;
    case SoDepthBufferElement::ALWAYS:
        res.depthfunc = Render::Material::Always; break;
    case SoDepthBufferElement::LESS:
        res.depthfunc = Render::Material::Less; break;
    case SoDepthBufferElement::EQUAL:
        res.depthfunc = Render::Material::Equal; break;
    case SoDepthBufferElement::GEQUAL:
        res.depthfunc = Render::Material::GEqual; break;
    case SoDepthBufferElement::GREATER:
        res.depthfunc = Render::Material::Greater; break;
    case SoDepthBufferElement::NOTEQUAL:
        res.depthfunc = Render::Material::NotEqual; break;
    default:
        res.depthfunc = Render::Material::LEqual; break;
    }

    // Same uniform-transparency rule as SoFCRenderer::setScene().
    res.transparent = m.transptexture
        || (!m.pervertexcolor && (m.diffuse & 0xff) != 0xff);
    res.ontop = m.isOnTop();

    // GL polygon offset only affects filled polygons (the LINES/POINTS
    // styles matter only with glPolygonMode, which the renderer never uses).
    res.polygonoffset =
        (m.polygonoffsetstyle & SoPolygonOffsetElement::FILLED)
        && (m.polygonoffsetfactor != 0.0f || m.polygonoffsetunits != 0.0f);
    res.polygonoffsetfactor = m.polygonoffsetfactor;
    res.polygonoffsetunits = m.polygonoffsetunits;
    // The Tessellation draw style rides in on this: SoFCUnifiedSelection
    // overrides SoDrawStyleElement to LINES and lets the shapes draw
    // their faces, which GL turns into a wireframe with glPolygonMode.
    // A plain SoDrawStyle node in the scene graph (PartGui's geometry
    // check box) arrives the same way but is only asking for a
    // wireframe, so the backend is told which of the two it has.
    res.drawstyle = uint8_t(m.drawstyle);
    res.drawstyleoverride = m.overrideflags.test(CoinMaterial::FLAG_DRAW_STYLE);

    // Depth-occluded parts of on-top lines/points are dimmed to this alpha
    // (SoFCRenderer's RenderPassLinePattern pass). The selection highlight
    // itself keeps full alpha there: GL's TransparencyOnTop dimming applies
    // only when the pass has no RenderPassHighlight bit (applyMaterial
    // ~564), i.e. never to the colored element draws of a partial
    // selection nor to the whole-object lines of a full selection. The
    // uncolored whole-on-top companions of a partial selection stay
    // dimmed like GL's selsontop bucket — they are told apart by the
    // FLAG_TRANSPARENCY override buildHighlightCache stamps on
    // non-triangle whole-on-top companion materials (addWholeOnTop),
    // which the colored highlight materials never carry.
    if (res.ontop && res.type != Render::Material::Triangle) {
        bool highlightline = false;
        if (selId > 0 && !m.partialhighlight) {
            if (selId & SoFCRenderer::SelIdPartial)
                highlightline = !m.overrideflags.test(
                        CoinMaterial::FLAG_TRANSPARENCY);
            else if (selId & SoFCRenderer::SelIdFull)
                highlightline = true;
        }
        if (!highlightline)
            res.hiddenlinealpha = float(ViewParams::getTransparencyOnTop());
    }

    // Selected/preselected face outline (GL: renderOutline under the
    // RenderPassSelectionOutline pass, issued for partial triangle
    // draws of on-top selections and of the preselection highlight).
    // The width rules mirror renderOutline ~1458: selection thickening
    // capped by SelectionLineMaxWidth, then max of 1.5x that and
    // linewidth * OutlineThicken.
    if (res.type == Render::Material::Triangle && (highlight || selId > 0)) {
        // The width is computed regardless of the face-outline params:
        // the hidden-line style's whole-object highlight outline uses it
        // even with face outlines disabled (GL: renderOutline ~1458).
        float lw = res.linewidth;
        float scale = float(ViewParams::getSelectionLineThicken());
        if (scale < 1.0f)
            scale = 1.0f;
        float w = lw * scale;
        if (ViewParams::getSelectionLineMaxWidth() > 1.0)
            w = std::min<float>(w, std::max<float>(lw,
                    float(ViewParams::getSelectionLineMaxWidth())));
        res.outlinewidth = std::max(w * 1.5f,
            lw * float(ViewParams::getOutlineThicken()));
        bool show = highlight
            ? ViewParams::getShowPreSelectedFaceOutline()
            : ViewParams::getShowSelectedFaceOutline();
        if (show) {
            res.faceoutline = true;
            res.outlineonly = highlight
                ? ViewParams::getNoPreSelFaceHighlightWithOutline()
                : ViewParams::getNoSelFaceHighlightWithOutline();
        }
    }

    // Line stipple (glLineStipple encoding, factor << 16 | pattern). The
    // dimmed pass of on-top lines falls back to the user-configurable
    // selection pattern when the material has none (GL: applyMaterial
    // ~539 under RenderPassLinePattern).
    // A triangle draw carrying LINES comes out as edges too
    // (submitTessellation, GL's glPolygonMode), so it wants the pattern
    // as much as a line draw does -- without this a dashed bounding box
    // (PartGui's geometry check) drew solid.
    if (res.type == Render::Material::Line
            || res.drawstyle == Render::Material::DrawLines) {
        res.linepattern = m.linepattern;
        res.hiddenlinepattern = m.linepattern;
        if (res.type == Render::Material::Line
                && res.ontop && !m.hasLinePattern()) {
            uint32_t sellinepattern =
                uint32_t(ViewParams::getSelectionLinePattern()) & 0xffff;
            if (sellinepattern) {
                if (ViewParams::getSelectionLinePatternScale() > 1)
                    sellinepattern |=
                        uint32_t(ViewParams::getSelectionLinePatternScale())
                            << 16;
                res.hiddenlinepattern = sellinepattern;
            }
        }
    }

    // Texture of triangle draws: unit 0 only (GL applies further units
    // on top of it, a known deviation). The texture matrix already
    // carries the merged SoTexture2Transform/SoTextureMatrixTransform
    // state (SoFCRenderCache::addTexture).
    if (res.type == Render::Material::Triangle && m.textures.getNum()) {
        if (const auto * info = m.textures.get(0)) {
            res.texture = translateTexture(*info, texmap);
            if (res.texture && !info->identity) {
                static_assert(sizeof(res.texmatrix) == sizeof(SbMat),
                              "matrix size mismatch");
                res.texidentity = false;
                std::memcpy(res.texmatrix, info->matrix.getValue(),
                            sizeof(res.texmatrix));
            }
        }
    }

    // Shadow participation flags for the backend's shadow caster and
    // receiver routing (only consulted while a scene light is fed).
    if (res.type == Render::Material::Triangle)
        res.shadowstyle = uint8_t(m.shadowstyle);

    // Per-object PBR parameters (SoFCRenderMaterial capture; < 0 = unset).
    if (res.type == Render::Material::Triangle) {
        res.metallic = m.metallic;
        res.roughness = m.roughness;
        res.finish = m.finish;
        res.finishpitch = m.finishpitch;
        res.finishdepth = m.finishdepth;
        res.finishangle = m.finishangle;
        // The palette rides along; whether a draw shades from it is
        // decided per draw below (a partial draw resolves its face's
        // entry into the scalars instead).
        res.finishpalette = m.finishpalette;
        // Entry 0 is the first face's frame, and it is the draw's own:
        // a mesh whose material stream collapsed (every face framed
        // alike, nothing else varying) carries no index to read, and
        // the backend uploads this as entry 0 either way.
        res.framepalette = m.framepalette;
        if (m.framepalette && !m.framepalette->entries.empty())
            res.frame = m.framepalette->entries.front();
        // The per-face texture palette, on the same terms: it rides
        // along and the per-draw pass below decides how a draw reads it
        // -- out of the material stream, or as the one layer a draw
        // with no stream (or a single-face draw) resolves to.
        if (!m.facetextureindices.getNum())
            res.texturepalette.reset();
        else {
            res.texturepalette = translateFaceTextures(m, texmap, palmap);
            if (res.texturepalette) {
                res.facetexscale = m.facetexscale;
                // The draw's own layer: face 0's, which is every face's
                // whenever the layers do not vary. A draw whose mesh
                // really carries the per-face bake overrides this with
                // -1 below and reads the stream instead.
                const int32_t layer = m.facetextureindices[0];
                res.facetexlayer = layer > 0 && layer < 128
                    ? int8_t(layer) : 0;
            }
        }
        res.water = m.water;
        res.waterdensity = m.waterdensity;
        res.glass = m.glass;
        res.glassior = m.glassior;
        res.glassdensity = m.glassdensity;
        res.glassroughness = m.glassroughness;
        // A MaterialX surface stating transmission is a glass body too
        // (docs/MaterialStorage.md sec 17.21): the document's
        // transmission colour, depth, IOR and roughness stand where
        // Render_Glass would put the card's, and every consumer of the
        // glass flag -- pass activation, the medium exemptions, the
        // shadow tint, instancing, the snapshot -- sees an ordinary
        // glass draw. Render_Glass wins when both are stated: it is the
        // user's own word on this shape, the document is its material's.
        if (!res.glass && m.usershader && m.usershader->glass.claimed
                && m.usershader->stage == "material") {
            const Render::UserShader::Glass & g = m.usershader->glass;
            res.glass = true;
            res.glassmtlx = true;
            res.glassior = g.ior;
            res.glassdensity = g.density;
            res.glassroughness = g.roughness;
            std::copy(g.color, g.color + 3, res.glasscolor);
        }
        res.cloud = m.cloud;
        res.clouddensity = m.clouddensity;
        res.clouddetail = m.clouddetail;
        res.cloudspeed = m.cloudspeed;
        res.fire = m.fire;
        res.fireintensity = m.fireintensity;
        res.firedetail = m.firedetail;
        res.firespeed = m.firespeed;
        res.fountain = m.fountain;
        res.fountaindensity = m.fountaindensity;
        res.fountaindetail = m.fountaindetail;
        res.fountainspeed = m.fountainspeed;
        res.lightsource = m.lightsource;
        res.lightintensity = m.lightintensity;
        res.lightrange = m.lightrange;
        res.lightshadow = m.lightshadow;
        res.lightshadowext = m.lightshadowext;
        // User "material"/"water"-stage shader (docs/RenderDebug.md §6);
        // the shared translation's pointer identity is the batch key.
        res.usershader = m.usershader;
        // Binding a "water"-stage effect IS the water activation
        // (docs/RenderEngine.md §5.11): the draw becomes a water body
        // exactly as if Render_Water were set, so the whole pass set
        // (body detection, scene copy, planar reflection, back depth,
        // medium exemptions) engages unchanged, with the user program
        // replacing fs_fc_water at the surface submit.
        if (res.usershader && res.usershader->stage == "water")
            res.water = true;
        // Likewise a "volume"-stage medium function makes the draw a
        // medium body: the proxy volume raymarches in the shared
        // volumetric pass, which dispatches the user functions for the
        // body's slot. The source picks its channel by contract
        // function — fcMediumScatter = the scattering channel (a
        // fountain body: flow frame + splash machinery engage),
        // fcMediumField/fcMediumRamp = the emissive channel (a fire
        // body).
        if (res.usershader && res.usershader->stage == "volume") {
            if (res.usershader->fragmentSource.find("fcMediumScatter")
                    != std::string::npos)
                res.fountain = true;
            else
                res.fire = true;
        }
    }

    // Bump map of triangle draws, unit 0 only like textures (the GL
    // renderer never draws these; SoBumpMap only acts during Coin GL
    // shape rendering, which the cached pipeline bypasses).
    if (res.type == Render::Material::Triangle && m.bumpmaps.getNum()) {
        if (const auto * info = m.bumpmaps.get(0))
            res.bumpmap = translateBumpMap(*info, texmap);
    }

    // Emissive/occlusion/metallic-roughness material maps
    // (SoFCRenderTexture capture), unit 0 only like the maps above.
    if (res.type == Render::Material::Triangle && m.emissivemaps.getNum()) {
        if (const auto * info = m.emissivemaps.get(0))
            res.emissivemap = translateRenderTexture(*info, texmap);
    }
    if (res.type == Render::Material::Triangle && m.occlusionmaps.getNum()) {
        if (const auto * info = m.occlusionmaps.get(0))
            res.occlusionmap = translateRenderTexture(*info, texmap);
    }
    if (res.type == Render::Material::Triangle
            && m.metallicroughnessmaps.getNum()) {
        if (const auto * info = m.metallicroughnessmaps.get(0))
            res.metallicroughnessmap = translateRenderTexture(*info, texmap);
    }

    // Autozoom transforms: mirror the material's node list; the backend
    // replays them per frame (GL: setupMatrix runs the nodes' GLRender).
    if (m.autozoom.getNum()) {
        res.autozoom.reserve(m.autozoom.getNum());
        for (const auto & info : m.autozoom.getData()) {
            res.autozoom.emplace_back();
            Render::Material::AutoZoomEntry & entry = res.autozoom.back();
            auto node = info.cast<SoAutoZoomTranslation>();
            entry.scaleFactor = node->scaleFactor.getValue();
            entry.billboard = node->billboard.getValue();
            entry.pixelscale = node->pixelScale.getValue();
            entry.datumFlip = node->datumFlip.getValue();
            const SbVec3f & nrm = node->flipNormal.getValue();
            entry.normal[0] = nrm[0];
            entry.normal[1] = nrm[1];
            entry.normal[2] = nrm[2];
            entry.identity = info.identity;
            entry.resetmatrix = info.resetmatrix;
            if (!info.identity) {
                static_assert(sizeof(entry.matrix) == sizeof(SbMat),
                              "matrix size mismatch");
                std::memcpy(entry.matrix, info.matrix.getValue(),
                            sizeof(entry.matrix));
            }
        }
    }

    // Clip planes (sections), as world-space plane equations. Same on-top
    // exception as SoFCRenderer::applyMaterial: on-top draws are not
    // sectioned when the section does not reach them (the default) or in
    // concave mode. Both come from the view being drawn.
    if (m.clippers.getNum()) {
        bool concave = sectionOnTop.concave && m.clippers.getNum() > 1;
        if (!((sectionOnTop.noOnTop || concave) && res.ontop)) {
            for (const auto & info : m.clippers.getData()) {
                const SoClipPlane * clipper = info.cast<SoClipPlane>();
                if (!clipper->on.getValue() || clipper->on.isIgnored())
                    continue;
                SbPlane plane = clipper->plane.getValue();
                if (!info.identity)
                    plane.transform(info.matrix);
                // SbPlane: kept points satisfy dot(p, normal) >= distance.
                float * eq = res.clipplanes[res.numclipplanes];
                plane.getNormal().getValue(eq[0], eq[1], eq[2]);
                eq[3] = -plane.getDistanceFromOrigin();
                if (++res.numclipplanes == Render::Material::MaxClipPlanes)
                    break;
            }
            res.clipconcave = concave && res.numclipplanes > 1;
        }
    }
    return res;
}

} // anonymous namespace

Render::DrawCallList
RendererBridge::translate(const SoFCRenderCache::VertexCacheMap & vcachemap,
                          const SectionOnTop & sectionOnTop,
                          int selId, bool highlight, bool sequentialOrder,
                          Render::ObjectInfoMap * objectInfo,
                          Render::ObjectInfoMap * addedInfo)
{
    Render::DrawCallList res;

    // Share one MeshData among all entries referring to the same cache,
    // and one TextureImage among all materials with the same texture node.
    std::unordered_map<SoFCVertexCache *,
                       std::shared_ptr<CacheMeshData>> meshes;
    TextureImageMap textures;
    TexturePaletteMap facepalettes;

    for (const auto & v : vcachemap) {
        const CoinMaterial & material = v.first;
        if (v.second.empty())
            continue;
        if (material.drawstyle == SoDrawStyleElement::INVISIBLE)
            continue;

        Render::Material rmat =
            translateMaterial(material, selId, highlight, textures,
                              facepalettes, sectionOnTop);

        for (const VertexCacheEntry & ventry : v.second) {
            if (!ventry.cache)
                continue;

            // Resolve a partial draw (single face/edge/point) into an index
            // range of the buffer selected by the material type.
            int indexStart = 0;
            int indexCount = 0;
            if (ventry.partidx >= 0) {
                SbBool ok = FALSE;
                switch (rmat.type) {
                case Render::Material::Line:
                    ok = ventry.cache->getLinePartRange(
                            ventry.partidx, indexStart, indexCount);
                    break;
                case Render::Material::Point:
                    ok = ventry.cache->getPointPartRange(
                            ventry.partidx, indexStart, indexCount);
                    break;
                default:
                    ok = ventry.cache->getTrianglePartRange(
                            ventry.partidx, indexStart, indexCount);
                    break;
                }
                if (!ok)
                    continue;
            }

            auto & mesh = meshes[ventry.cache];
            if (!mesh)
                mesh = acquireMesh(ventry.cache);

            // Hidden-line extras, filled on demand: the seam-filtered line
            // index set (hideSeam), and per-face-part triangle ranges for
            // outlining geometry part by part (GL: renderOutline switches
            // to getNumFaceParts() under clip planes or perFaceOutline,
            // and to getNonFlatParts() for perFaceOutline+sceneOutline).
            // Per-face triangle ranges for browser-side per-face
            // preselection: the WASM viewer maps a hovered triangle to
            // its face part. Filled for every whole-object triangle mesh
            // (not just outlined ones) so shaded-mode hover highlights the
            // face under the cursor, matching the desktop.
            if (rmat.type == Render::Material::Triangle && ventry.partidx < 0
                    && mesh->triangleParts.empty()) {
                int numparts = ventry.cache->getNumFaceParts();
                mesh->triangleParts.reserve(numparts);
                for (int i = 0; i < numparts; ++i) {
                    int start = 0, count = 0;
                    if (ventry.cache->getTrianglePartRange(i, start, count)
                            && count > 0)
                        mesh->triangleParts.emplace_back(start, count);
                }
            }

            // Per-edge / per-vertex ranges for browser-side edge/vertex
            // preselection, mirroring the triangleParts face table above.
            // No count API exists for line/point parts, so enumerate until
            // the range getter reports the part is out of range.
            if (rmat.type == Render::Material::Line && ventry.partidx < 0
                    && mesh->lineParts.empty()) {
                for (int i = 0; ; ++i) {
                    int start = 0, count = 0;
                    if (!ventry.cache->getLinePartRange(i, start, count))
                        break;
                    mesh->lineParts.emplace_back(start, count);
                }
            }
            if (rmat.type == Render::Material::Point && ventry.partidx < 0
                    && mesh->pointParts.empty()) {
                for (int i = 0; ; ++i) {
                    int start = 0, count = 0;
                    if (!ventry.cache->getPointPartRange(i, start, count))
                        break;
                    mesh->pointParts.emplace_back(start, count);
                }
            }

            if (rmat.outline && ventry.partidx < 0) {
                if (rmat.type == Render::Material::Line
                        && !mesh->noSeamLineIndices
                        && ventry.cache->getNumNoSeamLineIndices() > 0) {
                    mesh->numNoSeamLineIndices =
                        ventry.cache->getNumNoSeamLineIndices();
                    mesh->noSeamLineIndices = reinterpret_cast<const int32_t *>(
                            ventry.cache->getNoSeamLineIndices());
                }
                if (rmat.type == Render::Material::Triangle
                        && mesh->nonFlatParts.empty()) {
                    int numparts = ventry.cache->getNumNonFlatParts();
                    const int * parts = ventry.cache->getNonFlatParts();
                    mesh->nonFlatParts.reserve(numparts);
                    for (int i = 0; i < numparts; ++i) {
                        int start = 0, count = 0;
                        if (ventry.cache->getTrianglePartRange(
                                    parts[i], start, count) && count > 0)
                            mesh->nonFlatParts.emplace_back(start, count);
                    }
                }
            }

            // Section-cap solids, filled on demand for clipped whole
            // triangle draws: which triangle ranges the stencil cap pass
            // marks (GL: renderSolids from _renderSection).
            if (rmat.type == Render::Material::Triangle
                    && rmat.numclipplanes > 0 && ventry.partidx < 0
                    && mesh->hasSolid == 0) {
                mesh->hasSolid = ventry.cache->hasSolid();
                if (mesh->hasSolid == 1 && mesh->solidParts.empty()) {
                    int numparts = ventry.cache->getNumSolidParts();
                    mesh->solidParts.reserve(numparts);
                    for (int i = 0; i < numparts; ++i) {
                        int start = 0, count = 0;
                        if (ventry.cache->getSolidPartRange(i, start, count)
                                && count > 0)
                            mesh->solidParts.emplace_back(start, count);
                    }
                }
            }

            res.emplace_back();
            Render::DrawCall & draw = res.back();
            draw.material = rmat;
            draw.mesh = mesh;
            draw.objectKey = ventry.key ? ventry.key->hash() : 0;
            draw.objectIncomplete = ventry.incomplete;
            // A key already in the map is already right, so the lookup is
            // the whole cost of a draw whose object has been seen before,
            // and the two string copies happen once per object rather
            // than once per object per publish
            // (docs/IncrementalPublish.md §4d-iv).
            //
            // ⚠️ That leans on a key never being recycled onto a
            // different object, and an internal name *is* reused: delete
            // Box001 and the next object added can be given that name
            // back. It holds because a key is a chain of
            // SoFCSelectionRoot selnodeids and that counter only
            // increments, so the deleted object's key is retired for the
            // life of the process and the new Box001 composes one of its
            // own. But the origin is deliberately not part of
            // NodeKey::hash() or operator==, so nothing here enforces
            // this: pushing anything recyclable — a pointer — into a key
            // would make a stale entry reachable, and this guard would
            // never correct it.
            if (objectInfo && draw.objectKey
                    && !objectInfo->count(draw.objectKey)) {
                if (const auto & org = ventry.key->getOrigin()) {
                    // Identity only, and it costs two string copies: an
                    // internal name is fixed, so the origin captured at
                    // cache build still names the same object. The label
                    // and the type are presentation, they describe no
                    // mesh, and a viewer gets them from the serving
                    // source's ObjectMetaMap instead
                    // (Renderer::setObjectMeta) -- resolving them here
                    // meant a getDocument()+getObject() per draw on
                    // every publish, in every view, for a table only a
                    // remote viewer ever reads.
                    Render::ObjectInfo info;
                    info.doc = org->doc;
                    info.obj = org->obj;
                    // The container chain above the leaf, for per-view
                    // display mode overrides to match against
                    // (docs/CoinRetirement.md 5.9). Same once-per-key
                    // cost class as the leaf strings above; consecutive
                    // nodes owned by one object collapse to one step.
                    std::vector<std::shared_ptr<
                            const SoFCRenderCache::CacheKey::Origin>>
                        chain;
                    ventry.key->getOriginPath(chain);
                    info.path.reserve(chain.size());
                    for (const auto &org2 : chain) {
                        if (!org2)
                            continue;
                        if (!info.path.empty()
                                && info.path.back().obj == org2->obj
                                && info.path.back().doc == org2->doc)
                            continue;
                        info.path.push_back({org2->doc, org2->obj});
                    }
                    if (addedInfo)
                        (*addedInfo)[draw.objectKey] = info;
                    (*objectInfo)[draw.objectKey] = std::move(info);
                }
            }
            draw.wholeObject = ventry.partidx < 0
                && ventry.cache == ventry.cache->getWholeCache();
            draw.partIndex = ventry.partidx;
            draw.indexStart = indexStart;
            draw.indexCount = indexCount;

            // Per-face material (SoFCRenderCache::Material array form,
            // captured from the coin fork's extended lazy element). A
            // present array is authoritative for its channel — an
            // override that replaced a scalar dropped it. A whole draw
            // shades from the mesh's baked material stream; a partial
            // (single-face) draw resolves its face's values into the
            // scalars here, since it draws without the stream flag.
            if (rmat.type == Render::Material::Triangle) {
                const bool hasarrays = material.emissives.getNum()
                    || material.speculars.getNum()
                    || material.shininesses.getNum()
                    || material.metallics.getNum()
                    || material.roughnesses.getNum()
                    || material.finishindices.getNum()
                    // The frames too, and they are the only one of
                    // these a UNIFORM appearance can state: a knurled
                    // shaft has one material and still needs its
                    // per-face frame index read.
                    || material.frameindices.getNum()
                    // And the face texture layers, which a uniform
                    // appearance can state for the same reason: one
                    // colour over a part, one face of it imaged.
                    || material.facetextureindices.getNum();
                if (ventry.partidx < 0) {
                    draw.material.perfacematerial =
                        hasarrays && mesh->materials != nullptr;
                    // Which reading the stream's two alpha slots carry
                    // is the bake's own answer, not this material's:
                    // the cache that baked it is the authority.
                    draw.material.perfacepbr =
                        draw.material.perfacematerial
                        && ventry.cache->hasPbrMaterial();
                    // Whether the layers really got baked is the bake's
                    // answer too: a shape that paints every face with
                    // the same image collapses the stream away, and
                    // then the draw's own layer -- resolved above --
                    // is what every fragment reads.
                    if (draw.material.texturepalette
                            && draw.material.perfacematerial
                            && ventry.cache->hasFaceTexture())
                        draw.material.facetexlayer = -1;
                } else if (hasarrays) {
                    // A single-face draw carries no stream, so its
                    // face's values resolve into the scalars. The
                    // colour arrays are as long as the shape has faces
                    // (clamp); the PBR pair is as long as the
                    // appearance (pad with entry 0).
                    const int p = ventry.partidx;
                    if (int n = material.emissives.getNum())
                        draw.material.emissive =
                            material.emissives[std::min(p, n - 1)];
                    if (int n = material.speculars.getNum())
                        draw.material.specular =
                            material.speculars[std::min(p, n - 1)];
                    if (int n = material.shininesses.getNum())
                        draw.material.shininess =
                            material.shininesses[std::min(p, n - 1)];
                    if (int n = material.metallics.getNum())
                        draw.material.metallic = material.metallics[p < n ? p : 0];
                    if (int n = material.roughnesses.getNum())
                        draw.material.roughness = material.roughnesses[p < n ? p : 0];
                    // A single-face draw carries no stream to index the
                    // palette with, so its face's entry becomes the
                    // draw's own finish and the palette goes away.
                    const int n = material.finishindices.getNum();
                    if (n && material.finishpalette) {
                        const auto &entries = material.finishpalette->entries;
                        const int32_t idx = material.finishindices[p < n ? p : 0];
                        if (idx >= 0 && std::size_t(idx) < entries.size()) {
                            const auto &entry = entries[std::size_t(idx)];
                            draw.material.finish = entry.pattern;
                            draw.material.finishpitch = entry.pitch;
                            draw.material.finishdepth = entry.depth;
                            draw.material.finishangle = entry.angle;
                        }
                    }
                    draw.material.finishpalette.reset();
                    // The face's projection frame, resolved the same way
                    // and for the same reason -- one face has one frame,
                    // and the backend uploads a palette-less draw's own
                    // frame as entry 0.
                    const int nfr = material.frameindices.getNum();
                    if (nfr && material.framepalette) {
                        const auto &entries = material.framepalette->entries;
                        const int32_t idx = material.frameindices[p < nfr ? p : 0];
                        if (idx >= 0 && std::size_t(idx) < entries.size())
                            draw.material.frame = entries[std::size_t(idx)];
                    }
                    draw.material.framepalette.reset();
                    // The face's own image, resolved like its finish.
                    // The palette STAYS: one layer is still a layer of
                    // the array texture, and the draw names it here
                    // rather than reading a stream it does not have.
                    const int nft = material.facetextureindices.getNum();
                    if (nft && draw.material.texturepalette) {
                        const int32_t layer =
                            material.facetextureindices[p < nft ? p : 0];
                        draw.material.facetexlayer =
                            layer > 0 && layer < 128 ? int8_t(layer) : 0;
                    }
                }
            }
            draw.identity = ventry.identity;
            if (!ventry.identity) {
                static_assert(sizeof(draw.model) == sizeof(SbMat),
                              "matrix size mismatch");
                std::memcpy(draw.model, ventry.matrix.getValue(),
                            sizeof(draw.model));
            }

            // A gizmo captured under a SoSkipBoundingGroup keeps its own
            // bounds and stays out of the scene's (Material::skipbounds).
            draw.skipbounds = material.skipbounds;

            // The object's own display mode, so a view can resolve its
            // display style per object instead of the traversal baking
            // one style into this capture (5.8).
            draw.ownStyle = material.ownstyle;
            draw.registeredStyles = material.registeredstyles;
            draw.capturedMode = material.capturedmode;
            draw.traversedMode = material.traversedmode;
            draw.interestBits = material.interestbits;

            // Measured once per entry per publish: the draw-entry build
            // above asked the same question of the same entry.
            const SbBox3f & bbox = ventry.getBoundingBox();
            if (!bbox.isEmpty()) {
                bbox.getMin().getValue(
                        draw.bboxMin[0], draw.bboxMin[1], draw.bboxMin[2]);
                bbox.getMax().getValue(
                        draw.bboxMax[0], draw.bboxMax[1], draw.bboxMax[2]);
            }
            else {
                draw.bboxMin[0] = draw.bboxMin[1] = draw.bboxMin[2] = 1.0f;
                draw.bboxMax[0] = draw.bboxMax[1] = draw.bboxMax[2] = -1.0f;
            }
        }
    }

    // Overlay feeds: restore the scene-graph traversal order (vertex
    // caches are created in traversal order and their ids ascend), so
    // the backend's Sequential overlay view blends like the original GL
    // drawing sequence.
    if (sequentialOrder)
        std::stable_sort(res.begin(), res.end(),
            [](const Render::DrawCall &a, const Render::DrawCall &b) {
                uint64_t ka = a.mesh ? a.mesh->cacheId : 0;
                uint64_t kb = b.mesh ? b.mesh->cacheId : 0;
                return ka < kb;
            });
    // What this translation handed the plan that it cannot reach (see
    // translateCache). Reported per translation and reset, so the line
    // describes one publish rather than a running total, and only when
    // there is something to report -- silence means every drawn mesh
    // names a registered source.
    if (s_unownedProtoTags || s_unownedOwnTags) {
        std::string byType;
        for (const auto & t : s_unownedByType) {
            char buf[160];
            snprintf(buf, sizeof(buf), " %s:%zu/%.1fMB(nv<=%d)",
                     t.first.c_str(), t.second.count,
                     double(t.second.bytes) / 1048576.0,
                     t.second.maxVertices);
            byType += buf;
        }
        Base::Console().Message(
            "render levels: unowned source tags this publish: from proto "
            "node %zu | from own node %zu -- these publish as exact and "
            "the ladder cannot climb or descend them; by node class:%s\n",
            s_unownedProtoTags, s_unownedOwnTags, byType.c_str());
        // The individuals, largest first: a full-size vertex array
        // drawn as a handful of points is only visible at this grain.
        std::string top;
        for (const auto & s : s_unownedTop) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     " %s cache=%llx %.1fMB nv=%d pt=%d ln=%d tri=%d |",
                     s.type.c_str(), (unsigned long long)s.cacheId,
                     double(s.bytes) / 1048576.0, s.numVertices,
                     s.points, s.lines, s.triangles);
            top += buf;
        }
        if (!top.empty())
            Base::Console().Message(
                "render levels: largest unowned:%s\n", top.c_str());
        s_unownedProtoTags = s_unownedOwnTags = 0;
        s_unownedByType.clear();
        s_unownedTop.clear();
    }
    return res;
}

Render::HiddenLineConfig
RendererBridge::translateHiddenLineConfig(SoState * state)
{
    Render::HiddenLineConfig res;
    SoFCDisplayModeElement::HiddenLineConfig config;
    if (!SoFCDisplayModeElement::showHiddenLines(state, &config))
        return res;
    res.show = true;
    res.hideFace = config.hideFace;
    res.hideSeam = config.hideSeam;
    res.hideVertex = config.hideVertex;
    res.perFaceOutline = config.perFaceOutline;
    res.sceneOutline = config.sceneOutline;
    res.outlineWidth = config.outlineWidth;
    res.outlineThicken = float(ViewParams::getOutlineThicken());
    if (const SbColor * color = SoFCDisplayModeElement::getLineColor(state))
        res.lineColor = color->getPackedValue(0.0f);
    else
        res.lineColor = uint32_t(ViewParams::getHiddenLineColor());
    return res;
}

Render::SectionConfig
RendererBridge::translateSectionConfig(App::PropertyContainer * view)
{
    Render::SectionConfig res;
    res.fill = Gui::sectionStyle(view, "Fill", ViewParams::getSectionFill());
    res.fillInvert = Gui::sectionStyle(view, "FillInvert",
                                       ViewParams::getSectionFillInvert());
    res.fillGroup = Gui::sectionStyle(view, "FillGroup",
                                      ViewParams::getSectionFillGroup());
    res.concave = Gui::sectionStyle(view, "Concave",
                                    ViewParams::getSectionConcave());
    res.hatchEnable = Gui::sectionStyle(view, "Hatch",
                                        ViewParams::getSectionHatchTextureEnable());
    res.hatchScale = float(Gui::sectionStyle(view, "HatchScale",
                                             ViewParams::getSectionHatchTextureScale()));
    return res;
}

Render::AOConfig
RendererBridge::translateAOConfig(App::PropertyContainer * view)
{
    Render::AOConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "AO", RenderParams::getAO());
    res.radius = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "AORadius", RenderParams::getAORadius()));
    res.intensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "AOIntensity", RenderParams::getAOIntensity()));
    res.method = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "AOMethod", RenderParams::getAOMethod()));
    res.slices = int(viewParamOverride<App::PropertyInteger>(
            view, "Render", "AOSlices", RenderParams::getAOSlices()));
    res.steps = int(viewParamOverride<App::PropertyInteger>(
            view, "Render", "AOSteps", RenderParams::getAOSteps()));
    return res;
}

Render::CavityConfig
RendererBridge::translateCavityConfig(App::PropertyContainer * view)
{
    Render::CavityConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "Cavity", RenderParams::getCavity());
    res.valley = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CavityValley", RenderParams::getCavityValley()));
    res.ridge = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CavityRidge", RenderParams::getCavityRidge()));
    res.radius = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CavityRadius", RenderParams::getCavityRadius()));
    return res;
}

Render::MatcapConfig
RendererBridge::translateMatcapConfig(App::PropertyContainer * view)
{
    Render::MatcapConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "Matcap", RenderParams::getMatcap());
    res.preset = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "MatcapPreset",
            RenderParams::getMatcapPreset()));
    res.tint = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "MatcapTint", RenderParams::getMatcapTint()));
    return res;
}

bool
RendererBridge::translateShaderParamValues(const App::Property * prop,
                                           std::vector<float> & values)
{
    std::vector<float> res;
    if (auto p = dynamic_cast<const App::PropertyBool*>(prop))
        res = {p->getValue() ? 1.0f : 0.0f};
    else if (auto p = dynamic_cast<const App::PropertyEnumeration*>(prop))
        res = {float(p->getValue())};
    else if (auto p = dynamic_cast<const App::PropertyInteger*>(prop))
        res = {float(p->getValue())};
    else if (auto p = dynamic_cast<const App::PropertyFloat*>(prop))
        res = {float(p->getValue())};
    else if (auto p = dynamic_cast<const App::PropertyColor*>(prop)) {
        App::Color c = p->getValue();
        res = {c.r, c.g, c.b, c.a};
    }
    else if (auto p = dynamic_cast<const App::PropertyVector*>(prop)) {
        Base::Vector3d vec = p->getValue();
        res = {float(vec.x), float(vec.y), float(vec.z)};
    }
    else if (auto p = dynamic_cast<const App::PropertyFloatList*>(prop)) {
        for (double d : p->getValues())
            res.push_back(float(d));
    }
    else if (auto p = dynamic_cast<const App::PropertyIntegerList*>(prop)) {
        for (long l : p->getValues())
            res.push_back(float(l));
    }
    else
        return false;
    if (res.empty())
        return false;
    res.resize((res.size() + 3) & ~size_t(3), 0.0f);
    values = std::move(res);
    return true;
}

std::string
RendererBridge::shaderParamUniformName(const char * propName)
{
    std::string name(propName ? propName : "");
    auto pos = name.find('_');
    if (pos != std::string::npos && pos > 0)
        name = name.substr(pos + 1);
    if (name.compare(0, 2, "u_") != 0)
        name = "u_" + name;
    return name;
}

Render::OcclusionCullConfig
RendererBridge::translateOcclusionCullConfig(App::PropertyContainer *)
{
    // Global parameters only, no per-view override: occlusion culling is
    // a performance mechanism that is meant to leave the image alone, so
    // there is no per-view display intent to express -- and its knobs
    // saved inside a document's views were shadowing the globals every
    // measurement arm set (a saved RenderDebug_Timing=false once blanked
    // a whole A/B/C run the same way).
    Render::OcclusionCullConfig res;
    res.enabled = RenderParams::getOcclusion();
    // Clamped rather than trusted: a script can still set the parameters
    // to anything, and a zero budget or a zero hidden lifetime would
    // turn a performance knob into missing geometry.
    auto atLeast = [](long v, long floor) {
        return uint32_t(v < floor ? floor : v);
    };
    res.visibleTtl = atLeast(RenderParams::getOcclusionVisibleTtl(), 1);
    res.budget = atLeast(RenderParams::getOcclusionBudget(), 1);
    res.minSubtree = atLeast(RenderParams::getOcclusionMinSubtree(), 1);
    res.maxHiddenFrames = atLeast(RenderParams::getOcclusionMaxHidden(), 1);
    // Zero is allowed here, unlike the others: it is the un-padded box
    // test, which is what the failure of §12.6 was, and being able to
    // ask for it back is what lets the padding be measured rather than
    // asserted.
    res.depthPadLsb = float(atLeast(RenderParams::getOcclusionDepthPad(), 0));
    // At least one: zero confirmations would mean a node is skipped
    // without any answer having said so.
    res.hiddenConfirm = atLeast(RenderParams::getOcclusionConfirm(), 1);
    // The software oracle, and the three knobs that belong to it alone.
    // None of the ones above are read when it is on: they exist to
    // contain a latency it does not have (section 12.12).
    res.software = RenderParams::getOcclusionSoftware();
    // Zero is allowed: it is the occluder pass rasterizing nothing,
    // which culls nothing, and being able to ask for that is what makes
    // the pass ablatable rather than merely believed.
    res.occluderTriangles = uint32_t(std::max<long>(0,
            RenderParams::getOcclusionOccluderTris()));
    res.minOccluderPx = float(std::max<long>(0,
            RenderParams::getOcclusionMinOccluder()));
    res.softwareDivisor = atLeast(RenderParams::getOcclusionResolution(), 1);
    // Zero is the automatic pick, so the floor is zero and not one.
    res.softwareThreads = uint32_t(std::max<long>(0,
            RenderParams::getOcclusionThreads()));
    res.softwareSimd = RenderParams::getOcclusionSimd();
    res.benefitProbe = RenderParams::getOcclusionBenefitProbe();
    // The granularity the question is asked at (section 12.17), and the
    // coarse occluder hulls (section 12.16) -- both the software
    // oracle's alone.
    res.perInstance = RenderParams::getOcclusionPerInstance();
    // Occlusion feeding the level plan's downgrade sweep (occlusion as
    // a memory mechanism); 0 = never.
    res.demoteStreak = uint32_t(std::max<long>(0,
            RenderParams::getOcclusionDemoteStreak()));
    res.coarseOccluders = RenderParams::getOcclusionCoarse();
    res.coarseLevel = uint32_t(std::max<long>(0,
            RenderParams::getOcclusionCoarseLevel()));
    res.coarseMinTriangles = uint32_t(std::max<long>(0,
            RenderParams::getOcclusionCoarseMinTris()));
    // Zero is allowed: it is the cache frozen at what it holds, which is
    // how a measurement separates what the hulls do from what building
    // them costs.
    res.coarseBuilds = uint32_t(std::max<long>(0,
            RenderParams::getOcclusionCoarseBuilds()));
    // WARNING: Floored at zero rather than trusted. A negative bias would
    // pull every hull *towards* the camera, which invents occlusion --
    // the one failure this mechanism may not have.
    res.coarseBias = float(std::max<long>(0,
            RenderParams::getOcclusionCoarseBias())) / 100.0f;
    res.coarseMemory = size_t(std::max<long>(0,
            RenderParams::getOcclusionCoarseMemory())) << 20;
    return res;
}

Render::RenderDebugConfig
RendererBridge::translateRenderDebugConfig(App::PropertyContainer * view)
{
    // The debug switches are global parameters only, no per-view
    // override: they are measurement state, and a document that saved
    // them inside its views held every later measurement hostage to
    // what the file happened to carry (a saved RenderDebug_Timing=false
    // once blanked a whole A/B/C occlusion run). Only the custom
    // shader parameters below stay per-view -- they are dynamically
    // named, so no global parameter could stand in for them.
    Render::RenderDebugConfig res;
    res.viewMode = int(RenderParams::getDebugViewMode());
    res.freezeFrame = RenderParams::getDebugFreezeFrame();
    // The same switch as the pipeline stage timers, which the viewer
    // hands to RenderTiming directly: the backend's CPU-against-GPU
    // line is the continuation of that readout past submission, not a
    // separate thing to turn on (docs/FarFieldProxies.md §10.1).
    res.frameTiming = RenderParams::getDebugTiming();
    res.occlusion = RenderParams::getDebugOcclusion();
    res.coverage = RenderParams::getDebugCoverage();
    res.proxyCut = RenderParams::getDebugProxyCut();
    res.proxyGen = RenderParams::getDebugProxyGen();
    res.cullAudit = RenderParams::getDebugCullAudit();
    res.cullBounds = RenderParams::getDebugCullBounds();

    // Dynamic named shader parameters (docs/RenderDebug.md §2.5): every
    // further RenderDebug_* or RenderShadow_* property becomes a
    // like-named vec4(-array) uniform -- RenderDebug_myKnob feeds
    // "uniform vec4 u_myKnob"; list properties span multiple vec4 lanes
    // (RenderDebug_userParams with 16 floats fills the stock shaders'
    // u_userParams[4] fallback pool). The property map is name-ordered,
    // keeping the vector deterministic for the config-change comparison.
    //
    // Each prefixed group excludes the names the engine reads itself:
    // those are settings, not shader inputs, and every one of them would
    // otherwise upload a uniform nobody declares. Any name outside that
    // list is the user's.
    if (view) {
        static const char * const prefixes[] = {"RenderDebug_",
                                                "RenderShadow_"};
        std::map<std::string, App::Property*> props;
        view->getPropertyMap(props);
        for (const auto &v : props) {
            const char *prefix = nullptr;
            for (const char *p : prefixes) {
                if (v.first.compare(0, strlen(p), p) == 0) {
                    prefix = p;
                    break;
                }
            }
            if (!prefix)
                continue;
            std::string name = v.first.substr(strlen(prefix));
            if (name.empty())
                continue;
            if (strcmp(prefix, "RenderDebug_") == 0) {  // its fixed set
                if (name == "ViewMode" || name == "FreezeFrame"
                        || name == "Label"      // the sec 4.3 burn-in toggle
                        || name == "Timing"     // measurement switches, not
                        || name == "Delta"      // shader inputs: each would
                        || name == "Coverage"   // otherwise upload a vec4
                        || name == "Occlusion"  // uniform nobody declares
                        || name == "ProxyCut"
                        || name == "ProxyGen"
                        || name == "CullAudit"
                        || name == "CullBounds")
                    continue;
            }
            else {                          // RenderShadow's fixed set
                bool known = false;
                for (const char * const *k = Gui::shadowRenderPropertyNames();
                     *k && !known; ++k)
                    known = (v.first == *k);
                if (known)
                    continue;
            }
            Render::RenderDebugConfig::UserParam param;
            param.name = name.compare(0, 2, "u_") == 0 ? name : "u_" + name;
            App::Property *prop = v.second;
            if (!translateShaderParamValues(prop, param.values)) {
                static std::set<std::string> warned;
                if (warned.insert(v.first).second)
                    FC_WARN("render debug parameter " << v.first
                            << ": unsupported property type "
                            << prop->getTypeId().getName());
                continue;
            }
            res.userParams.push_back(std::move(param));
        }
    }
    return res;
}

// Extract a typed SoShaderParameter value as floats; empty = unsupported.
static std::vector<float> shaderParamValues(const SoNode * node)
{
    std::vector<float> res;
    if (auto p = dynamic_cast<const SoShaderParameter1f*>(node))
        res = {p->value.getValue()};
    else if (auto p = dynamic_cast<const SoShaderParameter1i*>(node))
        res = {float(p->value.getValue())};
    else if (auto p = dynamic_cast<const SoShaderParameter2f*>(node)) {
        const SbVec2f &v = p->value.getValue();
        res = {v[0], v[1]};
    }
    else if (auto p = dynamic_cast<const SoShaderParameter3f*>(node)) {
        const SbVec3f &v = p->value.getValue();
        res = {v[0], v[1], v[2]};
    }
    else if (auto p = dynamic_cast<const SoShaderParameter4f*>(node)) {
        const SbVec4f &v = p->value.getValue();
        res = {v[0], v[1], v[2], v[3]};
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray1f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i)
            res.push_back(p->value[i]);
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray2f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i) {
            res.push_back(p->value[i][0]);
            res.push_back(p->value[i][1]);
        }
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray3f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i) {
            res.push_back(p->value[i][0]);
            res.push_back(p->value[i][1]);
            res.push_back(p->value[i][2]);
        }
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray4f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i)
            for (int c = 0; c < 4; ++c)
                res.push_back(p->value[i][c]);
    }
    return res;
}

// Fetch a shader object's source and say what dialect it is: inline for
// BGFX_SC and MATERIALX, read from disk for FILENAME with a .sc or .mtlx
// suffix. Empty = not consumable.
static std::string shaderObjectSource(const SoShaderObject * obj,
                                      Render::UserShader::Dialect & dialect,
                                      std::string & sourcePath)
{
    dialect = Render::UserShader::Dialect::ShaderText;
    sourcePath.clear();
    SbString src = obj->sourceProgram.getValue();
    if (src.getLength() == 0)
        return {};
    int type = obj->sourceType.getValue();
    if (type == SoShaderObject::BGFX_SC)
        return src.getString();
    if (type == SoShaderObject::MATERIALX) {
        dialect = Render::UserShader::Dialect::MaterialX;
        return src.getString();
    }
    if (type == SoShaderObject::FILENAME) {
        int len = src.getLength();
        if (len > 5 && src.getSubString(len - 5) == ".mtlx")
            dialect = Render::UserShader::Dialect::MaterialX;
        else if (len <= 3 || src.getSubString(len - 3) != ".sc")
            return {};
        Base::FileInfo fi(src.getString());
        Base::ifstream file(fi);
        if (!file) {
            FC_WARN("user shader source not found: " << src.getString());
            return {};
        }
        std::stringstream ss;
        ss << file.rdbuf();
        // A MaterialX document states its images relative to itself,
        // so the consumer needs the file it was read from, not just
        // its text.
        if (dialect == Render::UserShader::Dialect::MaterialX)
            sourcePath = src.getString();
        return ss.str();
    }
    return {};
}

static std::shared_ptr<const Render::TextureImage>
loadParamImage(const std::string &path, bool keepGray);

/// Decode the images a MaterialX document names.
///
/// Parsing a document means loading the standard data library behind
/// it, which is far too much to do per capture -- and a capture happens
/// on every scene change. So the answer is cached on the document's own
/// identity, its file where it has one and its text where it does not,
/// exactly as the generator caches the shader it makes from it. The
/// pixels underneath are cached again by loadParamImage(), which is
/// what notices a map edited in another program.
///
/// The same inspection says what the worn surface's TRANSMISSION
/// resolves to flat (DocumentInfo::transmission), which is what lets
/// the engine's glass pass claim a transmissive MaterialX surface
/// (docs/MaterialStorage.md sec 17.21) -- so the cache is per document
/// AND surface, and the answer rides the shader as UserShader::glass.
static void loadMaterialXImages(const std::string & xml,
                                const std::string & sourcePath,
                                const std::string & surface,
                                std::vector<Render::UserShader::Image> & out,
                                Render::UserShader::Glass & glass)
{
    out.clear();
    glass = Render::UserShader::Glass();
    if (!Render::MaterialX::available())
        return;
    struct Entry {
        std::vector<std::string> images;
        Render::MaterialX::DocumentInfo::Transmission transmission;
    };
    static std::map<std::string, Entry> cache;
    const std::string key =
        (sourcePath.empty() ? xml : sourcePath) + '\0' + surface;
    auto it = cache.find(key);
    if (it == cache.end()) {
        Render::MaterialX::DocumentInfo info =
            Render::MaterialX::inspect(xml, sourcePath, surface);
        Entry entry;
        entry.images = std::move(info.images);
        entry.transmission = info.transmission;
        it = cache.emplace(key, std::move(entry)).first;
    }
    for (const std::string & path : it->second.images) {
        Render::UserShader::Image image;
        image.path = path;
        // Never as grey: a map the document reads one channel of is
        // still an RGB file, and the generated code samples .rgb.
        image.image = loadParamImage(path, false);
        out.push_back(std::move(image));
    }
    const auto & t = it->second.transmission;
    if (!t.glass)
        return;
    glass.claimed = true;
    glass.ior = t.ior > 0.0f ? t.ior : 1.5f;
    // OpenPBR: the colour is what survives the stated depth, so the
    // Beer-Lambert density is its reciprocal -- the same reading the
    // path tracer's absorption volume takes of it (CyclesMaterialX.cpp).
    // No depth is not "automatic" but none: the colour is then a tint
    // applied once at the surface, and glasscolor carries it either way.
    glass.density = t.depth > 0.0f ? 1.0f / t.depth : 0.0f;
    glass.roughness = std::min(std::max(t.roughness, 0.0f), 1.0f);
    std::copy(t.color, t.color + 3, glass.color);
    // A mapped roughness cannot be sampled by a pass that draws the
    // body with one roughness; the map's MEAN stands in, which follows
    // the document where the model's default would ignore it. The
    // decoded pixels are in hand already -- the map is one of the
    // document's images, loaded above.
    if (!t.roughnessImage.empty()) {
        for (const auto & image : out) {
            if (image.path != t.roughnessImage || !image.image)
                continue;
            const Render::TextureImage & img = *image.image;
            const size_t n = size_t(std::max(img.width, 0))
                * size_t(std::max(img.height, 0));
            const size_t stride = size_t(std::max(img.numComponents, 1));
            if (n == 0 || img.pixels.size() < n * stride * img.sampleSize())
                break;
            double sum = 0.0;
            for (size_t i = 0; i < n; ++i)
                sum += img.component(i * stride);
            glass.roughness = std::min(std::max(float(sum / double(n)), 0.0f), 1.0f);
            break;
        }
    }
}

bool
RendererBridge::translateShaderProgram(const SoNode * node,
                                       Render::UserShader & out)
{
    auto prog = dynamic_cast<const SoShaderProgram*>(node);
    if (!prog)
        return false;
    out.stage = prog->stage.getValue().getString();
    for (int i = 0; i < prog->shaderObject.getNum(); ++i) {
        SoNode * child = prog->shaderObject[i];
        auto obj = dynamic_cast<SoShaderObject*>(child);
        if (!obj || !obj->isActive.getValue())
            continue;
        Render::UserShader::Dialect dialect;
        std::string sourcePath;
        std::string src = shaderObjectSource(obj, dialect, sourcePath);
        if (src.empty())
            continue;
        // A MaterialX document describes the whole surface, so it can
        // only be the program's fragment source; a vertex object
        // carrying one is meaningless and dropped.
        if (dialect != Render::UserShader::Dialect::ShaderText) {
            out.dialect = dialect;
            out.sourcePath = sourcePath;
            // Which surface of the document is worn (sec 17.13). Read
            // off the object that carries the document, so a program
            // whose fragment object states one gets it and every other
            // shape of program leaves it empty.
            out.surface = obj->sourceSurface.getValue().getString();
            // A document names its maps as paths, and the consumer of
            // this shader may have no filesystem to open them with
            // (docs/CyclesIntegration.md sec 6.12). Decoded here, once
            // per document, and carried with it.
            loadMaterialXImages(src, sourcePath, out.surface, out.images,
                                out.glass);
            sourcePath.clear();
        }
        if (obj->isOfType(SoVertexShader::getClassTypeId())) {
            if (dialect != Render::UserShader::Dialect::ShaderText)
                continue;
            out.vertexSource = std::move(src);
        }
        else if (obj->isOfType(SoFragmentShader::getClassTypeId())) {
            // The second fragment object of a program is the particle
            // state step, not a replacement beauty stage
            // (docs/RenderEngine.md §5.8) — a stateful emitter needs
            // three sources and Coin's node triple only has room for
            // two, so the list carries the extra one.
            if (out.fragmentSource.empty())
                out.fragmentSource = std::move(src);
            else if (out.simulateSource.empty())
                out.simulateSource = std::move(src);
            else
                continue;
        }
        else
            continue;   // geometry shaders: not consumable by bgfx
        for (int j = 0; j < obj->parameter.getNum(); ++j) {
            auto pnode = obj->parameter[j];
            auto sp = dynamic_cast<const SoShaderParameter*>(pnode);
            if (!sp || sp->name.getValue().getLength() == 0)
                continue;
            Render::RenderDebugConfig::UserParam param;
            param.name = sp->name.getValue().getString();
            param.values = shaderParamValues(pnode);
            if (param.values.empty()) {
                static std::set<std::string> warned;
                if (warned.insert(param.name).second)
                    FC_WARN("user shader parameter " << param.name
                            << ": unsupported parameter node type "
                            << pnode->getTypeId().getName().getString());
                continue;
            }
            param.values.resize((param.values.size() + 3) & ~size_t(3),
                                0.0f);
            out.params.push_back(std::move(param));
        }
    }
    // A post program only needs a fragment stage (the backend supplies
    // the full-screen vertex shader); nothing at all means nothing to do.
    return !out.fragmentSource.empty() || !out.vertexSource.empty();
}

/// Image file (ground texture/bump map, PBR environment) decoded once
/// per path with Qt and cached — the backend keys GPU uploads on the
/// stable textureId. keepGray preserves grayscale images as one
/// component: a bump map's component count is what tells a height field
/// (1/2) from a tangent-space normal map (3/4).

/// Read a Radiance picture (.hdr / .pic, the RGBE format) into a float
/// TextureImage; null if the file is not one or is malformed.
///
/// This exists because an environment is the one image in a CAD scene
/// that genuinely needs more than a byte a channel: the sky is
/// thousands of times brighter than the wall under it, and clamping
/// that ratio into 0..1 is what makes an 8-bit panorama light a model
/// like a picture instead of like a place.
///
/// RGBE stores three mantissas and one SHARED exponent, so a pixel is
/// four bytes and decodes to `mantissa / 256 * 2^(e - 128)`. A zero
/// exponent is the format's exact zero, not 2^-128.
static std::shared_ptr<Render::TextureImage>
loadRadianceImage(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return nullptr;

    std::string line;
    if (!std::getline(in, line)
            || line.compare(0, 2, "#?") != 0)
        return nullptr;   // not a Radiance picture

    // Header lines until a blank one; only the format matters to us.
    bool rgbe = false;
    while (std::getline(in, line)) {
        if (line.empty() || line == "\r")
            break;
        if (line.find("FORMAT=") != std::string::npos)
            rgbe = line.find("32-bit_rle_rgbe") != std::string::npos;
    }
    if (!rgbe)
        return nullptr;   // XYZE and friends are not worth guessing at

    // Resolution line. Only the standard orientation is accepted: a
    // rotated or flipped panorama would need the same care in the
    // sampler, and no tool writes one.
    int height = 0, width = 0;
    if (!std::getline(in, line)
            || std::sscanf(line.c_str(), "-Y %d +X %d", &height, &width) != 2
            || width <= 0 || height <= 0
            || width > (1 << 16) || height > (1 << 16))
        return nullptr;

    const size_t n = size_t(width) * size_t(height);
    std::vector<uint8_t> rgbeRows(n * 4);

    auto readFlat = [&](uint8_t *dst, int count) {
        return bool(in.read(reinterpret_cast<char *>(dst), count * 4));
    };

    for (int y = 0; y < height; ++y) {
        uint8_t *row = rgbeRows.data() + size_t(y) * width * 4;
        uint8_t head[4];
        if (!in.read(reinterpret_cast<char *>(head), 4))
            return nullptr;
        const bool adaptive = head[0] == 2 && head[1] == 2
            && ((int(head[2]) << 8) | head[3]) == width && width >= 8
            && width < 32768;
        if (!adaptive) {
            // Flat scanline: the four bytes just read are its first
            // pixel, the rest follow.
            std::memcpy(row, head, 4);
            if (width > 1 && !readFlat(row + 4, width - 1))
                return nullptr;
            continue;
        }
        // Adaptive RLE: each of the four components is run-length
        // encoded across the whole scanline, one component at a time.
        for (int c = 0; c < 4; ++c) {
            int x = 0;
            while (x < width) {
                int code = in.get();
                if (code == EOF)
                    return nullptr;
                if (code > 128) {
                    const int run = code - 128;
                    const int value = in.get();
                    if (value == EOF || x + run > width)
                        return nullptr;
                    for (int i = 0; i < run; ++i, ++x)
                        row[x * 4 + c] = uint8_t(value);
                } else {
                    const int run = code ? code : 1;
                    if (x + run > width)
                        return nullptr;
                    for (int i = 0; i < run; ++i, ++x) {
                        const int value = in.get();
                        if (value == EOF)
                            return nullptr;
                        row[x * 4 + c] = uint8_t(value);
                    }
                }
            }
        }
    }

    auto tex = std::make_shared<Render::TextureImage>();
    static uint64_t nextHdrId = 0;
    tex->textureId = (uint64_t(1) << 62) | ++nextHdrId;
    tex->width = width;
    tex->height = height;
    tex->numComponents = 3;
    tex->sample = Render::TextureImage::F32;
    tex->wrapS = Render::TextureImage::Repeat;
    tex->wrapT = Render::TextureImage::Clamp;
    tex->pixels.resize(n * 3 * sizeof(float));

    auto *out = reinterpret_cast<float *>(tex->pixels.data());
    for (int y = 0; y < height; ++y) {
        // Radiance rows run top-down; TextureImage rows are bottom-up
        // like GL.
        const uint8_t *src = rgbeRows.data() + size_t(y) * width * 4;
        float *dst = out + size_t(height - 1 - y) * width * 3;
        for (int x = 0; x < width; ++x) {
            const uint8_t *p = src + x * 4;
            if (p[3] == 0) {
                dst[0] = dst[1] = dst[2] = 0.0f;   // the format's exact zero
            } else {
                const float f = std::ldexp(1.0f, int(p[3]) - (128 + 8));
                dst[0] = float(p[0]) * f;
                dst[1] = float(p[1]) * f;
                dst[2] = float(p[2]) * f;
            }
            dst += 3;
        }
    }
    return tex;
}

static std::shared_ptr<const Render::TextureImage>
decodeParamImage(const std::string &path, bool keepGray)
{
    // A Radiance picture carries real radiance and Qt cannot read
    // one; everything else goes through Qt as before. Which of the two
    // a file is, is decided by what is IN it and not by what it is
    // called: an environment image embedded in a document is stored
    // under the hash of its content and comes back with no extension at
    // all, and a name test then handed a perfectly good .hdr to Qt,
    // which reads nothing -- so a document reopened with its own
    // environment in it came back unlit. loadRadianceImage() reads the
    // signature line and gives up on anything that is not Radiance, so
    // asking it first costs one open of the file.
    std::shared_ptr<Render::TextureImage> tex = loadRadianceImage(path);
    QString qpath = QString::fromUtf8(path.c_str());
    QImage img;
    if (tex) {
        // already loaded
    } else if (img.load(qpath)) {
        bool alpha = img.hasAlphaChannel();
        bool gray = keepGray && !alpha && img.isGrayscale();
        img = img.convertToFormat(
            gray ? QImage::Format_Grayscale8
                 : alpha ? QImage::Format_RGBA8888
                         : QImage::Format_RGB888);
        // Render::TextureImage rows are bottom-up like GL.
        img = img.mirrored(false, true);
        tex = std::make_shared<Render::TextureImage>();
        // Outside the Coin node-id space the scene textures key on.
        static uint64_t nextId = 0;
        tex->textureId = 0x8000000000000000ULL + ++nextId;
        tex->width = img.width();
        tex->height = img.height();
        tex->numComponents = gray ? 1 : alpha ? 4 : 3;
        int rowLen = img.width() * tex->numComponents;
        tex->pixels.resize(size_t(rowLen) * img.height());
        for (int y = 0; y < img.height(); ++y)
            std::memcpy(tex->pixels.data() + size_t(y) * rowLen,
                        img.constScanLine(y), rowLen);
        // The file itself, when it is one every tier can decode: the
        // transport ships it in place of the pixels (SceneDump v75,
        // docs/MaterialStorage.md sec 17.23). Decided by what is in the
        // file, like the Radiance test above, not by its name.
        QFile file(qpath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray bytes = file.readAll();
            if (Render::isEncodedImage(
                    reinterpret_cast<const uint8_t *>(bytes.constData()),
                    size_t(bytes.size())))
                tex->encoded.assign(bytes.begin(), bytes.end());
        }
    }
    return tex;
}

static std::shared_ptr<const Render::TextureImage>
loadParamImage(const std::string &path, bool keepGray)
{
    if (path.empty())
        return nullptr;

    /*! A decoded image, and what the file it came from looked like when
     * it was decoded.
     *
     * The stamp is why this is not a plain path -> image map. Keyed by
     * path alone the cache never let anything go: an environment map or
     * a ground texture edited in another program went on showing the
     * copy decoded the first time that session, and a path that did not
     * exist yet was remembered as "no image" for good -- both of which
     * read as the file having failed to load. Modification time is only
     * good to the second, hence the size beside it; a rewrite inside one
     * second that lands on the same byte count is the one edit this
     * still cannot see.
     */
    struct Entry {
        std::shared_ptr<const Render::TextureImage> image;
        int64_t mtime = -1;
        uint64_t size = 0;
        std::chrono::steady_clock::time_point checked {};
    };
    static std::map<std::pair<std::string, bool>, Entry> cache;

    // Every frame comes through here, three times over -- environment,
    // ground texture, bump map -- and the render thread has no business
    // stat-ing a file that may be on a network share at frame rate.
    // Once a second is as often as it could tell anything anyway: that
    // is the resolution of the timestamp being compared.
    constexpr auto recheck = std::chrono::seconds(1);

    auto stampOf = [](const std::string &p) {
        Base::FileInfo fi(p);
        if (!fi.exists())
            return std::make_pair(int64_t(-1), uint64_t(0));
        return std::make_pair(fi.lastModified().getSeconds(), fi.size());
    };

    const auto now = std::chrono::steady_clock::now();
    auto key = std::make_pair(path, keepGray);
    auto it = cache.find(key);
    if (it != cache.end()) {
        if (now - it->second.checked < recheck)
            return it->second.image;
        it->second.checked = now;
        if (stampOf(path) == std::make_pair(it->second.mtime,
                                            it->second.size))
            return it->second.image;
    }

    Entry entry;
    entry.checked = now;
    // Stamped before the read, not after: a file still being written
    // when it is decoded then differs from what was recorded, and the
    // next check picks the finished version up rather than trusting a
    // half-written one for the rest of the session.
    const auto [mtime, size] = stampOf(path);
    entry.mtime = mtime;
    entry.size = size;
    entry.image = decodeParamImage(path, keepGray);
    auto &slot = cache[key];
    slot = std::move(entry);
    return slot.image;
}

Render::LightConfig
RendererBridge::translateLightConfig(SoState * state, App::PropertyContainer * view)
{
    // The Shadow draw style's light lives above the render-cache
    // traversal root, so it is resolved from the state's accumulated
    // light element instead of the material feed. The viewer headlight
    // is filtered by type: only Coin's shadow directional light and
    // spot lights qualify.
    Render::LightConfig res;
    // No traversal (a snapshot taken outside a render, e.g. the Cycles
    // path) has no light element to read; the view's own light below
    // is all there is then.
    const SoNodeList & lights = state ? SoLightElement::getLights(state) : SoNodeList();
    for (int i = 0; i < lights.getLength(); ++i) {
        SoNode * node = lights[i];
        if (!node || !node->isOfType(SoLight::getClassTypeId()))
            continue;
        auto light = static_cast<const SoLight *>(node);
        if (!light->on.getValue())
            continue;
        SbVec3f dir(0.0f, 0.0f, -1.0f);
        SbVec3f pos(0.0f, 0.0f, 0.0f);
        if (node->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
            res.spot = false;
            dir = static_cast<const SoDirectionalLight *>(light)
                ->direction.getValue();
        } else if (node->isOfType(SoSpotLight::getClassTypeId())) {
            auto spot = static_cast<const SoSpotLight *>(light);
            res.spot = true;
            dir = spot->direction.getValue();
            pos = spot->location.getValue();
            res.cutOffAngle = spot->cutOffAngle.getValue();
            res.dropOffRate = spot->dropOffRate.getValue();
        } else {
            continue;
        }
        // SoLightElement matrices map to *view reference* coordinates
        // (model * viewing); the backend expects world space (it
        // re-applies its own per-frame view matrix), so multiply the
        // inverse viewing matrix back in.
        SbMatrix mat = SoLightElement::getMatrix(state, i);
        mat.multRight(SoViewingMatrixElement::get(state).inverse());
        mat.multDirMatrix(dir, dir);
        mat.multVecMatrix(pos, pos);
        dir.normalize();
        res.direction[0] = dir[0];
        res.direction[1] = dir[1];
        res.direction[2] = dir[2];
        res.position[0] = pos[0];
        res.position[1] = pos[1];
        res.position[2] = pos[2];
        res.color = light->color.getValue().getPackedValue(0.0f);
        res.intensity = light->intensity.getValue();
        res.valid = true;
        break;
    }
    // No qualifying light in the traversal -- which is every draw style
    // except Shadow, the only one that puts an SoShadowDirectionalLight
    // or SoSpotLight in the graph. The renderer can supply its own from
    // Render_Light* instead, so that everything keyed off a light stops
    // depending on a draw style (docs/CoinRetirement.md stage 4a).
    //
    // Deliberately second, not first: while the Shadow style exists the
    // light it provides keeps winning, so this is additive and the style
    // behaves exactly as before. Off by default, so a view that has not
    // asked for it is lit as it always was.
    if (!res.valid
            && viewParamOverride<App::PropertyBool>(
                view, "Render", "Light", RenderParams::getLight())) {
        Base::Vector3d dir(RenderParams::getLightDirectionX(),
                           RenderParams::getLightDirectionY(),
                           RenderParams::getLightDirectionZ());
        if (auto prop = viewPropOverride<App::PropertyVector>(
                    view, "Render", "LightDirection"))
            dir = prop->getValue();
        if (dir.Length() < 1e-9)
            dir = Base::Vector3d(-1.0, -1.0, -1.0);
        dir.Normalize();
        // Already world space: these are the user's numbers, not a
        // traversal result, so none of the view-reference unwinding the
        // Coin branch needs applies.
        res.direction[0] = float(dir.x);
        res.direction[1] = float(dir.y);
        res.direction[2] = float(dir.z);
        res.spot = viewParamOverride<App::PropertyBool>(
                view, "Render", "LightSpot", RenderParams::getLightSpot());
        if (res.spot) {
            Base::Vector3d pos(RenderParams::getLightPositionX(),
                               RenderParams::getLightPositionY(),
                               RenderParams::getLightPositionZ());
            if (auto prop = viewPropOverride<App::PropertyVector>(
                        view, "Render", "LightPosition"))
                pos = prop->getValue();
            res.position[0] = float(pos.x);
            res.position[1] = float(pos.y);
            res.position[2] = float(pos.z);
            // Coin's SoSpotLight::cutOffAngle is radians; the property is
            // degrees, like the Shadow style's SpotLightCutOffAngle.
            res.cutOffAngle = float(viewParamOverride<App::PropertyFloat>(
                    view, "Render", "LightCutOffAngle",
                    RenderParams::getLightCutOffAngle()) * M_PI / 180.0);
            res.dropOffRate = float(viewParamOverride<App::PropertyFloat>(
                    view, "Render", "LightDropOffRate",
                    RenderParams::getLightDropOffRate()));
        }
        if (auto prop = viewPropOverride<App::PropertyColor>(
                    view, "Render", "LightColor"))
            res.color = prop->getValue().getPackedValue();
        else
            res.color = uint32_t(RenderParams::getLightColor());
        res.intensity = float(viewParamOverride<App::PropertyFloat>(
                view, "Render", "LightIntensity",
                RenderParams::getLightIntensity()));
        res.valid = true;
    }
    if (res.valid) {
        // Render_Shadow (Render group) is a convenience toggle to drop the
        // shadow map while keeping the scene lit; the Shadow draw style
        // still provides the light.
        res.shadow = viewParamOverride<App::PropertyBool>(
                view, "Render", "Shadow", RenderParams::getShadow());
        // Shadow border smoothing scales the backend's variance-map
        // blur; the Shadow draw style materializes Shadow_SmoothBorder
        // (0..100) with the ViewParams default.
        res.smoothBorder = float(viewParamOverride<App::PropertyInteger>(
                view, "RenderShadow", "SmoothBorder",
                ViewParams::getShadowSmoothBorder()));
        // Coin VsmLookup parameters of the plain-VSM (SmoothBorder 0)
        // path; the Shadow draw style materializes Shadow_Epsilon /
        // Shadow_Threshold like the rest.
        res.epsilon = float(viewParamOverride<App::PropertyFloat>(
                view, "RenderShadow", "Epsilon",
                ViewParams::getShadowEpsilon()));
        // A zero (or too-small) epsilon collapses the VSM variance floor,
        // so the Chebyshev bound flips per pixel on the self-shadowed
        // terminator (dark-spot acne). Enforce the same configurable
        // minimum the Shadow_Epsilon property clamps to (ViewParams
        // ShadowEpsilonMinimum, see docs/ShaderDesign.md) since a value
        // stored before that constraint can still reach the backend.
        float epsMin = float(ViewParams::getShadowEpsilonMinimum());
        if (res.epsilon < epsMin)
            res.epsilon = epsMin;
        res.threshold = float(viewParamOverride<App::PropertyFloat>(
                view, "RenderShadow", "Threshold",
                ViewParams::getShadowThreshold()));
        // Coin's N-tap receiver spread kernel (the Shadow draw style's
        // SpreadSize/SpreadSampleSize properties, packed into the Coin
        // smoothBorder field by the viewer; the backend consumes the
        // raw values).
        res.spreadSize = float(viewParamOverride<App::PropertyInteger>(
                view, "RenderShadow", "SpreadSize",
                ViewParams::getShadowSpreadSize()));
        res.spreadSampleSize = float(viewParamOverride<App::PropertyInteger>(
                view, "RenderShadow", "SpreadSampleSize",
                ViewParams::getShadowSpreadSampleSize()));
        res.precision = float(viewParamOverride<App::PropertyFloat>(
                view, "RenderShadow", "Precision",
                ViewParams::getShadowPrecision()));
        // The ground receiver settings honor the per-view RenderShadow_*
        // dynamic properties (Gui::materializeShadowRenderParams, which
        // creates them wherever the render properties are created) with
        // ViewParams fallback. Stage 4d moved them there from the Shadow
        // draw style's own Shadow_* family; the fallbacks are still the
        // ViewParams Shadow* preferences, which have not moved.
        res.ground = viewParamOverride<App::PropertyBool>(
                view, "RenderShadow", "ShowGround", ViewParams::getShadowShowGround());
        res.groundScale = float(viewParamOverride<App::PropertyFloat>(
                view, "RenderShadow", "GroundSizeScale", ViewParams::getShadowGroundScale()));
        // Sizing and placement. The defaults here have to match the ones
        // materializeShadowRenderParams uses, because a container that
        // has no render properties at all carries none of these.
        res.groundAuto = viewParamOverride<App::PropertyBool>(
                view, "RenderShadow", "GroundSizeAuto", true);
        res.groundFollowCamera = viewParamOverride<App::PropertyBool>(
                view, "RenderShadow", "GroundSizeFollowCamera", true);
        res.groundSizeX = float(viewParamOverride<App::PropertyLength>(
                view, "RenderShadow", "GroundSizeX", 100.0));
        res.groundSizeY = float(viewParamOverride<App::PropertyLength>(
                view, "RenderShadow", "GroundSizeY", 100.0));
        res.groundAutoPos = viewParamOverride<App::PropertyBool>(
                view, "RenderShadow", "GroundAutoPosition", true);
        // Coin reads one placement and uses it two ways: as the outright
        // position when GroundAutoPosition is off (and then applies no
        // transform), and as an additional offset -- rotation included --
        // when it is on. Split here so the backend does not have to know
        // the rule.
        Base::Placement pla;
        if (auto prop = viewPropOverride<App::PropertyPlacement>(
                    view, "RenderShadow", "GroundPlacement"))
            pla = prop->getValue();
        if (res.groundAutoPos) {
            res.groundPos[0] = res.groundPos[1] = res.groundPos[2] = 0.0f;
            Base::Matrix4D m = pla.toMatrix();
            // Base::Matrix4D is row-major; LightConfig::groundMatrix is
            // column-major like the renderer's other matrices.
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r)
                    res.groundMatrix[c * 4 + r] = float(m[r][c]);
            }
        }
        else {
            const Base::Vector3d &p = pla.getPosition();
            res.groundPos[0] = float(p.x);
            res.groundPos[1] = float(p.y);
            res.groundPos[2] = float(p.z);
            static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                               0, 0, 1, 0, 0, 0, 0, 1};
            std::copy(identity, identity + 16, res.groundMatrix);
        }
        // The two knobs Coin spent on scene-graph nodes rather than on
        // the quad itself -- an SoLightModel and an SoShapeHints ahead of
        // it -- and which the backend therefore has to be told about
        // separately. Both default on, so a ported ground that ignored
        // them differed from Coin's out of the box.
        res.groundShading = viewParamOverride<App::PropertyBool>(
                view, "RenderShadow", "GroundShading",
                ViewParams::getShadowGroundShading());
        res.groundBackFaceCull = viewParamOverride<App::PropertyBool>(
                view, "RenderShadow", "GroundBackFaceCull",
                ViewParams::getShadowGroundBackFaceCull());
        if (auto prop = viewPropOverride<App::PropertyColor>(view, "RenderShadow", "GroundColor"))
            res.groundColor = prop->getValue().getPackedValue();
        else
            res.groundColor = uint32_t(ViewParams::getShadowGroundColor());
        // Ground texture (Shadow_GroundTexture / ShadowGroundTexture):
        // decoded once per path with Qt and cached — the backend keys
        // GPU uploads on the stable textureId.
        std::string texpath;
        if (auto prop = viewPropOverride<App::PropertyFileIncluded>(
                    view, "RenderShadow", "GroundTexture")) {
            if (prop->getValue())
                texpath = prop->getValue();
        }
        else {
            texpath = ViewParams::getShadowGroundTexture();
        }
        res.groundTexture = loadParamImage(texpath, false);
        if (res.groundTexture) {
            res.groundTextureSize =
                float(viewParamOverride<App::PropertyFloat>(
                    view, "RenderShadow", "GroundTextureSize",
                    ViewParams::getShadowGroundTextureSize()));
        }

        // Ground transparency and bump map (Shadow_GroundTransparency /
        // Shadow_GroundBumpMap; the Shadow draw style materializes the
        // constrained float, so accept both float property types).
        if (auto prop = viewPropOverride<App::PropertyFloat>(
                    view, "RenderShadow", "GroundTransparency"))
            res.groundTransparency = float(prop->getValue());
        else
            res.groundTransparency =
                float(ViewParams::getShadowGroundTransparency());
        res.groundTransparency =
            std::min(1.0f, std::max(0.0f, res.groundTransparency));
        // The shadow's own transparency, which only a shadow-only
        // ground reads (Coin's SoShadowTransparency).
        if (auto prop = viewPropOverride<App::PropertyFloat>(
                    view, "RenderShadow", "Transparency"))
            res.shadowTransparency = float(prop->getValue());
        else
            res.shadowTransparency =
                float(ViewParams::getShadowTransparency());
        res.shadowTransparency =
            std::min(1.0f, std::max(0.0f, res.shadowTransparency));
        std::string bumppath;
        if (auto prop = viewPropOverride<App::PropertyFileIncluded>(
                    view, "RenderShadow", "GroundBumpMap")) {
            if (prop->getValue())
                bumppath = prop->getValue();
        }
        else {
            bumppath = ViewParams::getShadowGroundBumpMap();
        }
        res.groundBumpMap = loadParamImage(bumppath, true);
        // Ground reflection is a render-engine extra (no Coin shadow
        // ground counterpart), so it lives in the Render_* family.
        res.groundReflection = viewParamOverride<App::PropertyBool>(
                view, "Render", "GroundReflection",
                RenderParams::getGroundReflection());
        res.groundReflectionIntensity =
            float(viewParamOverride<App::PropertyFloat>(
                view, "Render", "GroundReflectionIntensity",
                RenderParams::getGroundReflectionIntensity()));
    }
    // The visible sun disc is a render-engine extra like the ground
    // reflection (no Coin counterpart).
    res.sunDisc = viewParamOverride<App::PropertyBool>(
            view, "Render", "SunDisc", RenderParams::getSunDisc());
    res.sunDiscSize = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "SunDiscSize",
            RenderParams::getSunDiscSize()));
    return res;
}

Render::ViewLightConfig
RendererBridge::translateViewLightConfig(SoState * state)
{
    // The ordinary lights of the traversal. The viewer's headlight and
    // backlight are plain SoDirectionalLights sitting in the root
    // *before* the camera, which is what makes a headlight a headlight:
    // with no viewing transform on the state yet, its direction is
    // fixed in eye space and the unwinding below hands the backend the
    // world-space direction that currently corresponds to it. So the
    // camera tracking costs nothing here -- it falls out of the same
    // matrix the scene light uses.
    //
    // Exactly the complement of translateLightConfig: it takes the
    // FIRST light of a shadow-casting type (SoShadowDirectionalLight or
    // SoSpotLight) as the single scene light and stops there, so that
    // one node -- and only that one -- is skipped here. Everything past
    // it is an ordinary light: a further spot keeps its cone but gets no
    // map, a further shadow directional falls through to the plain
    // directional branch below. Which one the scene light claimed is
    // decided by the same walk, in the same order, with the same `on`
    // filter, so the two agree without either seeing the other.
    bool sceneLightTaken = false;
    Render::ViewLightConfig res;
    res.fed = true;
    // GL's LIGHT_MODEL_AMBIENT, which Coin drives from SoEnvironment
    // (default 0.2 grey). A surface's ambient term is this times the
    // material's own ambient colour.
    {
        SbColor amb = SoEnvironmentElement::getAmbientColor(state);
        amb *= SoEnvironmentElement::getAmbientIntensity(state);
        res.ambient = amb.getPackedValue(0.0f);
    }
    const SoNodeList & lights = SoLightElement::getLights(state);
    for (int i = 0; i < lights.getLength(); ++i) {
        if (res.count >= Render::MaxViewLights)
            break;
        SoNode * node = lights[i];
        if (!node || !node->isOfType(SoLight::getClassTypeId()))
            continue;
        auto light = static_cast<const SoLight *>(node);
        // EnableHeadlight off is an `on` of FALSE, and dropping the
        // light here is the whole of honoring it.
        if (!light->on.getValue())
            continue;
        // The scene light's node types. Only the first such node is
        // claimed (translateLightConfig breaks there); the rest are
        // ordinary lights. SoShadowDirectionalLight derives from
        // SoDirectionalLight, so this test has to come before the
        // directional one below either way.
        if (node->isOfType(SoShadowDirectionalLight::getClassTypeId())
                || node->isOfType(SoSpotLight::getClassTypeId())) {
            if (!sceneLightTaken) {
                sceneLightTaken = true;
                continue;
            }
        }

        Render::ViewLight out;
        SbVec3f dir(0.0f, 0.0f, -1.0f);
        SbVec3f pos(0.0f, 0.0f, 0.0f);
        if (node->isOfType(SoSpotLight::getClassTypeId())) {
            // A spot is a positional light with a cone on top: same
            // location and the same SoEnvironment attenuation, the
            // direction read as the cone axis.
            auto spot = static_cast<const SoSpotLight *>(light);
            out.positional = true;
            out.spot = true;
            pos = spot->location.getValue();
            dir = spot->direction.getValue();
            out.cutOffAngle = spot->cutOffAngle.getValue();
            out.dropOffRate = spot->dropOffRate.getValue();
            const SbVec3f & att =
                SoEnvironmentElement::getLightAttenuation(state);
            for (int j = 0; j < 3; ++j)
                out.attenuation[j] = att[j];
        } else if (node->isOfType(SoDirectionalLight::getClassTypeId())) {
            dir = static_cast<const SoDirectionalLight *>(light)
                ->direction.getValue();
        } else if (node->isOfType(SoPointLight::getClassTypeId())) {
            auto point = static_cast<const SoPointLight *>(light);
            out.positional = true;
            pos = point->location.getValue();
            // Coin keeps distance attenuation on SoEnvironment, not on
            // the light, so it is a state read rather than a field.
            const SbVec3f & att =
                SoEnvironmentElement::getLightAttenuation(state);
            for (int j = 0; j < 3; ++j)
                out.attenuation[j] = att[j];
        } else {
            continue;
        }
        // Same view-reference unwinding as the scene light: the light
        // element's matrices are model * viewing, and the backend wants
        // world space because it re-applies its own view matrix.
        const SbMatrix lightMat = SoLightElement::getMatrix(state, i);
        // A light traversed BEFORE the camera carries no viewing
        // transform, so its element matrix is the model matrix alone --
        // identity for the headlight and backlight, which the viewer
        // hangs at the root. That is exactly what makes it a headlight,
        // and it is the one thing a consumer with its own camera has to
        // know: the unwinding below is only valid for the camera that
        // did it. Ship the eye-space direction alongside so a streamed
        // viewer can redo it against its own.
        out.eyeSpace = lightMat.equals(SbMatrix::identity(), 1e-6f);
        if (out.eyeSpace) {
            SbVec3f eyeDir = dir;
            if (eyeDir.length() > 0.0f)
                eyeDir.normalize();
            for (int j = 0; j < 3; ++j) {
                out.eyeDirection[j] = eyeDir[j];
                out.eyePosition[j] = pos[j];
            }
        }
        SbMatrix mat = lightMat;
        mat.multRight(SoViewingMatrixElement::get(state).inverse());
        mat.multDirMatrix(dir, dir);
        mat.multVecMatrix(pos, pos);
        if (dir.length() > 0.0f)
            dir.normalize();
        for (int j = 0; j < 3; ++j) {
            out.direction[j] = dir[j];
            out.position[j] = pos[j];
        }
        out.color = light->color.getValue().getPackedValue(0.0f);
        out.intensity = light->intensity.getValue();
        res.lights[res.count++] = out;
    }
    return res;
}

Render::VolumetricConfig
RendererBridge::translateVolumetricConfig(App::PropertyContainer * view)
{
    Render::VolumetricConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "Volumetric", RenderParams::getVolumetric());
    res.intensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "VolumetricIntensity",
            RenderParams::getVolumetricIntensity()));
    res.density = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "VolumetricDensity",
            RenderParams::getVolumetricDensity()));
    res.caustics = viewParamOverride<App::PropertyBool>(
            view, "Render", "Caustics", RenderParams::getCaustics());
    res.causticsIntensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CausticsIntensity",
            RenderParams::getCausticsIntensity()));
    res.causticsScale = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CausticsScale",
            RenderParams::getCausticsScale()));
    res.causticsSpeed = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CausticsSpeed",
            RenderParams::getCausticsSpeed()));
    return res;
}

Render::WaterConfig
RendererBridge::translateWaterConfig(App::PropertyContainer * view)
{
    Render::WaterConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterSurface",
            RenderParams::getWaterSurface());
    res.waveStrength = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterWaveStrength",
            RenderParams::getWaterWaveStrength()));
    res.waveScale = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterWaveScale",
            RenderParams::getWaterWaveScale()));
    res.waveSpeed = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterWaveSpeed",
            RenderParams::getWaterWaveSpeed()));
    res.absorption = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterAbsorption",
            RenderParams::getWaterAbsorption()));
    res.inscatter = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterInscatter",
            RenderParams::getWaterInscatter()));
    res.refraction = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterRefraction",
            RenderParams::getWaterRefraction());
    res.reflection = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterReflection",
            RenderParams::getWaterReflection());
    res.planarReflection = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterPlanarReflection",
            RenderParams::getWaterPlanarReflection());
    res.shadow = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterShadow",
            RenderParams::getWaterShadow());
    res.shadowWobble = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterShadowWobble",
            RenderParams::getWaterShadowWobble()));
    res.rippleType = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "WaterRippleType",
            RenderParams::getWaterRippleType()));
    res.rippleDensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterRippleDensity",
            RenderParams::getWaterRippleDensity()));
    res.impactStrength = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterImpactStrength",
            RenderParams::getWaterImpactStrength()));
    res.impactLife = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterImpactLife",
            RenderParams::getWaterImpactLife()));
    return res;
}

Render::BloomConfig
RendererBridge::translateBloomConfig(App::PropertyContainer * view)
{
    Render::BloomConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "Bloom", RenderParams::getBloom());
    res.threshold = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BloomThreshold",
            RenderParams::getBloomThreshold()));
    res.intensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BloomIntensity",
            RenderParams::getBloomIntensity()));
    res.radius = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BloomRadius",
            RenderParams::getBloomRadius()));
    return res;
}

Render::TemporalConfig
RendererBridge::translateTemporalConfig(App::PropertyContainer * view)
{
    Render::TemporalConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "TemporalAccum",
            RenderParams::getTemporalAccum());
    res.samples = int(viewParamOverride<App::PropertyInteger>(
            view, "Render", "TemporalAccumSamples",
            RenderParams::getTemporalAccumSamples()));
    return res;
}

// The per-draw face-outline width formula (see the selection block above)
// with a nominal 1px base line width: the viewer applies it to the hovered /
// selected face, which carries no explicit outline width.
static float preselOutlineWidth()
{
    float lw = 1.0f;
    float scale = float(ViewParams::getSelectionLineThicken());
    if (scale < 1.0f)
        scale = 1.0f;
    float w = lw * scale;
    if (ViewParams::getSelectionLineMaxWidth() > 1.0)
        w = std::min<float>(w, std::max<float>(lw,
                float(ViewParams::getSelectionLineMaxWidth())));
    return std::max(w * 1.5f, lw * float(ViewParams::getOutlineThicken()));
}

Render::PreselHighlightConfig
RendererBridge::translatePreselConfig()
{
    Render::PreselHighlightConfig res;
    res.color = (uint32_t)ViewParams::getHighlightColor();
    res.faceOutline = ViewParams::getShowPreSelectedFaceOutline();
    res.outlineOnly = ViewParams::getNoPreSelFaceHighlightWithOutline();
    res.outlineWidth = preselOutlineWidth();
    res.pickRadius = (float)ViewParams::getPickRadius();
    res.loupeLift = (float)ViewParams::getTouchLoupeLift();
    return res;
}

Render::PreselHighlightConfig
RendererBridge::translateSelConfig()
{
    Render::PreselHighlightConfig res;
    res.color = (uint32_t)ViewParams::getSelectionColor();
    res.faceOutline = ViewParams::getShowSelectedFaceOutline();
    res.outlineOnly = ViewParams::getNoSelFaceHighlightWithOutline();
    res.outlineWidth = preselOutlineWidth();
    res.pickRadius = (float)ViewParams::getPickRadius();
    return res;
}

Render::BumpConfig
RendererBridge::translateBumpConfig(App::PropertyContainer * view)
{
    Render::BumpConfig res;
    res.scale = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BumpScale", RenderParams::getBumpScale()));
    res.parallax = viewParamOverride<App::PropertyBool>(
            view, "Render", "Parallax", RenderParams::getParallax());
    return res;
}

Render::OutputConfig
RendererBridge::translateOutputConfig(App::PropertyContainer * view)
{
    Render::OutputConfig res;
    res.transform = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "OutputTransform",
            RenderParams::getOutputTransform()));
    res.exposure = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "Exposure", RenderParams::getExposure()));
    return res;
}

Render::PBRConfig
RendererBridge::translatePBRConfig(App::PropertyContainer * view)
{
    Render::PBRConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "PBR", RenderParams::getPBR());
    res.metallic = float(viewParamOverride<App::PropertyFloatConstraint>(
            view, "Render", "PBRMetallic", RenderParams::getPBRMetallic()));
    res.roughness = float(viewParamOverride<App::PropertyFloatConstraint>(
            view, "Render", "PBRRoughness", RenderParams::getPBRRoughness()));
    res.envPreset = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "PBREnvPreset", RenderParams::getPBREnvPreset()));
    res.envIntensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "PBREnvIntensity", RenderParams::getPBREnvIntensity()));
    res.envBackground = viewParamOverride<App::PropertyBool>(
            view, "Render", "PBREnvBackground",
            RenderParams::getPBREnvBackground());
    res.envBlur = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "PBREnvBlur", RenderParams::getPBREnvBlur()));
    res.fromSpecular = viewParamOverride<App::PropertyBool>(
            view, "Render", "PBRFromSpecular",
            RenderParams::getPBRFromSpecular());
    res.shininessMapping = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "ShininessMapping",
            RenderParams::getShininessMapping()));
    // User environment image. With no explicit path, fall back to the
    // image the Texture mapping dialog (Std_TextureMapping) holds — its
    // Environment mode sphere-maps that same file over the scene
    // through Coin, so picking one there also lights the backend.
    std::string envpath;
    // An embedded copy (Render_PBREnvEmbed) wins: it travels with the
    // document, while the path may not resolve on another machine.
    if (auto prop = viewPropOverride<App::PropertyFileIncluded>(
                view, "Render", "PBREnvImageData")) {
        if (prop->getValue())
            envpath = prop->getValue();
    }
    if (envpath.empty()) {
        if (auto prop = viewPropOverride<App::PropertyFile>(
                    view, "Render", "PBREnvImage")) {
            if (prop->getValue())
                envpath = prop->getValue();
        }
        else {
            envpath = RenderParams::getPBREnvImage();
        }
    }
    if (envpath.empty())
        envpath = App::GetApplication().Config()["TextureImage"];
    res.envImage = loadParamImage(envpath, false);
    return res;
}

float
RendererBridge::translateEffectResolution(App::PropertyContainer * view)
{
    return float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "EffectResolution",
            RenderParams::getEffectResolution()));
}

float
RendererBridge::translateSSAOResolution(App::PropertyContainer * view)
{
    return float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "AOResolution",
            RenderParams::getAOResolution()));
}

float
RendererBridge::translateLevelTolerance(App::PropertyContainer * view)
{
    return float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "LevelTolerance",
            RenderParams::getLevelTolerance()));
}

// The functions from here to translateGpuMemoryBudget read the GLOBAL
// RenderParams only -- no per-view property override. They are the
// ladder's tuning and measurement knobs, not display intent, and the
// per-view copies that documents saved shadowed whatever the harness or
// the user set globally (a saved Render_GpuMemoryBudgetMB once made a
// budget read back as unset after a view restore). The view parameter
// stays for interface stability; display-intent knobs around them
// (LevelTolerance, CoarseTessellation, the effects) still override.

float
RendererBridge::translateLevelPressureRelease(App::PropertyContainer *)
{
    return float(RenderParams::getLevelPressureRelease());
}

bool
RendererBridge::translateLevelDebug(App::PropertyContainer *)
{
    return RenderParams::getLevelDebug();
}

bool
RendererBridge::translateDowngradeLedger(App::PropertyContainer *)
{
    return RenderParams::getDowngradeLedger();
}

bool
RendererBridge::translateClimbHardLimit(App::PropertyContainer *)
{
    return RenderParams::getClimbHardLimit();
}

int
RendererBridge::translateClimbAdmitBatch(App::PropertyContainer *)
{
    return int(RenderParams::getClimbAdmitBatch());
}

int
RendererBridge::translateDescentOrderBatch(App::PropertyContainer *)
{
    return int(RenderParams::getDescentOrderBatch());
}

float
RendererBridge::translateLevelBudgetDeadband(App::PropertyContainer *)
{
    return float(RenderParams::getLevelBudgetDeadband());
}

bool
RendererBridge::translateShapeVertices(App::PropertyContainer *)
{
    return RenderParams::getShapeVertices();
}

bool
RendererBridge::translatePressureDropEdges(App::PropertyContainer *)
{
    return RenderParams::getPressureDropEdges();
}

int
RendererBridge::translateElementGateStagger(App::PropertyContainer *)
{
    return int(RenderParams::getElementGateStagger());
}

int
RendererBridge::translateTinyElementCutoff(App::PropertyContainer *)
{
    return int(RenderParams::getTinyElementCutoff());
}

bool
RendererBridge::translateLoadDropElements(App::PropertyContainer * view)
{
    if (!RenderParams::getLoadDropElements())
        return false;

    // Coarse-first must be on, because that is the arrival this makes
    // room for. Only the level half of PartGui::coarseTessellationLevel
    // is re-asked here: its other conditions -- render cache 3, a
    // backend that drives mesh levels -- are true by construction on
    // the path that reaches this function at all, and its answer is
    // per document while this is one state for the frame.
    {
        static const int envLevel = [] {
            const char *env = std::getenv("FC_COARSE_TESSELLATION");
            return env && *env ? std::atoi(env) : -2;
        }();
        const int level = envLevel != -2
            ? envLevel
            : int(viewParamOverride<App::PropertyInteger>(
                      view, "Render", "CoarseTessellation",
                      long(RenderParams::getCoarseTessellation())));
        if (level < 0)
            return false;
    }

    // The phase that actually matters, and it is the LAST one: the
    // deferred visual drain, where tessellations are built and fed to
    // the renderer a slice at a time.
    //
    // MEASURED, and it refutes the obvious predicate: on the
    // 5455-object rack model the renderer's scene held 0 drawables for
    // the whole of the App restore AND the whole of the deferred
    // view-provider drain -- `eligible 0` on every frame -- and jumped
    // to 11818 the instant both had cleared. A gate armed on the
    // document status bits alone is therefore ON only while there is
    // nothing on screen to suppress, and lifts exactly as the geometry
    // arrives. It would have measured as working and bought nothing.
    if (Gui::Application::Instance
            && Gui::Application::Instance->isBuildingVisuals())
        return true;

    // The other ways a document can still be arriving, kept because
    // they are real even though the case above dominates a .FCStd
    // open: a live progressive import builds its visuals inline as
    // objects appear, so its geometry does reach the view while the
    // document still carries the status bit.
    //
    // Asked of ANY document, not just the one the camera is over: the
    // frame draws them all, and a second document loading behind the
    // first is where the memory this frees is worth the most.
    for (auto doc : App::GetApplication().getDocuments()) {
        if (doc->testStatus(App::Document::Restoring)
                || doc->testStatus(App::Document::Importing)
                || doc->testStatus(App::Document::LiveImport))
            return true;
        auto guiDoc = Gui::Application::Instance
            ? Gui::Application::Instance->getDocument(doc) : nullptr;
        if (guiDoc && guiDoc->isRestoringViewProviders())
            return true;
    }
    return false;
}

size_t
RendererBridge::translateGpuMemoryBudget(App::PropertyContainer *)
{
    // Global parameter only. This used to honor a per-view
    // Render_GpuMemoryBudgetMB override, and documents that saved one
    // shadowed whatever the harness or the preferences set -- a machine
    // resource cap has no per-view intent to express in the first place.
    const long mb = long(RenderParams::getGpuMemoryBudgetMB());
    // Which value stands, said on change. A budget that arrives as 0
    // has two possible reasons -- the parameter is 0, or this translate
    // is never called -- and the frame-side readout can distinguish
    // neither, having only the result.
    // On change, not once: a one-shot here fires on the first frame of
    // the document load, long before anything sets a budget, and then
    // reports "param 0" forever after -- which reads as "the parameter
    // did not arrive" when it simply had not been set yet.
    if (std::getenv("FC_LEVEL_DEBUG")) {
        static long lastMb = -1;
        if (mb != lastMb) {
            lastMb = mb;
            Base::Console().Message(
                "render levels: budget resolve: param %ldMB\n", mb);
        }
    }
    return mb > 0 ? size_t(mb) << 20 : 0;
}

float
RendererBridge::translateAutoZoomScale(SoState * state)
{
  // SoAutoZoomTranslation::getScaleFactor with a node scaleFactor of 1;
  // each autozoom entry multiplies its own scaleFactor in the backend.
  const SbViewVolume & vv = SoViewVolumeElement::get(state);
  if (vv.getWidth() == 0.0f || vv.getHeight() == 0.0f)
    return 1.0f;
  float aspectRatio =
      SoViewportRegionElement::get(state).getViewportAspectRatio();
  return vv.getWorldToScreenScale(SbVec3f(0.f, 0.f, 0.f), 0.1f)
      / (5.0f * aspectRatio);
}

// vim: noai:ts=2:sw=2

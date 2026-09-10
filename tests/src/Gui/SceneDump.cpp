// SPDX-License-Identifier: LGPL-2.1-or-later

/// Round-trip and republish tests for the scene-stream serializer
/// (src/Gui/Renderer/SceneDump.cpp, docs/SceneStreaming.md). Both
/// layouts are covered: the monolithic one a bundled .fcsd capture
/// must stay, and the manifest one the live stream uses.

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <cstring>

#include <Gui/Renderer/MeshSource.h>
#include <Gui/Renderer/SceneDump.h>
#include <Gui/Renderer/SceneLadder.h>
#include <Gui/Renderer/SceneServer.h>

namespace
{

/// Stands in for the publisher's blob store: everything the writer
/// hands over, addressed by content key, and a record of which keys a
/// given publish actually sent bytes for.
struct BlobStore
{
    std::map<std::string, std::vector<uint8_t>> blobs;
    std::set<std::string> sentThisPublish;
    /// The keys that carried geometry, so a test can hold the geometry
    /// back and let everything else arrive.
    std::set<std::string> meshKeys;

    void take(const std::string& key, std::vector<uint8_t>&& bytes)
    {
        sentThisPublish.insert(key);
        blobs.emplace(key, std::move(bytes));
    }
    bool has(const std::string& key) const
    {
        return blobs.find(key) != blobs.end();
    }
};

std::shared_ptr<Render::MeshData> makeMesh(int id, int verts)
{
    // The arrays have to outlive the writer, so they hang off a struct
    // that owns them and is kept alive by the shared_ptr.
    struct Owned: Render::MeshData
    {
        std::vector<float> pos;
        std::vector<int32_t> tris;
    };
    auto m = std::make_shared<Owned>();
    m->cacheId = uint64_t(id);
    m->numVertices = verts;
    m->pos.resize(size_t(verts) * 3);
    for (size_t i = 0; i < m->pos.size(); ++i) {
        m->pos[i] = float(i) + float(id);
    }
    m->tris = {0, 1, 2};
    m->positions = m->pos.data();
    m->triangleIndices = m->tris.data();
    m->numTriangleIndices = int(m->tris.size());
    return m;
}

/// A soup grid heavy enough to declare a ladder (its chunk passes the
/// 64 KB threshold) and real enough to decimate: n x n quads on z = 0,
/// every quad carrying its own six vertices, the way a CAD
/// tessellation arrives.
std::shared_ptr<Render::MeshData> makeGridMesh(int id, int n)
{
    struct Owned: Render::MeshData
    {
        std::vector<float> pos;
        std::vector<float> norm;
        std::vector<int32_t> tris;
    };
    auto m = std::make_shared<Owned>();
    m->cacheId = uint64_t(id);
    const float step = 1.0f / float(n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const float x0 = float(i) * step, x1 = x0 + step;
            const float y0 = float(j) * step, y1 = y0 + step;
            const float quad[6][2] = {{x0, y0}, {x1, y0}, {x1, y1},
                                      {x0, y0}, {x1, y1}, {x0, y1}};
            for (const auto& p : quad) {
                const int32_t base = int32_t(m->pos.size() / 3);
                m->pos.insert(m->pos.end(), {p[0], p[1], 0.0f});
                m->norm.insert(m->norm.end(), {0.0f, 0.0f, 1.0f});
                m->tris.push_back(base);
            }
        }
    }
    m->numVertices = int(m->pos.size() / 3);
    m->positions = m->pos.data();
    m->normals = m->norm.data();
    m->triangleIndices = m->tris.data();
    m->numTriangleIndices = int(m->tris.size());
    return m;
}

std::shared_ptr<Render::TextureImage> makeTexture(int id, size_t bytes)
{
    auto t = std::make_shared<Render::TextureImage>();
    t->textureId = uint64_t(id);
    t->width = 4;
    t->height = 4;
    t->numComponents = 4;
    t->pixels.assign(bytes, uint8_t(id));
    return t;
}

Render::DrawCall makeDraw(uint64_t objectKey,
                          const std::shared_ptr<const Render::MeshData>& mesh,
                          uint32_t diffuse)
{
    Render::DrawCall d;
    d.objectKey = objectKey;
    d.mesh = mesh;
    d.material.diffuse = diffuse;
    d.material.type = 0;
    d.bboxMin[0] = -1.0f;
    d.bboxMin[1] = -1.0f;
    d.bboxMin[2] = -1.0f;
    d.bboxMax[0] = 1.0f;
    d.bboxMax[1] = 1.0f;
    d.bboxMax[2] = 1.0f;
    return d;
}

/// A scene with shared geometry, shared and distinct materials, a
/// texture, an unnamed draw, an overlay and a selection feed — one of
/// each thing the two layouts treat differently.
Render::SceneSnapshot makeScene()
{
    Render::SceneSnapshot snap;
    auto meshA = makeMesh(1, 8);
    auto meshB = makeMesh(2, 12);
    auto tex = makeTexture(7, 256);

    auto shader = std::make_shared<Render::UserShader>();
    shader->stage = "material";
    shader->vertexSource = "// material stage vertex";
    shader->fragmentSource = "// material stage fragment";

    snap.scene.push_back(makeDraw(0x1111, meshA, 0xff0000ff));
    snap.scene.back().material.usershader = shader;
    snap.scene.push_back(makeDraw(0x1111, meshA, 0x00ff00ff));
    snap.scene.push_back(makeDraw(0x2222, meshB, 0xff0000ff));
    snap.scene.back().material.texture = tex;
    snap.scene.push_back(makeDraw(0, meshB, 0x0000ffff));

    snap.selections.emplace_back(3, Render::DrawCallList());
    snap.selections.back().second.push_back(makeDraw(0x1111, meshA, 0xffffffff));
    snap.highlight.push_back(makeDraw(0x2222, meshB, 0xffff00ff));

    Render::SceneSnapshot::Overlay ov;
    ov.id = 9;
    ov.anchor.corner = 2;
    ov.draws.push_back(makeDraw(0x3333, meshA, 0x808080ff));
    snap.overlays.push_back(std::move(ov));

    Render::UserShader post;
    post.stage = "post";
    post.fragmentSource = "// post stage fragment";
    snap.usershaderconf.shaders.push_back(post);

    snap.hatch = makeTexture(8, 128);
    snap.width = 640;
    snap.height = 480;
    return snap;
}

/// Point a snapshot's save-side hooks at \a store, which is what puts
/// the writer into the manifest layout.
void attachSinks(Render::SceneSnapshot& snap, BlobStore& store)
{
    snap.textureBlobs = [&store](const std::string& key,
                                 std::vector<uint8_t>&& px) {
        store.take(key, std::move(px));
    };
    snap.chunkBlobs = [&store](const std::string& key,
                               std::vector<uint8_t>&& bytes) {
        // What the live publisher does: hold on to the bytes only when
        // the store does not have them already.
        if (store.has(key)) {
            store.sentThisPublish.insert(key);
            return;
        }
        store.take(key, std::move(bytes));
    };
    snap.meshBlobs.reuse = [&store](uint64_t, uint32_t&) {
        // No cacheId memo here: every mesh is serialized and hashed, so
        // that the test exercises the hashing path rather than the memo.
        return std::string();
    };
    snap.meshBlobs.store = [&store](uint64_t, const std::string& key,
                                    std::vector<uint8_t>&& chunk) {
        store.meshKeys.insert(key);
        if (store.has(key)) {
            store.sentThisPublish.insert(key);
            return;
        }
        store.take(key, std::move(chunk));
    };
}

/// Resolve everything a loaded snapshot named, out of \a store, the
/// way the viewer does — in rounds, because a payload's parse names
/// further payloads — then run the pass that puts the staged draws and
/// materials where a backend expects them.
::testing::AssertionResult resolve(Render::SceneSnapshot& snap,
                                   const BlobStore& store,
                                   const std::set<std::string>& hold = {})
{
    for (int round = 0; round < 16; ++round) {
        bool progress = false;
        for (size_t i = 0; i < snap.deferredChunks.size(); ++i) {
            auto fill = snap.deferredChunks[i].fill;
            if (!fill) {
                continue;
            }
            const std::string key = snap.deferredChunks[i].key;
            if (hold.count(key)) {
                continue;
            }
            // Every entry must be able to say how big it is: that is
            // the only thing a consumer gets to choose a fetch policy
            // from, so an entry without it would have to be special-cased.
            if (snap.deferredChunks[i].size == 0) {
                return ::testing::AssertionFailure()
                    << "payload " << key << " does not name its size";
            }
            auto it = store.blobs.find(key);
            if (it == store.blobs.end()) {
                return ::testing::AssertionFailure()
                    << "payload " << key << " was never published";
            }
            snap.deferredChunks[i].fill = nullptr;
            if (!fill(snap, it->second.data(), it->second.size())) {
                return ::testing::AssertionFailure()
                    << "payload " << key << " failed to parse";
            }
            progress = true;
        }
        if (!progress) {
            break;
        }
    }
    for (const auto& chunk : snap.deferredChunks) {
        if (chunk.fill && !hold.count(chunk.key)) {
            return ::testing::AssertionFailure()
                << "payload " << chunk.key << " left outstanding";
        }
    }
    if (snap.finalize) {
        snap.finalize(snap);
    }
    return ::testing::AssertionSuccess();
}

/// Resolve, then merge into \a model — what a streaming consumer does,
/// and the only way the scene feed gets assembled under the manifest
/// layout, since a publish may not carry every object.
::testing::AssertionResult resolveInto(Render::SceneSnapshot& snap,
                                       const BlobStore& store,
                                       Render::SceneObjectModel& model)
{
    auto res = resolve(snap, store);
    if (!res) {
        return res;
    }
    if (snap.finalize && !Render::applySceneObjects(snap, model)) {
        return ::testing::AssertionFailure()
            << "delta against version " << snap.baseVersion
            << ", model holds " << model.version;
    }
    return ::testing::AssertionSuccess();
}

/// The invariants a reloaded scene has to satisfy whichever layout
/// carried it.
void expectScene(const Render::SceneSnapshot& snap)
{
    ASSERT_EQ(snap.scene.size(), 4u);
    // Grouping reorders the scene by object, so check by objectKey
    // rather than by position.
    std::map<uint64_t, size_t> perObject;
    for (const auto& d : snap.scene) {
        ++perObject[d.objectKey];
    }
    EXPECT_EQ(perObject[0x1111], 2u);
    EXPECT_EQ(perObject[0x2222], 1u);
    EXPECT_EQ(perObject[0], 1u);

    size_t textured = 0;
    for (const auto& d : snap.scene) {
        ASSERT_TRUE(d.mesh) << "every draw keeps its geometry";
        EXPECT_GT(d.mesh->numVertices, 0);
        if (d.material.texture) {
            ++textured;
            EXPECT_EQ(d.material.texture->pixels.size(), 256u);
            EXPECT_FALSE(d.material.texture->deferred);
        }
    }
    EXPECT_EQ(textured, 1u);

    // Identical geometry collapses onto one object, so the two draws of
    // object 0x1111 share their mesh.
    const Render::MeshData* first = nullptr;
    for (const auto& d : snap.scene) {
        if (d.objectKey != 0x1111) {
            continue;
        }
        if (!first) {
            first = d.mesh.get();
        }
        else {
            EXPECT_EQ(first, d.mesh.get());
        }
    }

    // A shader is a chunk of its own in the manifest layout, so a
    // reference to one that arrived empty would go unnoticed without
    // looking at its source.
    size_t shaded = 0;
    for (const auto& d : snap.scene) {
        if (d.material.usershader) {
            ++shaded;
            EXPECT_EQ(d.material.usershader->fragmentSource,
                      "// material stage fragment");
        }
    }
    EXPECT_EQ(shaded, 1u);
    ASSERT_EQ(snap.usershaderconf.shaders.size(), 1u);
    EXPECT_EQ(snap.usershaderconf.shaders[0].fragmentSource,
              "// post stage fragment");

    ASSERT_EQ(snap.selections.size(), 1u);
    EXPECT_EQ(snap.selections[0].first, 3);
    EXPECT_EQ(snap.selections[0].second.size(), 1u);
    EXPECT_EQ(snap.highlight.size(), 1u);
    ASSERT_EQ(snap.overlays.size(), 1u);
    EXPECT_EQ(snap.overlays[0].id, 9);
    EXPECT_EQ(snap.overlays[0].anchor.corner, 2);
    ASSERT_EQ(snap.overlays[0].draws.size(), 1u);
    EXPECT_EQ(snap.overlays[0].draws[0].material.diffuse, 0x808080ffu);
    ASSERT_TRUE(snap.hatch);
    EXPECT_EQ(snap.hatch->pixels.size(), 128u);
    EXPECT_EQ(snap.width, 640);
}

}  // namespace

/// A bundled capture has nowhere to put chunks and must come back
/// whole, out of the payload alone.
TEST(SceneDump, monolithicRoundTrip)
{
    Render::SceneSnapshot snap = makeScene();
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    EXPECT_TRUE(loaded.deferredChunks.empty());
    EXPECT_FALSE(loaded.finalize);
    expectScene(loaded);
}

/// The output colour transform crosses the wire (v60).
///
/// It has to: the desktop, the headless-serve backend and the browser
/// viewer each finish their own frames, so a transform that stayed on
/// the machine that chose it would leave the tiers rendering the same
/// scene in two different colour spaces. A snapshot older than v60 has
/// no field to read and must come back None -- unencoded is how those
/// frames were written and how they have to keep being drawn.
TEST(SceneDump, theOutputTransformCrossesTheWire)
{
    Render::SceneSnapshot snap = makeScene();
    // Both values, because the DEFAULT is not the thing under test --
    // it has already changed once, and a test that pinned it would fail
    // for that rather than for anything about the wire.
    for (int transform : {int(Render::OutputConfig::None),
                          int(Render::OutputConfig::SRGB)}) {
        snap.outconf.transform = transform;
        // The exposure travels with it (v62) -- a viewer developing the
        // frame a stop darker than the machine that published it is the
        // same divergence as the transform itself.
        snap.outconf.exposure = transform ? 2.5f : 1.0f;
        std::vector<uint8_t> payload;
        ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
        Render::SceneSnapshot loaded;
        ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(),
                                              payload.size(), loaded));
        EXPECT_EQ(loaded.outconf.transform, transform);
        EXPECT_FLOAT_EQ(loaded.outconf.exposure, snap.outconf.exposure);
    }
}

/// A float texture payload crosses the wire as floats (v63).
///
/// This is what an HDR environment is: the sky is thousands of times
/// brighter than the wall under it, and that ratio is the whole reason
/// an image based light reads as a place. A reader that took the bytes
/// for a byte-per-component image would not merely dim it -- it would
/// read four texels' worth of mantissa as four pixels.
TEST(SceneDump, aFloatTexturePayloadKeepsItsFloats)
{
    Render::SceneSnapshot snap = makeScene();
    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = 4242;
    tex->width = 2;
    tex->height = 1;
    tex->numComponents = 3;
    tex->sample = Render::TextureImage::F32;
    // A dim texel and one far beyond anything a byte could hold.
    const float radiance[6] = {0.25f, 0.25f, 0.25f, 800.0f, 800.0f, 800.0f};
    tex->pixels.resize(sizeof(radiance));
    std::memcpy(tex->pixels.data(), radiance, sizeof(radiance));
    snap.hatch = tex;

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));

    ASSERT_TRUE(loaded.hatch);
    EXPECT_EQ(loaded.hatch->sample, Render::TextureImage::F32);
    EXPECT_EQ(loaded.hatch->sampleSize(), 4u);
    ASSERT_EQ(loaded.hatch->pixels.size(), sizeof(radiance));
    for (size_t i = 0; i < 6; ++i)
        EXPECT_FLOAT_EQ(loaded.hatch->component(i), radiance[i])
            << "component " << i;
}

/// The per-face texture palette crosses the wire (v65).
///
/// The images a shape puts on its individual faces are ordinary
/// textures, but WHICH face wears which is carried by the palette's
/// order alone -- so a reader that dropped an entry, or took them out
/// of order, would repaint the wrong faces rather than fail. The layer
/// a draw with no per-vertex stream reads travels with them, since that
/// is the only thing such a draw has to go on.
TEST(SceneDump, thePerFaceTexturePaletteCrossesTheWire)
{
    Render::SceneSnapshot snap = makeScene();
    ASSERT_FALSE(snap.scene.empty());
    auto palette = std::make_shared<Render::TexturePalette>();
    for (int i = 0; i < 3; ++i) {
        auto tex = std::make_shared<Render::TextureImage>();
        tex->textureId = 9100 + uint64_t(i);
        tex->width = 1;
        tex->height = 1;
        tex->numComponents = 3;
        tex->pixels = {uint8_t(10 * i), uint8_t(20 * i), uint8_t(30 * i)};
        palette->entries.push_back(tex);
    }
    snap.scene[0].material.texturepalette = palette;
    snap.scene[0].material.facetexscale = 12.5f;
    snap.scene[0].material.facetexlayer = 2;

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));

    ASSERT_FALSE(loaded.scene.empty());
    const auto &mat = loaded.scene[0].material;
    ASSERT_TRUE(mat.texturepalette);
    ASSERT_EQ(mat.texturepalette->entries.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        const auto &e = mat.texturepalette->entries[size_t(i)];
        ASSERT_TRUE(e) << "layer " << i;
        EXPECT_EQ(e->textureId, 9100u + uint64_t(i)) << "layer " << i;
        ASSERT_EQ(e->pixels.size(), 3u) << "layer " << i;
        EXPECT_EQ(e->pixels[1], uint8_t(20 * i)) << "layer " << i;
    }
    EXPECT_FLOAT_EQ(mat.facetexscale, 12.5f);
    EXPECT_EQ(mat.facetexlayer, 2);

    // And a draw with no palette comes back with none, rather than
    // inheriting the one before it in the table.
    ASSERT_GT(loaded.scene.size(), 1u);
    EXPECT_FALSE(loaded.scene[1].material.texturepalette);
}

TEST(SceneDump, manifestRoundTrip)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    EXPECT_FALSE(loaded.deferredChunks.empty())
        << "the manifest layout names its payloads rather than carrying them";
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));
    expectScene(loaded);
}

/// A texture whose producer kept the file it was decoded from travels
/// as that file (v75): the blob the transport hands over is the PNG,
/// forty bytes here where the pixels would be twelve, and the reader
/// decodes it back to the same twelve on arrival. The desktop's own
/// object keeps both.
static const uint8_t kPng2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73, 0x00, 0x00, 0x00,
    0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xc0,
    0xf0, 0x9f, 0x81, 0x91, 0x81, 0xe1, 0xff, 0xff, 0xff, 0x0c, 0x00, 0x1e,
    0xf6, 0x04, 0xfd, 0x09, 0xed, 0x34, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
// Rows bottom-up like GL: the file's top row (red, green) is second.
static const std::vector<uint8_t> kPng2x2Pixels = {
    0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 0};

std::shared_ptr<Render::TextureImage> makeEncodedTexture(int id)
{
    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = uint64_t(id);
    tex->width = 2;
    tex->height = 2;
    tex->numComponents = 3;
    tex->pixels = kPng2x2Pixels;
    tex->encoded.assign(kPng2x2, kPng2x2 + sizeof(kPng2x2));
    return tex;
}

/// A MaterialX shader travels whole for a tier that has no generator
/// (v74, docs/MaterialStorage.md sec 17.23): its images as ordinary
/// texture records -- deferred pixels under the manifest layout, inline
/// under the bundled one -- each with the array layer the ship hook
/// resolved for it, the sampler and unit the generated program
/// declares, and the glass splice binary beside the mesh one. The hook
/// works on the COPY that travels: the desktop's own object keeps its
/// unresolved layout and empty compiled list.
Render::SceneSnapshot makeMaterialXScene(
    std::shared_ptr<Render::UserShader>& shader)
{
    Render::SceneSnapshot snap;
    auto mesh = makeMesh(3, 8);
    shader = std::make_shared<Render::UserShader>();
    shader->dialect = Render::UserShader::Dialect::MaterialX;
    shader->stage = "material";
    shader->sourcePath = "/models/chess.mtlx";
    shader->fragmentSource = "<materialx version=\"1.39\"/>";
    Render::UserShader::Image a;
    a.path = "/models/albedo.png";
    // One map as its FILE (v75), decoded on arrival like a material's.
    a.image = makeEncodedTexture(11);
    Render::UserShader::Image b;
    b.path = "/models/rough.png";
    b.image = makeTexture(12, 96);
    shader->images = {a, b};
    shader->glass.claimed = true;
    snap.scene.push_back(makeDraw(0x4444, mesh, 0xffffffff));
    snap.scene.back().material.usershader = shader;
    snap.scene.back().material.glass = true;
    snap.scene.back().material.glassmtlx = true;
    snap.width = 320;
    snap.height = 240;
    // What the backend's hook does with its generator: the layers in
    // the order the generated code reads them (here reversed, so a
    // reader that took document order would be caught), the sampler
    // and unit, and a variant carrying both splices.
    snap.shipShader = [](Render::UserShader& s) {
        s.imageSampler = "s_fcMtlxImages";
        s.imageUnit = 13;
        for (auto& img : s.images) {
            img.layer = img.path == "/models/rough.png" ? 0 : 1;
        }
        Render::UserShader::Compiled c;
        c.profile = "300_es";
        c.fsBin = {1, 2, 3};
        c.glassBin = {4, 5, 6};
        s.compiled.push_back(std::move(c));
    };
    return snap;
}

void expectShippedMaterialX(const Render::SceneSnapshot& loaded)
{
    ASSERT_EQ(loaded.scene.size(), 1u);
    const auto& sh = loaded.scene.front().material.usershader;
    ASSERT_TRUE(sh);
    EXPECT_EQ(sh->dialect, Render::UserShader::Dialect::MaterialX);
    EXPECT_EQ(sh->sourcePath, "/models/chess.mtlx");
    EXPECT_EQ(sh->imageSampler, "s_fcMtlxImages");
    EXPECT_EQ(sh->imageUnit, 13);
    ASSERT_EQ(sh->images.size(), 2u);
    EXPECT_EQ(sh->images[0].path, "/models/albedo.png");
    EXPECT_EQ(sh->images[0].layer, 1);
    ASSERT_TRUE(sh->images[0].image);
    EXPECT_EQ(sh->images[0].image->pixels, kPng2x2Pixels);
    EXPECT_EQ(sh->images[0].image->width, 2);
    EXPECT_FALSE(sh->images[0].image->deferred);
    EXPECT_EQ(sh->images[1].path, "/models/rough.png");
    EXPECT_EQ(sh->images[1].layer, 0);
    ASSERT_TRUE(sh->images[1].image);
    EXPECT_EQ(sh->images[1].image->pixels.size(), 96u);
    ASSERT_EQ(sh->compiled.size(), 1u);
    EXPECT_EQ(sh->compiled[0].profile, "300_es");
    EXPECT_EQ(sh->compiled[0].fsBin, (std::vector<uint8_t>{1, 2, 3}));
    EXPECT_EQ(sh->compiled[0].glassBin, (std::vector<uint8_t>{4, 5, 6}));
}

TEST(SceneDump, anEncodedTextureTravelsAsItsFile)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto mesh = makeMesh(4, 8);
    auto tex = makeEncodedTexture(21);
    snap.scene.push_back(makeDraw(0x5555, mesh, 0xffffffff));
    snap.scene.back().material.texture = tex;
    snap.width = 64;
    snap.height = 64;
    attachSinks(snap, store);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    // The blob under the texture's key is the file, not the pixels.
    ASSERT_EQ(tex->contentKey.size(), 40u);
    auto it = store.blobs.find(tex->contentKey);
    ASSERT_NE(it, store.blobs.end());
    EXPECT_EQ(it->second.size(), sizeof(kPng2x2));
    EXPECT_EQ(tex->pixels, kPng2x2Pixels) << "the producer's copy is untouched";

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));
    ASSERT_EQ(loaded.scene.size(), 1u);
    const auto& got = loaded.scene.front().material.texture;
    ASSERT_TRUE(got);
    EXPECT_FALSE(got->deferred);
    EXPECT_EQ(got->width, 2);
    EXPECT_EQ(got->height, 2);
    EXPECT_EQ(got->numComponents, 3);
    EXPECT_EQ(got->pixels, kPng2x2Pixels);

    // Bundled: the file rides inline and is decoded on read.
    Render::SceneSnapshot bundled;
    bundled.scene.push_back(makeDraw(0x5555, mesh, 0xffffffff));
    bundled.scene.back().material.texture = makeEncodedTexture(22);
    bundled.width = 64;
    bundled.height = 64;
    std::vector<uint8_t> file;
    ASSERT_TRUE(Render::saveSceneSnapshot(file, bundled));
    Render::SceneSnapshot back;
    ASSERT_TRUE(Render::loadSceneSnapshot(file.data(), file.size(), back));
    ASSERT_EQ(back.scene.size(), 1u);
    ASSERT_TRUE(back.scene.front().material.texture);
    EXPECT_EQ(back.scene.front().material.texture->pixels, kPng2x2Pixels);
}

/// A texture in flight when the next publish lands is filled where the
/// draws look (v75's memo, SceneSnapshot::textureMemo): the second
/// parse of the same shader chunk hands back the FIRST parse's texture
/// object, and either snapshot's fill lands in it. Without the memo
/// the first object -- the one an unchanged draw keeps -- stayed empty
/// once the publish naming its fetch was superseded.
TEST(SceneDump, aTextureInFlightIsSharedAcrossPublishes)
{
    std::shared_ptr<Render::UserShader> shader;
    BlobStore store;
    Render::SceneSnapshot snap = makeMaterialXScene(shader);
    attachSinks(snap, store);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    auto memo = std::make_shared<Render::SceneSnapshot::TextureMemo>();
    // First publish: parse everything but the image payload itself.
    Render::SceneSnapshot first;
    first.textureMemo = memo;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), first));
    std::string held;
    for (const auto& c : first.deferredChunks)
        if (c.texture && c.fill)
            held = c.key;
    // The image keys are only known once the shader chunk is parsed,
    // so resolve once with nothing held, then find them.
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(first, store, model));
    const auto& shFirst = first.scene.front().material.usershader;
    ASSERT_TRUE(shFirst);
    ASSERT_EQ(shFirst->images.size(), 2u);
    const auto texA = shFirst->images[0].image;
    ASSERT_TRUE(texA);
    EXPECT_EQ(texA->pixels, kPng2x2Pixels);

    // Second publish of the same bytes, parsed afresh: its shader
    // holds the very object the first one does, filled already, and
    // names no fetch for it.
    Render::SceneSnapshot second;
    second.textureMemo = memo;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), second));
    ASSERT_TRUE(resolveInto(second, store, model));
    const auto& shSecond = second.scene.front().material.usershader;
    ASSERT_TRUE(shSecond);
    ASSERT_NE(shSecond.get(), shFirst.get()) << "a re-parse is a fresh shader";
    ASSERT_EQ(shSecond->images.size(), 2u);
    EXPECT_EQ(shSecond->images[0].image.get(), texA.get())
        << "but the same texture object";

    // And the other order: the first publish's fetch never lands (the
    // consumer superseded it), the second's does -- into the object the
    // first parse handed out, which is what an unchanged draw holds.
    // The image payloads are the blobs that ARE the PNG.
    std::set<std::string> pngKeys;
    for (const auto& kv : store.blobs) {
        if (kv.second.size() == sizeof(kPng2x2)
                && std::equal(kv.second.begin(), kv.second.end(), kPng2x2))
            pngKeys.insert(kv.first);
    }
    ASSERT_FALSE(pngKeys.empty());
    auto memo2 = std::make_shared<Render::SceneSnapshot::TextureMemo>();
    Render::SceneSnapshot a;
    a.textureMemo = memo2;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(), a));
    ASSERT_TRUE(resolve(a, store, pngKeys));
    Render::SceneObjectModel modelA;
    ASSERT_TRUE(Render::applySceneObjects(a, modelA));
    auto objA = modelA.objects.find(0x4444);
    ASSERT_NE(objA, modelA.objects.end());
    ASSERT_FALSE(objA->second.draws.empty());
    const auto& shA = objA->second.draws.front().material.usershader;
    ASSERT_TRUE(shA);
    ASSERT_EQ(shA->images.size(), 2u);
    const auto texHeldByA = shA->images[0].image;
    ASSERT_TRUE(texHeldByA);
    EXPECT_TRUE(texHeldByA->deferred);
    EXPECT_TRUE(texHeldByA->pixels.empty());

    Render::SceneSnapshot b;
    b.textureMemo = memo2;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(), b));
    Render::SceneObjectModel model2;
    ASSERT_TRUE(resolveInto(b, store, model2));
    EXPECT_EQ(b.scene.front().material.usershader->images[0].image.get(),
              texHeldByA.get());
    EXPECT_FALSE(texHeldByA->deferred);
    EXPECT_EQ(texHeldByA->pixels, kPng2x2Pixels)
        << "the second publish's fetch filled the first's object";
}

TEST(SceneDump, aMaterialXShaderShipsItsImagesAndGlassSplice)
{
    std::shared_ptr<Render::UserShader> shader;
    BlobStore store;
    Render::SceneSnapshot snap = makeMaterialXScene(shader);
    attachSinks(snap, store);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    // The hook worked on the travelling copy.
    EXPECT_TRUE(shader->compiled.empty());
    EXPECT_TRUE(shader->imageSampler.empty());
    EXPECT_EQ(shader->images[0].layer, -1);

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    // Before the payloads land the images are named, not carried: the
    // shader chunk is outstanding, and once it is parsed its pixels are.
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));
    expectShippedMaterialX(loaded);
}

TEST(SceneDump, aBundledMaterialXShaderCarriesItsImagesInline)
{
    std::shared_ptr<Render::UserShader> shader;
    Render::SceneSnapshot snap = makeMaterialXScene(shader);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    EXPECT_TRUE(loaded.deferredChunks.empty())
        << "a bundled capture carries its pixels, it names nothing";
    expectShippedMaterialX(loaded);
}

/// The label a viewer shows comes from the metadata table, not from the
/// identity the publish path resolves.
///
/// The publish path knows an object's document and internal name for
/// free — they are fixed, and the cache key's origin carries them. It
/// deliberately does NOT know the label: that is presentation, it
/// changes only when a user renames something, and resolving it per
/// draw per publish cost a document lookup for a table only a remote
/// viewer reads. The writer joins the two here.
TEST(SceneDump, aLabelComesFromTheMetaTableNotTheIdentity)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    // What the publish path produces: identity, no label, no type.
    Render::ObjectInfoMap info;
    info[0x1111] = {"MainDoc", "Box", "", ""};
    snap.objectInfo = &info;

    // What the serving source pushes when a document is renamed.
    Render::ObjectMetaMap meta;
    meta["MainDoc"]["Box"] = {"Bo\"x é", "Part::Box"};
    snap.objectMeta = &meta;

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.objectEntries = &entries;

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    auto named = model.objects.find(0x1111);
    ASSERT_TRUE(named != model.objects.end());
    EXPECT_EQ(named->second.entry.info.doc, "MainDoc");
    EXPECT_EQ(named->second.entry.info.obj, "Box");
    EXPECT_EQ(named->second.entry.info.label, "Bo\"x é");
    EXPECT_EQ(named->second.entry.info.type, "Part::Box");
}

/// Both halves are arbitrary UTF-8, INTERNAL NAMES INCLUDED: a name may
/// hold anything a Python identifier may. Nothing on this path may
/// assume ASCII — and the metadata table is keyed by document name then
/// object name for exactly this reason, since no separator byte is
/// unavailable to a name.
TEST(SceneDump, identityAndLabelSurviveNonAsciiNames)
{
    const std::string doc = "文書";
    const std::string obj = "Boîte_日本";
    const std::string label = "Ma « boîte » é";

    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    Render::ObjectInfoMap info;
    info[0x1111] = {doc, obj, "", ""};
    snap.objectInfo = &info;
    Render::ObjectMetaMap meta;
    meta[doc][obj] = {label, "Part::Box"};
    snap.objectMeta = &meta;
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.objectEntries = &entries;

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    auto named = model.objects.find(0x1111);
    ASSERT_TRUE(named != model.objects.end());
    // Byte for byte: the wire carries lengths, so nothing here is
    // scanned for a terminator or cut to a character count.
    EXPECT_EQ(named->second.entry.info.doc, doc);
    EXPECT_EQ(named->second.entry.info.obj, obj);
    EXPECT_EQ(named->second.entry.info.label, label);
}

/// An object with no metadata keeps its identity and goes out unlabelled
/// — which is what the viewer's fallback expects: it shows the internal
/// name, and that is what identifies the object anyway.
TEST(SceneDump, anObjectWithoutMetaKeepsItsIdentity)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    Render::ObjectInfoMap info;
    info[0x1111] = {"MainDoc", "Box", "", ""};
    snap.objectInfo = &info;
    Render::ObjectMetaMap meta;   // a document nobody renamed anything in
    snap.objectMeta = &meta;
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.objectEntries = &entries;

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    auto named = model.objects.find(0x1111);
    ASSERT_TRUE(named != model.objects.end());
    EXPECT_EQ(named->second.entry.info.obj, "Box");
    EXPECT_TRUE(named->second.entry.info.label.empty());
}

/// v38: an object entry names its document object, the naming survives
/// the round trip into the consumer model, and an object the producer
/// could not name stays unnamed rather than inventing one.
TEST(SceneDump, anObjectEntryNamesItsDocumentObject)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    Render::ObjectInfoMap info;
    info[0x1111] = {"MainDoc", "Box", "Bo\"x é", "Part::Box"};
    snap.objectInfo = &info;
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.objectEntries = &entries;

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    // The publisher's own copy is stamped: a later delta re-describing
    // the object serializes the identity from these entries.
    bool stamped = false;
    for (const auto& e : entries) {
        if (e.objectKey == 0x1111) {
            stamped = true;
            EXPECT_EQ(e.info.obj, "Box");
        }
    }
    ASSERT_TRUE(stamped);

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    auto named = model.objects.find(0x1111);
    ASSERT_TRUE(named != model.objects.end());
    EXPECT_EQ(named->second.entry.info.doc, "MainDoc");
    EXPECT_EQ(named->second.entry.info.obj, "Box");
    EXPECT_EQ(named->second.entry.info.label, "Bo\"x é");
    EXPECT_EQ(named->second.entry.info.type, "Part::Box");

    auto unnamed = model.objects.find(0x2222);
    ASSERT_TRUE(unnamed != model.objects.end());
    EXPECT_TRUE(unnamed->second.entry.info.obj.empty());
    EXPECT_TRUE(unnamed->second.entry.info.doc.empty());
}

/// What the whole phase is for: publishing an unchanged scene again
/// must send no bytes, and the root must stay small because the draws
/// are not in it.
TEST(SceneDump, republishSendsNothingNew)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<uint8_t> first;
    ASSERT_TRUE(Render::saveSceneSnapshot(first, snap));
    const size_t published = store.blobs.size();
    EXPECT_GT(published, 0u);

    store.sentThisPublish.clear();
    std::vector<uint8_t> second;
    ASSERT_TRUE(Render::saveSceneSnapshot(second, snap));

    EXPECT_EQ(first, second) << "an unchanged scene serializes identically";
    EXPECT_EQ(store.blobs.size(), published)
        << "a republish of an unchanged scene mints no new content keys";
}

/// Changing one object must leave every other object's chunk alone —
/// the property that index-based referencing would destroy.
TEST(SceneDump, changingOneObjectSparesTheOthers)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    const std::set<std::string> before = store.sentThisPublish;

    // Repaint one draw of object 0x2222 and publish again.
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x2222) {
            d.material.diffuse = 0x123456ffu;
        }
    }
    store.sentThisPublish.clear();
    std::vector<uint8_t> repainted;
    ASSERT_TRUE(Render::saveSceneSnapshot(repainted, snap));

    std::vector<std::string> minted;
    for (const auto& key : store.sentThisPublish) {
        if (!before.count(key)) {
            minted.push_back(key);
        }
    }
    // One new material chunk and the one group manifest that names it.
    // Not the other object's group, and not any mesh: the geometry did
    // not change, and the material is not part of what a mesh hashes.
    EXPECT_EQ(minted.size(), 2u)
        << "only the changed object's manifest and its new material";
}

/// The consumer never learns what kind of payload it is holding, so
/// "can the scene do without this?" has to be answered by the entry.
/// A payload that could not be obtained is offered to `fill` as
/// nothing at all, and the entry says whether that is survivable.
TEST(SceneDump, entriesDecideWhatCanBeGivenUpOn)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    // The two texture payloads, by the keys their pixels hash to.
    const std::vector<uint8_t> texPixels(256, uint8_t(7));
    const std::vector<uint8_t> hatchPixels(128, uint8_t(8));
    std::set<std::string> giveUp {
        Render::sha1Hex(texPixels.data(), texPixels.size()),
        Render::sha1Hex(hatchPixels.data(), hatchPixels.size()),
    };

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));

    size_t abandoned = 0;
    for (int round = 0; round < 16; ++round) {
        bool progress = false;
        for (size_t i = 0; i < loaded.deferredChunks.size(); ++i) {
            auto fill = loaded.deferredChunks[i].fill;
            if (!fill) {
                continue;
            }
            const std::string key = loaded.deferredChunks[i].key;
            loaded.deferredChunks[i].fill = nullptr;
            if (giveUp.count(key)) {
                ++abandoned;
                EXPECT_TRUE(fill(loaded, nullptr, 0))
                    << "a scene renders untextured rather than not at all";
            }
            else {
                auto it = store.blobs.find(key);
                ASSERT_NE(it, store.blobs.end());
                ASSERT_TRUE(fill(loaded, it->second.data(), it->second.size()));
                // Geometry is the opposite case: without it there is
                // nothing to draw, so giving up is refused.
                EXPECT_FALSE(fill(loaded, nullptr, 0))
                    << "payload " << key << " claimed to be dispensable";
            }
            progress = true;
        }
        if (!progress) {
            break;
        }
    }
    EXPECT_EQ(abandoned, 2u) << "both textures were reached and abandoned";
    ASSERT_TRUE(loaded.finalize);
    loaded.finalize(loaded);
    Render::SceneObjectModel model;
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));

    // The scene is whole; only the images are missing.
    EXPECT_EQ(loaded.scene.size(), 4u);
    for (const auto& d : loaded.scene) {
        EXPECT_TRUE(d.mesh);
        if (d.material.texture) {
            EXPECT_FALSE(d.material.texture->deferred)
                << "an abandoned texture must not still read as pending";
            EXPECT_TRUE(d.material.texture->pixels.empty());
        }
    }
}

/// A delta names only what moved. Publishing an unchanged scene against
/// the version a viewer holds should carry no objects at all, and one
/// repainted object should carry exactly itself.
TEST(SceneDump, deltaCarriesOnlyWhatChanged)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    // Publish 1: a full root, and the entries to encode the next
    // against.
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> full;
    ASSERT_TRUE(Render::saveSceneSnapshot(full, snap));
    // The two objects of the *scene* feed. The overlay is its own feed
    // and its draws are not scene objects, however they are keyed.
    ASSERT_EQ(entries.size(), 2u);

    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(full.data(), full.size(), loaded));
    ASSERT_TRUE(resolveInto(loaded, store, model));
    expectScene(loaded);
    EXPECT_EQ(model.version, 1u);

    // Publish 2: nothing changed.
    std::vector<Render::SceneSnapshot::ObjectEntry> entries2;
    snap.baseObjects = entries;
    snap.baseVersion = 1;
    snap.manifestVersion = 2;
    snap.objectEntries = &entries2;
    std::vector<uint8_t> quiet;
    ASSERT_TRUE(Render::saveSceneSnapshot(quiet, snap));
    EXPECT_LT(quiet.size(), full.size())
        << "a delta of nothing must be smaller than naming every object";

    Render::SceneSnapshot loaded2;
    ASSERT_TRUE(Render::loadSceneSnapshot(quiet.data(), quiet.size(), loaded2));
    EXPECT_TRUE(loaded2.objectUpdates.empty())
        << "an unchanged scene carries no object entries";
    EXPECT_TRUE(loaded2.objectsRemoved.empty());
    ASSERT_TRUE(resolveInto(loaded2, store, model));
    EXPECT_EQ(model.version, 2u);
    // The feed is whole even though the publish described none of it.
    expectScene(loaded2);

    // Publish 3: repaint one object.
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x2222) {
            d.material.diffuse = 0x123456ff;
        }
    }
    snap.baseObjects = entries2;
    snap.baseVersion = 2;
    snap.manifestVersion = 3;
    std::vector<Render::SceneSnapshot::ObjectEntry> entries3;
    snap.objectEntries = &entries3;
    std::vector<uint8_t> repaint;
    ASSERT_TRUE(Render::saveSceneSnapshot(repaint, snap));

    Render::SceneSnapshot loaded3;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(repaint.data(), repaint.size(), loaded3));
    ASSERT_EQ(loaded3.objectUpdates.size(), 1u)
        << "only the repainted object";
    EXPECT_EQ(loaded3.objectUpdates[0].entry.objectKey, 0x2222u);
    EXPECT_TRUE(loaded3.objectsRemoved.empty());
    ASSERT_TRUE(resolveInto(loaded3, store, model));
    expectScene(loaded3);
    for (const auto& d : loaded3.scene) {
        if (d.objectKey == 0x2222) {
            EXPECT_EQ(d.material.diffuse, 0x123456ffu);
        }
    }
}

/// An object whose companion draw the producer held back says so on the
/// wire (v55, docs/SceneStreaming.md #13b). Without it a consumer reads
/// every object as complete and grants the Points/Wireframe exemption to
/// one whose faces are merely still coming -- the dots-first load storm,
/// reproduced a tier further out.
TEST(SceneDump, anIncompleteObjectSaysSoOnTheWire)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    // One of the two scene objects published with a companion deferred.
    // Marked on ONE of its two draws: the producer stamps the draws
    // whose companion is missing, and the question is about the object.
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x1111 && d.material.diffuse == 0x00ff00ffu) {
            d.objectIncomplete = true;
        }
    }

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));
    expectScene(loaded);

    auto held = model.objects.find(0x1111);
    ASSERT_TRUE(held != model.objects.end());
    EXPECT_TRUE(held->second.entry.incomplete);
    auto whole = model.objects.find(0x2222);
    ASSERT_TRUE(whole != model.objects.end());
    EXPECT_FALSE(whole->second.entry.incomplete);

    // And it reaches the draws, which is where the contract reads it --
    // every draw of the object, not just the one the producer stamped.
    size_t marked = 0;
    for (const auto& d : loaded.scene) {
        if (d.objectIncomplete) {
            ++marked;
            EXPECT_EQ(d.objectKey, 0x1111u);
        }
    }
    EXPECT_EQ(marked, 2u);
}

/// The mark clears without any geometry moving, which is the case the
/// object entry's placement makes possible and a content key would have
/// hidden: it lives outside the group chunk, so a publish that only
/// drains the producer's capture backlog re-keys nothing. A delta that
/// carried entries by manifest key alone would carry none of it, and the
/// consumer would wait forever for a companion that had arrived.
TEST(SceneDump, theIncompleteMarkClearsWithoutTheGeometryMoving)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x1111) {
            d.objectIncomplete = true;
        }
    }

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> full;
    ASSERT_TRUE(Render::saveSceneSnapshot(full, snap));

    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(full.data(), full.size(), loaded));
    ASSERT_TRUE(resolveInto(loaded, store, model));
    ASSERT_TRUE(model.objects[0x1111].entry.incomplete);

    // Publish 2: the backlog drained. Nothing else about the object
    // changed -- same meshes, same materials, same box -- so its group
    // manifest key is the one publish 1 sent.
    for (auto& d : snap.scene) {
        d.objectIncomplete = false;
    }
    std::vector<Render::SceneSnapshot::ObjectEntry> entries2;
    snap.baseObjects = entries;
    snap.baseVersion = 1;
    snap.manifestVersion = 2;
    snap.objectEntries = &entries2;
    std::vector<uint8_t> drained;
    ASSERT_TRUE(Render::saveSceneSnapshot(drained, snap));

    Render::SceneSnapshot loaded2;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(drained.data(), drained.size(), loaded2));
    ASSERT_EQ(loaded2.objectUpdates.size(), 1u)
        << "the delta has to carry an object whose mark moved, even though "
           "its manifest key did not";
    EXPECT_EQ(loaded2.objectUpdates[0].entry.objectKey, 0x1111u);
    EXPECT_EQ(loaded2.objectUpdates[0].entry.key,
              model.objects[0x1111].entry.key)
        << "the mark is outside the content key: draining the backlog must "
           "not re-key the geometry";
    ASSERT_TRUE(resolveInto(loaded2, store, model));
    EXPECT_FALSE(model.objects[0x1111].entry.incomplete);
    for (const auto& d : loaded2.scene) {
        EXPECT_FALSE(d.objectIncomplete);
    }
}

/// A bundled capture has no object section to hang the mark on, and a
/// capture taken mid-load is exactly the one that needs it.
TEST(SceneDump, aBundledCaptureCarriesTheIncompleteMark)
{
    Render::SceneSnapshot snap = makeScene();
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x2222) {
            d.objectIncomplete = true;
        }
    }
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    expectScene(loaded);
    size_t marked = 0;
    for (const auto& d : loaded.scene) {
        if (d.objectIncomplete) {
            ++marked;
            EXPECT_EQ(d.objectKey, 0x2222u);
        }
    }
    EXPECT_EQ(marked, 1u);
}

/// A delta's changed manifests ride inline (v37). They are new by
/// definition — an object is in the delta exactly because its manifest
/// key changed — so no cache ever answers for them, and fetching them
/// by round trip is what a delta's staging used to be gated on. Full
/// roots stay by reference: their keys usually ARE cached.
TEST(SceneDump, aDeltaCarriesItsChangedManifestsInline)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> full;
    ASSERT_TRUE(Render::saveSceneSnapshot(full, snap));

    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(full.data(), full.size(), loaded));
    for (const auto& c : loaded.deferredChunks) {
        EXPECT_TRUE(c.inlineData.empty())
            << "a full root goes by reference (the store is warm)";
    }
    ASSERT_TRUE(resolveInto(loaded, store, model));

    // Repaint one object: the delta must carry that object's manifest
    // bytes, and they must be exactly what the key names.
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x2222) {
            d.material.diffuse = 0x9abcdef0;
        }
    }
    snap.baseObjects = entries;
    snap.baseVersion = 1;
    snap.manifestVersion = 2;
    std::vector<Render::SceneSnapshot::ObjectEntry> entries2;
    snap.objectEntries = &entries2;
    std::vector<uint8_t> delta;
    ASSERT_TRUE(Render::saveSceneSnapshot(delta, snap));

    Render::SceneSnapshot loaded2;
    ASSERT_TRUE(Render::loadSceneSnapshot(delta.data(), delta.size(),
                                          loaded2));
    ASSERT_EQ(loaded2.objectUpdates.size(), 1u);
    const std::string want = loaded2.objectUpdates[0].entry.key;
    size_t inlined = 0;
    for (size_t i = 0; i < loaded2.deferredChunks.size(); ++i) {
        // Copy out before filling: a group's fill appends its meshes
        // and materials, and no reference into the vector survives it.
        const std::vector<uint8_t> bytes =
            loaded2.deferredChunks[i].inlineData;
        if (bytes.empty()) {
            continue;
        }
        ++inlined;
        EXPECT_EQ(loaded2.deferredChunks[i].key, want)
            << "only the changed object's manifest rides inline";
        EXPECT_EQ(Render::sha1Hex(bytes.data(), bytes.size()),
                  loaded2.deferredChunks[i].key)
            << "the inline bytes must be exactly what the key names";
        // Ingest the way the viewer does: fill from the payload
        // itself, no store answer needed for the manifest.
        auto fill = loaded2.deferredChunks[i].fill;
        ASSERT_TRUE(bool(fill));
        loaded2.deferredChunks[i].fill = nullptr;
        ASSERT_TRUE(fill(loaded2, bytes.data(), bytes.size()));
    }
    EXPECT_EQ(inlined, 1u) << "exactly the changed object's manifest";
    // The leaves it names — meshes, materials — still come by
    // reference, out of the store.
    ASSERT_TRUE(resolveInto(loaded2, store, model));
    expectScene(loaded2);
    for (const auto& d : loaded2.scene) {
        if (d.objectKey == 0x2222) {
            EXPECT_EQ(d.material.diffuse, 0x9abcdef0u);
        }
    }
}

/// An edit re-keys the object's meshes, so the content-addressed
/// bridge (lastGood) cannot answer by construction — and the edited
/// object used to drop to its box for the length of two fetches.
/// Identity survives the re-key: the object keeps showing what it was
/// (SceneObjectModel::lastRole) until the new geometry lands.
TEST(SceneDump, anEditedObjectStandsOnItsOldGeometryNotItsBox)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> full;
    ASSERT_TRUE(Render::saveSceneSnapshot(full, snap));
    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(full.data(), full.size(), loaded));
    ASSERT_TRUE(resolveInto(loaded, store, model));

    // The edit: object 0x2222 gets different geometry — new bytes,
    // therefore a new content key its viewer has never seen.
    auto edited = makeMesh(5, 20);
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x2222) {
            d.mesh = edited;
        }
    }
    snap.baseObjects = entries;
    snap.baseVersion = 1;
    snap.manifestVersion = 2;
    std::vector<Render::SceneSnapshot::ObjectEntry> entries2;
    snap.objectEntries = &entries2;
    std::vector<uint8_t> delta;
    ASSERT_TRUE(Render::saveSceneSnapshot(delta, snap));

    Render::SceneSnapshot loaded2;
    ASSERT_TRUE(Render::loadSceneSnapshot(delta.data(), delta.size(),
                                          loaded2));
    // Fill the changed object's manifest first — from the bytes the
    // delta itself carried (v37) — because its mesh entries only exist
    // after the group chunk parses. THEN withhold every geometry
    // payload: the re-keyed meshes are exactly what has not arrived at
    // the moment a real delta stages.
    {
        bool filledInline = false;
        for (size_t i = 0; i < loaded2.deferredChunks.size(); ++i) {
            const std::vector<uint8_t> bytes =
                loaded2.deferredChunks[i].inlineData;
            if (bytes.empty()) {
                continue;
            }
            auto fill = loaded2.deferredChunks[i].fill;
            ASSERT_TRUE(bool(fill));
            loaded2.deferredChunks[i].fill = nullptr;
            ASSERT_TRUE(fill(loaded2, bytes.data(), bytes.size()));
            filledInline = true;
        }
        ASSERT_TRUE(filledInline);
    }
    std::set<std::string> hold;
    for (const auto& c : loaded2.deferredChunks) {
        if (c.release && c.fill) {
            hold.insert(c.key);
        }
    }
    ASSERT_FALSE(hold.empty());
    ASSERT_TRUE(resolve(loaded2, store, hold));
    ASSERT_TRUE(Render::applySceneObjects(loaded2, model));

    bool found = false;
    for (const auto& d : loaded2.scene) {
        if (d.objectKey != 0x2222) {
            continue;
        }
        found = true;
        EXPECT_FALSE(d.standIn)
            << "the edited object must not fall to its box";
        ASSERT_TRUE(d.mesh);
        EXPECT_GT(d.mesh->numVertices, 0)
            << "it stands on the geometry it was last drawn with";
    }
    EXPECT_TRUE(found) << "the edited object is still in the scene";

    // The bridge ends by itself: deliver the held payloads and the
    // new geometry takes over.
    ASSERT_TRUE(resolveInto(loaded2, store, model));
    for (const auto& d : loaded2.scene) {
        if (d.objectKey == 0x2222 && d.mesh) {
            EXPECT_EQ(d.mesh->numVertices, 20)
                << "the edit shows once its geometry arrives";
        }
    }
}

/// An object that goes away has to be retired by name: nothing else in
/// a delta would say it is gone.
TEST(SceneDump, deltaRetiresObjectsThatWentAway)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> full;
    ASSERT_TRUE(Render::saveSceneSnapshot(full, snap));

    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(full.data(), full.size(), loaded));
    ASSERT_TRUE(resolveInto(loaded, store, model));
    ASSERT_EQ(loaded.scene.size(), 4u);

    // Drop object 0x2222 from the feed.
    Render::DrawCallList kept;
    for (const auto& d : snap.scene) {
        if (d.objectKey != 0x2222) {
            kept.push_back(d);
        }
    }
    snap.scene = kept;
    snap.baseObjects = entries;
    snap.baseVersion = 1;
    snap.manifestVersion = 2;
    std::vector<Render::SceneSnapshot::ObjectEntry> entries2;
    snap.objectEntries = &entries2;
    std::vector<uint8_t> delta;
    ASSERT_TRUE(Render::saveSceneSnapshot(delta, snap));

    Render::SceneSnapshot loaded2;
    ASSERT_TRUE(Render::loadSceneSnapshot(delta.data(), delta.size(), loaded2));
    ASSERT_EQ(loaded2.objectsRemoved.size(), 1u);
    EXPECT_EQ(loaded2.objectsRemoved[0], 0x2222u);
    EXPECT_TRUE(loaded2.objectUpdates.empty());
    ASSERT_TRUE(resolveInto(loaded2, store, model));

    EXPECT_EQ(loaded2.scene.size(), 3u);
    for (const auto& d : loaded2.scene) {
        EXPECT_NE(d.objectKey, 0x2222u) << "a retired object still drawn";
    }
}

/// A delta against a version the consumer does not hold cannot be
/// applied at all — the objects it says nothing about are exactly the
/// ones it assumes are already right.
TEST(SceneDump, deltaAgainstAnUnheldVersionIsRefused)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 7;
    snap.objectEntries = &entries;
    snap.baseObjects = entries;
    snap.baseVersion = 6;   // a version no fresh consumer holds
    std::vector<uint8_t> delta;
    ASSERT_TRUE(Render::saveSceneSnapshot(delta, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(delta.data(), delta.size(), loaded));
    ASSERT_TRUE(resolve(loaded, store));

    Render::SceneObjectModel fresh;
    EXPECT_FALSE(Render::applySceneObjects(loaded, fresh))
        << "a consumer holding nothing must be told to ask for a full root";
}

/// A scene has to be drawable while it is still arriving, so assembly
/// runs again on every chunk that lands. Running it twice must
/// therefore be running it once: the feeds take each group exactly
/// once, and the scene is rebuilt from the model rather than
/// accumulated onto whatever was there.
TEST(SceneDump, assemblingTwiceIsAssemblingOnce)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(),
                                          loaded));
    ASSERT_TRUE(resolve(loaded, store));
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));

    const size_t draws = loaded.scene.size();
    const size_t overlayDraws = loaded.overlays.empty()
        ? 0 : loaded.overlays[0].draws.size();
    expectScene(loaded);

    // Everything that already landed lands again: finalize and the
    // merge both re-run, as they do when a later chunk arrives.
    loaded.finalize(loaded);
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));
    EXPECT_EQ(loaded.scene.size(), draws) << "the feed must not accumulate";
    EXPECT_EQ(loaded.overlays.empty() ? 0 : loaded.overlays[0].draws.size(),
              overlayDraws) << "a feed must not be emptied by a second pass";
    EXPECT_EQ(model.version, 1u);
    expectScene(loaded);
}

/// A server holds published bytes, not the scene behind them, so the
/// only way it can answer a viewer that is behind is by rewriting the
/// object list of a payload it already has. That rewrite has to produce
/// exactly what the serializer would have produced from the scene.
TEST(SceneDump, aSplicedDeltaIsTheDeltaTheWriterWouldHaveWritten)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    // Publish 1, full.
    std::vector<Render::SceneSnapshot::ObjectEntry> entries1;
    snap.manifestVersion = 1;
    snap.sessionId = 99;
    snap.objectEntries = &entries1;
    std::vector<uint8_t> full1;
    ASSERT_TRUE(Render::saveSceneSnapshot(full1, snap));

    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded1;
    ASSERT_TRUE(Render::loadSceneSnapshot(full1.data(), full1.size(),
                                          loaded1));
    ASSERT_TRUE(resolveInto(loaded1, store, model));
    ASSERT_EQ(model.version, 1u);

    // Publish 2 repaints one object and is serialized FULL, recording
    // where its object list landed — this is what a server would hold.
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x2222) {
            d.material.diffuse = 0x777777ff;
        }
    }
    std::vector<Render::SceneSnapshot::ObjectEntry> entries2;
    Render::SceneSnapshot::RootSpans spans;
    snap.manifestVersion = 2;
    snap.baseVersion = 0;
    snap.baseObjects.clear();
    snap.objectEntries = &entries2;
    snap.rootSpans = &spans;
    std::vector<uint8_t> full2;
    ASSERT_TRUE(Render::saveSceneSnapshot(full2, snap));
    snap.rootSpans = nullptr;
    EXPECT_GT(spans.listEnd, spans.listBegin);
    EXPECT_LT(spans.baseVersionAt, spans.listBegin);

    // The same publish serialized natively as a delta against 1 — what
    // the splice has to reproduce.
    std::vector<Render::SceneSnapshot::ObjectEntry> entriesNative;
    snap.baseObjects = entries1;
    snap.baseVersion = 1;
    snap.objectEntries = &entriesNative;
    std::vector<uint8_t> native;
    ASSERT_TRUE(Render::saveSceneSnapshot(native, snap));

    std::vector<Render::SceneSnapshot::ObjectEntry> changed;
    std::vector<uint64_t> removed;
    Render::diffObjectLists(entries1, entries2, changed, removed);
    ASSERT_EQ(changed.size(), 1u) << "one object was repainted";
    EXPECT_EQ(changed[0].objectKey, 0x2222u);
    EXPECT_TRUE(removed.empty());

    // The server answers manifest bytes from its store (v37): with the
    // same bytes the native writer inlined, the splice reproduces it
    // byte for byte.
    auto bytesFor =
        [&store](const std::string& key) -> const std::vector<uint8_t>* {
        auto it = store.blobs.find(key);
        return it == store.blobs.end() ? nullptr : &it->second;
    };
    std::vector<uint8_t> spliced;
    ASSERT_TRUE(Render::spliceObjectDelta(full2, spans, 1, changed, removed,
                                          spliced, bytesFor));
    EXPECT_EQ(spliced, native)
        << "a spliced delta must be byte-identical to a written one";
    // A delta saves the UNCHANGED objects' entries but carries the
    // changed ones' manifests inline (v37), so at two objects it can
    // outweigh its root — the saving is round trips, and bytes only on
    // scenes where unchanged objects dominate, which is what deltas
    // are for. What must still hold: the section grew by no more than
    // the one inline manifest and its framing.
    EXPECT_LT(spliced.size(),
              full2.size() + changed[0].size + 16);

    // And it applies onto a model holding exactly what it is against.
    Render::SceneSnapshot loaded2;
    ASSERT_TRUE(Render::loadSceneSnapshot(spliced.data(), spliced.size(),
                                          loaded2));
    ASSERT_TRUE(resolveInto(loaded2, store, model));
    EXPECT_EQ(model.version, 2u);
    expectScene(loaded2);

    // Bad spans are refused rather than silently producing a payload
    // that parses into nonsense.
    Render::SceneSnapshot::RootSpans bad = spans;
    bad.listEnd = full2.size() + 1;
    std::vector<uint8_t> nope;
    EXPECT_FALSE(Render::spliceObjectDelta(full2, bad, 1, changed, removed,
                                           nope));
}

/// A version is only meaningful within the run that issued it, so the
/// run has to travel with it (v35). Without that a consumer cannot tell
/// "publish 3 of this backend" from "publish 3 of the one before the
/// restart", and a delta would be applied onto the wrong model.
TEST(SceneDump, theRootNamesTheRunThatVersionedIt)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 3;
    snap.sessionId = 0x5eed1234abcdULL;
    snap.objectEntries = &entries;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(),
                                          loaded));
    EXPECT_EQ(loaded.manifestVersion, 3u);
    EXPECT_EQ(loaded.sessionId, 0x5eed1234abcdULL);

    // A bundled capture is one self-contained document with no stream
    // behind it, so it carries neither.
    Render::SceneSnapshot bundle = makeScene();
    bundle.manifestVersion = 3;
    bundle.sessionId = 0x5eed1234abcdULL;
    std::vector<uint8_t> mono;
    ASSERT_TRUE(Render::saveSceneSnapshot(mono, bundle));
    Render::SceneSnapshot loadedMono;
    ASSERT_TRUE(Render::loadSceneSnapshot(mono.data(), mono.size(),
                                          loadedMono));
    EXPECT_EQ(loadedMono.sessionId, 0u);
    EXPECT_EQ(loadedMono.manifestVersion, 0u);
}

/// A scene is drawn while it is still arriving (docs/SceneStreaming.md
/// §6) at the best rung it holds. A draw whose mesh chunk has not
/// landed points at an empty MeshData, which the backend would read
/// past the end of — so what is submitted in its place is a box on the
/// bounds the manifest carries anyway, and the real geometry replaces
/// it through the ordinary update path.
TEST(SceneDump, aDrawStandsInForTheMeshItNames)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    ASSERT_FALSE(store.meshKeys.empty());

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(),
                                          loaded));

    // Everything but the geometry, which is the common case: a mesh
    // chunk is orders of magnitude larger than the manifest naming it.
    ASSERT_TRUE(resolve(loaded, store, store.meshKeys));
    Render::SceneObjectModel model;
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));
    // Two objects and one unnamed draw, over two distinct meshes: one
    // box each. The two draws of the object that names meshA twice
    // share a box, because they describe one piece of geometry and a
    // box apiece would be the same silhouette drawn twice.
    ASSERT_EQ(loaded.scene.size(), 3u)
        << "one box per mesh stands in until the geometry lands";
    for (const auto& d : loaded.scene) {
        EXPECT_TRUE(d.standIn);
        EXPECT_EQ(d.mesh, Render::standInMesh())
            << "every stand-in is the one shared box";
        EXPECT_FALSE(d.identity);
        EXPECT_EQ(d.partIndex, -1)
            << "a box knows the whole object, never a sub-element";
        // Scaled onto the [-1, 1] bounds the draws carry: extent 2 on
        // the diagonal, origin at the low corner.
        EXPECT_FLOAT_EQ(d.model[0], 2.0f);
        EXPECT_FLOAT_EQ(d.model[5], 2.0f);
        EXPECT_FLOAT_EQ(d.model[10], 2.0f);
        EXPECT_FLOAT_EQ(d.model[12], -1.0f);
        EXPECT_FLOAT_EQ(d.model[15], 1.0f);
    }
    // It stands in for the object, so it is the object's colour — the
    // coarse scene reads as the model, not as scaffolding.
    EXPECT_EQ(loaded.scene[0].material.diffuse, 0xff0000ffu);
    EXPECT_EQ(model.objects.size(), 2u)
        << "the objects are known regardless — that is what a root is for";
    for (const auto& entry : model.objects) {
        EXPECT_TRUE(entry.second.resolved())
            << "their manifests did arrive; only the geometry did not";
    }

    // The geometry lands into the very meshes those draws point at, so
    // the next pass has a whole scene without re-reading anything.
    ASSERT_TRUE(resolve(loaded, store));
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));
    expectScene(loaded);
    for (const auto& d : loaded.scene) {
        EXPECT_FALSE(d.standIn)
            << "a rung is climbed through the ordinary update path, so "
               "nothing stands in once the geometry is in hand";
    }
}

/// A fetch order needs to know what a payload is *for*, and a key says
/// nothing: it is a hash. So every chunk carries the objects that want
/// it (docs/SceneStreaming.md §6, phase 4) — recorded as the references
/// are read, which is the only moment the object is known.
///
/// Every object that wants it, not the first: content addressing means
/// one material backs many objects, and taking whoever named it first
/// would rank a chunk the whole scene is waiting on by whatever the
/// smallest, most distant object that happens to wear it is worth.
TEST(SceneDump, everyChunkKnowsWhichObjectsWantIt)
{
    // A scene of its own, because sharing is the point: two objects in
    // the same appearance over the same geometry, which content
    // addressing collapses onto one material chunk and one mesh chunk
    // between them. The stock scene deliberately has no two objects
    // alike.
    BlobStore store;
    Render::SceneSnapshot snap;
    auto mesh = makeMesh(5, 16);
    snap.scene.push_back(makeDraw(0xaaaa, mesh, 0x336699ff));
    snap.scene.push_back(makeDraw(0xbbbb, mesh, 0x336699ff));
    Render::UserShader post;
    post.stage = "post";
    post.fragmentSource = "// post stage fragment";
    snap.usershaderconf.shaders.push_back(post);
    attachSinks(snap, store);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));

    // Straight out of the root, before a single chunk has been fetched:
    // an object's manifest is named with the object beside it, which is
    // what everything below it inherits from.
    std::set<uint64_t> named;
    for (const auto& up : loaded.objectUpdates) {
        named.insert(up.entry.objectKey);
        bool found = false;
        for (const auto& chunk : loaded.deferredChunks) {
            if (chunk.key != up.entry.key) {
                continue;
            }
            found = true;
            EXPECT_EQ(chunk.owners,
                      std::vector<uint64_t>{up.entry.objectKey})
                << "a group manifest is wanted by exactly its object";
        }
        EXPECT_TRUE(found) << "the root named a manifest it did not defer";
    }
    ASSERT_FALSE(named.empty());

    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    // Now that the manifests and the materials have been read, every
    // level below them is attributed too — the meshes and materials a
    // manifest named, and the texture a material named.
    size_t shared = 0, owned = 0;
    for (const auto& chunk : loaded.deferredChunks) {
        for (uint64_t owner : chunk.owners) {
            EXPECT_TRUE(named.count(owner))
                << "chunk " << chunk.key << " claims an object that the "
                   "root never named";
        }
        if (chunk.owners.size() > 1) {
            ++shared;
        }
        if (!chunk.owners.empty()) {
            ++owned;
        }
    }
    EXPECT_GT(owned, named.size())
        << "the manifests alone are owned, so nothing below them was "
           "attributed";
    EXPECT_EQ(shared, 2u)
        << "the material and the mesh are each one chunk behind both "
           "objects, and each has to name both";

    // The chunks nothing in the scene claims stay unclaimed rather than
    // being attributed to whoever happened to read them: the
    // post-stage shader belongs to the view, not to a model object,
    // and a consumer fetches it ahead of the model precisely because
    // no bounding box can rank it.
    bool anyUnowned = false;
    for (const auto& chunk : loaded.deferredChunks) {
        anyUnowned = anyUnowned || chunk.owners.empty();
    }
    EXPECT_TRUE(anyUnowned);
}

/// The rung below that one. Before any group manifest arrives, the only
/// thing known about an object is the box the root named — so that is
/// what is drawn, one box for the whole object, and the model has a
/// silhouette before a single mesh has been asked for.
TEST(SceneDump, anObjectStandsInForItselfBeforeItsManifest)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    // The root alone: nothing else is resolved, which is what a viewer
    // holds one round trip in.
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(),
                                          loaded));
    ASSERT_TRUE(loaded.finalize);
    loaded.finalize(loaded);
    Render::SceneObjectModel model;
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));

    ASSERT_EQ(model.objects.size(), 2u);
    // One box per object the root named, plus one for the draw no
    // object owns: those ride inline in the root, so that draw is
    // already in hand at this rung and stands in on its own bounds.
    ASSERT_EQ(loaded.scene.size(), 3u);
    size_t named = 0;
    for (const auto& d : loaded.scene) {
        EXPECT_TRUE(d.standIn);
        EXPECT_EQ(d.mesh, Render::standInMesh());
        if (d.objectKey) {
            ++named;
            // Nothing names a material yet, so an object's box wears
            // the default appearance rather than a wrong one.
            EXPECT_EQ(d.materialIndex, -1);
        }
    }
    EXPECT_EQ(named, model.objects.size())
        << "a box picks as the object it stands for";

    // And it gives way to the geometry, without the root being re-read.
    ASSERT_TRUE(resolve(loaded, store));
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));
    expectScene(loaded);
}

/// The same, one chunk at a time and assembling after every one, which
/// is what an arrival actually looks like. This is the order that
/// catches a reference resolved too early: finalize copies a material
/// into the draw and the reference is gone, so an object taken before
/// its material chunk arrived would keep a default-constructed
/// appearance — a scene fully shaped and entirely blank — for as long
/// as it lives.
TEST(SceneDump, assemblingAfterEveryChunkGivesTheWholeScene)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(),
                                          loaded));
    Render::SceneObjectModel model;

    size_t applied = 0;
    for (int round = 0; round < 64; ++round) {
        bool progress = false;
        for (size_t i = 0; i < loaded.deferredChunks.size(); ++i) {
            auto fill = loaded.deferredChunks[i].fill;
            if (!fill) {
                continue;
            }
            const std::string key = loaded.deferredChunks[i].key;
            auto it = store.blobs.find(key);
            ASSERT_NE(it, store.blobs.end());
            loaded.deferredChunks[i].fill = nullptr;
            ASSERT_TRUE(fill(loaded, it->second.data(), it->second.size()));
            progress = true;
            // One chunk, then assemble — the viewer commits on every
            // arrival that made something drawable.
            ASSERT_TRUE(loaded.finalize);
            loaded.finalize(loaded);
            ASSERT_TRUE(Render::applySceneObjects(loaded, model));
            ++applied;
            EXPECT_LE(loaded.scene.size(), 4u)
                << "the feed never accumulates past the whole scene";
            break;   // rescan: a parse can name further chunks
        }
        if (!progress) {
            break;
        }
    }
    EXPECT_GT(applied, 1u) << "the scene arrived in more than one piece";
    expectScene(loaded);
}

/// The ladder runs backwards (docs/SceneStreaming.md §6, phase 4b).
///
/// Under memory pressure a viewer gives a payload back, and what that
/// has to leave behind is the state the entry was in before the
/// payload arrived — not a state of its own. So the checks are: only
/// geometry offers to be given back, giving it back puts the draws on
/// the box rung they were on while it was in flight, and the entry's
/// own fill puts it all the way back.
TEST(SceneDump, geometryGivenBackDescendsToTheBoxAndClimbsAgain)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);
    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(payload.data(), payload.size(),
                                          loaded));
    // Keep every fill: an eviction re-arms the entry with the one it
    // resolved with, which is what the viewer's refill map holds.
    std::map<std::string, decltype(loaded.deferredChunks[0].fill)> fills;
    for (const auto& chunk : loaded.deferredChunks) {
        if (chunk.fill) {
            fills[chunk.key] = chunk.fill;
        }
    }
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));
    expectScene(loaded);

    // Only payloads with a rung below them offer to be given back.
    size_t releasable = 0;
    for (const auto& chunk : loaded.deferredChunks) {
        if (chunk.release) {
            ++releasable;
        }
    }
    ASSERT_GT(releasable, 0u) << "no payload can be given back at all";

    const size_t whole = loaded.scene.size();
    size_t standIns = 0;
    for (const auto& d : loaded.scene) {
        standIns += d.standIn ? 1 : 0;
    }
    ASSERT_EQ(standIns, 0u) << "the scene starts fully refined";

    // Give one back, and re-arm it exactly as the viewer does.
    std::string released;
    for (auto& chunk : loaded.deferredChunks) {
        if (!chunk.release || chunk.fill) {
            continue;
        }
        released = chunk.key;
        chunk.release();
        chunk.fill = fills[chunk.key];
        break;
    }
    ASSERT_FALSE(released.empty());
    ASSERT_TRUE(loaded.finalize);
    loaded.finalize(loaded);
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));

    size_t after = 0;
    for (const auto& d : loaded.scene) {
        after += d.standIn ? 1 : 0;
    }
    EXPECT_GT(after, 0u)
        << "geometry given back leaves the draws on the box rung";
    for (const auto& d : loaded.scene) {
        if (d.standIn) {
            // A box is the shared unit box, never the emptied mesh
            // wearing its identity: a backend keys uploads by id, so a
            // stand-in that claimed the mesh's id could be filled in
            // later as if it were the geometry.
            EXPECT_EQ(d.mesh, Render::standInMesh());
        }
    }

    // And the entry's own fill climbs back, without the root being
    // re-read: eviction is the reverse of arrival, not a state.
    ASSERT_TRUE(resolve(loaded, store));
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));
    EXPECT_EQ(loaded.scene.size(), whole);
    expectScene(loaded);
}

/// Phase 5 slice 2 (docs/SceneStreaming.md §7): a deferred mesh
/// reference is a ladder of levels. A big mesh declares coarser levels
/// it cannot yet serve — error stated, key absent — and always closes
/// the list with the exact mesh, keyed, at error 0. A small mesh
/// declares nothing. Either way the entry's own key is the finest
/// built level, so nothing downstream changes until selection lands.
TEST(SceneDump, aBigMeshDeclaresItsLadderOfLevels)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    // ~96 KB of positions: over the declaration threshold. The small
    // one is far under it.
    auto big = makeMesh(1, 8000);
    auto small = makeMesh(2, 8);
    snap.scene.push_back(makeDraw(0x1111, big, 0xff0000ff));
    snap.scene.push_back(makeDraw(0x2222, small, 0x00ff00ff));
    attachSinks(snap, store);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));

    // The mesh entries live inside the group chunks, so they only
    // exist as deferred entries once those have been parsed — and the
    // declared-but-unbuilt levels must not stop any of it: the scene
    // resolves and assembles from built payloads alone.
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));
    ASSERT_EQ(loaded.scene.size(), 2u);
    for (const auto& d : loaded.scene) {
        ASSERT_TRUE(d.mesh);
        EXPECT_GT(d.mesh->numVertices, 0);
    }

    size_t bigSeen = 0, smallSeen = 0;
    for (const auto& chunk : loaded.deferredChunks) {
        if (!store.meshKeys.count(chunk.key)) {
            continue;
        }
        ASSERT_FALSE(chunk.levels.empty())
            << "every mesh entry carries its ladder";
        const auto& exact = chunk.levels.back();
        EXPECT_EQ(exact.error, 0.0f);
        EXPECT_EQ(exact.key, chunk.key)
            << "the entry fetches the finest built level";
        EXPECT_EQ(exact.size, chunk.size);
        if (chunk.size >= 64 * 1024) {
            ++bigSeen;
            ASSERT_EQ(chunk.levels.size(), 3u);
            // Coarsest first, strictly refining, and declared only:
            // stated error, no key, because nothing has generated
            // their bytes and a chunk with no bytes has no address.
            EXPECT_NEAR(chunk.levels[0].error, 1.0f / 8.0f, 1e-6f);
            EXPECT_NEAR(chunk.levels[1].error, 1.0f / 16.0f, 1e-6f);
            EXPECT_GT(chunk.levels[0].error, chunk.levels[1].error);
            EXPECT_TRUE(chunk.levels[0].key.empty());
            EXPECT_TRUE(chunk.levels[1].key.empty());
        }
        else {
            ++smallSeen;
            EXPECT_EQ(chunk.levels.size(), 1u)
                << "a small mesh declares no ladder";
        }
    }
    EXPECT_EQ(bigSeen, 1u);
    EXPECT_EQ(smallSeen, 1u);
}

/// Phase 5c: a declared level can actually be built from the exact
/// chunk's bytes alone — no shape, no scene, no producer state — and
/// what comes out is an ordinary mesh chunk the entry's own fill
/// parses.
TEST(SceneDump, aDeclaredLevelIsBuiltFromTheExactChunk)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto big = makeGridMesh(1, 37);
    const int srcVerts = big->numVertices;
    snap.scene.push_back(makeDraw(0x1111, big, 0xff0000ff));
    attachSinks(snap, store);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    // Resolve everything but the geometry, which is exactly the moment
    // a level exists to serve: the entry is named, its ladder is
    // declared, and the exact mesh has not arrived.
    ASSERT_TRUE(resolve(loaded, store, store.meshKeys));

    const Render::SceneSnapshot::DeferredChunk* entry = nullptr;
    for (const auto& chunk : loaded.deferredChunks) {
        if (store.meshKeys.count(chunk.key)) {
            entry = &chunk;
        }
    }
    ASSERT_TRUE(entry);
    ASSERT_EQ(entry->levels.size(), 3u) << "the big mesh declares two levels";
    ASSERT_TRUE(entry->fill);
    auto fill = entry->fill;
    const auto& source = store.blobs.at(entry->key);

    std::vector<uint8_t> level0;
    ASSERT_TRUE(Render::generateMeshLevel(source.data(), source.size(), 0,
                                          level0));
    ASSERT_FALSE(level0.empty());
    EXPECT_LT(level0.size(), source.size()) << "a coarser rung weighs less";

    // Determinism, invariant 2's precondition: varying bytes would
    // mint a varying key for the same level of the same source, and
    // no cache — the server's memo, a viewer's IndexedDB — could ever
    // hit.
    std::vector<uint8_t> again;
    ASSERT_TRUE(Render::generateMeshLevel(source.data(), source.size(), 0,
                                          again));
    EXPECT_EQ(level0, again);

    // The level lands through the entry's ordinary fill and stands in
    // for the mesh; the exact chunk arriving later refines it in
    // place. That is the whole consumption story: a level is a chunk.
    ASSERT_TRUE(fill(loaded, level0.data(), level0.size()));
    Render::SceneObjectModel model;
    ASSERT_TRUE(Render::applySceneObjects(loaded, model));
    ASSERT_EQ(loaded.scene.size(), 1u);
    ASSERT_TRUE(loaded.scene[0].mesh);
    EXPECT_FALSE(loaded.scene[0].standIn)
        << "a resident level is geometry, not a box";
    EXPECT_GT(loaded.scene[0].mesh->numVertices, 0);
    EXPECT_LT(loaded.scene[0].mesh->numVertices, srcVerts);

    ASSERT_TRUE(fill(loaded, source.data(), source.size()));
    EXPECT_EQ(loaded.scene[0].mesh->numVertices, srcVerts)
        << "the exact mesh still lands on top of the level";
}

/// Phase 5c: once somebody has built a level, the publish that writes
/// the ladder announces it — a keyed entry where the declaration was,
/// through the sink the serializer already consults per level.
TEST(SceneDump, aBuiltLevelIsAnnouncedAsAKeyedEntry)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto big = makeMesh(1, 8000);
    snap.scene.push_back(makeDraw(0x1111, big, 0xff0000ff));
    attachSinks(snap, store);
    const std::string builtKey(40, 'a');
    std::string askedSource;
    snap.meshBlobs.built = [&](const std::string& source, uint32_t level,
                               uint32_t& size) {
        askedSource = source;
        if (level != 1) {
            return std::string();
        }
        size = 4242;
        return builtKey;
    };

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    const Render::SceneSnapshot::DeferredChunk* entry = nullptr;
    for (const auto& chunk : loaded.deferredChunks) {
        if (store.meshKeys.count(chunk.key)) {
            entry = &chunk;
        }
    }
    ASSERT_TRUE(entry);
    ASSERT_EQ(entry->levels.size(), 3u);
    EXPECT_EQ(askedSource, entry->key)
        << "a level is named by the exact chunk it would be built from";
    EXPECT_TRUE(entry->levels[0].key.empty()) << "level 0 stays declared";
    EXPECT_EQ(entry->levels[1].key, builtKey);
    EXPECT_EQ(entry->levels[1].size, 4242u);
    EXPECT_EQ(entry->levels.back().key, entry->key)
        << "the entry still fetches the finest built level, the exact mesh";
}

/// Phase 5c: the server's work queue end to end, no sockets — accept
/// the request, refuse what could never be generated, build off the
/// connection threads, and remember the answer where the next
/// publish's sink will find it.
TEST(SceneDump, theServerWorkQueueBuildsARequestedLevel)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto big = makeGridMesh(1, 37);
    snap.scene.push_back(makeDraw(0x1111, big, 0xff0000ff));
    attachSinks(snap, store);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    ASSERT_EQ(store.meshKeys.size(), 1u);
    const std::string sourceKey = *store.meshKeys.begin();

    auto& server = Render::SceneStreamServer::instance();
    server.publishBlob(sourceKey,
                       std::vector<uint8_t>(store.blobs.at(sourceKey)));

    EXPECT_FALSE(server.requestLevel(std::string(40, 'b'), 0))
        << "a source the server does not hold is a refusal, not a job";
    EXPECT_FALSE(server.requestLevel(sourceKey, 99))
        << "a level no ladder could declare is a bad request, not work";

    ASSERT_TRUE(server.requestLevel(sourceKey, 0));
    for (int i = 0; i < 500 && server.levelsBuilt() < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(server.levelsBuilt(), 1u) << "the worker never finished";

    uint32_t size = 0;
    const std::string key = server.builtLevel(sourceKey, 0, &size);
    ASSERT_FALSE(key.empty());
    EXPECT_NE(key, sourceKey);
    EXPECT_GT(size, 0u);

    // Idempotent: the same ask again is accepted and costs nothing.
    EXPECT_TRUE(server.requestLevel(sourceKey, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(server.levelsBuilt(), 1u);

    // A level nobody asked for stays unbuilt.
    EXPECT_TRUE(server.builtLevel(sourceKey, 1).empty());
}

/// A line-only mesh — edge chunks are what this stands for. The soup
/// polyline is big enough to pass the line declaration threshold and
/// far under the triangle one.
std::shared_ptr<Render::MeshData> makeLineMesh(int id, int verts)
{
    struct Owned: Render::MeshData
    {
        std::vector<float> pos;
        std::vector<int32_t> lines;
    };
    auto m = std::make_shared<Owned>();
    m->cacheId = uint64_t(id);
    m->numVertices = verts;
    m->pos.resize(size_t(verts) * 3);
    for (size_t i = 0; i < m->pos.size(); ++i) {
        m->pos[i] = float(i) * 0.25f + float(id);
    }
    for (int i = 0; i + 1 < verts; ++i) {
        m->lines.push_back(i);
        m->lines.push_back(i + 1);
    }
    m->positions = m->pos.data();
    m->lineIndices = m->lines.data();
    m->numLineIndices = int(m->lines.size());
    return m;
}

/// The new public chunk API (parseMeshChunk/encodeMeshChunk): what a
/// shape-backed generator writes must read back as the same mesh —
/// tables, flags and all — because the source chunk it validates
/// against went through exactly this pair.
TEST(SceneDump, aMeshChunkRoundTripsThroughThePublicParse)
{
    auto grid = makeGridMesh(1, 9);
    grid->triangleParts = {{0, 120}, {120, int(grid->numTriangleIndices) - 120}};
    grid->nonFlatParts = {{120, int(grid->numTriangleIndices) - 120}};
    grid->hasSolid = 1;
    grid->solidParts = {{0, 120}};
    grid->hasTransparency = true;

    std::vector<uint8_t> chunk;
    ASSERT_TRUE(Render::encodeMeshChunk(*grid, chunk));

    Render::ParsedMeshChunk parsed;
    ASSERT_TRUE(Render::parseMeshChunk(chunk.data(), chunk.size(), parsed));
    EXPECT_EQ(parsed.numVertices, grid->numVertices);
    ASSERT_EQ(parsed.numTriangleIndices, grid->numTriangleIndices);
    EXPECT_EQ(0, std::memcmp(parsed.positions, grid->positions,
                             size_t(grid->numVertices) * 3 * sizeof(float)));
    ASSERT_TRUE(parsed.normals);
    EXPECT_FALSE(parsed.colors);
    EXPECT_FALSE(parsed.texCoords);
    EXPECT_EQ(parsed.triangleParts, grid->triangleParts);
    EXPECT_EQ(parsed.nonFlatParts, grid->nonFlatParts);
    EXPECT_EQ(parsed.hasSolid, grid->hasSolid);
    EXPECT_EQ(parsed.solidParts, grid->solidParts);
    EXPECT_TRUE(parsed.hasTransparency);
    EXPECT_FALSE(parsed.hasOpaqueParts);

    // And the encoding is the publish encoding: the same bytes, the
    // same content key.
    std::vector<uint8_t> again;
    ASSERT_TRUE(Render::encodeMeshChunk(parsed, again));
    EXPECT_EQ(chunk, again);
}

/// A line mesh declares its ladder at a fraction of the triangle
/// threshold: when an object's faces step onto a coarse rung, its
/// edges must have a rung to step onto too, or the exact polylines
/// float off the coarse silhouette — the artifact the shape-backed
/// generator exists to fix.
TEST(SceneDump, aLineMeshDeclaresItsLadderEarlier)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto edges = makeLineMesh(1, 800);   // ~16 KB: over 1 KB, far under 64
    auto few = makeLineMesh(2, 20);      // a few hundred bytes: under both
    auto d1 = makeDraw(0x1111, edges, 0xff0000ff);
    d1.material.type = 1;
    auto d2 = makeDraw(0x2222, few, 0x00ff00ff);
    d2.material.type = 1;
    snap.scene.push_back(d1);
    snap.scene.push_back(d2);
    attachSinks(snap, store);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    size_t declared = 0, bare = 0;
    for (const auto& chunk : loaded.deferredChunks) {
        if (!store.meshKeys.count(chunk.key)) {
            continue;
        }
        ASSERT_FALSE(chunk.levels.empty());
        if (chunk.levels.size() > 1) {
            ++declared;
            EXPECT_GT(chunk.size, 1024u);
            EXPECT_LT(chunk.size, 64u * 1024)
                << "declared by the line rule, not the triangle one";
        }
        else {
            ++bare;
            EXPECT_LT(chunk.size, 1024u);
        }
    }
    EXPECT_EQ(declared, 1u);
    EXPECT_EQ(bare, 1u);
}

/// The shape-backed source seam (MeshSource.h): a registered generator
/// claims its chunk's level jobs ahead of decimation, and removing it
/// hands the next job back.
TEST(SceneDump, aShapeBackedSourceBuildsTheLevelAheadOfDecimation)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto big = makeGridMesh(1, 41);
    snap.scene.push_back(makeDraw(0x1111, big, 0xff0000ff));
    attachSinks(snap, store);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    ASSERT_EQ(store.meshKeys.size(), 1u);
    const std::string sourceKey = *store.meshKeys.begin();

    auto& server = Render::SceneStreamServer::instance();
    server.publishBlob(sourceKey,
                       std::vector<uint8_t>(store.blobs.at(sourceKey)));

    // The "re-tessellation": any distinguishable, well-formed chunk.
    auto marker = makeMesh(7, 5);
    std::vector<uint8_t> markerChunk;
    ASSERT_TRUE(Render::encodeMeshChunk(*marker, markerChunk));
    const std::string markerKey =
        Render::sha1Hex(markerChunk.data(), markerChunk.size());

    int tagStorage = 0;
    const void* tag = &tagStorage;
    auto& reg = Render::MeshSourceRegistry::instance();
    uint32_t seenLevel = 99;
    size_t seenSize = 0;
    reg.add(tag, [&](uint32_t level, const void* src, size_t n,
                     std::vector<uint8_t>& out) {
        (void)src;
        seenLevel = level;
        seenSize = n;
        out = markerChunk;
        return true;
    });
    // Association before registration is dropped; after, it sticks.
    const void* unregistered = &markerChunk;
    reg.associate(sourceKey, unregistered);
    reg.associate(sourceKey, tag);

    size_t base = server.levelsBuilt();
    ASSERT_TRUE(server.requestLevel(sourceKey, 0));
    for (int i = 0; i < 500 && server.levelsBuilt() < base + 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(server.levelsBuilt(), base + 1) << "the worker never finished";
    uint32_t size = 0;
    EXPECT_EQ(server.builtLevel(sourceKey, 0, &size), markerKey)
        << "the registered source built the level, not decimation";
    EXPECT_EQ(seenLevel, 0u);
    EXPECT_EQ(seenSize, store.blobs.at(sourceKey).size())
        << "the generator saw the exact chunk's bytes";

    // Removal: the next level of the same source decimates instead.
    reg.remove(tag);
    base = server.levelsBuilt();
    ASSERT_TRUE(server.requestLevel(sourceKey, 1));
    for (int i = 0; i < 500 && server.levelsBuilt() < base + 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(server.levelsBuilt(), base + 1);
    const std::string decimated = server.builtLevel(sourceKey, 1, &size);
    ASSERT_FALSE(decimated.empty());
    EXPECT_NE(decimated, markerKey);
}

/// Coarse-first publish (§7): a mesh the producer deliberately
/// tessellated at a ladder rung is written at its own error, with only
/// strictly coarser rungs declared below it and the exact mesh
/// declared *unbuilt* above it — until someone builds the exact rung,
/// whereupon its key is announced like any generated level and the
/// entry's fetch identity (finest built) moves onto it.
TEST(SceneDump, aCoarsePublishedMeshDeclaresTheExactRung)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto big = makeGridMesh(1, 37);
    big->levelError = 1.0f / 16.0f;   // published mesh = grid level 1
    snap.scene.push_back(makeDraw(0x1111, big, 0xff0000ff));
    attachSinks(snap, store);
    const std::string exactKey(40, 'e');
    bool exactBuilt = false;
    uint32_t askedLevel = 0;
    snap.meshBlobs.built = [&](const std::string&, uint32_t level,
                               uint32_t& size) {
        askedLevel = level;
        if (!exactBuilt || level != Render::kExactMeshLevel) {
            return std::string();
        }
        size = 777777;
        return exactKey;
    };

    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), loaded));
    Render::SceneObjectModel model;
    ASSERT_TRUE(resolveInto(loaded, store, model));

    ASSERT_EQ(store.meshKeys.size(), 1u);
    const std::string published = *store.meshKeys.begin();
    const Render::SceneSnapshot::DeferredChunk* entry = nullptr;
    for (const auto& chunk : loaded.deferredChunks) {
        if (chunk.key == published) {
            entry = &chunk;
        }
    }
    ASSERT_TRUE(entry);
    // [coarser declared, the published mesh at its error, exact unbuilt]
    ASSERT_EQ(entry->levels.size(), 3u);
    EXPECT_NEAR(entry->levels[0].error, 1.0f / 8.0f, 1e-6f);
    EXPECT_TRUE(entry->levels[0].key.empty());
    EXPECT_NEAR(entry->levels[1].error, 1.0f / 16.0f, 1e-6f);
    EXPECT_EQ(entry->levels[1].key, published)
        << "the published mesh sits on the ladder at its own error";
    EXPECT_EQ(entry->levels.back().error, 0.0f);
    EXPECT_TRUE(entry->levels.back().key.empty())
        << "the exact mesh is declared, not built";
    EXPECT_EQ(entry->key, published)
        << "the finest built level is the published coarse mesh";
    EXPECT_EQ(askedLevel, Render::kExactMeshLevel)
        << "the exact slot consults built() under its sentinel";

    // Someone builds the exact rung: the next publish announces it and
    // the entry's fetch identity moves onto it.
    exactBuilt = true;
    payload.clear();
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    Render::SceneSnapshot again;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), again));
    // Mesh entries exist only once the group chunks resolved; the
    // exact chunk itself is missing from the store (nothing built it
    // for real) — held back, exactly like a viewer that has not
    // fetched it yet.
    ASSERT_TRUE(resolve(again, store, {exactKey}));
    entry = nullptr;
    for (const auto& chunk : again.deferredChunks) {
        if (!chunk.levels.empty() && chunk.levels[1].key == published) {
            entry = &chunk;
        }
    }
    ASSERT_TRUE(entry);
    ASSERT_EQ(entry->levels.size(), 3u);
    EXPECT_EQ(entry->levels.back().key, exactKey);
    EXPECT_EQ(entry->levels.back().size, 777777u);
    EXPECT_EQ(entry->key, exactKey)
        << "the exact rung, once built, is the finest built level";
}

/// The canonical-key rule (MeshSource.h): a generated level's key names
/// the same source non-canonically, so a request reaching the server
/// through any built rung dedups onto the one job identity — the
/// published key, which is the one the serializer's announcement
/// lookup uses.
TEST(SceneDump, aRequestThroughAGeneratedRungNamesTheSameJob)
{
    BlobStore store;
    Render::SceneSnapshot snap;
    auto big = makeGridMesh(1, 43);
    snap.scene.push_back(makeDraw(0x1111, big, 0xff0000ff));
    attachSinks(snap, store);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));
    ASSERT_EQ(store.meshKeys.size(), 1u);
    const std::string sourceKey = *store.meshKeys.begin();

    auto& server = Render::SceneStreamServer::instance();
    server.publishBlob(sourceKey,
                       std::vector<uint8_t>(store.blobs.at(sourceKey)));

    auto marker = makeMesh(9, 6);
    std::vector<uint8_t> markerChunk;
    ASSERT_TRUE(Render::encodeMeshChunk(*marker, markerChunk));
    const std::string markerKey =
        Render::sha1Hex(markerChunk.data(), markerChunk.size());

    int tagStorage = 0;
    const void* tag = &tagStorage;
    auto& reg = Render::MeshSourceRegistry::instance();
    reg.add(tag, [&](uint32_t, const void*, size_t,
                     std::vector<uint8_t>& out) {
        out = markerChunk;
        return true;
    });
    reg.associate(sourceKey, tag);

    size_t base = server.levelsBuilt();
    ASSERT_TRUE(server.requestLevel(sourceKey, 0));
    for (int i = 0; i < 500 && server.levelsBuilt() < base + 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(server.levelsBuilt(), base + 1);

    // The generated chunk's key now names the source too, back to the
    // canonical published one.
    EXPECT_EQ(reg.canonical(markerKey), sourceKey);
    EXPECT_EQ(reg.canonical(sourceKey), sourceKey);

    // A request through the generated rung is the same job: accepted,
    // memoized, nothing rebuilt.
    ASSERT_TRUE(server.requestLevel(markerKey, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(server.levelsBuilt(), base + 1)
        << "the sibling key deduped onto the canonical job";
    uint32_t size = 0;
    EXPECT_EQ(server.builtLevel(sourceKey, 0, &size), markerKey)
        << "the answer lives under the canonical key, where the "
           "announcement looks";

    reg.remove(tag);
}

/// A publish re-parses its manifests into fresh, empty payload objects
/// under the same content keys, and until those bytes are re-read every
/// re-described object used to drop to its box — the "all meshes flash
/// to boxes" artifact. The model bridges the gap with the copy still in
/// hand (SceneObjectModel::lastGood): same content id, same bytes.
TEST(SceneDump, aReparsedMeshBridgesFromTheLastLiveCopy)
{
    BlobStore store;
    Render::SceneSnapshot snap = makeScene();
    attachSinks(snap, store);

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    snap.manifestVersion = 1;
    snap.objectEntries = &entries;
    std::vector<uint8_t> full;
    ASSERT_TRUE(Render::saveSceneSnapshot(full, snap));

    Render::SceneObjectModel model;
    Render::SceneSnapshot loaded;
    ASSERT_TRUE(Render::loadSceneSnapshot(full.data(), full.size(), loaded));
    ASSERT_TRUE(resolveInto(loaded, store, model));
    expectScene(loaded);

    // Repaint one object: same meshes, a re-keyed group manifest.
    for (auto& d : snap.scene) {
        if (d.objectKey == 0x2222) {
            d.material.diffuse = 0x654321ff;
        }
    }
    std::vector<Render::SceneSnapshot::ObjectEntry> entries2;
    snap.baseObjects = entries;
    snap.baseVersion = 1;
    snap.manifestVersion = 2;
    snap.objectEntries = &entries2;
    std::vector<uint8_t> delta;
    ASSERT_TRUE(Render::saveSceneSnapshot(delta, snap));

    Render::SceneSnapshot loaded2;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(delta.data(), delta.size(), loaded2));
    // Resolve the manifest layer only: the geometry entries stay
    // pending, exactly as they do in a live viewer for the length of a
    // re-read of bytes it was already drawing.
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto& entry : loaded2.deferredChunks) {
            if (!entry.fill || entry.release) {
                continue;
            }
            auto fill = entry.fill;
            entry.fill = nullptr;
            auto it = store.blobs.find(entry.key);
            ASSERT_NE(it, store.blobs.end());
            ASSERT_TRUE(fill(loaded2, it->second.data(), it->second.size()));
            progress = true;
        }
    }
    ASSERT_TRUE(loaded2.finalize);
    loaded2.finalize(loaded2);
    ASSERT_TRUE(Render::applySceneObjects(loaded2, model));

    size_t seen = 0;
    for (const auto& d : loaded2.scene) {
        if (d.objectKey != 0x2222) {
            continue;
        }
        ++seen;
        EXPECT_FALSE(d.standIn)
            << "a re-described object must not drop to its box while "
               "identical bytes are re-read";
        ASSERT_TRUE(d.mesh);
        EXPECT_GT(d.mesh->numVertices, 0u) << "the bridged copy is live";
    }
    EXPECT_GT(seen, 0u);
}

// A publish that re-describes an object replaces its ladder with a
// freshly parsed one — empty stores, residentMask zero. The geometry
// must ride across by content key (carryResidentRungs), or an
// announcement chain refetches the whole resident set every commit.
TEST(SceneDump, residentRungsSurviveAReparse)
{
    auto snap = makeScene();
    BlobStore store;
    attachSinks(snap, store);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(Render::saveSceneSnapshot(payload, snap));

    Render::SceneSnapshot a;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), a));
    ASSERT_TRUE(resolve(a, store));
    // Mark the geometry ladders resident the way the viewer's fill
    // does: the finest built rung of every release-capable entry.
    size_t ladders = 0;
    for (auto& e : a.deferredChunks) {
        if (!e.release || !e.levelMeshes) {
            continue;
        }
        const int rung = Render::finestBuiltRung(e);
        ASSERT_GE(rung, 0);
        e.residentMask = uint16_t(1u << rung);
        ++ladders;
    }
    ASSERT_GT(ladders, 0u);

    // The re-parse: the same publish read fresh, nothing resolved —
    // the mesh bytes never arrive a second time.
    Render::SceneSnapshot b;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), b));
    EXPECT_EQ(Render::carryResidentRungs(b, a), ladders);
    for (const auto& e : b.deferredChunks) {
        if (!e.release || !e.levelMeshes) {
            continue;
        }
        EXPECT_NE(e.residentMask, 0u)
            << "ladder " << e.key << " lost residency across the re-parse";
        auto mesh = e.levelMeshes->at(e.key);
        ASSERT_TRUE(mesh) << "ladder " << e.key << " has no adopted rung";
        EXPECT_GT(mesh->numVertices, 0)
            << "adopted rung of " << e.key << " is empty";
        // The adopted content landed in the identity object the fresh
        // parse's draws alias, so the scene renders it immediately.
        EXPECT_EQ(e.levelMeshes->identity().get(), mesh.get());
    }
    // An unrelated old scene offers nothing: adoption is keyed by
    // content, not position.
    Render::SceneSnapshot c;
    ASSERT_TRUE(
        Render::loadSceneSnapshot(payload.data(), payload.size(), c));
    Render::SceneSnapshot empty;
    EXPECT_EQ(Render::carryResidentRungs(c, empty), 0u);
}

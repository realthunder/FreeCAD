// SPDX-License-Identifier: LGPL-2.1-or-later

/// Round-trip and republish tests for the scene-stream serializer
/// (src/Gui/Renderer/SceneDump.cpp, docs/SceneStreaming.md). Both
/// layouts are covered: the monolithic one a bundled .fcsd capture
/// must stay, and the manifest one the live stream uses.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <Gui/Renderer/SceneDump.h>

namespace
{

/// Stands in for the publisher's blob store: everything the writer
/// hands over, addressed by content key, and a record of which keys a
/// given publish actually sent bytes for.
struct BlobStore
{
    std::map<std::string, std::vector<uint8_t>> blobs;
    std::set<std::string> sentThisPublish;

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
        if (store.has(key)) {
            store.sentThisPublish.insert(key);
            return;
        }
        store.take(key, std::move(chunk));
    };
}

/// Resolve everything a loaded manifest snapshot named, out of \a
/// store, the way the viewer does — in rounds, because a chunk's parse
/// names further chunks — then run the pass that puts the staged draws
/// and materials where a backend expects them.
::testing::AssertionResult resolve(Render::SceneSnapshot& snap,
                                   const BlobStore& store)
{
    for (int round = 0; round < 16; ++round) {
        bool progress = false;
        for (size_t i = 0; i < snap.deferredChunks.size(); ++i) {
            auto fill = snap.deferredChunks[i].fill;
            if (!fill) {
                continue;
            }
            auto it = store.blobs.find(snap.deferredChunks[i].key);
            if (it == store.blobs.end()) {
                return ::testing::AssertionFailure()
                    << "chunk " << snap.deferredChunks[i].key
                    << " was never published";
            }
            snap.deferredChunks[i].fill = nullptr;
            if (!fill(snap, it->second.data(), it->second.size())) {
                return ::testing::AssertionFailure()
                    << "chunk " << it->first << " failed to parse";
            }
            progress = true;
        }
        if (!progress) {
            break;
        }
    }
    for (const auto& tex : snap.deferredTextures) {
        if (!tex || !tex->deferred) {
            continue;
        }
        auto it = store.blobs.find(tex->contentKey);
        if (it == store.blobs.end()) {
            return ::testing::AssertionFailure()
                << "texture " << tex->contentKey << " was never published";
        }
        tex->pixels = it->second;
        tex->deferred = false;
    }
    for (const auto& chunk : snap.deferredChunks) {
        if (chunk.fill) {
            return ::testing::AssertionFailure()
                << "chunk " << chunk.key << " left outstanding";
        }
    }
    if (snap.finalize) {
        snap.finalize(snap);
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
    ASSERT_TRUE(resolve(loaded, store));
    expectScene(loaded);
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

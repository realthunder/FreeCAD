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

    std::vector<uint8_t> spliced;
    ASSERT_TRUE(Render::spliceObjectDelta(full2, spans, 1, changed, removed,
                                          spliced));
    EXPECT_EQ(spliced, native)
        << "a spliced delta must be byte-identical to a written one";
    EXPECT_LT(spliced.size(), full2.size());

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
/// §6), so the feed has to state what is not in hand yet: a draw whose
/// mesh chunk has not landed points at an empty MeshData, which the
/// backend would read past the end of. It is left out and picked up on
/// a later pass — the bottom rung of the fidelity ladder, until there
/// is a box to draw in its place.
TEST(SceneDump, aDrawWaitsForTheMeshItNames)
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
    EXPECT_TRUE(loaded.scene.empty())
        << "a draw is not submitted before the mesh it names is in hand";
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

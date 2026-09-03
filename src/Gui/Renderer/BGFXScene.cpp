/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include "BGFXRendererP.h"

void BGFXRenderer::Private::makeSnapshot(Render::SceneSnapshot &snap,
                  const void *viewMatrix,
                  const void *projMatrix,
                  uint16_t width,
                  uint16_t height,
                  uint32_t clearColor)
{
    // A snapshot is what the dump and serve tiers replay, and the
    // viewer draws every draw it is handed: it has no override table,
    // no interest list and no per-object resolution of its own. So the
    // resolution happens HERE (docs/CoinRetirement.md 5.16), against
    // the main view's style -- the same BGFXStyleState the frame loop
    // latches onto a view, asked by the same styleAdmits, so there is
    // no second copy of rules this subtle. What crosses the wire is
    // then what this view draws: exactly ONE copy of an overridden
    // object, in the mode its override resolves to, and none of the
    // buckets a Class-A style removes.
    //
    // Until 5.16 the additively captured copies were merely dropped
    // and a served scene showed the mode the normal flow traversed
    // (5.13) -- an override was simply not applied remotely, and a
    // Class-A style arrived as the superset child it was captured
    // from. That behaviour survives exactly where it was correct: with
    // no capture, no override table and no view style nothing is
    // tagged and nothing filters, and this is the plain assignment it
    // always was.
    //
    // A unified canvas resolves per CELL, which one snapshot cannot
    // express; the main view's style is what a served scene has always
    // meant, and a canvas cell's own style is not carried here.
    BGFXStyleState mainStyle;
    const bool resolving = captureInterest
            || (mainStyleOverrides && !mainStyleOverrides->entries.empty())
            || mainStyleMask != Render::StyleAsIs;
    if (resolving) {
        mainStyle.drawStyleMask = mainStyleMask;
        mainStyle.drawStyleName = mainStyleName;
        mainStyle.styleFromSuperset = mainFromSuperset;
        // The style as an additive mode, latched exactly as
        // BGFXFrame.cpp latches it: both zero unless the interest list
        // actually carries the mode.
        if (mainFromSuperset && captureInterest && mainStyleMode) {
            if (const uint16_t bit = captureInterest->bitOf(mainStyleMode)) {
                mainStyle.drawStyleMode = mainStyleMode;
                mainStyle.drawStyleModeBit = bit;
            }
        }
        if (mainStyleOverrides && !mainStyleOverrides->entries.empty()) {
            // A cache of this snapshot's own: it lives and dies with
            // the local state, so it needs no version invalidation.
            mainStyle.ovCache = &mainStyle.subOvCaches[0];
            mainStyle.ovTable = mainStyleOverrides;
            mainStyle.ovInfo = &objectInfo;
        }
        mainStyle.ovInterest = captureInterest;
    }
    auto copyFeed = [&](const Render::DrawCallList &src) {
        if (!resolving)
            return src;
        Render::DrawCallList out;
        out.reserve(src.size());
        for (const auto &d : src) {
            if (mainStyle.styleAdmits(d))
                out.push_back(d);
        }
        return out;
    };

    snap.scene = copyFeed(scene);
    snap.objectInfo = &objectInfo;
    snap.objectMeta = &objectMeta;
    snap.selections.reserve(selections.size());
    for (const auto &sel : selections)
        snap.selections.emplace_back(sel.first, copyFeed(sel.second));
    snap.highlight = copyFeed(highlight);
    snap.highlightWholeOnTop = hlWholeOnTop;
    for (const auto &ov : overlays) {
        Render::SceneSnapshot::Overlay sov;
        sov.id = ov.first;
        sov.anchor = ov.second.anchor;
        sov.draws = copyFeed(ov.second.draws);
        snap.overlays.push_back(std::move(sov));
    }
    snap.background = background;
    snap.hlconfig = hlconfig;
    snap.secconf = secconf;
    snap.aoconf = aoconf;
    snap.pbrconf = pbrconf;
    snap.bumpconf = bumpconf;
    snap.lightconf = lightconf;
    snap.viewlightconf = viewlightconf;
    snap.volconf = volconf;
    snap.waterconf = waterconf;
    snap.bloomconf = bloomconf;
    snap.outconf = outconf;
    snap.debugconf = debugconf;
    snap.usershaderconf = usershaderconf;
#ifndef FC_RENDERER_STANDALONE
    // Server-side compile hook (docs/RenderDebug.md §6.3): the
    // serializer asks for the viewer-tier binaries of every
    // unique user shader it writes. Ready variants attach to
    // the snapshot; pending compiles republish when they
    // finish (dirtyChanged above).
    snap.shipShader = [](Render::UserShader &s) {
        _BGFXLib.shipUserShader(s);
    };
    // v24: assemble the volume-splice variants exactly as the
    // frame loop would (shared collectMediumUsers keeps the
    // sources byte-identical) so the shader table carries their
    // viewer binaries; the viewer adopts them by source match.
    {
        constexpr int kSlots = BGFXView::kMediumSlots;
        std::shared_ptr<const Render::UserShader> fu[kSlots];
        std::shared_ptr<const Render::UserShader> cu[kSlots];
        collectMediumUsers(snap.scene, fu, cu, kSlots);
        for (const char *body :
                 {"fc_volume_fs.sh", "fc_volume_ext_fs.sh",
                  "fc_refl_media_fs.sh"}) {
            if (auto s = assembleMediumVariant(body, fu, cu,
                                               kSlots))
                snap.usershaderconf.splices.push_back(*s);
        }
    }
#endif
    snap.preselconf = preselconf;
    snap.selconf = selconf;
    snap.autozoomScale = autozoomScale;
    snap.effectResolution = _BGFXLib.effectResolution;
    snap.ssaoResolution = _BGFXLib.ssaoResolution;
    snap.hatch = hatchTex;
    std::memcpy(snap.viewMatrix, viewMatrix, sizeof(snap.viewMatrix));
    std::memcpy(snap.projMatrix, projMatrix, sizeof(snap.projMatrix));
    snap.width = width;
    snap.height = height;
    snap.clearColor = clearColor;
}

uint64_t BGFXRenderer::Private::sceneFingerprint() const
{
    uint64_t hash = 1469598103934665603ull;   // FNV-1a
    auto mix = [&hash](uint64_t value) {
        hash = (hash ^ value) * 1099511628211ull;
    };
    auto mixDraws = [&mix](const Render::DrawCallList &draws) {
        mix(draws.size());
        for (const auto &draw : draws) {
            mix(draw.objectKey);
            mix(uint64_t(draw.material.diffuse));
            mix(uint64_t(draw.partIndex));
            if (draw.mesh) {
                mix(draw.mesh->cacheId);
                mix(uint64_t(draw.mesh->generation));
                mix(uint64_t(draw.mesh->numVertices));
            }
        }
    };
    mixDraws(scene);
    mixDraws(highlight);
    for (const auto &sel : selections) {
        mix(uint64_t(sel.first));
        mixDraws(sel.second);
    }
    for (const auto &ov : overlays) {
        mix(uint64_t(ov.first));
        mixDraws(ov.second.draws);
    }
    return hash;
}

void BGFXRenderer::Private::maybeDumpScene(const void *viewMatrix,
                    const void *projMatrix,
                    uint16_t width,
                    uint16_t height,
                    uint32_t clearColor,
                    bool dirtyChanged)
{
    static const char *dumpPath = getenv("FC_BGFX_DUMP_SCENE");
    static const char *dumpDelay = getenv("FC_BGFX_DUMP_SCENE_DELAY");
    static const bool dumpSel = getenv("FC_BGFX_DUMP_SCENE_SEL") != nullptr;
    static const char *dumpSettle = getenv("FC_BGFX_DUMP_SCENE_SETTLE");
    static const int settleFrames = dumpSettle ? atoi(dumpSettle) : 0;

    // What re-arms the wait is the scene's *content* changing, not
    // the dirty flag: the per-frame config push (SoFCRenderer's ~20
    // setters) leaves something dirty on essentially every frame, so
    // "nothing is dirty" is a state a live viewer never reaches.
    // A fingerprint of what the draws are does reach it.
    (void)dirtyChanged;
    const uint64_t print = sceneFingerprint();
    if (print != dumpFingerprint) {
        dumpFingerprint = print;
        dumpQuietFrames = 0;
    }
    else
        ++dumpQuietFrames;

    if (dumpPath && *dumpPath && !sceneDumped
            && !(scene.empty() && overlays.empty())
            && ++dumpFrames > (dumpDelay ? atoi(dumpDelay) : 0)
            && (!dumpSel || !selections.empty())
            && dumpQuietFrames >= settleFrames) {
        sceneDumped = true;
        Render::SceneSnapshot snap;
        makeSnapshot(snap, viewMatrix, projMatrix, width, height,
                     clearColor);
        // The SNAPSHOT's count, not the feed's: since 5.16 the two
        // differ by whatever the view's style resolution removed, and
        // the number worth reading is what actually goes on the wire.
        fprintf(stderr,
                "bgfx: scene snapshot (%zu of %zu draws) -> %s: %s\n",
                snap.scene.size(), scene.size(), dumpPath,
                Render::saveSceneSnapshot(dumpPath, snap)
                    ? "ok" : "FAILED");
    }
}

#ifndef FC_RENDERER_STANDALONE
void BGFXRenderer::Private::publishScene(const void *viewMatrix,
                  const void *projMatrix,
                  uint16_t width,
                  uint16_t height,
                  uint32_t clearColor,
                  bool dirtyChanged)
{
    // FC_BGFX_SERVE_SCENE=<port>: publish the feeds to the
    // standalone/wasm viewer over the snapshot HTTP server whenever
    // they change (SceneServer.h).
    static const char *servePort = getenv("FC_BGFX_SERVE_SCENE");
    auto &server = Render::SceneStreamServer::instance();
    if (servePort && *servePort) {
        static bool serveFailed = false;
        if (!server.running() && !serveFailed && !serveStarted) {
            serveStarted = true;
            if (server.start(atoi(servePort)))
                fprintf(stderr, "bgfx: scene server on port %s\n",
                        servePort);
            else {
                serveFailed = true;
                fprintf(stderr,
                        "bgfx: scene server FAILED on port %s\n",
                        servePort);
            }
        }
    }
    // The environment variable is one way to start the server, not
    // the definition of serving: Gui.serveDocument(doc, port) starts
    // it directly (docs/HeadlessServe.md §4, stage 2d). What decides
    // whether to publish is whether anything is listening.
    if (server.running()) {
        // Publish whenever there is anything to show, not just a non-empty
        // main scene: while editing the only object (e.g. a Sketcher sketch
        // with no other geometry) the whole edit graph lives in the editing
        // overlay and the main scene is empty — the datums/leaders must
        // still stream.
        uint64_t publishVersion = 0;
        // A level-generation job finishing (§7, phase 5c) is a
        // publish trigger of its own: nothing in the feeds moved,
        // but the manifest a fresh publish writes is what carries
        // the new key to every viewer.
        const size_t levelsBuilt =
            server.running() ? server.levelsBuilt(publishGroup) : 0;
        if (server.running()
                && (dirtyChanged || !scenePublished
                    || levelsBuilt != publishedLevelsBuilt)
                && !(scene.empty() && overlays.empty())
                // Claims the stream on the first publish and states
                // which publish this is; 0 means another renderer
                // owns it and this one stays off the wire.
                && (publishVersion =
                        server.beginPublish(this, publishGroup)) != 0) {
            scenePublished = true;
            Render::SceneSnapshot snap;
            makeSnapshot(snap, viewMatrix, projMatrix, width,
                         height, clearColor);
            snap.manifestVersion = publishVersion;
            snap.sessionId = server.sessionId(publishGroup);
            // Texture pixels leave the stream and are served out of
            // band instead (SceneDump.h, v26): a republish fires on
            // every feed change, down to a selection pick, and the
            // embedded images do not change with it.
            snap.textureBlobs = [this, &server](
                    const std::string &key,
                    std::vector<uint8_t> &&pixels) {
                server.publishBlob(key, std::move(pixels),
                                   publishGroup);
            };
            // Mesh chunks likewise (v28), but keyed through a memo
            // on cacheId: hashing every mesh on every publish would
            // just move the cost from the network to the CPU. The
            // memo spans two publishes — an entry has to be
            // reachable while the blob it names is still retained,
            // and retainBlob() failing is what expires it.
            meshKeysPrev = std::move(meshKeys);
            meshKeys.clear();
            snap.meshBlobs.reuse = [this, &server](uint64_t cacheId,
                                                   uint32_t &size) {
                auto it = meshKeys.find(cacheId);
                if (it != meshKeys.end()) {
                    size = it->second.second;
                    return it->second.first;
                }
                it = meshKeysPrev.find(cacheId);
                if (it == meshKeysPrev.end())
                    return std::string();
                if (!server.retainBlob(it->second.first, &size,
                                       publishGroup))
                    return std::string();
                meshKeys[cacheId] = {it->second.first, size};
                return it->second.first;
            };
            snap.meshBlobs.store = [this, &server](
                    uint64_t cacheId, const std::string &key,
                    std::vector<uint8_t> &&chunk) {
                meshKeys[cacheId] = {key, uint32_t(chunk.size())};
                server.publishBlob(key, std::move(chunk), publishGroup);
            };
            // Generated levels (§7, phase 5c): the server's work
            // queue built them, the serializer asks per declared
            // level, and writing the key is the announcement.
            snap.meshBlobs.built = [this, &server](
                    const std::string &source, uint32_t level,
                    uint32_t &size) {
                return server.builtLevel(source, level, &size,
                                         publishGroup);
            };
            // v33: the manifest layout. Setting this is what
            // selects it — the group manifests, the materials and
            // the user shaders all become content-keyed chunks,
            // and an object nothing touched then costs its root
            // entry alone. Each is rebuilt and hashed every
            // publish (it has to be built to know it is
            // unchanged), but its bytes only leave the process
            // when the publisher does not already hold them.
            snap.chunkBlobs = [this, &server](
                    const std::string &key,
                    std::vector<uint8_t> &&bytes) {
                if (!server.retainBlob(key, nullptr, publishGroup))
                    server.publishBlob(key, std::move(bytes),
                                       publishGroup);
            };
            // The root is always written with a complete object
            // list. What a viewer that is behind gets instead is
            // derived from these bytes by the server, which is the
            // only party that knows what any given viewer is
            // missing — so this runs once however many viewers are
            // connected, and however far behind they are.
            std::vector<Render::SceneSnapshot::ObjectEntry> entries;
            Render::SceneSnapshot::RootSpans spans;
            snap.objectEntries = &entries;
            snap.rootSpans = &spans;
            Render::SceneStreamServer::ScenePublish pub;
            pub.version = publishVersion;
            if (Render::saveSceneSnapshot(pub.payload, snap)) {
                pub.spans = spans;
                pub.objects = entries.size();
                Render::diffObjectLists(publishedObjects, entries,
                                        pub.changed, pub.removed);
                publishedObjects = std::move(entries);
                server.publish(std::move(pub), publishGroup);
                // This publish consulted builtLevel() for every
                // ladder it wrote, so every level finished by the
                // count taken above is now announced. A job that
                // finishes mid-serialization moves the counter
                // past this mark and triggers the next round.
                publishedLevelsBuilt = levelsBuilt;
                // Tell the level-source registry which published
                // key each shape-backed mesh landed under, so the
                // server's level worker can re-tessellate instead
                // of decimate (MeshSource.h). Idempotent, and a
                // tag nobody registered is skipped inside.
                auto &sources = Render::MeshSourceRegistry::instance();
                for (const auto &d : snap.scene) {
                    if (!d.mesh || !d.mesh->sourceTag)
                        continue;
                    auto it = meshKeys.find(d.mesh->cacheId);
                    if (it != meshKeys.end())
                        sources.associate(it->second.first,
                                          d.mesh->sourceTag);
                }
            }
        }
    }
}

bool BGFXRenderer::Private::publishNoDraw(const QColor &col,
                   const void *viewMatrix,
                   const void *projMatrix,
                   uint16_t width,
                   uint16_t height)
{
    if (_deinit)
        return false;

    // Same accounting render() does, and for the same reason: the
    // pending data is consumed by this publish, so needsRedraw()
    // reports false until more arrives. A finished async user-shader
    // compile counts as a change, because the snapshot carries the
    // freshly compiled viewer binaries.
    bool dirtyChanged = sceneDirty;
    if (userShaderGen != _BGFXLib.userCompileGeneration)
        dirtyChanged = true;
    feedDirty = false;
    sceneDirty = false;
    userShaderGen = _BGFXLib.userCompileGeneration;

    const uint32_t clearColor = (uint32_t(col.red()) << 24)
        | (uint32_t(col.green()) << 16)
        | (uint32_t(col.blue()) << 8)
        | 0xff;
    // The scene dump is the diffable form of a publish, so it has
    // to be reachable without a frame — it is how a view-less
    // source is checked against a real viewer.
    maybeDumpScene(viewMatrix, projMatrix, width, height, clearColor,
                   dirtyChanged);
    publishScene(viewMatrix, projMatrix, width, height, clearColor,
                 dirtyChanged);
    return true;
}

#endif // !FC_RENDERER_STANDALONE

auto BGFXRenderer::Private::meshContent(const Render::MeshData &mesh)
    -> const MeshContent &
{
    auto res = meshContents.try_emplace(mesh.cacheId);
    MeshContent &c = res.first->second;
    // The level ladder refines meshes in place under one cache id,
    // bumping MeshData::generation (the same guard getMesh uses) —
    // a memo from before the refine describes the coarse arrays.
    if (res.second || c.generation != mesh.generation) {
        c.geomHash = computeGeomKey(mesh).hash;
        c.colorHash = mesh.colors
            ? fnv1a64(0xcbf29ce484222325ull, mesh.colors,
                      size_t(mesh.numVertices) * 4)
            : 0;
        c.numVertices = mesh.numVertices;
        c.numTri = mesh.numTriangleIndices;
        c.generation = mesh.generation;
    }
    c.stamp = meshContentStamp;
    return c;
}

bool BGFXRenderer::Private::instancableDraw(const Render::DrawCall &d)
{
    const Render::Material &m = d.material;
    return d.mesh && d.mesh->numTriangleIndices > 0
        && m.type == Render::Material::Triangle
        && !m.ontop
        && m.autozoom.empty()
        && m.numclipplanes == 0
        && !m.water
        && !m.glass
        && !m.cloud
        && !m.fire
        && !m.usershader
        && !m.faceoutline
        // Per-face-material draws keep per-draw submits: the instanced
        // path forces the flag off (PartGui flattens such shapes out of
        // TShape sharing anyway, so groups of two never form).
        && !m.perfacematerial;
}

void BGFXRenderer::Private::buildDecorReach()
{
    // Resolved from the draw list rather than read off each fill's
    // material because the fill cannot know it: SoDrawStyle's line width
    // lives inside the wireframe separator, which ViewProviderExt adds
    // AFTER the faces, so every fill reports linewidth 1 however thick
    // its edges are.
    //
    // On-top and highlight draws are excluded: they do not depth-test
    // against the fill, so they are not what the fill has to clear, and
    // a preselection thickening would otherwise shove every fill back.
    decorReach.clear();
    for (const auto &d : scene) {
        if (d.material.ontop || !d.objectKey)
            continue;
        float reach = 0.0f;
        if (d.material.type == Render::Material::Line)
            reach = 0.5f * std::max(1.0f, d.material.linewidth) + 0.5f;
        else if (d.material.type == Render::Material::Point)
            reach = 0.5f * std::max(1.0f, d.material.pointsize);
        else
            continue;
        auto &slot = decorReach[d.objectKey];
        slot = std::max(slot, reach);
    }
}

void BGFXRenderer::Private::buildInstanceGroups()
{
    buildDecorReach();
    instGroups.clear();
    instGroupOf.assign(scene.size(), -1);
    ++meshContentStamp;

    // Group key: the geometry content identity plus every material
    // field the instanced submit consumes besides the diffuse color
    // (which rides the instance data). Byte-compared, so the struct
    // is zeroed first (padding).
    struct InstKey {
        uint64_t geomHash, colorHash;
        uint64_t texId, bumpId, emissiveId, occlusionId, mrId;
        /// The per-face palette rides a uniform and a sampler, so
        /// instances in one batch must share the whole thing -- the
        /// images, the tile size and which layer is being read.
        uint64_t facePalette;
        float facetexscale;
        int facetexlayer;
        float texmatrix[16];
        int numVertices, numTri;
        int start, count, part;
        /// The decoration reach of the OBJECT the draw belongs to.
        /// Not a material field and not shared by identical geometry:
        /// two boxes of one size whose edges differ in width resolve
        /// different reaches, and one instanced submit can only bind one
        /// polygon offset (the prototype's). Left out of the key, the
        /// thin-edged member's fill would be shoved back with the thick
        /// one -- or, prototype the other way round, the thick-edged
        /// member would lose the pull-back that keeps its edge from
        /// being half-eaten by its own face, which is the whole defect
        /// 34461d03b7 fixed. Keying on it splits the batch instead.
        float reach;
        float shininess, pofactor, pounits, metallic, roughness;
        float finishpitch, finishdepth, finishangle;
        uint32_t emissive, specular, ambient, texBlend;
        uint8_t texModel, texWrapS, texWrapT, texComps;
        uint8_t depthfunc, shadowstyle, finish;
        uint8_t depthtest, depthwrite, pervertexcolor, lighting,
            twoside, culling, ccw, polygonoffset, solidshape,
            transparent;
    };
    struct KeyLess {
        bool operator()(const InstKey &a, const InstKey &b) const
        { return std::memcmp(&a, &b, sizeof(InstKey)) < 0; }
    };
    std::map<InstKey, int, KeyLess> groups;
    for (int i = 0; i < int(scene.size()); ++i) {
        const auto &d = scene[i];
        if (!instancableDraw(d))
            continue;
        const Render::Material &m = d.material;
        const MeshContent &c = meshContent(*d.mesh);
        InstKey k;
        std::memset(&k, 0, sizeof(k));
        k.geomHash = c.geomHash;
        k.colorHash = c.colorHash;
        k.numVertices = c.numVertices;
        k.numTri = c.numTri;
        if (m.texture) {
            k.texId = m.texture->textureId;
            k.texModel = m.texture->model;
            k.texWrapS = m.texture->wrapS;
            k.texWrapT = m.texture->wrapT;
            k.texComps = uint8_t(m.texture->numComponents);
            k.texBlend = m.texture->blendColor;
        }
        if (m.bumpmap)
            k.bumpId = m.bumpmap->textureId;
        if (m.emissivemap)
            k.emissiveId = m.emissivemap->textureId;
        if (m.occlusionmap)
            k.occlusionId = m.occlusionmap->textureId;
        if (m.metallicroughnessmap)
            k.mrId = m.metallicroughnessmap->textureId;
        k.facePalette =
            uint64_t(reinterpret_cast<uintptr_t>(m.texturepalette.get()));
        k.facetexscale = m.facetexscale;
        k.facetexlayer = m.facetexlayer;
        // The texture matrix feeds any textured route (a bump or
        // material map transforms its texcoords through it too).
        if ((k.texId || k.bumpId || k.emissiveId || k.occlusionId
             || k.mrId) && !m.texidentity)
            std::memcpy(k.texmatrix, m.texmatrix, sizeof(k.texmatrix));
        k.transparent = m.transparent
            || (m.pervertexcolor && d.mesh->hasTransparency);
        k.start = d.indexStart;
        k.count = d.indexCount;
        k.part = d.partIndex;
        k.shininess = m.shininess;
        k.pofactor = m.polygonoffsetfactor;
        k.pounits = m.polygonoffsetunits;
        // Keyed on the RESOLVED factor, not the raw reach: the two are
        // max()'d, so reaches that both sit under the material's own
        // factor -- and every reach at all on a fill without polygon
        // offset -- come out equal and must not split a batch.
        {
            auto it = decorReach.find(d.objectKey);
            k.reach = BGFXView::polygonOffsetFactor(
                m, it == decorReach.end() ? 1.0f : it->second);
        }
        k.metallic = m.metallic;
        k.roughness = m.roughness;
        // The finish rides a uniform, so instances sharing a batch have
        // to share the whole record (the pattern coordinate itself is
        // object space, which every instance of a geometry shares).
        k.finish = m.finish;
        k.finishpitch = m.finishpitch;
        k.finishdepth = m.finishdepth;
        k.finishangle = m.finishangle;
        k.emissive = m.emissive;
        k.specular = m.specular;
        k.ambient = m.ambient;
        k.depthfunc = m.depthfunc;
        k.shadowstyle = m.shadowstyle;
        k.depthtest = m.depthtest;
        k.depthwrite = m.depthwrite;
        k.pervertexcolor = m.pervertexcolor;
        k.lighting = m.lighting;
        k.twoside = m.twoside;
        k.culling = m.culling;
        k.ccw = m.ccw;
        k.polygonoffset = m.polygonoffset;
        k.solidshape = m.solidshape;
        auto res = groups.emplace(k, int(instGroups.size()));
        if (res.second)
            instGroups.emplace_back();
        instGroups[res.first->second].members.push_back(i);
        instGroupOf[i] = res.first->second;
    }
    // Drop content entries no recent scene referenced (cache ids of
    // removed geometry never come back).
    if (meshContents.size() > 4 * scene.size() + 64) {
        for (auto it = meshContents.begin();
             it != meshContents.end();) {
            if (it->second.stamp + 4 < meshContentStamp)
                it = meshContents.erase(it);
            else
                ++it;
        }
    }
    // Singletons gain nothing; keep them on the per-draw path.
    for (auto &g : instGroups) {
        if (g.members.size() < 2) {
            for (int i : g.members)
                instGroupOf[i] = -1;
            g.members.clear();
        }
    }
    while (!instGroups.empty() && instGroups.back().members.empty())
        instGroups.pop_back();
    if (getenv("FC_BGFX_DEBUG_FEED")) {
        size_t n = 0, draws = 0;
        for (const auto &g : instGroups) {
            if (!g.members.empty()) {
                ++n;
                draws += g.members.size();
            }
        }
        fprintf(stderr,
                "bgfx feed instancing: %zu groups covering %zu of %zu"
                " draws\n", n, draws, scene.size());
    }
}

void BGFXRenderer::Private::updateBBox()
{
    static const bool dbg = getenv("FC_BGFX_DEBUG_BBOX") != nullptr;
    bboxValid = false;
    for (const auto &draw : scene) {
        // A navigation gizmo is not the scene (DrawCall::skipbounds):
        // the rotation-centre sphere sits wherever the spin is centred
        // and moves with it, and counting it would drag the shadow
        // ground and the auto near/far along with the mouse.
        if (draw.skipbounds)
            continue;
        if (draw.bboxMin[0] > draw.bboxMax[0])
            continue;
        if (!bboxValid) {
            bboxValid = true;
            for (int i = 0; i < 3; ++i) {
                bboxMin[i] = draw.bboxMin[i];
                bboxMax[i] = draw.bboxMax[i];
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                bboxMin[i] = qMin(bboxMin[i], draw.bboxMin[i]);
                bboxMax[i] = qMax(bboxMax[i], draw.bboxMax[i]);
            }
        }
    }
    if (dbg && bboxValid)
        fprintf(stderr, "bgfx bbox: %g,%g,%g - %g,%g,%g\n",
                bboxMin[0], bboxMin[1], bboxMin[2],
                bboxMax[0], bboxMax[1], bboxMax[2]);
}

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

#include "SceneDump.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

using namespace Render;

namespace {

const uint32_t kMagic = 0x46435344;  // 'FCSD'
// v2: selection/highlight feeds appended (v1 files still load).
// v11: per-edge/vertex part tables in the mesh + pickRadius in the
// presel/sel config, for browser-side edge/vertex picking.
// v13: AO method selector (SSAO / GTAO) in the AO config.
const uint32_t kVersion = 13;

//////////////////////////////////////////////////////////////////////
// Little-endian raw stream helpers. Every scalar goes through num()
// with an explicit fixed-width type so both sides agree on layout
// regardless of host struct packing (x86_64 dumper, wasm32 loader).

struct Writer {
    FILE *fp = nullptr;
    bool ok = true;

    void raw(const void *data, size_t size)
    {
        if (ok && size && std::fwrite(data, 1, size, fp) != size)
            ok = false;
    }
    template<typename T>
    void num(T v) { raw(&v, sizeof(v)); }
    void b(bool v) { num<uint8_t>(v ? 1 : 0); }
    void f(float v) { num<float>(v); }
    void u8(uint8_t v) { num<uint8_t>(v); }
    void u32(uint32_t v) { num<uint32_t>(v); }
    void i32(int32_t v) { num<int32_t>(v); }
    void u64(uint64_t v) { num<uint64_t>(v); }
    void floats(const float *v, size_t n) { raw(v, n * sizeof(float)); }
    void parts(const std::vector<std::pair<int, int>> &v)
    {
        u32(uint32_t(v.size()));
        for (const auto &p : v) {
            i32(p.first);
            i32(p.second);
        }
    }
};

struct Reader {
    FILE *fp = nullptr;
    bool ok = true;

    void raw(void *data, size_t size)
    {
        if (ok && size && std::fread(data, 1, size, fp) != size)
            ok = false;
        else if (!ok)
            std::memset(data, 0, size);
    }
    template<typename T>
    T num() { T v{}; raw(&v, sizeof(v)); return v; }
    bool b() { return num<uint8_t>() != 0; }
    float f() { return num<float>(); }
    uint8_t u8() { return num<uint8_t>(); }
    uint32_t u32() { return num<uint32_t>(); }
    int32_t i32() { return num<int32_t>(); }
    uint64_t u64() { return num<uint64_t>(); }
    void floats(float *v, size_t n) { raw(v, n * sizeof(float)); }
    void parts(std::vector<std::pair<int, int>> &v)
    {
        uint32_t n = u32();
        if (!ok || n > 0x1000000u) { ok = false; return; }
        v.resize(n);
        for (auto &p : v) {
            p.first = i32();
            p.second = i32();
        }
    }
};

//////////////////////////////////////////////////////////////////////
// Mesh payload

void writeMesh(Writer &w, const MeshData &m)
{
    w.u64(m.cacheId);
    w.i32(m.numVertices);
    uint8_t flags = (m.normals ? 1 : 0) | (m.colors ? 2 : 0)
        | (m.texCoords ? 4 : 0);
    w.u8(flags);
    w.raw(m.positions, size_t(m.numVertices) * 3 * sizeof(float));
    if (m.normals)
        w.raw(m.normals, size_t(m.numVertices) * 3 * sizeof(float));
    if (m.colors)
        w.raw(m.colors, size_t(m.numVertices) * 4);
    if (m.texCoords)
        w.raw(m.texCoords, size_t(m.numVertices) * 4 * sizeof(float));

    auto indices = [&](const int32_t *v, int n) {
        w.i32(v ? n : 0);
        if (v)
            w.raw(v, size_t(n) * sizeof(int32_t));
    };
    indices(m.triangleIndices, m.numTriangleIndices);
    indices(m.lineIndices, m.numLineIndices);
    indices(m.pointIndices, m.numPointIndices);
    indices(m.noSeamLineIndices, m.numNoSeamLineIndices);

    w.parts(m.triangleParts);
    w.parts(m.nonFlatParts);
    w.i32(m.hasSolid);
    w.parts(m.solidParts);
    w.b(m.hasTransparency);
    w.b(m.hasOpaqueParts);
    w.parts(m.lineParts);   // v11
    w.parts(m.pointParts);  // v11
}

/// Loader-side mesh: the arrays live in the owned vectors, the base
/// MeshData pointers point into them. Held by DrawCall::mesh.
struct OwnedMeshData : MeshData {
    std::vector<float> posStore;
    std::vector<float> normStore;
    std::vector<uint8_t> colorStore;
    std::vector<float> uvStore;
    std::vector<int32_t> triStore;
    std::vector<int32_t> lineStore;
    std::vector<int32_t> pointStore;
    std::vector<int32_t> noSeamStore;
};

std::shared_ptr<const MeshData> readMesh(Reader &r, uint32_t version)
{
    auto mesh = std::make_shared<OwnedMeshData>();
    mesh->cacheId = r.u64();
    mesh->numVertices = r.i32();
    uint8_t flags = r.u8();
    if (!r.ok || mesh->numVertices < 0
            || mesh->numVertices > 0x8000000) {
        r.ok = false;
        return nullptr;
    }
    size_t nv = size_t(mesh->numVertices);
    mesh->posStore.resize(nv * 3);
    r.floats(mesh->posStore.data(), nv * 3);
    mesh->positions = mesh->posStore.data();
    if (flags & 1) {
        mesh->normStore.resize(nv * 3);
        r.floats(mesh->normStore.data(), nv * 3);
        mesh->normals = mesh->normStore.data();
    }
    if (flags & 2) {
        mesh->colorStore.resize(nv * 4);
        r.raw(mesh->colorStore.data(), nv * 4);
        mesh->colors = mesh->colorStore.data();
    }
    if (flags & 4) {
        mesh->uvStore.resize(nv * 4);
        r.floats(mesh->uvStore.data(), nv * 4);
        mesh->texCoords = mesh->uvStore.data();
    }

    auto indices = [&](std::vector<int32_t> &store, const int32_t *&ptr,
                       int &count) {
        int n = r.i32();
        if (!r.ok || n < 0 || n > 0x8000000) {
            r.ok = false;
            return;
        }
        count = n;
        if (n) {
            store.resize(size_t(n));
            r.raw(store.data(), size_t(n) * sizeof(int32_t));
            ptr = store.data();
        }
    };
    indices(mesh->triStore, mesh->triangleIndices,
            mesh->numTriangleIndices);
    indices(mesh->lineStore, mesh->lineIndices, mesh->numLineIndices);
    indices(mesh->pointStore, mesh->pointIndices, mesh->numPointIndices);
    indices(mesh->noSeamStore, mesh->noSeamLineIndices,
            mesh->numNoSeamLineIndices);

    r.parts(mesh->triangleParts);
    r.parts(mesh->nonFlatParts);
    mesh->hasSolid = r.i32();
    r.parts(mesh->solidParts);
    mesh->hasTransparency = r.b();
    mesh->hasOpaqueParts = r.b();
    if (version >= 11) {
        r.parts(mesh->lineParts);
        r.parts(mesh->pointParts);
    }
    return mesh;
}

//////////////////////////////////////////////////////////////////////
// Texture payload

void writeTexture(Writer &w, const TextureImage &t)
{
    w.u64(t.textureId);
    w.i32(t.width);
    w.i32(t.height);
    w.i32(t.numComponents);
    w.u32(uint32_t(t.pixels.size()));
    w.raw(t.pixels.data(), t.pixels.size());
    w.u8(t.wrapS);
    w.u8(t.wrapT);
    w.u8(t.model);
    w.u32(t.blendColor);
}

std::shared_ptr<const TextureImage> readTexture(Reader &r)
{
    auto tex = std::make_shared<TextureImage>();
    tex->textureId = r.u64();
    tex->width = r.i32();
    tex->height = r.i32();
    tex->numComponents = r.i32();
    uint32_t n = r.u32();
    if (!r.ok || n > 0x20000000u) {
        r.ok = false;
        return nullptr;
    }
    tex->pixels.resize(n);
    r.raw(tex->pixels.data(), n);
    tex->wrapS = r.u8();
    tex->wrapT = r.u8();
    tex->model = r.u8();
    tex->blendColor = r.u32();
    return tex;
}

//////////////////////////////////////////////////////////////////////
// Material / draw call

typedef std::map<const TextureImage *, int32_t> TextureIndex;
typedef std::vector<std::shared_ptr<const TextureImage>> TextureTable;

void writeMaterial(Writer &w, const Material &m, const TextureIndex &tex)
{
    auto texref = [&](const std::shared_ptr<const TextureImage> &t) {
        auto it = tex.find(t.get());
        w.i32(it == tex.end() ? -1 : it->second);
    };

    w.u8(m.type);
    w.u8(m.depthfunc);
    w.b(m.depthtest);
    w.b(m.depthwrite);
    w.b(m.pervertexcolor);
    w.b(m.lighting);
    w.b(m.twoside);
    w.b(m.culling);
    w.b(m.ccw);
    w.b(m.transparent);
    w.b(m.ontop);
    w.b(m.polygonoffset);
    w.u32(m.diffuse);
    w.u32(m.emissive);
    w.u32(m.specular);
    w.u32(m.ambient);
    w.u32(m.linecolor);
    w.f(m.shininess);
    w.f(m.linewidth);
    w.f(m.pointsize);
    w.u32(m.linepattern);
    w.u32(m.hiddenlinepattern);
    w.f(m.polygonoffsetfactor);
    w.f(m.polygonoffsetunits);
    w.f(m.hiddenlinealpha);
    w.b(m.faceoutline);
    w.b(m.outlineonly);
    w.f(m.outlinewidth);
    w.b(m.outline);
    w.u8(m.shadowstyle);
    w.b(m.solidshape);
    w.f(m.metallic);
    w.f(m.roughness);
    w.b(m.water);
    w.f(m.waterdensity);
    w.b(m.glass);
    w.f(m.glassior);
    w.f(m.glassdensity);
    w.f(m.glassroughness);
    w.b(m.cloud);
    w.f(m.clouddensity);
    w.f(m.clouddetail);
    w.f(m.cloudspeed);
    w.b(m.fire);
    w.f(m.fireintensity);
    w.f(m.firedetail);
    w.f(m.firespeed);
    texref(m.texture);
    w.floats(m.texmatrix, 16);
    w.b(m.texidentity);
    texref(m.bumpmap);
    texref(m.emissivemap);
    texref(m.occlusionmap);
    texref(m.metallicroughnessmap);
    w.u32(uint32_t(m.autozoom.size()));
    for (const auto &az : m.autozoom) {
        w.floats(az.matrix, 16);
        w.f(az.scaleFactor);
        w.b(az.identity);
        w.b(az.resetmatrix);
        w.b(az.billboard);  // v7
        w.b(az.datumFlip);  // v8
        w.floats(az.normal, 3);  // v8
    }
    w.u8(m.numclipplanes);
    w.b(m.clipconcave);
    for (int i = 0; i < Material::MaxClipPlanes; ++i)
        w.floats(m.clipplanes[i], 4);
}

void readMaterial(Reader &r, Material &m, const TextureTable &tex, uint32_t version)
{
    auto texref = [&](std::shared_ptr<const TextureImage> &t) {
        int32_t idx = r.i32();
        if (idx >= 0 && size_t(idx) < tex.size())
            t = tex[size_t(idx)];
    };

    m.type = r.u8();
    m.depthfunc = r.u8();
    m.depthtest = r.b();
    m.depthwrite = r.b();
    m.pervertexcolor = r.b();
    m.lighting = r.b();
    m.twoside = r.b();
    m.culling = r.b();
    m.ccw = r.b();
    m.transparent = r.b();
    m.ontop = r.b();
    m.polygonoffset = r.b();
    m.diffuse = r.u32();
    m.emissive = r.u32();
    m.specular = r.u32();
    m.ambient = r.u32();
    m.linecolor = r.u32();
    m.shininess = r.f();
    m.linewidth = r.f();
    m.pointsize = r.f();
    m.linepattern = r.u32();
    m.hiddenlinepattern = r.u32();
    m.polygonoffsetfactor = r.f();
    m.polygonoffsetunits = r.f();
    m.hiddenlinealpha = r.f();
    m.faceoutline = r.b();
    m.outlineonly = r.b();
    m.outlinewidth = r.f();
    m.outline = r.b();
    m.shadowstyle = r.u8();
    m.solidshape = r.b();
    m.metallic = r.f();
    m.roughness = r.f();
    m.water = r.b();
    m.waterdensity = r.f();
    m.glass = r.b();
    m.glassior = r.f();
    m.glassdensity = r.f();
    m.glassroughness = r.f();
    m.cloud = r.b();
    m.clouddensity = r.f();
    m.clouddetail = r.f();
    m.cloudspeed = r.f();
    m.fire = r.b();
    m.fireintensity = r.f();
    m.firedetail = r.f();
    m.firespeed = r.f();
    texref(m.texture);
    r.floats(m.texmatrix, 16);
    m.texidentity = r.b();
    texref(m.bumpmap);
    texref(m.emissivemap);
    texref(m.occlusionmap);
    texref(m.metallicroughnessmap);
    uint32_t naz = r.u32();
    if (!r.ok || naz > 0x100000u) {
        r.ok = false;
        return;
    }
    m.autozoom.resize(naz);
    for (auto &az : m.autozoom) {
        r.floats(az.matrix, 16);
        az.scaleFactor = r.f();
        az.identity = r.b();
        az.resetmatrix = r.b();
        az.billboard = version >= 7 ? r.b() : false;
        if (version >= 8) {
            az.datumFlip = r.b();
            r.floats(az.normal, 3);
        }
    }
    m.numclipplanes = r.u8();
    m.clipconcave = r.b();
    for (int i = 0; i < Material::MaxClipPlanes; ++i)
        r.floats(m.clipplanes[i], 4);
}

//////////////////////////////////////////////////////////////////////
// Configs

void writeLight(Writer &w, const LightConfig &l, const TextureIndex &tex)
{
    auto texref = [&](const std::shared_ptr<const TextureImage> &t) {
        auto it = tex.find(t.get());
        w.i32(it == tex.end() ? -1 : it->second);
    };
    w.b(l.valid);
    w.b(l.spot);
    w.floats(l.direction, 3);
    w.floats(l.position, 3);
    w.f(l.cutOffAngle);
    w.f(l.dropOffRate);
    w.u32(l.color);
    w.f(l.intensity);
    w.f(l.smoothBorder);
    w.f(l.epsilon);
    w.f(l.threshold);
    w.f(l.spreadSize);
    w.f(l.spreadSampleSize);
    w.f(l.precision);
    w.b(l.ground);
    w.f(l.groundScale);
    w.u32(l.groundColor);
    texref(l.groundTexture);
    w.f(l.groundTextureSize);
    w.f(l.groundTransparency);
    texref(l.groundBumpMap);
    w.b(l.groundReflection);
    w.f(l.groundReflectionIntensity);
}

void readLight(Reader &r, LightConfig &l, const TextureTable &tex)
{
    auto texref = [&](std::shared_ptr<const TextureImage> &t) {
        int32_t idx = r.i32();
        if (idx >= 0 && size_t(idx) < tex.size())
            t = tex[size_t(idx)];
    };
    l.valid = r.b();
    l.spot = r.b();
    r.floats(l.direction, 3);
    r.floats(l.position, 3);
    l.cutOffAngle = r.f();
    l.dropOffRate = r.f();
    l.color = r.u32();
    l.intensity = r.f();
    l.smoothBorder = r.f();
    l.epsilon = r.f();
    l.threshold = r.f();
    l.spreadSize = r.f();
    l.spreadSampleSize = r.f();
    l.precision = r.f();
    l.ground = r.b();
    l.groundScale = r.f();
    l.groundColor = r.u32();
    texref(l.groundTexture);
    l.groundTextureSize = r.f();
    l.groundTransparency = r.f();
    texref(l.groundBumpMap);
    l.groundReflection = r.b();
    l.groundReflectionIntensity = r.f();
}

//////////////////////////////////////////////////////////////////////
// Draw calls

typedef std::map<const MeshData *, int32_t> MeshIndex;
typedef std::vector<std::shared_ptr<const MeshData>> MeshTable;

void writeDraw(Writer &w, const DrawCall &d, const MeshIndex &meshIndex,
               const TextureIndex &texIndex)
{
    writeMaterial(w, d.material, texIndex);
    auto it = d.mesh ? meshIndex.find(d.mesh.get()) : meshIndex.end();
    w.i32(it == meshIndex.end() ? -1 : it->second);
    w.floats(d.model, 16);
    w.b(d.identity);
    w.u64(d.objectKey);
    w.b(d.wholeObject);
    w.i32(d.partIndex);
    w.i32(d.indexStart);
    w.i32(d.indexCount);
    w.floats(d.bboxMin, 3);
    w.floats(d.bboxMax, 3);
}

void readDraw(Reader &r, DrawCall &d, const MeshTable &meshes,
              const TextureTable &textures, uint32_t version)
{
    readMaterial(r, d.material, textures, version);
    int32_t mi = r.i32();
    if (mi >= 0 && size_t(mi) < meshes.size())
        d.mesh = meshes[size_t(mi)];
    r.floats(d.model, 16);
    d.identity = r.b();
    d.objectKey = r.u64();
    d.wholeObject = r.b();
    d.partIndex = r.i32();
    d.indexStart = r.i32();
    d.indexCount = r.i32();
    r.floats(d.bboxMin, 3);
    r.floats(d.bboxMax, 3);
}

void writeDrawList(Writer &w, const DrawCallList &draws,
                   const MeshIndex &meshIndex, const TextureIndex &texIndex)
{
    w.u32(uint32_t(draws.size()));
    for (const auto &d : draws)
        writeDraw(w, d, meshIndex, texIndex);
}

bool readDrawList(Reader &r, DrawCallList &draws, const MeshTable &meshes,
                  const TextureTable &textures, uint32_t version)
{
    uint32_t n = r.u32();
    if (!r.ok || n > 0x1000000u) {
        r.ok = false;
        return false;
    }
    draws.clear();
    for (uint32_t i = 0; r.ok && i < n; ++i) {
        DrawCall d;
        readDraw(r, d, meshes, textures, version);
        draws.push_back(std::move(d));
    }
    return r.ok;
}

} // anonymous namespace

//////////////////////////////////////////////////////////////////////

static bool saveSnapshotFp(FILE *fp, const SceneSnapshot &snap)
{
    Writer w;
    w.fp = fp;
    w.u32(kMagic);
    w.u32(kVersion);

    // Unique mesh and texture tables referenced by index from the draws.
    MeshIndex meshIndex;
    std::vector<const MeshData *> meshes;
    TextureIndex texIndex;
    std::vector<const TextureImage *> textures;
    auto addTex = [&](const std::shared_ptr<const TextureImage> &t) {
        if (t && texIndex.emplace(t.get(),
                                  int32_t(textures.size())).second)
            textures.push_back(t.get());
    };
    auto addDraws = [&](const DrawCallList &draws) {
        for (const auto &d : draws) {
            if (d.mesh && meshIndex.emplace(d.mesh.get(),
                                            int32_t(meshes.size())).second)
                meshes.push_back(d.mesh.get());
            addTex(d.material.texture);
            addTex(d.material.bumpmap);
            addTex(d.material.emissivemap);
            addTex(d.material.occlusionmap);
            addTex(d.material.metallicroughnessmap);
        }
    };
    addDraws(snap.scene);
    for (const auto &sel : snap.selections)
        addDraws(sel.second);
    addDraws(snap.highlight);
    for (const auto &ov : snap.overlays)
        addDraws(ov.draws);
    addTex(snap.lightconf.groundTexture);
    addTex(snap.lightconf.groundBumpMap);

    w.u32(uint32_t(meshes.size()));
    for (auto *m : meshes)
        writeMesh(w, *m);
    w.u32(uint32_t(textures.size()));
    for (auto *t : textures)
        writeTexture(w, *t);

    writeDrawList(w, snap.scene, meshIndex, texIndex);

    // Background + per-frame configs.
    w.u8(snap.background.type);
    w.u32(snap.background.fromColor);
    w.u32(snap.background.toColor);
    w.u32(snap.background.midColor);
    w.b(snap.background.hasMid);

    const HiddenLineConfig &hl = snap.hlconfig;
    w.b(hl.show); w.b(hl.hideFace); w.b(hl.hideSeam); w.b(hl.hideVertex);
    w.b(hl.perFaceOutline); w.b(hl.sceneOutline);
    w.f(hl.outlineWidth); w.f(hl.outlineThicken); w.u32(hl.lineColor);

    const SectionConfig &sc = snap.secconf;
    w.b(sc.fill); w.b(sc.fillInvert); w.b(sc.fillGroup); w.b(sc.concave);
    w.b(sc.hatchEnable); w.f(sc.hatchScale);

    w.b(snap.aoconf.enabled);
    w.f(snap.aoconf.radius);
    w.f(snap.aoconf.intensity);
    w.i32(snap.aoconf.method);

    w.b(snap.pbrconf.enabled);
    w.f(snap.pbrconf.metallic);
    w.f(snap.pbrconf.roughness);
    w.f(snap.pbrconf.envIntensity);

    w.f(snap.bumpconf.scale);
    w.b(snap.bumpconf.parallax);

    writeLight(w, snap.lightconf, texIndex);

    const VolumetricConfig &vc = snap.volconf;
    w.b(vc.enabled); w.f(vc.intensity); w.f(vc.density);
    w.b(vc.caustics); w.f(vc.causticsIntensity); w.f(vc.causticsScale);
    w.f(vc.causticsSpeed);

    const WaterConfig &wc = snap.waterconf;
    w.b(wc.enabled); w.f(wc.waveStrength); w.f(wc.waveScale);
    w.f(wc.waveSpeed);

    w.f(snap.autozoomScale);
    w.f(snap.effectResolution);
    w.f(snap.ssaoResolution);

    w.i32(snap.hatchWidth);
    w.i32(snap.hatchHeight);
    w.u32(uint32_t(snap.hatchRGBA.size()));
    w.raw(snap.hatchRGBA.data(), snap.hatchRGBA.size());

    w.floats(snap.viewMatrix, 16);
    w.floats(snap.projMatrix, 16);
    w.i32(snap.width);
    w.i32(snap.height);
    w.u32(snap.clearColor);

    // v2: selection/highlight feeds (appended so the v1 prefix layout
    // is unchanged).
    w.u32(uint32_t(snap.selections.size()));
    for (const auto &sel : snap.selections) {
        w.i32(sel.first);
        writeDrawList(w, sel.second, meshIndex, texIndex);
    }
    writeDrawList(w, snap.highlight, meshIndex, texIndex);
    w.b(snap.highlightWholeOnTop);

    // v3: overlay feeds (appended so the v2 prefix layout is unchanged).
    w.u32(uint32_t(snap.overlays.size()));
    for (const auto &ov : snap.overlays) {
        w.i32(ov.id);
        const OverlayAnchor &a = ov.anchor;
        w.u8(a.corner);
        w.f(a.sizeFraction);
        w.f(a.fovDeg);
        w.f(a.orthoHeight);
        w.f(a.cameraDistance);
        w.f(a.nearPlane);
        w.f(a.farPlane);
        w.b(a.orientFromScene);
        w.b(a.pixelSpace); // v4
        w.f(a.marginX);    // v5
        w.f(a.marginY);
        w.b(a.sceneCamera); // v6
        writeDrawList(w, ov.draws, meshIndex, texIndex);
    }

    // v10: preselection + selection highlight config (client-side styling).
    for (const PreselHighlightConfig *c : {&snap.preselconf, &snap.selconf}) {
        w.u32(c->color);
        w.f(c->outlineWidth);
        w.b(c->faceOutline);
        w.b(c->outlineOnly);
        w.f(c->pickRadius);  // v11
    }

    return w.ok;
}

static bool loadSnapshotFp(FILE *fp, SceneSnapshot &snap)
{
    Reader r;
    r.fp = fp;
    uint32_t magic = r.u32();
    uint32_t version = r.u32();
    if (magic != kMagic || version < 1 || version > kVersion)
        return false;

    uint32_t nmesh = r.u32();
    MeshTable meshes;
    if (nmesh > 0x100000u)
        r.ok = false;
    for (uint32_t i = 0; r.ok && i < nmesh; ++i)
        meshes.push_back(readMesh(r, version));
    uint32_t ntex = r.u32();
    TextureTable textures;
    if (ntex > 0x100000u)
        r.ok = false;
    for (uint32_t i = 0; r.ok && i < ntex; ++i)
        textures.push_back(readTexture(r));

    readDrawList(r, snap.scene, meshes, textures, version);

    snap.background.type = r.u8();
    snap.background.fromColor = r.u32();
    snap.background.toColor = r.u32();
    snap.background.midColor = r.u32();
    snap.background.hasMid = r.b();

    HiddenLineConfig &hl = snap.hlconfig;
    hl.show = r.b(); hl.hideFace = r.b(); hl.hideSeam = r.b();
    hl.hideVertex = r.b(); hl.perFaceOutline = r.b();
    hl.sceneOutline = r.b();
    hl.outlineWidth = r.f(); hl.outlineThicken = r.f();
    hl.lineColor = r.u32();

    SectionConfig &sc = snap.secconf;
    sc.fill = r.b(); sc.fillInvert = r.b(); sc.fillGroup = r.b();
    sc.concave = r.b(); sc.hatchEnable = r.b(); sc.hatchScale = r.f();

    snap.aoconf.enabled = r.b();
    snap.aoconf.radius = r.f();
    snap.aoconf.intensity = r.f();
    snap.aoconf.method = version >= 13 ? r.i32() : 0;

    snap.pbrconf.enabled = r.b();
    snap.pbrconf.metallic = r.f();
    snap.pbrconf.roughness = r.f();
    snap.pbrconf.envIntensity = r.f();

    snap.bumpconf.scale = r.f();
    snap.bumpconf.parallax = r.b();

    readLight(r, snap.lightconf, textures);

    VolumetricConfig &vc = snap.volconf;
    vc.enabled = r.b(); vc.intensity = r.f(); vc.density = r.f();
    vc.caustics = r.b(); vc.causticsIntensity = r.f();
    vc.causticsScale = r.f(); vc.causticsSpeed = r.f();

    WaterConfig &wc = snap.waterconf;
    wc.enabled = r.b(); wc.waveStrength = r.f(); wc.waveScale = r.f();
    wc.waveSpeed = r.f();

    snap.autozoomScale = r.f();
    snap.effectResolution = version >= 9 ? r.f() : 1.0f;
    snap.ssaoResolution = version >= 12 ? r.f() : 1.0f;

    snap.hatchWidth = r.i32();
    snap.hatchHeight = r.i32();
    uint32_t nhatch = r.u32();
    if (nhatch > 0x10000000u)
        r.ok = false;
    if (r.ok) {
        snap.hatchRGBA.resize(nhatch);
        r.raw(snap.hatchRGBA.data(), nhatch);
    }

    r.floats(snap.viewMatrix, 16);
    r.floats(snap.projMatrix, 16);
    snap.width = r.i32();
    snap.height = r.i32();
    snap.clearColor = r.u32();

    snap.selections.clear();
    snap.highlight.clear();
    snap.highlightWholeOnTop = false;
    if (version >= 2) {
        uint32_t nsel = r.u32();
        if (!r.ok || nsel > 0x10000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < nsel; ++i) {
            int id = r.i32();
            DrawCallList draws;
            if (readDrawList(r, draws, meshes, textures, version))
                snap.selections.emplace_back(id, std::move(draws));
        }
        readDrawList(r, snap.highlight, meshes, textures, version);
        snap.highlightWholeOnTop = r.b();
    }

    snap.overlays.clear();
    if (version >= 3) {
        uint32_t nov = r.u32();
        if (!r.ok || nov > 0x10000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < nov; ++i) {
            SceneSnapshot::Overlay ov;
            ov.id = r.i32();
            OverlayAnchor &a = ov.anchor;
            a.corner = r.u8();
            a.sizeFraction = r.f();
            a.fovDeg = r.f();
            a.orthoHeight = r.f();
            a.cameraDistance = r.f();
            a.nearPlane = r.f();
            a.farPlane = r.f();
            a.orientFromScene = r.b();
            a.pixelSpace = version >= 4 ? r.b() : false;
            if (version >= 5) {
                a.marginX = r.f();
                a.marginY = r.f();
            }
            a.sceneCamera = version >= 6 ? r.b() : false;
            if (readDrawList(r, ov.draws, meshes, textures, version))
                snap.overlays.push_back(std::move(ov));
        }
    }

    if (version >= 10) {
        for (PreselHighlightConfig *c : {&snap.preselconf, &snap.selconf}) {
            c->color = r.u32();
            c->outlineWidth = r.f();
            c->faceOutline = r.b();
            c->outlineOnly = r.b();
            if (version >= 11)
                c->pickRadius = r.f();
        }
    }

    return r.ok;
}

bool Render::saveSceneSnapshot(const char *path, const SceneSnapshot &snap)
{
    FILE *fp = std::fopen(path, "wb");
    if (!fp)
        return false;
    bool ok = saveSnapshotFp(fp, snap);
    std::fclose(fp);
    return ok;
}

bool Render::loadSceneSnapshot(const char *path, SceneSnapshot &snap)
{
    FILE *fp = std::fopen(path, "rb");
    if (!fp)
        return false;
    bool ok = loadSnapshotFp(fp, snap);
    std::fclose(fp);
    return ok;
}

#ifdef _WIN32
// No open_memstream/fmemopen: stage through a temporary file.

bool Render::saveSceneSnapshot(std::vector<uint8_t> &out,
                               const SceneSnapshot &snap)
{
    FILE *fp = std::tmpfile();
    if (!fp)
        return false;
    bool ok = saveSnapshotFp(fp, snap);
    if (ok) {
        long size = (std::fseek(fp, 0, SEEK_END) == 0) ? std::ftell(fp) : -1;
        ok = size >= 0 && std::fseek(fp, 0, SEEK_SET) == 0;
        if (ok) {
            out.resize(size_t(size));
            ok = std::fread(out.data(), 1, out.size(), fp) == out.size();
        }
    }
    std::fclose(fp);
    return ok;
}

bool Render::loadSceneSnapshot(const void *data, size_t size,
                               SceneSnapshot &snap)
{
    FILE *fp = std::tmpfile();
    if (!fp)
        return false;
    bool ok = std::fwrite(data, 1, size, fp) == size
        && std::fseek(fp, 0, SEEK_SET) == 0
        && loadSnapshotFp(fp, snap);
    std::fclose(fp);
    return ok;
}

#else // !_WIN32

bool Render::saveSceneSnapshot(std::vector<uint8_t> &out,
                               const SceneSnapshot &snap)
{
    char *buf = nullptr;
    size_t size = 0;
    FILE *fp = open_memstream(&buf, &size);
    if (!fp)
        return false;
    bool ok = saveSnapshotFp(fp, snap);
    std::fclose(fp);
    if (ok)
        out.assign(buf, buf + size);
    std::free(buf);
    return ok;
}

bool Render::loadSceneSnapshot(const void *data, size_t size,
                               SceneSnapshot &snap)
{
    FILE *fp = fmemopen(const_cast<void *>(data), size, "rb");
    if (!fp)
        return false;
    bool ok = loadSnapshotFp(fp, snap);
    std::fclose(fp);
    return ok;
}

#endif // _WIN32

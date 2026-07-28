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
#include <set>
#include <vector>

using namespace Render;

namespace {

const uint32_t kMagic = 0x46435344;  // 'FCSD'
// v2: selection/highlight feeds appended (v1 files still load).
// v11: per-edge/vertex part tables in the mesh + pickRadius in the
// presel/sel config, for browser-side edge/vertex picking.
// v13: AO method selector (SSAO / GTAO) in the AO config.
// v14: GTAO slice/step tuning in the AO config.
// 22: RenderDebug view modes 5-8 (shader + pass support the viewer
//     binary must have — the bump forces stale pages to self-reload).
// 23: user shaders (docs/RenderDebug.md §6.3): deduplicated shader
//     table (sources + params + server-compiled viewer binaries),
//     post-stage config list, per-material shader reference.
// 24: assembled volume-splice variants (docs/RenderEngine.md §5.11)
//     as extra shader-table entries + index list, so compiler-less
//     tiers can adopt the server-compiled binaries by source match.
// 25: PBRConfig::envBackground + envImage (the environment drawn as
//     the background; a user image replacing the procedural one).
// 26: texture entries carry a content key (SHA-1 of the pixels) and a
//     deferred flag: the streaming transport writes the key alone and
//     serves the payload out of band, so a republish stops re-sending
//     every embedded image and the viewer caches each key.
// 27: the section-cap hatch image joins the texture table (was a raw
//     blob written inline), so it rides the v26 content key and is
//     served out of band like any other image.
// 28: mesh entries carry a content key and a deferred flag, and their
//     cacheId leaves the hashed payload (it is a counter, so hashing
//     it would mint a new key for unchanged geometry). The streaming
//     transport writes the key alone and serves the chunks in batches.
// 29: materials are deduplicated into a table and referenced by index
//     instead of being written inline in every draw. A material is
//     ~347 of a draw's 463 bytes and repeats heavily — an assembly
//     whose thousands of parts share a dozen appearances writes each
//     of them thousands of times.
// 30: the matrices the format itself documents as "valid when
//     !identity" (draw model, material texture, autozoom) are written
//     only when they are not, and only the clip planes a material
//     actually uses are written instead of all MaxClipPlanes slots.
// 31: the material table can be served out of band under its content
//     key, like the mesh chunks — deduplication makes it the largest
//     section, and it rarely changes from one publish to the next.
// 32: every out-of-band chunk starts with kChunkVersion, so a change
//     to a chunk's layout changes the content key of every chunk and
//     a cached one can never be parsed by a reader that disagrees
//     with it (the key covers the format, not just the payload).
// 33: two layouts, chosen by a flag after the version and by whether
//     the writer was given a chunk sink. The monolithic one is
//     unchanged and is what a bundled .fcsd capture must be. The
//     manifest one (docs/SceneStreaming.md §4) has no global tables at
//     all: draws are grouped by objectKey into content-keyed chunks,
//     and materials and shaders become individually keyed leaves. An
//     unchanged object then costs its root entry and nothing else,
//     where before every publish re-sent every draw.
// 34: the manifest root carries its version and the version it is
//     encoded against, and its object list can be a delta — the
//     objects that changed and the ones that went away, against a
//     list the consumer already holds. Naming every object cost 79 B
//     each on every publish, which is what a big model actually pays.
// 35: the manifest root names the backend run that numbered its
//     version. Versions restart when the backend does, so a consumer
//     reconnecting across a restart would otherwise offer a version
//     the new run will happily believe.
const uint32_t kVersion = 35;

/// Layout revision of the out-of-band chunks (mesh, material, shader,
/// group manifest). Written as the first field of each chunk, so it is
/// part of what the content key hashes: bump it whenever a chunk's own
/// layout changes and every key changes with it, which retires the
/// entries cached by older builds instead of letting them be misread.
const uint32_t kChunkVersion = 2;

//////////////////////////////////////////////////////////////////////
// Little-endian raw stream helpers. Every scalar goes through num()
// with an explicit fixed-width type so both sides agree on layout
// regardless of host struct packing (x86_64 dumper, wasm32 loader).

struct Writer {
    FILE *fp = nullptr;
    /// Target a buffer instead of a stream. Used for the many small
    /// records that are hashed or deduplicated before they are written
    /// (mesh chunks, materials), where a memory stream per record would
    /// cost an allocation and a FILE for a few hundred bytes.
    std::vector<uint8_t> *vec = nullptr;
    bool ok = true;

    void raw(const void *data, size_t size)
    {
        if (!ok || !size)
            return;
        if (vec) {
            const uint8_t *p = static_cast<const uint8_t *>(data);
            vec->insert(vec->end(), p, p + size);
            return;
        }
        if (std::fwrite(data, 1, size, fp) != size)
            ok = false;
    }
    /// How far into the payload the next write lands — what a span is
    /// recorded from (SceneDump.h, RootSpans).
    size_t pos() const
    {
        if (vec)
            return vec->size();
        long at = fp ? std::ftell(fp) : -1;
        return at < 0 ? 0 : size_t(at);
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
    void str(const std::string &s)
    {
        u32(uint32_t(s.size()));
        raw(s.data(), s.size());
    }
    void bytes(const std::vector<uint8_t> &v)
    {
        u32(uint32_t(v.size()));
        raw(v.data(), v.size());
    }
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
    void str(std::string &s, uint32_t maxLen = 0x1000000u)
    {
        uint32_t len = u32();
        if (!ok || len > maxLen) { ok = false; return; }
        s.resize(len);
        raw(len ? &s[0] : nullptr, len);
    }
    void bytes(std::vector<uint8_t> &v, uint32_t maxLen = 0x2000000u)
    {
        uint32_t len = u32();
        if (!ok || len > maxLen) { ok = false; return; }
        v.resize(len);
        raw(v.data(), len);
    }
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

/// Serialize a detached record (an out-of-band mesh payload, a material
/// table entry) into a buffer.
static bool writeChunk(std::vector<uint8_t> &out,
                       const std::function<void(Writer &)> &fn)
{
    Writer w;
    w.vec = &out;
    fn(w);
    return w.ok;
}
/// Parse one back. Defined with the platform's memory-stream shim at
/// the end of this file.
static bool readChunk(const void *data, size_t size,
                      const std::function<void(Reader &)> &fn);
std::string sha1Hex(const uint8_t *data, size_t size);
/// Defined with the manifest layout, which is what makes it necessary.
uint64_t meshIdFromKey(const std::string &key);

/// Fill a 4x4 with the identity: the matrices skipped by v30 are not
/// default-initialized in Renderer.h, so a consumer that reads one
/// despite its flag must still find something sane.
void setIdentity(float m[16])
{
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

//////////////////////////////////////////////////////////////////////
// Mesh payload

/// The mesh content proper — everything but the cacheId, which names
/// the mesh rather than describing it and would poison the content key
/// (it is a counter, bumped even by a re-tessellation that reproduces
/// the same geometry byte for byte).
void writeMeshChunk(Writer &w, const MeshData &m)
{
    w.u32(kChunkVersion);
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

/// \a blobs unset (a bundled snapshot) writes the identity and the
/// chunk inline as before; set (the streaming transport) writes the
/// content key alone and hands the chunk over to be served out of
/// band. On the streaming path a memoized key costs nothing at all:
/// no serialization, no hash, no copy of the geometry.
void writeMesh(Writer &w, const MeshData &m,
               const SceneSnapshot::MeshBlobSink &blobs)
{
    if (!blobs) {
        w.u8(0);
        w.u64(m.cacheId);
        writeMeshChunk(w, m);
        return;
    }

    uint32_t size = 0;
    std::string key = blobs.reuse(m.cacheId, size);
    if (key.empty()) {
        std::vector<uint8_t> chunk;
        if (!writeChunk(chunk, [&m](Writer &cw) { writeMeshChunk(cw, m); })) {
            w.ok = false;
            return;
        }
        key = sha1Hex(chunk.data(), chunk.size());
        size = uint32_t(chunk.size());
        blobs.store(m.cacheId, key, std::move(chunk));
    }
    w.u8(1);
    w.str(key);
    // The payload size the key stands for: the viewer routes a small
    // chunk into a batch and a large one into its own request, and it
    // cannot know which without being told.
    w.u32(size);
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

/// Parse the mesh content proper (writeMeshChunk's output) into \a mesh,
/// from either the stream itself or a separately fetched chunk.
void readMeshChunk(Reader &r, OwnedMeshData *mesh, uint32_t version)
{
    if (version >= 32 && r.u32() != kChunkVersion) {
        // Only reachable from a cache that outlived the build that
        // wrote it; the caller drops the entry and refetches.
        r.ok = false;
        return;
    }
    mesh->numVertices = r.i32();
    uint8_t flags = r.u8();
    if (!r.ok || mesh->numVertices < 0
            || mesh->numVertices > 0x8000000) {
        r.ok = false;
        return;
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
}

/// Read one mesh table entry. A deferred entry (v28, streaming) yields
/// an empty MeshData plus the means to fill it once its chunk arrives;
/// the draw calls already alias it, so filling it in place is enough.
std::shared_ptr<const MeshData> readMesh(Reader &r, uint32_t version,
                                         SceneSnapshot &snap)
{
    auto mesh = std::make_shared<OwnedMeshData>();
    if (version >= 28 && r.u8() != 0) {
        SceneSnapshot::DeferredChunk entry;
        r.str(entry.key, 128);
        entry.size = r.u32();
        if (!r.ok)
            return nullptr;
        mesh->cacheId = meshIdFromKey(entry.key);
        // Parse with the version of the snapshot that named the chunk,
        // not this build's: a viewer newer than the payload reads it
        // as it is, and only a payload newer than the viewer makes the
        // viewer reload.
        entry.fill = [mesh, version](SceneSnapshot &, const void *data,
                                     size_t size) {
            uint64_t id = mesh->cacheId;
            bool ok = readChunk(data, size, [&mesh, version](Reader &cr) {
                readMeshChunk(cr, mesh.get(), version);
            });
            if (!ok) {
                // A partial parse leaves a vertex count with no arrays
                // behind it, which a backend would read straight past
                // the end. Empty is the only safe failure.
                *mesh = OwnedMeshData();
            }
            mesh->cacheId = id;
            return ok;
        };
        snap.deferredChunks.push_back(std::move(entry));
        return mesh;
    }
    mesh->cacheId = r.u64();
    readMeshChunk(r, mesh.get(), version);
    return r.ok ? mesh : nullptr;
}

//////////////////////////////////////////////////////////////////////
// Content key

/// SHA-1 (RFC 3174) of a byte range, as 40 lowercase hex characters.
/// Self-contained on purpose: this file also compiles into the wasm
/// viewer, which links neither Qt nor any crypto library, and both
/// tiers must address a payload identically.
std::string sha1Hex(const uint8_t *data, size_t size)
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                     0xC3D2E1F0u};
    auto rol = [](uint32_t v, int n) {
        return uint32_t((v << n) | (v >> (32 - n)));
    };
    // The message is processed as 64-byte blocks; the tail block(s) hold
    // the 0x80 terminator, zero padding and the 64-bit bit count.
    uint64_t bits = uint64_t(size) * 8;
    size_t total = size + 1;
    total += (56 - total % 64 + 64) % 64;
    total += 8;
    for (size_t base = 0; base < total; base += 64) {
        uint8_t block[64];
        for (size_t i = 0; i < 64; ++i) {
            size_t pos = base + i;
            if (pos < size)
                block[i] = data[pos];
            else if (pos == size)
                block[i] = 0x80;
            else if (pos < total - 8)
                block[i] = 0;
            else
                block[i] = uint8_t(bits >> ((total - 1 - pos) * 8));
        }
        uint32_t wv[80];
        for (int i = 0; i < 16; ++i)
            wv[i] = (uint32_t(block[i * 4]) << 24)
                    | (uint32_t(block[i * 4 + 1]) << 16)
                    | (uint32_t(block[i * 4 + 2]) << 8)
                    | uint32_t(block[i * 4 + 3]);
        for (int i = 16; i < 80; ++i)
            wv[i] = rol(wv[i - 3] ^ wv[i - 8] ^ wv[i - 14] ^ wv[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + wv[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    static const char *hex = "0123456789abcdef";
    std::string out(40, '0');
    for (int i = 0; i < 20; ++i) {
        uint8_t byte = uint8_t(h[i / 4] >> ((3 - i % 4) * 8));
        out[i * 2] = hex[byte >> 4];
        out[i * 2 + 1] = hex[byte & 0xf];
    }
    return out;
}

//////////////////////////////////////////////////////////////////////
// Texture payload

/// \a blobs unset (a bundled snapshot) writes the pixels inline as
/// before; set (the streaming transport) writes the key alone and hands
/// the payload over to be served out of band.
///
/// \a sent, when given, is the set of keys already handed over during
/// this write. The manifest layout has no global texture table, so the
/// same image is described once per material that uses it — cheap,
/// since only the ~70-byte header repeats, but the payload must not be
/// copied out of the renderer's texture again for each of them.
void writeTexture(Writer &w, const TextureImage &t,
                  const SceneSnapshot::TextureBlobSink &blobs,
                  std::set<std::string> *sent = nullptr)
{
    // An empty texture has nothing to fetch, so it stays inline whatever
    // the transport: deferring it would cost a round trip for no bytes.
    bool defer = bool(blobs) && !t.pixels.empty();
    if (t.contentKey.size() != 40 && !t.pixels.empty())
        t.contentKey = sha1Hex(t.pixels.data(), t.pixels.size());

    w.u64(t.textureId);
    w.i32(t.width);
    w.i32(t.height);
    w.i32(t.numComponents);
    w.u8(defer ? 1 : 0);
    w.u32(uint32_t(t.contentKey.size()));
    w.raw(t.contentKey.data(), t.contentKey.size());
    // The payload size either way (v33): deferred, it is what the
    // consumer picks a fetch policy from — one request of its own for a
    // big image, a shared batch for a small one — and it cannot make
    // that call without being told. Inline, it is the byte count that
    // follows.
    w.u32(uint32_t(t.pixels.size()));
    if (!defer)
        w.raw(t.pixels.data(), t.pixels.size());
    w.u8(t.wrapS);
    w.u8(t.wrapT);
    w.u8(t.model);
    w.u32(t.blendColor);

    if (defer && (!sent || sent->insert(t.contentKey).second))
        blobs(t.contentKey, std::vector<uint8_t>(t.pixels));
}

/// \a payloadSize, when given, receives the size of a deferred
/// texture's pixels — zero for one that came inline.
std::shared_ptr<TextureImage> readTexture(Reader &r, uint32_t version,
                                          uint32_t *payloadSize = nullptr)
{
    auto tex = std::make_shared<TextureImage>();
    tex->textureId = r.u64();
    tex->width = r.i32();
    tex->height = r.i32();
    tex->numComponents = r.i32();
    if (version >= 26) {
        tex->deferred = r.u8() != 0;
        uint32_t klen = r.u32();
        if (!r.ok || klen > 128) {
            r.ok = false;
            return nullptr;
        }
        tex->contentKey.resize(klen);
        r.raw(&tex->contentKey[0], klen);
    }
    uint32_t n = r.u32();
    if (!r.ok || n > 0x20000000u) {
        r.ok = false;
        return nullptr;
    }
    if (tex->deferred) {
        // v33 names the size here; v26-v32 wrote a zero, which simply
        // leaves the consumer to fetch without one.
        if (payloadSize)
            *payloadSize = n;
    }
    else {
        tex->pixels.resize(n);
        r.raw(tex->pixels.data(), n);
    }
    tex->wrapS = r.u8();
    tex->wrapT = r.u8();
    tex->model = r.u8();
    tex->blendColor = r.u32();
    return tex;
}

/// Queue a key-only texture as an ordinary deferred payload. Its
/// pixels are filled in place, which is enough because every material
/// that uses the image points at this same object.
///
/// A texture is the one payload a scene survives without, and that is
/// expressed here rather than in the consumer: asked to give up, it
/// clears the flag and reports success, so the draw renders untextured
/// instead of the whole snapshot being withheld.
void deferTexture(SceneSnapshot &snap,
                  const std::shared_ptr<TextureImage> &tex, uint32_t size)
{
    SceneSnapshot::DeferredChunk entry;
    entry.key = tex->contentKey;
    entry.size = size;
    entry.fill = [tex](SceneSnapshot &, const void *data, size_t size) {
        if (data) {
            const uint8_t *bytes = static_cast<const uint8_t *>(data);
            tex->pixels.assign(bytes, bytes + size);
        }
        tex->deferred = false;
        return true;
    };
    snap.deferredChunks.push_back(std::move(entry));
}

//////////////////////////////////////////////////////////////////////
// Material / draw call

typedef std::map<const DrawCall *, int32_t> MaterialIndex;
typedef std::map<const TextureImage *, int32_t> TextureIndex;
typedef std::vector<std::shared_ptr<const TextureImage>> TextureTable;

//////////////////////////////////////////////////////////////////////
// User shaders (v23): a deduplicated table referenced by index from
// the post-stage config list and the draw materials. Each entry
// carries its sources, parameters and the server-compiled viewer
// binaries (UserShader::Compiled).

typedef std::map<const UserShader *, int32_t> ShaderIndex;
typedef std::vector<std::shared_ptr<const UserShader>> ShaderTable;

void writeUserShader(
    Writer &w, const UserShader &s,
    const std::function<void(const UserShader &,
                             std::vector<UserShader::Compiled> &)> &bins)
{
    w.str(s.stage);
    w.str(s.vertexSource);
    w.str(s.fragmentSource);
    w.u32(uint32_t(s.params.size()));
    for (const auto &p : s.params) {
        w.str(p.name);
        w.u32(uint32_t(p.values.size()));
        w.floats(p.values.data(), p.values.size());
    }
    // The shader's own variants plus whatever the save-side compile
    // hook has ready, first entry per profile wins on the viewer.
    std::vector<UserShader::Compiled> compiled = s.compiled;
    if (bins)
        bins(s, compiled);
    w.u32(uint32_t(compiled.size()));
    for (const auto &c : compiled) {
        w.str(c.profile);
        w.bytes(c.vsBin);
        w.bytes(c.fsBin);
    }
}

std::shared_ptr<const UserShader> readUserShader(Reader &r)
{
    auto s = std::make_shared<UserShader>();
    r.str(s->stage, 0x100u);
    r.str(s->vertexSource);
    r.str(s->fragmentSource);
    uint32_t np = r.u32();
    if (!r.ok || np > 0x10000u) {
        r.ok = false;
        return s;
    }
    s->params.resize(np);
    for (auto &p : s->params) {
        r.str(p.name, 0x1000u);
        uint32_t nv = r.u32();
        if (!r.ok || nv > 0x10000u) {
            r.ok = false;
            return s;
        }
        p.values.resize(nv);
        r.floats(p.values.data(), nv);
    }
    uint32_t nc = r.u32();
    if (!r.ok || nc > 0x100u) {
        r.ok = false;
        return s;
    }
    s->compiled.resize(nc);
    for (auto &c : s->compiled) {
        r.str(c.profile, 0x100u);
        r.bytes(c.vsBin);
        r.bytes(c.fsBin);
    }
    return s;
}

//////////////////////////////////////////////////////////////////////

/// How a record places a reference to something it does not itself
/// contain. The monolithic layout writes an index into a global table;
/// the manifest layout writes the thing inline (a texture header) or
/// its content key (pixels, a shader), because a chunk whose bytes
/// depend on a position in a table outside it would be invalidated by
/// every insertion anywhere in that table — the false invalidation the
/// manifest tree exists to avoid (docs/SceneStreaming.md §3).
struct RefWriter {
    std::function<void(Writer &,
                       const std::shared_ptr<const TextureImage> &)> tex;
    std::function<void(Writer &, const UserShader *)> shader;
};
struct RefReader {
    std::function<void(Reader &,
                       std::shared_ptr<const TextureImage> &)> tex;
    std::function<void(Reader &,
                       std::shared_ptr<const UserShader> &)> shader;
};

/// The monolithic pair: an index into the snapshot's global tables.
RefWriter tableRefWriter(const TextureIndex &tex, const ShaderIndex &shaders)
{
    RefWriter refs;
    refs.tex = [&tex](Writer &w, const std::shared_ptr<const TextureImage> &t) {
        auto it = tex.find(t.get());
        w.i32(it == tex.end() ? -1 : it->second);
    };
    refs.shader = [&shaders](Writer &w, const UserShader *s) {
        auto it = s ? shaders.find(s) : shaders.end();
        w.i32(it == shaders.end() ? -1 : it->second);
    };
    return refs;
}

RefReader tableRefReader(const TextureTable &tex, const ShaderTable &shaders)
{
    RefReader refs;
    refs.tex = [&tex](Reader &r, std::shared_ptr<const TextureImage> &t) {
        int32_t idx = r.i32();
        if (idx >= 0 && size_t(idx) < tex.size())
            t = tex[size_t(idx)];
    };
    refs.shader = [&shaders](Reader &r,
                             std::shared_ptr<const UserShader> &s) {
        int32_t idx = r.i32();
        if (idx >= 0 && size_t(idx) < shaders.size())
            s = shaders[size_t(idx)];
    };
    return refs;
}

void writeMaterial(Writer &w, const Material &m, const RefWriter &refs)
{
    auto texref = [&](const std::shared_ptr<const TextureImage> &t) {
        refs.tex(w, t);
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
    w.b(m.fountain);
    w.f(m.fountaindensity);
    w.f(m.fountaindetail);
    w.f(m.fountainspeed);
    w.b(m.lightsource);   // v18
    w.f(m.lightintensity);
    w.f(m.lightrange);
    w.b(m.lightshadow);
    w.b(m.lightshadowext);   // v19
    texref(m.texture);
    // v30: the flag first, then the matrix only when it says there is
    // one — Material documents texmatrix as valid only when
    // !texidentity, and most materials are untextured.
    w.b(m.texidentity);
    if (!m.texidentity)
        w.floats(m.texmatrix, 16);
    texref(m.bumpmap);
    texref(m.emissivemap);
    texref(m.occlusionmap);
    texref(m.metallicroughnessmap);
    w.u32(uint32_t(m.autozoom.size()));
    for (const auto &az : m.autozoom) {
        w.b(az.identity);           // v30: matrix only when it is one
        if (!az.identity)
            w.floats(az.matrix, 16);
        w.f(az.scaleFactor);
        w.b(az.resetmatrix);
        w.b(az.billboard);  // v7
        w.b(az.datumFlip);  // v8
        w.floats(az.normal, 3);  // v8
    }
    w.u8(m.numclipplanes);
    w.b(m.clipconcave);
    // v30: the planes in use, not all MaxClipPlanes slots.
    int nclip = m.numclipplanes < Material::MaxClipPlanes
        ? int(m.numclipplanes) : Material::MaxClipPlanes;
    for (int i = 0; i < nclip; ++i)
        w.floats(m.clipplanes[i], 4);
    // v23: the user "material"-stage shader.
    refs.shader(w, m.usershader.get());
}

void readMaterial(Reader &r, Material &m, const RefReader &refs,
                  uint32_t version)
{
    auto texref = [&](std::shared_ptr<const TextureImage> &t) {
        refs.tex(r, t);
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
    if (version >= 17) {
        m.fountain = r.b();
        m.fountaindensity = r.f();
        m.fountaindetail = r.f();
        m.fountainspeed = r.f();
    }
    if (version >= 18) {
        m.lightsource = r.b();
        m.lightintensity = r.f();
        m.lightrange = r.f();
        m.lightshadow = r.b();
        if (version >= 19)
            m.lightshadowext = r.b();
    }
    texref(m.texture);
    if (version >= 30) {
        m.texidentity = r.b();
        if (!m.texidentity)
            r.floats(m.texmatrix, 16);
        else
            setIdentity(m.texmatrix);
    }
    else {
        r.floats(m.texmatrix, 16);
        m.texidentity = r.b();
    }
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
        if (version >= 30) {
            az.identity = r.b();
            if (!az.identity)
                r.floats(az.matrix, 16);
            else
                setIdentity(az.matrix);
            az.scaleFactor = r.f();
        }
        else {
            r.floats(az.matrix, 16);
            az.scaleFactor = r.f();
            az.identity = r.b();
        }
        az.resetmatrix = r.b();
        az.billboard = version >= 7 ? r.b() : false;
        if (version >= 8) {
            az.datumFlip = r.b();
            r.floats(az.normal, 3);
        }
    }
    m.numclipplanes = r.u8();
    m.clipconcave = r.b();
    int nclip = Material::MaxClipPlanes;
    if (version >= 30) {
        nclip = m.numclipplanes < Material::MaxClipPlanes
            ? int(m.numclipplanes) : Material::MaxClipPlanes;
    }
    for (int i = 0; i < nclip; ++i)
        r.floats(m.clipplanes[i], 4);
    // The array is not default-initialized, so the slots the stream
    // does not describe have to be cleared rather than left as noise.
    for (int i = nclip; i < Material::MaxClipPlanes; ++i)
        std::memset(m.clipplanes[i], 0, sizeof(m.clipplanes[i]));
    if (version >= 23)
        refs.shader(r, m.usershader);
}

//////////////////////////////////////////////////////////////////////
// Configs

void writeLight(Writer &w, const LightConfig &l, const RefWriter &refs)
{
    auto texref = [&](const std::shared_ptr<const TextureImage> &t) {
        refs.tex(w, t);
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
    w.b(l.sunDisc);   // v18
    w.f(l.sunDiscSize);
}

void readLight(Reader &r, LightConfig &l, const RefReader &refs,
               uint32_t version)
{
    auto texref = [&](std::shared_ptr<const TextureImage> &t) {
        refs.tex(r, t);
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
    if (version >= 18) {
        l.sunDisc = r.b();
        l.sunDiscSize = r.f();
    }
}

//////////////////////////////////////////////////////////////////////
// Draw calls

typedef std::map<const MeshData *, int32_t> MeshIndex;
typedef std::vector<std::shared_ptr<const MeshData>> MeshTable;

/// A draw's references to its mesh and its material. Both are shared
/// heavily, so both are placed indirectly — into the snapshot's global
/// tables in the monolithic layout, into the enclosing group's local
/// key lists in the manifest one.
struct DrawRefWriter {
    std::function<void(Writer &,
                       const std::shared_ptr<const MeshData> &)> mesh;
    std::function<void(Writer &, const DrawCall &)> material;
};
struct DrawRefReader {
    std::function<void(Reader &, std::shared_ptr<const MeshData> &)> mesh;
    std::function<void(Reader &, DrawCall &)> material;
};

void writeDraw(Writer &w, const DrawCall &d, const DrawRefWriter &refs)
{
    refs.material(w, d);
    refs.mesh(w, d.mesh);
    // v30: as for the material matrices, the flag first and the matrix
    // only when there is one.
    w.b(d.identity);
    if (!d.identity)
        w.floats(d.model, 16);
    w.u64(d.objectKey);
    w.b(d.wholeObject);
    w.i32(d.partIndex);
    w.i32(d.indexStart);
    w.i32(d.indexCount);
    w.floats(d.bboxMin, 3);
    w.floats(d.bboxMax, 3);
}

typedef std::vector<Material> MaterialTable;

void readDraw(Reader &r, DrawCall &d, const DrawRefReader &refs,
              uint32_t version)
{
    refs.material(r, d);
    refs.mesh(r, d.mesh);
    if (version >= 30) {
        d.identity = r.b();
        if (!d.identity)
            r.floats(d.model, 16);
        else
            setIdentity(d.model);
    }
    else {
        r.floats(d.model, 16);
        d.identity = r.b();
    }
    d.objectKey = r.u64();
    d.wholeObject = r.b();
    d.partIndex = r.i32();
    d.indexStart = r.i32();
    d.indexCount = r.i32();
    r.floats(d.bboxMin, 3);
    r.floats(d.bboxMax, 3);
}

void writeDrawList(Writer &w, const DrawCallList &draws,
                   const DrawRefWriter &refs)
{
    w.u32(uint32_t(draws.size()));
    for (const auto &d : draws)
        writeDraw(w, d, refs);
}

/// Serialize every draw's material once, keeping the distinct ones.
/// Equality is the serialized bytes, which is exact by construction —
/// two materials that write the same bytes restore identically — and
/// needs no hand-written comparison over ~60 fields to stay in step
/// with the format.
void collectMaterials(const DrawCallList &draws, const RefWriter &refs,
                      std::vector<std::vector<uint8_t>> &table,
                      std::map<std::vector<uint8_t>, int32_t> &seen,
                      MaterialIndex &matIndex)
{
    for (const auto &d : draws) {
        std::vector<uint8_t> bytes;
        if (!writeChunk(bytes, [&](Writer &cw) {
                writeMaterial(cw, d.material, refs);
            }))
            continue;
        auto it = seen.find(bytes);
        if (it == seen.end()) {
            it = seen.emplace(bytes, int32_t(table.size())).first;
            table.push_back(std::move(bytes));
        }
        matIndex[&d] = it->second;
    }
}

bool readDrawList(Reader &r, DrawCallList &draws, const DrawRefReader &refs,
                  uint32_t version)
{
    uint32_t n = r.u32();
    if (!r.ok || n > 0x1000000u) {
        r.ok = false;
        return false;
    }
    draws.clear();
    for (uint32_t i = 0; r.ok && i < n; ++i) {
        DrawCall d;
        readDraw(r, d, refs, version);
        draws.push_back(std::move(d));
    }
    return r.ok;
}

//////////////////////////////////////////////////////////////////////
// Manifest layout (v33) — docs/SceneStreaming.md §4
//
// No global tables: draws are cut into groups, a group is addressed by
// the hash of its own bytes, and everything a group references is
// either inside it or named by a content key. What that buys is that
// an object nobody touched costs its root entry and nothing else.

/// The unit written and cached: the draws of one object, of one
/// overlay, or of a volatile feed. Pointers, because grouping the
/// scene must not copy every draw (a DrawCall carries a Material) on
/// every publish.
typedef std::vector<const DrawCall *> DrawRefs;

/// Where a group's draws belong once every chunk is in.
struct GroupTarget {
    enum Kind { Scene, Keyless, Overlay, Selection, Highlight };
    int kind = Scene;
    size_t index = 0;
};

/// Backend id for a streamed mesh, derived from its content key. The
/// stream does not carry cacheId — it is a counter, bumped by every
/// re-tessellation whether the geometry changed or not — so the id
/// that keys the GPU upload is taken from the content instead, and an
/// unchanged mesh then skips the upload as well as the fetch. The top
/// bit is set so these can never collide with the ids a bundled
/// snapshot carries inline.
uint64_t meshIdFromKey(const std::string &key)
{
    uint64_t id = 0;
    for (size_t i = 0; i < key.size() && i < 15; ++i) {
        char c = key[i];
        uint64_t v = c >= 'a' ? uint64_t(c - 'a' + 10)
                              : uint64_t(c - '0');
        id = (id << 4) | (v & 0xf);
    }
    return id | 0x8000000000000000ull;
}

//////////////////////////////////////////////////////////////////////
// Manifest writer

/// Per-publish memos. Every leaf is hashed at most once even though
/// the layout has it described once per group that uses it: the
/// shader compile hook is expensive, a material is serialized to be
/// hashed at all, and a texture payload is a copy out of the
/// renderer's image.
struct ManifestWriter {
    const SceneSnapshot *snap = nullptr;
    RefWriter refs;
    std::set<std::string> texSent;
    std::map<const UserShader *, std::pair<std::string, uint32_t>> shaderKeys;
    std::map<std::vector<uint8_t>, std::pair<std::string, uint32_t>> matKeys;
};

void initManifestWriter(ManifestWriter &st, const SceneSnapshot &snap)
{
    st.snap = &snap;
    // A texture is described where it is used — the header repeats per
    // material, the payload does not.
    st.refs.tex = [&st](Writer &w,
                        const std::shared_ptr<const TextureImage> &t) {
        if (!t) {
            w.u8(0);
            return;
        }
        w.u8(1);
        writeTexture(w, *t, st.snap->textureBlobs, &st.texSent);
    };
    st.refs.shader = [&st](Writer &w, const UserShader *s) {
        if (!s) {
            w.u8(0);
            return;
        }
        w.u8(1);
        auto it = st.shaderKeys.find(s);
        if (it == st.shaderKeys.end()) {
            std::vector<uint8_t> chunk;
            writeChunk(chunk, [&st, s](Writer &cw) {
                cw.u32(kChunkVersion);
                writeUserShader(cw, *s, st.snap->shaderBins);
            });
            std::string key = sha1Hex(chunk.data(), chunk.size());
            auto entry = std::make_pair(key, uint32_t(chunk.size()));
            it = st.shaderKeys.emplace(s, entry).first;
            st.snap->chunkBlobs(key, std::move(chunk));
        }
        w.str(it->second.first);
        w.u32(it->second.second);
    };
}

/// The content key of a material, hashing its serialized bytes —
/// exact by construction, where a hand-written comparison over ~60
/// fields would drift out of step with the format.
std::pair<std::string, uint32_t> materialKey(ManifestWriter &st,
                                             const Material &m)
{
    std::vector<uint8_t> chunk;
    if (!writeChunk(chunk, [&st, &m](Writer &cw) {
            cw.u32(kChunkVersion);
            writeMaterial(cw, m, st.refs);
        }))
        return {};
    auto it = st.matKeys.find(chunk);
    if (it != st.matKeys.end())
        return it->second;
    auto entry = std::make_pair(sha1Hex(chunk.data(), chunk.size()),
                                uint32_t(chunk.size()));
    st.matKeys.emplace(chunk, entry);
    st.snap->chunkBlobs(entry.first, std::move(chunk));
    return entry;
}

/// The group's world bounds — what lets a viewer act on a group it
/// does not have yet: frame the model before the first triangle, order
/// the fetch by what is on screen, stand a proxy box in for it. The
/// root carries it per object so it is known before the chunk arrives,
/// and the chunk repeats it so that it stays self-contained.
void groupBBox(const DrawRefs &draws, float bbox[6])
{
    for (int i = 0; i < 3; ++i) {
        bbox[i] = 1e30f;
        bbox[3 + i] = -1e30f;
    }
    for (const DrawCall *d : draws) {
        for (int i = 0; i < 3; ++i) {
            if (d->bboxMin[i] < bbox[i])
                bbox[i] = d->bboxMin[i];
            if (d->bboxMax[i] > bbox[3 + i])
                bbox[3 + i] = d->bboxMax[i];
        }
    }
}

/// One group's payload: the local lists of what its draws reference,
/// then the draws indexing into those lists. Local, so the chunk names
/// nothing by a position outside itself — a global index would make
/// every insertion anywhere renumber, and renumbering invalidates
/// caches that did not change.
void writeGroupChunk(Writer &w, const DrawRefs &draws, uint64_t objectKey,
                     ManifestWriter &st)
{
    std::vector<const MeshData *> meshes;
    MeshIndex meshIndex;
    std::vector<std::pair<std::string, uint32_t>> materials;
    std::map<std::string, int32_t> matLocal;
    std::map<const DrawCall *, int32_t> drawMat;
    for (const DrawCall *d : draws) {
        if (d->mesh
                && meshIndex.emplace(d->mesh.get(),
                                     int32_t(meshes.size())).second)
            meshes.push_back(d->mesh.get());
        auto key = materialKey(st, d->material);
        int32_t local = -1;
        if (!key.first.empty()) {
            auto it = matLocal.find(key.first);
            if (it == matLocal.end()) {
                it = matLocal.emplace(key.first,
                                      int32_t(materials.size())).first;
                materials.push_back(key);
            }
            local = it->second;
        }
        drawMat.emplace(d, local);
    }

    float bbox[6];
    groupBBox(draws, bbox);
    w.u32(kChunkVersion);
    w.u64(objectKey);
    w.floats(bbox, 6);
    w.u32(uint32_t(meshes.size()));
    for (auto *m : meshes)
        writeMesh(w, *m, st.snap->meshBlobs);
    w.u32(uint32_t(materials.size()));
    for (const auto &m : materials) {
        w.str(m.first);
        w.u32(m.second);
    }

    DrawRefWriter drefs;
    drefs.mesh = [&meshIndex](Writer &ww,
                              const std::shared_ptr<const MeshData> &m) {
        auto it = m ? meshIndex.find(m.get()) : meshIndex.end();
        ww.i32(it == meshIndex.end() ? -1 : it->second);
    };
    drefs.material = [&drawMat](Writer &ww, const DrawCall &d) {
        auto it = drawMat.find(&d);
        ww.i32(it == drawMat.end() ? -1 : it->second);
    };
    w.u32(uint32_t(draws.size()));
    for (const DrawCall *d : draws)
        writeDraw(w, *d, drefs);
}

/// A group goes out of band when it is worth caching across publishes
/// — a scene object or an overlay, both of which usually do not change
/// — and inline when it is not. Selection and highlight are per-viewer
/// and change on every pick, and a round trip for a handful of draws
/// costs more than the draws.
/// Build an out-of-band group: serialize it, hash it, hand it over,
/// and report the key and size it is now addressed by. Separate from
/// writing, because the root names a scene object's group in an entry
/// that a delta may or may not go on to send.
bool storeGroup(const DrawRefs &draws, uint64_t objectKey,
                ManifestWriter &st, SceneSnapshot::ObjectEntry &entry)
{
    std::vector<uint8_t> chunk;
    if (!writeChunk(chunk, [&](Writer &cw) {
            writeGroupChunk(cw, draws, objectKey, st);
        }))
        return false;
    entry.objectKey = objectKey;
    groupBBox(draws, entry.bbox);
    entry.key = sha1Hex(chunk.data(), chunk.size());
    entry.size = uint32_t(chunk.size());
    st.snap->chunkBlobs(entry.key, std::move(chunk));
    return true;
}

/// A reference to a group that was stored out of band.
void writeGroupRef(Writer &w, const SceneSnapshot::ObjectEntry &entry)
{
    w.u8(1);
    w.str(entry.key);
    w.u32(entry.size);
}

void writeGroup(Writer &w, const DrawRefs &draws, uint64_t objectKey,
                ManifestWriter &st, bool outOfBand)
{
    if (!outOfBand) {
        w.u8(0);
        writeGroupChunk(w, draws, objectKey, st);
        return;
    }
    SceneSnapshot::ObjectEntry entry;
    if (!storeGroup(draws, objectKey, st, entry)) {
        w.ok = false;
        return;
    }
    writeGroupRef(w, entry);
}

/// The scene feed cut into objects, **in objectKey order**. Order by
/// identity rather than by first appearance, because a delta names
/// only what changed and the consumer reassembles the rest from what
/// it already holds — so the order has to be one both sides can arrive
/// at without being told it, and has to be the same whether a scene
/// came as one full root or as a root and a chain of deltas.
///
/// Draws the producer could not name (objectKey 0) have no identity to
/// cache under and are left for the root to carry inline.
void groupScene(const DrawCallList &scene,
                std::map<uint64_t, DrawRefs> &byKey, DrawRefs &keyless)
{
    for (const auto &d : scene) {
        if (!d.objectKey)
            keyless.push_back(&d);
        else
            byKey[d.objectKey].push_back(&d);
    }
}

/// The object-list section: what this publish retires, then what it
/// carries. One shape for both forms — a full list is the one that
/// retires nothing and carries everything, which is why a consumer
/// needs `baseVersion`, not the section, to tell them apart.
template<typename EntryPtr>
void writeObjectSection(Writer &w,
                        const std::vector<uint64_t> &removed,
                        const std::vector<EntryPtr> &carried)
{
    w.u32(uint32_t(removed.size()));
    for (uint64_t key : removed)
        w.u64(key);
    w.u32(uint32_t(carried.size()));
    for (const auto &ref : carried) {
        const SceneSnapshot::ObjectEntry &e = *ref;
        w.u64(e.objectKey);
        w.floats(e.bbox, 6);
        writeGroupRef(w, e);
    }
}

/// The whole object list: what a viewer needs when it holds nothing,
/// and the only form a bundled or first-connect root can take.
void writeObjectList(Writer &w,
                     const std::vector<SceneSnapshot::ObjectEntry> &entries)
{
    std::vector<const SceneSnapshot::ObjectEntry *> all;
    all.reserve(entries.size());
    for (const auto &e : entries)
        all.push_back(&e);
    // Nothing retired: this list replaces whatever was held.
    writeObjectSection(w, std::vector<uint64_t>(), all);
}

/// Both lists are ordered by objectKey, so the difference is one linear
/// pass. An object is unchanged exactly when its group manifest key is
/// unchanged: the key covers the whole group, bounding box included, so
/// there is nothing else that could have moved.
void diffObjectPtrs(const std::vector<SceneSnapshot::ObjectEntry> &from,
                    const std::vector<SceneSnapshot::ObjectEntry> &to,
                    std::vector<const SceneSnapshot::ObjectEntry *> &changed,
                    std::vector<uint64_t> &removed)
{
    size_t i = 0, j = 0;
    while (i < to.size() || j < from.size()) {
        if (j >= from.size()
                || (i < to.size() && to[i].objectKey < from[j].objectKey)) {
            changed.push_back(&to[i++]);      // new object
        }
        else if (i >= to.size()
                 || from[j].objectKey < to[i].objectKey) {
            removed.push_back(from[j++].objectKey);
        }
        else {
            if (to[i].key != from[j].key)
                changed.push_back(&to[i]);
            ++i;
            ++j;
        }
    }
}

/// The difference against \a base.
void writeObjectDelta(Writer &w,
                      const std::vector<SceneSnapshot::ObjectEntry> &entries,
                      const std::vector<SceneSnapshot::ObjectEntry> &base)
{
    std::vector<uint64_t> removed;
    std::vector<const SceneSnapshot::ObjectEntry *> changed;
    diffObjectPtrs(base, entries, changed, removed);
    writeObjectSection(w, removed, changed);
}

//////////////////////////////////////////////////////////////////////
// Manifest loader

/// Shared across every chunk parse of one snapshot: what has already
/// been slotted, so a leaf two groups both name is fetched, parsed and
/// uploaded once, and where each group's draws belong at the end.
struct ManifestLoader {
    uint32_t version = kVersion;
    std::map<std::string, int32_t> matSlots;
    std::map<std::string, std::shared_ptr<OwnedMeshData>> meshes;
    std::map<std::string, std::shared_ptr<UserShader>> shaders;
    std::map<std::string, std::shared_ptr<TextureImage>> textures;
    std::vector<GroupTarget> targets;
    /// The scene-level user shader lists, held as the shared objects
    /// their chunks will fill rather than copied into the config at
    /// read time — at read time they are still empty.
    std::vector<std::shared_ptr<const UserShader>> postShaders;
    std::vector<std::shared_ptr<const UserShader>> postSplices;
};

typedef std::shared_ptr<ManifestLoader> LoaderPtr;

RefReader manifestRefReader(const LoaderPtr &st, SceneSnapshot &snap);

/// One mesh reference inside a group: the counterpart of writeMesh.
std::shared_ptr<const MeshData> readMeshRef(Reader &r, SceneSnapshot &snap,
                                            const LoaderPtr &st)
{
    if (r.u8() == 0) {
        auto mesh = std::make_shared<OwnedMeshData>();
        mesh->cacheId = r.u64();
        readMeshChunk(r, mesh.get(), st->version);
        return r.ok ? mesh : nullptr;
    }
    std::string key;
    r.str(key, 128);
    uint32_t size = r.u32();
    if (!r.ok)
        return nullptr;
    auto it = st->meshes.find(key);
    if (it != st->meshes.end())
        return it->second;
    auto mesh = std::make_shared<OwnedMeshData>();
    mesh->cacheId = meshIdFromKey(key);
    st->meshes.emplace(key, mesh);
    SceneSnapshot::DeferredChunk c;
    c.key = key;
    c.size = size;
    uint32_t version = st->version;
    c.fill = [mesh, version](SceneSnapshot &, const void *data, size_t size) {
        uint64_t id = mesh->cacheId;
        bool ok = readChunk(data, size, [&mesh, version](Reader &cr) {
            readMeshChunk(cr, mesh.get(), version);
        });
        if (!ok) {
            // A partial parse leaves a vertex count with no arrays
            // behind it, which a backend would read straight past the
            // end of. Empty is the only safe failure.
            *mesh = OwnedMeshData();
        }
        mesh->cacheId = id;
        return ok;
    };
    snap.deferredChunks.push_back(std::move(c));
    return mesh;
}

/// The snapshot slot a material key occupies, deferring its chunk the
/// first time the key is seen. Draws carry the slot rather than the
/// material itself, because the chunk may well arrive after them.
int32_t materialSlot(SceneSnapshot &snap, const LoaderPtr &st,
                     const std::string &key, uint32_t size)
{
    auto it = st->matSlots.find(key);
    if (it != st->matSlots.end())
        return it->second;
    int32_t slot = int32_t(snap.materials.size());
    snap.materials.emplace_back();
    st->matSlots.emplace(key, slot);
    SceneSnapshot::DeferredChunk c;
    c.key = key;
    c.size = size;
    c.fill = [st, slot](SceneSnapshot &target, const void *data, size_t size) {
        return readChunk(data, size, [&](Reader &cr) {
            if (cr.u32() != kChunkVersion) {
                cr.ok = false;
                return;
            }
            if (size_t(slot) >= target.materials.size()) {
                cr.ok = false;
                return;
            }
            RefReader refs = manifestRefReader(st, target);
            readMaterial(cr, target.materials[size_t(slot)], refs,
                         st->version);
        });
    };
    snap.deferredChunks.push_back(std::move(c));
    return slot;
}

RefReader manifestRefReader(const LoaderPtr &st, SceneSnapshot &snap)
{
    RefReader refs;
    refs.tex = [st, &snap](Reader &r,
                           std::shared_ptr<const TextureImage> &t) {
        if (r.u8() == 0)
            return;
        uint32_t size = 0;
        auto tex = readTexture(r, st->version, &size);
        if (!tex)
            return;
        if (!tex->contentKey.empty()) {
            auto it = st->textures.find(tex->contentKey);
            if (it != st->textures.end()) {
                t = it->second;
                return;
            }
            st->textures.emplace(tex->contentKey, tex);
        }
        if (tex->deferred)
            deferTexture(snap, tex, size);
        t = tex;
    };
    refs.shader = [st, &snap](Reader &r,
                              std::shared_ptr<const UserShader> &s) {
        if (r.u8() == 0)
            return;
        std::string key;
        r.str(key, 128);
        uint32_t size = r.u32();
        if (!r.ok)
            return;
        auto it = st->shaders.find(key);
        if (it == st->shaders.end()) {
            auto sh = std::make_shared<UserShader>();
            it = st->shaders.emplace(key, sh).first;
            SceneSnapshot::DeferredChunk c;
            c.key = key;
            c.size = size;
            c.fill = [sh](SceneSnapshot &, const void *data, size_t size) {
                return readChunk(data, size, [&sh](Reader &cr) {
                    if (cr.u32() != kChunkVersion) {
                        cr.ok = false;
                        return;
                    }
                    auto parsed = readUserShader(cr);
                    if (cr.ok && parsed)
                        *sh = *parsed;
                });
            };
            snap.deferredChunks.push_back(std::move(c));
        }
        s = it->second;
    };
    return refs;
}

bool readGroupChunk(Reader &r, DrawCallList &out, SceneSnapshot &snap,
                    const LoaderPtr &st)
{
    if (r.u32() != kChunkVersion) {
        r.ok = false;
        return false;
    }
    r.u64();            // objectKey — the root already carries it
    float bbox[6];
    r.floats(bbox, 6);  // likewise; the chunk repeats it to stay whole
    uint32_t nmesh = r.u32();
    if (!r.ok || nmesh > 0x100000u) {
        r.ok = false;
        return false;
    }
    std::vector<std::shared_ptr<const MeshData>> meshes;
    for (uint32_t i = 0; r.ok && i < nmesh; ++i)
        meshes.push_back(readMeshRef(r, snap, st));
    uint32_t nmat = r.u32();
    if (!r.ok || nmat > 0x100000u) {
        r.ok = false;
        return false;
    }
    std::vector<int32_t> slots;
    for (uint32_t i = 0; r.ok && i < nmat; ++i) {
        std::string key;
        r.str(key, 128);
        uint32_t size = r.u32();
        if (!r.ok)
            break;
        slots.push_back(materialSlot(snap, st, key, size));
    }
    if (!r.ok)
        return false;

    DrawRefReader drefs;
    drefs.mesh = [&meshes](Reader &rr, std::shared_ptr<const MeshData> &m) {
        int32_t i = rr.i32();
        if (i >= 0 && size_t(i) < meshes.size())
            m = meshes[size_t(i)];
    };
    drefs.material = [&slots](Reader &rr, DrawCall &d) {
        int32_t i = rr.i32();
        d.materialIndex = i >= 0 && size_t(i) < slots.size()
            ? slots[size_t(i)] : -1;
    };
    return readDrawList(r, out, drefs, st->version);
}

/// Read one group reference: parse it now when it came inline, or take
/// a slot and defer it when it came as a key. Either way the draws end
/// up at a fixed index in snap.groups, so the order a backend sees does
/// not depend on the order the chunks came back in.
void readGroup(Reader &r, SceneSnapshot &snap, const LoaderPtr &st,
               const GroupTarget &target,
               SceneSnapshot::ObjectEntry *entry = nullptr)
{
    size_t slot = snap.groups.size();
    snap.groups.emplace_back();
    st->targets.push_back(target);
    if (r.u8() == 0) {
        readGroupChunk(r, snap.groups[slot], snap, st);
        return;
    }
    std::string key;
    r.str(key, 128);
    uint32_t size = r.u32();
    if (entry) {
        entry->key = key;
        entry->size = size;
    }
    if (!r.ok)
        return;
    SceneSnapshot::DeferredChunk c;
    c.key = key;
    c.size = size;
    c.fill = [st, slot](SceneSnapshot &target, const void *data, size_t size) {
        if (slot >= target.groups.size())
            return false;
        DrawCallList draws;
        bool ok = readChunk(data, size, [&](Reader &cr) {
            readGroupChunk(cr, draws, target, st);
        });
        if (ok)
            target.groups[slot] = std::move(draws);
        return ok;
    };
    snap.deferredChunks.push_back(std::move(c));
}

/// The pass that runs once every chunk is in: groups into the feeds
/// they belong to, then the materials into the draws that named them
/// by slot.
void setManifestFinalize(SceneSnapshot &snap, const LoaderPtr &st)
{
    snap.finalize = [st](SceneSnapshot &s) {
        // The post-stage shaders, now that their chunks have landed.
        s.usershaderconf.shaders.clear();
        for (const auto &sh : st->postShaders)
            s.usershaderconf.shaders.push_back(*sh);
        s.usershaderconf.splices.clear();
        for (const auto &sh : st->postSplices)
            s.usershaderconf.splices.push_back(*sh);

        // Everything but the scene objects goes to its feed. The
        // objects stay where they are: a delta carries only the ones
        // that changed, and assembling the feed needs the ones it did
        // not carry too — which only a consumer holding a model across
        // publishes has (applySceneObjects).
        s.scene.clear();
        for (size_t i = 0; i < st->targets.size() && i < s.groups.size(); ++i) {
            const GroupTarget &t = st->targets[i];
            DrawCallList &draws = s.groups[i];
            switch (t.kind) {
            case GroupTarget::Scene:
                break;
            case GroupTarget::Keyless:
                s.scene = std::move(draws);
                break;
            case GroupTarget::Overlay:
                if (t.index < s.overlays.size())
                    s.overlays[t.index].draws = std::move(draws);
                break;
            case GroupTarget::Selection:
                if (t.index < s.selections.size())
                    s.selections[t.index].second = std::move(draws);
                break;
            case GroupTarget::Highlight:
                s.highlight = std::move(draws);
                break;
            }
        }

        auto apply = [&s](DrawCallList &draws) {
            for (auto &d : draws) {
                if (d.materialIndex >= 0
                        && size_t(d.materialIndex) < s.materials.size())
                    d.material = s.materials[size_t(d.materialIndex)];
            }
        };
        apply(s.scene);
        apply(s.highlight);
        // The object groups are patched in place, where they wait for
        // the model to take them.
        for (auto &group : s.groups)
            apply(group);
        for (auto &sel : s.selections)
            apply(sel.second);
        for (auto &ov : s.overlays)
            apply(ov.draws);
    };
}


} // anonymous namespace

//////////////////////////////////////////////////////////////////////

std::string Render::sha1Hex(const void *data, size_t size)
{
    return ::sha1Hex(static_cast<const uint8_t *>(data), size);
}

uint32_t Render::sceneDumpVersion()
{
    return kVersion;
}

uint32_t Render::sceneSnapshotVersion(const void *data, size_t size)
{
    if (!data || size < 8)
        return 0;
    uint32_t magic, version;
    std::memcpy(&magic, data, 4);
    std::memcpy(&version, static_cast<const char *>(data) + 4, 4);
    return magic == kMagic ? version : 0;
}

static bool saveSnapshotFp(FILE *fp, const SceneSnapshot &snap)
{
    Writer w;
    w.fp = fp;
    w.u32(kMagic);
    w.u32(kVersion);

    // v33: which of the two layouts follows. Having somewhere to hand
    // chunks is what decides it — a bundled capture has nowhere, and
    // has to stay one self-contained document.
    const bool manifest = bool(snap.chunkBlobs);
    w.u8(manifest ? 1 : 0);
    if (manifest) {
        // v34: which publish this is, and which one its object list is
        // a difference against (0 = none, the list is complete). v35
        // adds the run those numbers belong to.
        w.u64(snap.manifestVersion);
        if (snap.rootSpans)
            snap.rootSpans->baseVersionAt = w.pos();
        w.u64(snap.baseVersion);
        w.u64(snap.sessionId);
    }

    // Unique mesh and texture tables referenced by index from the
    // draws. Monolithic layout only: the manifest layout has no global
    // tables at all, by design (docs/SceneStreaming.md §3).
    MeshIndex meshIndex;
    std::vector<const MeshData *> meshes;
    TextureIndex texIndex;
    std::vector<const TextureImage *> textures;
    auto addTex = [&](const std::shared_ptr<const TextureImage> &t) {
        if (t && texIndex.emplace(t.get(),
                                  int32_t(textures.size())).second)
            textures.push_back(t.get());
    };
    // Unique user shader table (v23): material-stage shaders from the
    // draws plus the scene-level post-stage list.
    ShaderIndex shaderIndex;
    std::vector<const UserShader *> shaders;
    auto addShader = [&](const UserShader *s) {
        if (s && shaderIndex.emplace(s, int32_t(shaders.size())).second)
            shaders.push_back(s);
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
            addShader(d.material.usershader.get());
        }
    };
    // How the records below place their references, and how a feed's
    // draws are written: the one difference between the two layouts,
    // so that everything after this point is shared.
    RefWriter refs;
    ManifestWriter mst;
    MaterialIndex matIndex;
    DrawRefWriter drefs;
    auto writeFeed = [&](const DrawCallList &draws, uint64_t objectKey,
                         bool outOfBand) {
        if (!manifest) {
            writeDrawList(w, draws, drefs);
            return;
        }
        DrawRefs refsList;
        refsList.reserve(draws.size());
        for (const auto &d : draws)
            refsList.push_back(&d);
        writeGroup(w, refsList, objectKey, mst, outOfBand);
    };

    if (manifest) {
        initManifestWriter(mst, snap);
        refs = mst.refs;
        // The scene-level user shader lists, by content key rather
        // than by table index.
        w.u32(uint32_t(snap.usershaderconf.shaders.size()));
        for (const auto &s : snap.usershaderconf.shaders)
            refs.shader(w, &s);
        w.u32(uint32_t(snap.usershaderconf.splices.size()));
        for (const auto &s : snap.usershaderconf.splices)
            refs.shader(w, &s);

        // The scene cut into one content-keyed group per object.
        // Draws the producer could not name have no identity to cache
        // under and ride along in a group of their own, written inline.
        std::map<uint64_t, DrawRefs> byKey;
        DrawRefs keyless;
        groupScene(snap.scene, byKey, keyless);

        // Every group is built either way — it has to be, to know
        // whether it changed — but a delta only *sends* the entries
        // that moved, and the chunk sink only takes bytes the
        // publisher does not already hold.
        std::vector<SceneSnapshot::ObjectEntry> entries;
        entries.reserve(byKey.size());
        for (const auto &group : byKey) {
            SceneSnapshot::ObjectEntry entry;
            if (!storeGroup(group.second, group.first, mst, entry)) {
                w.ok = false;
                break;
            }
            entries.push_back(std::move(entry));
        }

        if (snap.rootSpans)
            snap.rootSpans->listBegin = w.pos();
        if (!snap.baseVersion)
            writeObjectList(w, entries);
        else
            writeObjectDelta(w, entries, snap.baseObjects);
        if (snap.rootSpans)
            snap.rootSpans->listEnd = w.pos();
        if (snap.objectEntries)
            *snap.objectEntries = entries;

        writeGroup(w, keyless, 0, mst, false);
    }
    else {
        addDraws(snap.scene);
        for (const auto &sel : snap.selections)
            addDraws(sel.second);
        addDraws(snap.highlight);
        for (const auto &ov : snap.overlays)
            addDraws(ov.draws);
        addTex(snap.pbrconf.envImage);
        addTex(snap.hatch);
        addTex(snap.lightconf.groundTexture);
        addTex(snap.lightconf.groundBumpMap);
        for (const auto &s : snap.usershaderconf.shaders)
            addShader(&s);
        for (const auto &s : snap.usershaderconf.splices)
            addShader(&s);

        w.u32(uint32_t(meshes.size()));
        for (auto *m : meshes)
            writeMesh(w, *m, snap.meshBlobs);
        w.u32(uint32_t(textures.size()));
        for (auto *t : textures)
            writeTexture(w, *t, snap.textureBlobs);

        // v23: the user shader table (before the draws that reference
        // it) + the post-stage config list as table indices.
        w.u32(uint32_t(shaders.size()));
        for (auto *s : shaders)
            writeUserShader(w, *s, snap.shaderBins);
        w.u32(uint32_t(snap.usershaderconf.shaders.size()));
        for (const auto &s : snap.usershaderconf.shaders)
            w.i32(shaderIndex[&s]);
        // v24: the assembled volume-splice variants as table indices.
        w.u32(uint32_t(snap.usershaderconf.splices.size()));
        for (const auto &s : snap.usershaderconf.splices)
            w.i32(shaderIndex[&s]);

        refs = tableRefWriter(texIndex, shaderIndex);

        // v29: the deduplicated material table, ahead of every draw
        // list that indexes into it. Materials reference the texture
        // and shader tables, so it has to follow those.
        std::vector<std::vector<uint8_t>> table;
        std::map<std::vector<uint8_t>, int32_t> seen;
        auto collect = [&](const DrawCallList &draws) {
            collectMaterials(draws, refs, table, seen, matIndex);
        };
        collect(snap.scene);
        for (const auto &sel : snap.selections)
            collect(sel.second);
        collect(snap.highlight);
        for (const auto &ov : snap.overlays)
            collect(ov.draws);
        w.u8(0);
        w.u32(kChunkVersion);
        w.u32(uint32_t(table.size()));
        for (const auto &bytes : table)
            w.raw(bytes.data(), bytes.size());

        drefs.mesh = [&meshIndex](Writer &ww,
                                  const std::shared_ptr<const MeshData> &m) {
            auto it = m ? meshIndex.find(m.get()) : meshIndex.end();
            ww.i32(it == meshIndex.end() ? -1 : it->second);
        };
        drefs.material = [&matIndex](Writer &ww, const DrawCall &d) {
            auto it = matIndex.find(&d);
            ww.i32(it == matIndex.end() ? -1 : it->second);
        };

        writeDrawList(w, snap.scene, drefs);
    }

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
    w.i32(snap.aoconf.slices);
    w.i32(snap.aoconf.steps);

    w.b(snap.pbrconf.enabled);
    w.f(snap.pbrconf.metallic);
    w.f(snap.pbrconf.roughness);
    w.f(snap.pbrconf.envIntensity);
    w.b(snap.pbrconf.envBackground);
    refs.tex(w, snap.pbrconf.envImage);

    w.f(snap.bumpconf.scale);
    w.b(snap.bumpconf.parallax);

    writeLight(w, snap.lightconf, refs);

    const VolumetricConfig &vc = snap.volconf;
    w.b(vc.enabled); w.f(vc.intensity); w.f(vc.density);
    w.b(vc.caustics); w.f(vc.causticsIntensity); w.f(vc.causticsScale);
    w.f(vc.causticsSpeed);

    const WaterConfig &wc = snap.waterconf;
    w.b(wc.enabled); w.f(wc.waveStrength); w.f(wc.waveScale);
    w.f(wc.waveSpeed);
    // v15: the rest of WaterConfig (was defaulted on the viewer side).
    w.f(wc.absorption); w.f(wc.inscatter);
    w.b(wc.refraction); w.b(wc.reflection); w.b(wc.planarReflection);
    w.b(wc.shadow); w.f(wc.shadowWobble);
    // v16: ripple type + rain drop density.
    w.i32(wc.rippleType); w.f(wc.rippleDensity);

    // v18: bloom.
    const BloomConfig &blc = snap.bloomconf;
    w.b(blc.enabled); w.f(blc.threshold); w.f(blc.intensity);
    w.f(blc.radius);

    w.f(snap.autozoomScale);
    w.f(snap.effectResolution);
    w.f(snap.ssaoResolution);

    // v27: the hatch image as a texture-table index (v26 and older
    // wrote its size and pixels inline here).
    refs.tex(w, snap.hatch);

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
        writeFeed(sel.second, 0, false);
    }
    writeFeed(snap.highlight, 0, false);
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
        writeFeed(ov.draws, 0, true);
    }

    // v10: preselection + selection highlight config (client-side styling).
    for (const PreselHighlightConfig *c : {&snap.preselconf, &snap.selconf}) {
        w.u32(c->color);
        w.f(c->outlineWidth);
        w.b(c->faceOutline);
        w.b(c->outlineOnly);
        w.f(c->pickRadius);  // v11
    }

    // v20: render debugging config (docs/RenderDebug.md).
    w.i32(snap.debugconf.viewMode);
    w.b(snap.debugconf.freezeFrame);

    // v21: dynamically bound named shader parameters (§2.5).
    w.u32(uint32_t(snap.debugconf.userParams.size()));
    for (const auto &p : snap.debugconf.userParams) {
        w.u32(uint32_t(p.name.size()));
        w.raw(p.name.data(), p.name.size());
        w.u32(uint32_t(p.values.size()));
        w.floats(p.values.data(), p.values.size());
    }

    return w.ok;
}

/// The monolithic layout's global tables, then the scene draw list
/// that indexes into them. A bundled capture is the only thing that
/// still writes this, but every older snapshot on disk is one too, so
/// it reads every version back to 1.
void loadMonolithicTables(Reader &r, SceneSnapshot &snap, uint32_t version,
                          MeshTable &meshes, TextureTable &textures,
                          ShaderTable &shaders, MaterialTable &materials,
                          RefReader &refs, DrawRefReader &drefs)
{
    uint32_t nmesh = r.u32();
    if (nmesh > 0x100000u)
        r.ok = false;
    for (uint32_t i = 0; r.ok && i < nmesh; ++i)
        meshes.push_back(readMesh(r, version, snap));
    uint32_t ntex = r.u32();
    if (ntex > 0x100000u)
        r.ok = false;
    for (uint32_t i = 0; r.ok && i < ntex; ++i) {
        uint32_t size = 0;
        auto tex = readTexture(r, version, &size);
        if (tex && tex->deferred)
            deferTexture(snap, tex, size);
        textures.push_back(tex);
    }

    // v23: user shader table + post-stage list (see saveSnapshotFp).
    snap.usershaderconf = UserShaderConfig();
    if (version >= 23) {
        uint32_t ns = r.u32();
        if (!r.ok || ns > 0x10000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < ns; ++i)
            shaders.push_back(readUserShader(r));
        uint32_t npost = r.u32();
        if (!r.ok || npost > 0x10000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < npost; ++i) {
            int32_t si = r.i32();
            if (si >= 0 && size_t(si) < shaders.size())
                snap.usershaderconf.shaders.push_back(*shaders[size_t(si)]);
        }
        // v24: assembled volume-splice variants (adopted by source
        // match in the compiler-less tiers).
        if (version >= 24) {
            uint32_t nspl = r.u32();
            if (!r.ok || nspl > 0x10000u)
                r.ok = false;
            for (uint32_t i = 0; r.ok && i < nspl; ++i) {
                int32_t si = r.i32();
                if (si >= 0 && size_t(si) < shaders.size())
                    snap.usershaderconf.splices.push_back(
                        *shaders[size_t(si)]);
            }
        }
    }

    // The tables are complete; the records below reference them.
    refs = tableRefReader(textures, shaders);

    if (version >= 29) {
        // v31/v32 served this table out of band on the streaming path.
        // The manifest layout keys materials individually instead, so
        // nothing writes that form any more and a bundled capture,
        // which is all this layout is now used for, never did.
        if (version >= 31 && r.u8() != 0) {
            r.ok = false;
            return;
        }
        // The table is the same chunk either way, so it carries the
        // same version field (v32) inline as it did as a blob.
        if (version >= 32 && r.u32() != kChunkVersion)
            r.ok = false;
        uint32_t nmat = r.u32();
        if (!r.ok || nmat > 0x1000000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < nmat; ++i) {
            materials.emplace_back();
            readMaterial(r, materials.back(), refs, version);
        }
    }

    drefs.mesh = [&meshes](Reader &rr, std::shared_ptr<const MeshData> &m) {
        int32_t i = rr.i32();
        if (i >= 0 && size_t(i) < meshes.size())
            m = meshes[size_t(i)];
    };
    drefs.material = [&materials, version, &refs](Reader &rr, DrawCall &d) {
        if (version < 29) {
            readMaterial(rr, d.material, refs, version);
            return;
        }
        d.materialIndex = rr.i32();
        if (d.materialIndex >= 0 && size_t(d.materialIndex) < materials.size())
            d.material = materials[size_t(d.materialIndex)];
    };

    readDrawList(r, snap.scene, drefs, version);
}

static bool loadSnapshotFp(FILE *fp, SceneSnapshot &snap)
{
    Reader r;
    r.fp = fp;
    uint32_t magic = r.u32();
    uint32_t version = r.u32();
    if (magic != kMagic || version < 1 || version > kVersion)
        return false;
    const bool manifest = version >= 33 && r.u8() != 0;
    snap.manifestVersion = 0;
    snap.baseVersion = 0;
    snap.sessionId = 0;
    if (manifest && version >= 34) {
        snap.manifestVersion = r.u64();
        snap.baseVersion = r.u64();
        if (version >= 35)
            snap.sessionId = r.u64();
    }

    snap.objectUpdates.clear();
    snap.objectsRemoved.clear();
    snap.deferredChunks.clear();
    snap.groups.clear();
    snap.materials.clear();
    snap.finalize = nullptr;

    // Set by whichever layout follows; everything after the feeds is
    // shared and goes through these.
    RefReader refs;
    DrawRefReader drefs;
    LoaderPtr st;
    MeshTable meshes;
    TextureTable textures;
    ShaderTable shaders;
    MaterialTable materials;

    if (manifest) {
        st = std::make_shared<ManifestLoader>();
        st->version = version;
        refs = manifestRefReader(st, snap);
        snap.usershaderconf = UserShaderConfig();
        uint32_t npost = r.u32();
        if (!r.ok || npost > 0x10000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < npost; ++i) {
            std::shared_ptr<const UserShader> s;
            refs.shader(r, s);
            if (s)
                st->postShaders.push_back(s);
        }
        uint32_t nspl = r.u32();
        if (!r.ok || nspl > 0x10000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < nspl; ++i) {
            std::shared_ptr<const UserShader> s;
            refs.shader(r, s);
            if (s)
                st->postSplices.push_back(s);
        }

        // v34: what this publish retires, then what it carries. A full
        // root retires nothing and carries everything.
        uint32_t nremoved = version >= 34 ? r.u32() : 0;
        if (!r.ok || nremoved > 0x1000000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < nremoved; ++i)
            snap.objectsRemoved.push_back(r.u64());

        uint32_t nobj = r.u32();
        if (!r.ok || nobj > 0x1000000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < nobj; ++i) {
            SceneSnapshot::ObjectUpdate up;
            up.entry.objectKey = r.u64();
            r.floats(up.entry.bbox, 6);
            up.group = snap.groups.size();
            GroupTarget t;
            t.kind = GroupTarget::Scene;
            // The group's key is what readGroup consumes next; keep it
            // so the model can tell an unchanged object from a changed
            // one without re-reading the chunk.
            readGroup(r, snap, st, t, &up.entry);
            snap.objectUpdates.push_back(std::move(up));
        }
        if (r.ok) {
            GroupTarget t;
            t.kind = GroupTarget::Keyless;
            readGroup(r, snap, st, t);   // the draws with no objectKey
        }
        setManifestFinalize(snap, st);
    }
    else
        loadMonolithicTables(r, snap, version, meshes, textures, shaders,
                             materials, refs, drefs);

    // The feeds that follow the configs. In the monolithic layout they
    // are draw lists indexing the global tables; in the manifest one
    // they are groups, whose draws land in a fixed slot and are moved
    // into place by finalize() once every chunk is in — so that the
    // order a backend sees never depends on the order chunks arrived.
    auto readFeed = [&](DrawCallList &target, int kind, size_t index) {
        if (!manifest) {
            readDrawList(r, target, drefs, version);
            return;
        }
        GroupTarget t;
        t.kind = kind;
        t.index = index;
        readGroup(r, snap, st, t);
    };


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
    snap.aoconf.slices = version >= 14 ? r.i32() : 0;
    snap.aoconf.steps = version >= 14 ? r.i32() : 0;

    snap.pbrconf.enabled = r.b();
    snap.pbrconf.metallic = r.f();
    snap.pbrconf.roughness = r.f();
    snap.pbrconf.envIntensity = r.f();
    snap.pbrconf.envBackground = version >= 25 ? r.b() : false;
    if (version >= 25)
        refs.tex(r, snap.pbrconf.envImage);

    snap.bumpconf.scale = r.f();
    snap.bumpconf.parallax = r.b();

    readLight(r, snap.lightconf, refs, version);

    VolumetricConfig &vc = snap.volconf;
    vc.enabled = r.b(); vc.intensity = r.f(); vc.density = r.f();
    vc.caustics = r.b(); vc.causticsIntensity = r.f();
    vc.causticsScale = r.f(); vc.causticsSpeed = r.f();

    WaterConfig &wc = snap.waterconf;
    wc.enabled = r.b(); wc.waveStrength = r.f(); wc.waveScale = r.f();
    wc.waveSpeed = r.f();
    if (version >= 15) {
        wc.absorption = r.f(); wc.inscatter = r.f();
        wc.refraction = r.b(); wc.reflection = r.b();
        wc.planarReflection = r.b();
        wc.shadow = r.b(); wc.shadowWobble = r.f();
    }
    if (version >= 16) {
        wc.rippleType = r.i32(); wc.rippleDensity = r.f();
    }

    snap.bloomconf = BloomConfig();
    if (version >= 18) {
        BloomConfig &blc = snap.bloomconf;
        blc.enabled = r.b(); blc.threshold = r.f();
        blc.intensity = r.f(); blc.radius = r.f();
    }

    snap.autozoomScale = r.f();
    snap.effectResolution = version >= 9 ? r.f() : 1.0f;
    snap.ssaoResolution = version >= 12 ? r.f() : 1.0f;

    snap.hatch.reset();
    if (version >= 27)
        refs.tex(r, snap.hatch);
    else {
        // v26 and older: width, height and the pixels written inline.
        auto hatch = std::make_shared<TextureImage>();
        hatch->width = r.i32();
        hatch->height = r.i32();
        hatch->numComponents = 4;
        uint32_t nhatch = r.u32();
        if (nhatch > 0x10000000u)
            r.ok = false;
        if (r.ok) {
            hatch->pixels.resize(nhatch);
            r.raw(hatch->pixels.data(), nhatch);
        }
        if (r.ok && nhatch)
            snap.hatch = hatch;
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
            snap.selections.emplace_back(id, DrawCallList());
            readFeed(snap.selections.back().second, GroupTarget::Selection,
                     snap.selections.size() - 1);
        }
        readFeed(snap.highlight, GroupTarget::Highlight, 0);
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
            snap.overlays.push_back(std::move(ov));
            readFeed(snap.overlays.back().draws, GroupTarget::Overlay,
                     snap.overlays.size() - 1);
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

    snap.debugconf = RenderDebugConfig();
    if (version >= 20) {
        snap.debugconf.viewMode = r.i32();
        snap.debugconf.freezeFrame = r.b();
    }
    if (version >= 21) {
        uint32_t n = r.u32();
        if (n > 0x10000u)
            r.ok = false;
        for (uint32_t i = 0; r.ok && i < n; ++i) {
            RenderDebugConfig::UserParam p;
            uint32_t len = r.u32();
            if (!r.ok || len > 0x1000u) { r.ok = false; break; }
            p.name.resize(len);
            r.raw(&p.name[0], len);
            uint32_t nv = r.u32();
            if (!r.ok || nv > 0x10000u) { r.ok = false; break; }
            p.values.resize(nv);
            r.floats(p.values.data(), nv);
            snap.debugconf.userParams.push_back(std::move(p));
        }
    }

    return r.ok;
}

bool Render::applySceneObjects(SceneSnapshot &snap, SceneObjectModel &model)
{
    if (snap.baseVersion && snap.baseVersion != model.version) {
        // A difference against something this consumer does not hold.
        // There is nothing to be salvaged from it — the entries it does
        // not mention are exactly the ones it assumes are already
        // right — so the caller has to be given a full root.
        return false;
    }
    if (!snap.baseVersion)
        model.objects.clear();      // a complete list replaces the model
    for (uint64_t key : snap.objectsRemoved)
        model.objects.erase(key);
    for (auto &up : snap.objectUpdates) {
        auto &obj = model.objects[up.entry.objectKey];
        obj.entry = up.entry;
        if (up.group < snap.groups.size())
            obj.draws = std::move(snap.groups[up.group]);
    }
    model.version = snap.manifestVersion;

    // The feed, in objectKey order, then the draws the producer could
    // not name — which finalize() left in `scene` because they belong
    // to no object and are re-sent whole every publish.
    DrawCallList scene;
    for (const auto &entry : model.objects) {
        const DrawCallList &draws = entry.second.draws;
        scene.insert(scene.end(), draws.begin(), draws.end());
    }
    scene.insert(scene.end(), std::make_move_iterator(snap.scene.begin()),
                 std::make_move_iterator(snap.scene.end()));
    snap.scene = std::move(scene);
    return true;
}

void Render::diffObjectLists(
        const std::vector<SceneSnapshot::ObjectEntry> &from,
        const std::vector<SceneSnapshot::ObjectEntry> &to,
        std::vector<SceneSnapshot::ObjectEntry> &changed,
        std::vector<uint64_t> &removed)
{
    std::vector<const SceneSnapshot::ObjectEntry *> refs;
    diffObjectPtrs(from, to, refs, removed);
    changed.reserve(changed.size() + refs.size());
    for (const auto *e : refs)
        changed.push_back(*e);
}

bool Render::spliceObjectDelta(
        const std::vector<uint8_t> &full,
        const SceneSnapshot::RootSpans &spans,
        uint64_t baseVersion,
        const std::vector<SceneSnapshot::ObjectEntry> &changed,
        const std::vector<uint64_t> &removed,
        std::vector<uint8_t> &out)
{
    // A splice is only ever as sound as the spans, and they come from a
    // different call than this one: refuse rather than produce a
    // payload that parses into nonsense.
    if (spans.listEnd < spans.listBegin || spans.listEnd > full.size()
            || spans.baseVersionAt + sizeof(uint64_t) > spans.listBegin
            || !baseVersion)
        return false;

    std::vector<uint8_t> section;
    Writer w;
    w.vec = &section;
    std::vector<const SceneSnapshot::ObjectEntry *> refs;
    refs.reserve(changed.size());
    for (const auto &e : changed)
        refs.push_back(&e);
    writeObjectSection(w, removed, refs);
    if (!w.ok)
        return false;

    out.clear();
    out.reserve(full.size() - (spans.listEnd - spans.listBegin)
                + section.size());
    out.insert(out.end(), full.begin(), full.begin() + spans.listBegin);
    out.insert(out.end(), section.begin(), section.end());
    out.insert(out.end(), full.begin() + spans.listEnd, full.end());
    // The header still says the list is complete; it no longer is.
    std::memcpy(out.data() + spans.baseVersionAt, &baseVersion,
                sizeof(baseVersion));
    return true;
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

namespace {

bool readChunk(const void *data, size_t size,
               const std::function<void(Reader &)> &fn)
{
    FILE *fp = std::tmpfile();
    if (!fp)
        return false;
    bool ok = std::fwrite(data, 1, size, fp) == size
        && std::fseek(fp, 0, SEEK_SET) == 0;
    if (ok) {
        Reader r;
        r.fp = fp;
        fn(r);
        ok = r.ok;
    }
    std::fclose(fp);
    return ok;
}

} // namespace

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

namespace {

bool readChunk(const void *data, size_t size,
               const std::function<void(Reader &)> &fn)
{
    FILE *fp = fmemopen(const_cast<void *>(data), size, "rb");
    if (!fp)
        return false;
    Reader r;
    r.fp = fp;
    fn(r);
    bool ok = r.ok;
    std::fclose(fp);
    return ok;
}

} // namespace

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

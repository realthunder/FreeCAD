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
#include "ImageDecode.h"
#include "MeshSimplify.h"
#include "SceneLadder.h"

#include <algorithm>
#include <cstddef>
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
// 36: a deferred mesh reference is a ladder of levels, coarsest first
//     and ending with the exact mesh (docs/SceneStreaming.md §7). A
//     level states its error relative to the mesh diagonal and,
//     optionally, its key — **absent means declared possible but not
//     generated**, a request for work rather than for bytes. Meshes
//     over a size threshold declare coarser levels ahead of any
//     generator existing; the consumer records the ladder and still
//     fetches the finest built level, which today is always the exact
//     mesh, so behaviour is unchanged until slices 3 and 4 land.
// 37: a delta's object section carries each changed object's group
//     manifest INLINE after its reference (u8 flag, then the chunk
//     bytes). A delta's manifests are new by definition — the object
//     is in the delta exactly because its manifest key changed — so no
//     cache ever answers for them, and fetching them by round trip is
//     what a delta's staging used to be gated on. The consumer ingests
//     the bytes under their key and stages unconditionally. Full
//     roots and leaves stay by reference (their keys usually ARE
//     cached).
// 38: each object entry in the root/delta object section carries the
//     document identity of its object (doc, object internal name,
//     label, type — four strings after the bbox), so a viewer can name
//     what it picks without a round trip (docs/ThinClient.md §4.1).
//     Outside the group chunk on purpose: identity must not disturb
//     content keys.
// 39: a user shader carries its particle state step (simulateSource)
//     and the step's compiled viewer binary (Compiled::simBin) — the
//     stateful particle tier (docs/RenderEngine.md §5.8). Both the
//     inline shader table and the out-of-band shader chunk gained the
//     fields, so kChunkVersion moves with it.
// 40: WaterConfig carries the impact-ring strength and lifetime — the
//     droplet rings the particle impact map drives
//     (docs/RenderEngine.md §5.8). Without them a viewer defaulted the
//     strength to 1 and rang the water whatever the property said.
// 41: the presel/sel highlight configs carry loupeLift
//     (ViewParams::TouchLoupeLift) — how far above the fingertip the
//     touch loupe picks (docs/ThinClientUI.md).
// 42: CavityConfig — screen-space cavity (curvature) shading, the
//     prepass-normal darkening applied to the opaque scene
//     (docs/RenderEngine.md). A viewer older than this defaults it off,
//     which is the pre-feature look.
// 43: MatcapConfig — matcap shading, the camera-fixed studio that
//     replaces the scene lighting (docs/RenderEngine.md). A viewer
//     older than this defaults it off, which is the pre-feature look.
// 44: CavityConfig carries its radius — the pixel baseline the
//     curvature is measured over. A viewer older than this measures
//     over one pixel whatever the property says, which reads as the
//     effect barely being there on anything but a hard crease.
// 45: Material carries its SoDrawStyleElement style, which is how the
//     Tessellation draw style reaches the backend at all. A viewer
//     older than this fills the faces, i.e. shows Shaded instead.
// 46: LightConfig carries the ground's sizing and placement --
//     explicit half extents, an explicit position, and the placement
//     matrix -- which the Coin quad has always honoured and the backend
//     sized from the scene bounding box alone. A viewer older than this
//     keeps doing that, i.e. ignores an explicitly sized or moved
//     ground rather than misplacing one.
// 47: Per-face material -- a mesh chunk may carry the baked
//     emissive/specular/shininess stream (flags bit 8) and Material
//     carries the perfacematerial flag selecting it. A viewer older
//     than this shades such draws with the material scalars, which is
//     the pre-feature look.
// 48: Per-face PBR -- Material carries the perfacepbr flag saying that
//     the stream of v47 spends its two alpha slots on the metallic and
//     roughness factors instead of a constant and the shininess. A
//     viewer older than this reads the roughness slot as a shininess
//     (and never the metallic), i.e. shades those faces as the uniform
//     PBR object they were before.
// 49: Machined surface finish -- Material carries the App::SurfaceFinish
//     pattern with its pitch, depth and lay angle, which the backend
//     shades as a procedural normal perturbation. A viewer older than
//     this draws the plain surface, i.e. the pre-feature look.
// 50: Per-face surface finish -- the per-vertex material stream widens
//     from 8 to 12 bytes, the third slot carrying an index into the
//     draw material's new finish palette (the distinct finishes of a
//     per-face-finished object). A viewer older than this cannot read
//     the stream at all, which is why the mesh chunk revision moves
//     with it.
// 51: Surface finish projection frames -- the material carries the frame
//     each face's finish is laid out in (the plane's own axes, or the
//     axis a cylinder was turned about), and the material stream's
//     third slot names one in its SECOND byte. The stride does not
//     change: that byte was reserved and read as zero, which is the
//     unframed frame, so an older viewer draws the triplanar projection
//     this replaces rather than misreading anything.
// 52: The ordinary Coin lights (ViewLightConfig) -- the viewer's
//     headlight and backlight and any document directional/point light,
//     which until now never left the Coin side at all and were stood in
//     for by a hard-coded white headlight. An older snapshot carries
//     none, and its `fed` reads false, which is the flag that asks a
//     backend for exactly that stand-in: old dumps render unchanged.
// 53: ViewLightConfig also carries the traversal's global ambient
//     (SoEnvironment ambientColor * ambientIntensity). With it the
//     ambient term becomes Coin's -- the material's own ambient colour
//     times this -- instead of a flat fraction of the diffuse. A v52
//     snapshot has no such field and its `fed` still selects the
//     legacy floor, so it renders as it did.
// 54: A ViewLight may be a spot -- an SoSpotLight past the one the
//     scene light claims, carrying Coin's cutOffAngle and dropOffRate
//     beside the position and attenuation it already had. A v53
//     snapshot has no such field and its lights read as the plain
//     directional/positional ones they were, which is what a writer of
//     that version could produce anyway.
// 55: an object says whether the producer held part of it back --
//     DrawCall::objectIncomplete, until now publish-transient and never
//     serialized (docs/SceneStreaming.md #13b). The element contract
//     needs it to tell a LATE companion draw (its capture deferred by
//     the publish budget) from an ABSENT one (a display mode drawing
//     points or edges as its own subject): the first is waited for, the
//     second is exempt. A consumer without it grants the exemption to
//     every object, which is the dots-first load storm in the browser.
//     It rides the object entry in the manifest root, beside the
//     identity of v38 and outside the group chunk for the same reason
//     -- it is a property of the publish, not of the geometry, and
//     folding it into a content key would retire an object's cached
//     chunks every time the producer's capture backlog drained. The
//     monolithic layout has no object section, so a bundled capture
//     carries the keys as a list of their own after the scene draws.
// 56: a ViewLight says whether it is CAMERA-RELATIVE, and carries the
//     eye-space direction the world-space one was unwound from. The
//     unwinding is only valid for the camera that did it, and a streamed
//     viewer's camera is its own -- so a headlight arrived as a fixed
//     world direction pointing wherever the producer happened to look,
//     which for a headless serving process is a default down -Z: every
//     surface facing the viewer rendered at ambient (measured: mean
//     luminance 20 against the desktop's 108 on the same model). A v55
//     snapshot has no flag, its lights stay world-space, and it renders
//     exactly as it did.
// 57: a material says whether its LINES draw style is the scene-wide
//     override -- the Tessellation display mode -- or a plain
//     SoDrawStyle node asking for a wireframe. Only the mode wants the
//     faces filled in the background colour to occlude; filling a
//     wireframe hides what it was drawn around. A v56 snapshot has no
//     flag and every wireframe reads as the mode, which is the only
//     thing a writer of that version distinguished.
// 58: LightConfig carries the ground's shading and back-face culling
//     (ShadowGroundShading / ShadowGroundBackFaceCull), the two ground
//     properties Coin spends on nodes above the quad -- an SoLightModel
//     and an SoShapeHints -- rather than on the quad, and which the
//     ported ground therefore never saw. A viewer older than this
//     draws the lit two-sided quad it always did; a snapshot older
//     than this reads as both knobs on, which is what the properties
//     it was written from defaulted to.
// 59: PBRConfig says whether a Phong specular colour is read as PBR
//     material data where nothing states a metalness (PBRFromSpecular).
//     A snapshot older than this was written by a build that always
//     dropped that colour, so it reads as off and renders as it did.
// 65: a material carries a per-face TEXTURE palette -- the images its
//     individual faces are painted with, which the backend uploads as
//     the layers of one array texture -- with the tile size they are
//     laid out at and the layer a draw with no per-vertex stream reads.
//     A snapshot older than this has no palette, and its faces are
//     textured the one way they always were.
// 66: the light says how transparent the SHADOW is on a shadow-only
//     ground (LightConfig::shadowTransparency). A snapshot older than
//     this reads the struct default, 0.2 -- the translucent shadow the
//     mode was first written with, and Coin's own default.
// 67: a draw says whether it is out of the SCENE's bounds
//     (DrawCall::skipbounds) -- a navigation gizmo captured under a
//     Gui::SoSkipBoundingGroup, which is what Coin drops from the
//     scene bounding box. A snapshot older than this has no flag and
//     every draw counts, which is what those builds measured: the
//     rotation-centre sphere then drags the shadow ground and the
//     auto near/far around with the spin.
// 68: the light says whether the shadow ground sizes itself to the
//     CAMERA rather than to the scene bounds
//     (LightConfig::groundFollowCamera). A snapshot older than this
//     was written by a build that only had the scene-bounds sizing, so
//     it reads as off and lays its ground out the way it was measured.
// 70: how far out of focus the environment background is
//     (PBRConfig::envBlur). A snapshot older than this was written by
//     a build whose background was fixed two cubemap levels down, so
//     it reads as the blur that stands for the same softness rather
//     than as the sharper default.
// 71: a user shader says what DIALECT its sources are
//     (UserShader::dialect): shading-language text, or a MaterialX
//     document in fragmentSource, and where that document came from
//     (UserShader::sourcePath), which is what its image references
//     resolve against. A snapshot older than this reads as
//     shader text, which is all those builds could write.
//     (This was 70 on the branch it was written on, and 70 was taken
//     by envBlur meanwhile; a dump from one of those unpublished
//     builds reads its shader table as text.)
// 72: a MaterialX user shader says WHICH of the document's surfaces it
//     wears (UserShader::surface, docs/MaterialStorage.md sec 17.13).
//     One document usually carries a whole asset's material set, and a
//     snapshot older than this names none, which reads as the first
//     surface -- what those builds rendered. The out-of-band shader
//     chunk carries the same field, so kChunkVersion moves with it.
// 73: a material carries glassmtlx and glasscolor -- a glass body
//     claimed by its MaterialX surface's transmission, with the
//     document's linear colour (docs/MaterialStorage.md sec 17.21). An
//     older snapshot has neither: its glass bodies are all Render_Glass
//     ones, which is what those builds drew. The material chunk carries
//     the same fields, so kChunkVersion moves with it.
// 74: a user shader carries its MaterialX images -- each one an
//     ordinary texture record (deferred pixels under the streaming
//     transport) with the layer of the generated program's image array
//     it is -- plus that array's sampler name and unit, and each
//     compiled variant carries the glass body splice's binary
//     (docs/MaterialStorage.md sec 17.23). A viewer tier binds the
//     document's maps and draws a MaterialX glass per fragment with
//     them. An older snapshot has none of it, and the writer never
//     shipped an image document's program at all, so the stock
//     appearance stood in -- which is what those builds drew. The
//     shader chunk carries the same fields, so kChunkVersion moves.
// 75: a texture's payload may be the ENCODED file it was decoded from
//     (a JPEG or a PNG, TextureImage::encoded) instead of its pixels,
//     said by a flag after the sample kind; the reader decodes it on
//     arrival (ImageDecode.h). Forty-three 2k maps were five hundred
//     megabytes of raw pixels over the wire and twenty as files
//     (docs/MaterialStorage.md sec 17.23). An older snapshot carries
//     pixels only, and reads as before. Every chunk that inlines a
//     texture header moves with it, so kChunkVersion moves.
const uint32_t kVersion = 75;

/// Layout revision of the out-of-band chunks (mesh, material, shader,
/// group manifest). Written as the first field of each chunk, so it is
/// part of what the content key hashes: bump it whenever a chunk's own
/// layout changes and every key changes with it, which retires the
/// entries cached by older builds instead of letting them be misread.
/// (4: a shader chunk carries the particle state step and its binary.
///  5: a mesh chunk may carry the per-face material stream, and a
///     material chunk the perfacematerial flag.
///  6: a material chunk carries the perfacepbr flag beside it.
///  7: a material chunk carries the surface finish record.
///  8: the mesh chunk's material stream is 12 bytes a vertex (the
///     finish palette index in the third slot), and a material chunk
///     carries the palette itself.
///  9: a material chunk carries the surface finish's projection frame
///     and the palette of frames its faces name.
/// 10: a mesh chunk carries attachedOnly, the producer's one-bit
///     element classification -- #13b. Nothing can be MISREAD without
///     the bump: it rides a free bit of an existing flags byte, so an
///     old chunk reads as unclassified. The bump is for the other
///     failure, the one this file's own guard comment names: a cached
///     chunk from an older build would answer "unclassified" forever,
///     and the gate would work on the desktop and quietly do nothing
///     in the browser.
/// 11: a material chunk carries drawstyleoverride, which tells the
///     Tessellation display mode from a plain wireframe node. A cached
///     chunk from an older build would answer "the mode" forever and
///     keep filling the wireframe.
/// 12: a group chunk's draws carry skipbounds (v67). The bytes moved,
///     so an older cached chunk would be MISREAD from that flag on --
///     the bump retires it.
/// 13: a shader chunk carries the source dialect and source path
///     (v70), ahead of the stage. Same story: the bytes moved, so an
///     older cached chunk would read the stage string out of the
///     dialect byte.
/// 15: a material chunk carries glassmtlx and glasscolor (v73) after
///     glassroughness. The bytes moved, so an older cached chunk would
///     read its cloud flag out of the new field.
/// 16: a shader chunk carries the glass splice binary per compiled
///     variant and the document's images with their array layout
///     (v74). Appended after everything the chunk carried, so nothing
///     moved -- but an older cached chunk would answer no images and
///     no glass splice forever, and the bump retires it.
/// 17: a texture header, inlined by material and shader chunks alike,
///     carries the encoded-payload flag (v75). The bytes moved.)
const uint32_t kChunkVersion = 17;

/// Bytes per vertex of MeshData::materials, whose layout Renderer.h
/// documents. Named here because the stride is what a reader of an
/// older dump would get wrong.
const size_t kMaterialStride = MeshData::MaterialStride;

//////////////////////////////////////////////////////////////////////
// Streamed config layout guards.
//
// The per-frame configs below are serialized field by field (see the
// "Background + per-frame configs" block in saveSnapshotFp and its
// mirror in loadSnapshotFp). Adding a field to one of those structs
// without adding the matching write and read is invisible: the dump
// still loads, the viewer just keeps the struct's default forever, so
// the property works on the desktop and does nothing in the browser.
// That is exactly how WaterConfig::impactStrength shipped broken
// before v40.
//
// These asserts make that a build error. Change one of the structs and
// the size no longer matches, which forces a visit here — and the fix
// is never "bump the number": write the new field, read it, bump
// kVersion (and gate the read on it), then bump the size.
//
// Sizes hold on both tiers because every guarded member is a
// float/int/bool: same size and alignment on the x86_64 dumper and the
// wasm32 loader. The three configs that carry a pointer-sized member
// (std::shared_ptr / std::vector, whose alignment does differ) guard
// the offset of their last POD field instead — that still moves
// whenever a streamed field is added ahead of it.
//
// Four guarded fields are deliberately *not* on the wire, and should
// stay that way: AOConfig::fast is a per-frame interaction hint the
// viewer decides for itself, and RenderDebugConfig::frameTiming,
// ::occlusion and ::coverage drive backend-local logs rather than any
// pixel.
static_assert(sizeof(HiddenLineConfig) == 20, "HiddenLineConfig changed: stream the new field, then update this");
static_assert(sizeof(PreselHighlightConfig) == 20, "PreselHighlightConfig changed: stream the new field, then update this");
static_assert(sizeof(SectionConfig) == 12, "SectionConfig changed: stream the new field, then update this");
static_assert(sizeof(AOConfig) == 28, "AOConfig changed: stream the new field, then update this");
static_assert(sizeof(CavityConfig) == 16, "CavityConfig changed: stream the new field, then update this");
static_assert(sizeof(MatcapConfig) == 12, "MatcapConfig changed: stream the new field, then update this");
static_assert(sizeof(BumpConfig) == 8, "BumpConfig changed: stream the new field, then update this");
static_assert(sizeof(VolumetricConfig) == 28, "VolumetricConfig changed: stream the new field, then update this");
static_assert(sizeof(WaterConfig) == 48, "WaterConfig changed: stream the new field, then update this");
static_assert(sizeof(BloomConfig) == 16, "BloomConfig changed: stream the new field, then update this");
static_assert(offsetof(PBRConfig, envPreset) == 32, "PBRConfig changed: stream the new field, then update this");
static_assert(offsetof(LightConfig, groundColor) == 172,"LightConfig changed: stream the new field, then update this");
static_assert(offsetof(RenderDebugConfig, coverage) == 7, "RenderDebugConfig changed: stream the new field, then update this");

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
    // Bit 16 is the odd one out: the lower four say a payload follows,
    // this one is the producer's classification and carries no bytes
    // (attachedOnly, #13b -- every vertex is an edge endpoint, or every
    // edge bounds a face). It rides the flags byte rather than
    // appending a field because a reader that does not know it simply
    // does not test it, and the bit costs nothing on a mesh that has no
    // points or lines at all. It must stay off the payload bits: one of
    // those set with no bytes behind it desynchronises the reader.
    uint8_t flags = (m.normals ? 1 : 0) | (m.colors ? 2 : 0)
        | (m.texCoords ? 4 : 0) | (m.materials ? 8 : 0)
        | (m.attachedOnly ? 16 : 0);
    w.u8(flags);
    w.raw(m.positions, size_t(m.numVertices) * 3 * sizeof(float));
    if (m.normals)
        w.raw(m.normals, size_t(m.numVertices) * 3 * sizeof(float));
    if (m.colors)
        w.raw(m.colors, size_t(m.numVertices) * 4);
    if (m.texCoords)
        w.raw(m.texCoords, size_t(m.numVertices) * 4 * sizeof(float));
    if (m.materials)
        w.raw(m.materials, size_t(m.numVertices) * kMaterialStride);

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

/// Meshes whose payload reaches this size declare coarser levels
/// (§7): below it the exact mesh costs about what a level would, and
/// a ladder of variants of a small payload is bookkeeping for nothing.
const uint32_t kLodDeclareSize = 64u * 1024;
/// Line meshes declare much earlier: an edge chunk is a fraction of
/// its face sibling's size, but when the faces of an object step onto
/// a coarse rung its exact edges float off the coarse surface at the
/// silhouettes — the matching edge rung has to exist for the pair to
/// descend together. Only a chunk of a few segments, where the float
/// is subpixel anyway, is exempt.
const uint32_t kLodDeclareSizeLines = 1024;
/// How many coarser levels a big mesh declares ahead of any generator
/// existing. The declared errors follow the generator's construction
/// (MeshSimplify::levelCellSize): level L clusters on cells of
/// diagonal/(8<<L), so its error relative to the diagonal is 1/(8<<L).
const uint32_t kLodDeclareLevels = 2;

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
    // v36: the ladder of levels, coarsest first, ending with the exact
    // mesh. A declared level has no key yet — nothing has generated
    // its bytes, so it has no content address and cannot be fetched,
    // only asked for (§7). The exact mesh closes the list at error 0 —
    // keyed when the published mesh IS the exact tessellation (the
    // usual case), declared-unbuilt when the producer tessellated
    // coarse-first (m.levelError > 0): then the published mesh sits at
    // its own error and the exact rung is generated on demand like any
    // other, asked for as kExactMeshLevel.
    const bool coarsePublished = m.levelError > 0.0f;
    const bool lineMesh = m.numTriangleIndices <= 0 && m.numLineIndices > 0;
    const uint32_t declareAt = lineMesh ? kLodDeclareSizeLines
                                        : kLodDeclareSize;
    uint32_t declared = size >= declareAt ? kLodDeclareLevels : 0;
    // Only rungs strictly coarser than the published mesh are worth
    // declaring below it (the grid level whose error equals the
    // published one IS the published mesh).
    while (declared > 0
           && coarsePublished
           && !(1.0f / float(8u << (declared - 1)) > m.levelError * 1.001f))
        --declared;
    w.u8(uint8_t(declared + 1 + (coarsePublished ? 1 : 0)));
    auto writeBuilt = [&](uint32_t lvl, float error) {
        w.f(error);
        // A level someone has generated since the last publish gets
        // its key here (§7, phase 5c) — which is the whole
        // announcement: writing the key changes these bytes, so the
        // group's content key moves and the delta carries the news.
        uint32_t lvlSize = 0;
        std::string lvlKey =
            blobs.built ? blobs.built(key, lvl, lvlSize) : std::string();
        if (lvlKey.empty()) {
            w.u8(0);
            return;
        }
        w.u8(1);
        w.str(lvlKey);
        w.u32(lvlSize);
    };
    for (uint32_t lvl = 0; lvl < declared; ++lvl)
        writeBuilt(lvl, 1.0f / float(8u << lvl));
    w.f(coarsePublished ? m.levelError : 0.0f);
    w.u8(1);
    w.str(key);
    // The payload size the key stands for: the viewer routes a small
    // chunk into a batch and a large one into its own request, and it
    // cannot know which without being told.
    w.u32(size);
    if (coarsePublished)
        writeBuilt(kExactMeshLevel, 0.0f);
}

/// The reader half of writeMesh's deferred branch: the level ladder
/// into \a entry's `levels`, and the finest *built* level into the
/// entry's own key and size — which is what the ordinary fetch path
/// acts on, so a consumer that never looks at the ladder behaves
/// exactly as it did before v36.
bool readMeshLevels(Reader &r, uint32_t version,
                    SceneSnapshot::DeferredChunk &entry)
{
    if (version < 36) {
        r.str(entry.key, 128);
        entry.size = r.u32();
        if (r.ok) {
            SceneSnapshot::DeferredChunk::Level level;
            level.key = entry.key;
            level.size = entry.size;
            entry.levels.push_back(std::move(level));
        }
        return r.ok;
    }
    const uint32_t count = r.u8();
    if (!r.ok || count == 0) {
        r.ok = false;
        return false;
    }
    entry.key.clear();
    entry.size = 0;
    for (uint32_t i = 0; i < count; ++i) {
        SceneSnapshot::DeferredChunk::Level level;
        level.error = r.f();
        if (r.u8() != 0) {
            r.str(level.key, 128);
            level.size = r.u32();
            // Coarsest first, so the last keyed level is the finest
            // built one.
            entry.key = level.key;
            entry.size = level.size;
        }
        if (!r.ok)
            return false;
        entry.levels.push_back(std::move(level));
    }
    // A ladder with nothing built is a mesh that cannot be obtained at
    // all; the producer always keys the exact mesh, so this is a
    // damaged payload, not a valid state.
    if (entry.key.empty())
        r.ok = false;
    return r.ok;
}

/// Loader-side mesh: the arrays live in the owned vectors, the base
/// MeshData pointers point into them. Held by DrawCall::mesh.
using OwnedMeshData = Render::ParsedMeshChunk;

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
    if (flags & 8) {
        mesh->matStore.resize(nv * kMaterialStride);
        r.raw(mesh->matStore.data(), nv * kMaterialStride);
        mesh->materials = mesh->matStore.data();
    }
    // Absent bit = unclassified = always draws, which is the safe
    // direction and exactly what a chunk written by an older build
    // means: nobody judged this drawable, so nothing on screen may be
    // assumed to be standing in for it.
    mesh->attachedOnly = (flags & 16) != 0;

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

} // anonymous namespace — resumed below; the level generator has
  // external linkage and lives at Render:: scope, but sits here with
  // the chunk machinery it is made of.

bool Render::generateMeshLevel(const void *chunk, size_t size, uint32_t level,
                               std::vector<uint8_t> &out)
{
    // The exact rung of a coarse-first ladder cannot be decimated into
    // existence: decimation only ever removes information, and a weld
    // of the coarse source would be announced as the exact mesh. Only
    // a shape-backed generator (MeshSource.h) can build it.
    if (level == kExactMeshLevel)
        return false;

    // Parse the exact chunk back into a mesh. The chunk came out of
    // this build's own store, so it is this build's layout — current
    // version, no compatibility question.
    OwnedMeshData mesh;
    bool ok = readChunk(chunk, size, [&mesh](Reader &r) {
        readMeshChunk(r, &mesh, kVersion);
    });
    if (!ok || mesh.numVertices <= 0)
        return false;

    // The declared error was stated relative to the mesh's own
    // diagonal (writeMesh), and the generator's grid is stated the
    // same way — so the bbox the cell size needs is the mesh's own.
    float bbox[6] = {mesh.positions[0], mesh.positions[1],
                     mesh.positions[2], mesh.positions[0],
                     mesh.positions[1], mesh.positions[2]};
    for (int i = 1; i < mesh.numVertices; ++i) {
        for (int a = 0; a < 3; ++a) {
            float v = mesh.positions[size_t(i) * 3 + a];
            bbox[a] = std::min(bbox[a], v);
            bbox[a + 3] = std::max(bbox[a + 3], v);
        }
    }
    float cellSize = levelCellSize(bbox, level);
    if (!(cellSize > 0.0f))
        return false;

    SimplifiedMesh simplified;
    if (!simplifyMesh(mesh, cellSize, simplified))
        return false;

    // A level is an ordinary mesh chunk. What fill() does not know —
    // the material-facing flags, invariant under decimation — carries
    // over from the source.
    MeshData levelMesh;
    simplified.fill(levelMesh);
    levelMesh.hasTransparency = mesh.hasTransparency;
    levelMesh.hasOpaqueParts = mesh.hasOpaqueParts;
    // A statement about the shape's topology, not about this mesh's
    // resolution: whether a vertex sits on an edge does not change
    // because the surface was decimated. Dropping it here would make a
    // drawable un-gateable on exactly the coarse rungs a tier under
    // memory pressure is standing on.
    levelMesh.attachedOnly = mesh.attachedOnly;
    return writeChunk(out, [&levelMesh](Writer &w) {
        writeMeshChunk(w, levelMesh);
    });
}

bool Render::parseMeshChunk(const void *chunk, size_t size,
                            ParsedMeshChunk &out)
{
    out = ParsedMeshChunk();
    bool ok = readChunk(chunk, size, [&out](Reader &r) {
        readMeshChunk(r, &out, kVersion);
    });
    return ok && out.numVertices > 0;
}

bool Render::encodeMeshChunk(const MeshData &m, std::vector<uint8_t> &out)
{
    return writeChunk(out, [&m](Writer &w) { writeMeshChunk(w, m); });
}

namespace {

/// The ladder's per-rung mesh store (SceneDump.h, LevelMeshes;
/// docs/SceneStreaming.md §7 "one rung per instance"). Rungs are keyed
/// by content key — the one name a rung keeps through ladder
/// re-declarations — and each gets its own mesh object under its own
/// cacheId, so to the GPU cache a refinement is a different mesh, not
/// an overwrite. The identity object is the parse-time finest built
/// rung's slot: it is what every draw of the ladder holds, and what a
/// consumer with no level selection (the generic fill loop, the
/// desktop resolve, the tests) fills through the entry's own closures.
class LevelMeshStore : public Render::LevelMeshes {
public:
    LevelMeshStore(std::shared_ptr<OwnedMeshData> identity,
                   const std::string &identityKey, uint32_t version)
        : m_version(version), m_identity(std::move(identity))
    {
        m_rungs.emplace(identityKey, m_identity);
    }
    std::shared_ptr<const Render::MeshData> identity() const override
    {
        return m_identity;
    }
    std::shared_ptr<const Render::MeshData> at(
        const std::string &key) const override
    {
        auto it = m_rungs.find(key);
        return it == m_rungs.end() ? nullptr : it->second;
    }
    bool fill(const std::string &key, const void *data,
              size_t size) override
    {
        auto &slot = m_rungs[key];
        if (!slot) {
            slot = std::make_shared<OwnedMeshData>();
            slot->cacheId = meshIdFromKey(key);
        }
        // A null payload is the consumer probing whether the chunk can
        // be given up on, and the answer for geometry is no — without
        // destroying what an earlier fill already read.
        if (!data)
            return false;
        const uint64_t id = slot->cacheId;
        const uint32_t gen = slot->generation;
        const uint32_t version = m_version;
        bool ok = readChunk(data, size, [&slot, version](Reader &cr) {
            readMeshChunk(cr, slot.get(), version);
        });
        if (!ok) {
            // A partial parse leaves a vertex count with no arrays
            // behind it, which a backend would read straight past the
            // end of. Empty is the only safe failure.
            *slot = OwnedMeshData();
        }
        slot->cacheId = id;
        slot->generation = gen + 1;
        return ok;
    }
    void stampError(const std::string &key, float error) override
    {
        auto it = m_rungs.find(key);
        if (it != m_rungs.end() && it->second)
            it->second->levelError = error;
    }
    void release(const std::string &key) override
    {
        auto it = m_rungs.find(key);
        if (it == m_rungs.end() || !it->second)
            return;
        auto &slot = it->second;
        const uint64_t id = slot->cacheId;
        const uint32_t gen = slot->generation;
        *slot = OwnedMeshData();
        slot->cacheId = id;
        slot->generation = gen + 1;
    }

    /// The filled mesh under \a key, for carrying residency across a
    /// re-parse (carryResidentRungs); null when the rung is empty.
    std::shared_ptr<OwnedMeshData> owned(const std::string &key) const
    {
        auto it = m_rungs.find(key);
        return it != m_rungs.end() && it->second
                && it->second->numVertices > 0 && it->second->positions
            ? it->second
            : nullptr;
    }

    /// Take over a rung another store already holds under the same
    /// content key. The identity slot is aliased by this parse's draws
    /// and cannot be re-pointed, so it takes the content by copy; any
    /// other slot simply shares the mesh object — the superseded
    /// snapshot is on its way out, and a content key never names two
    /// different meshes.
    bool adopt(const std::string &key,
               const std::shared_ptr<OwnedMeshData> &mesh)
    {
        auto it = m_rungs.find(key);
        if (it == m_rungs.end()) {
            m_rungs.emplace(key, mesh);
            return true;
        }
        auto &slot = it->second;
        if (!slot) {
            slot = mesh;
            return true;
        }
        if (slot->numVertices > 0 && slot->positions)
            return true;   // already filled — an inline delta payload
        const uint64_t id = slot->cacheId;
        const uint32_t gen = slot->generation;
        copyParsedMesh(*slot, *mesh);
        slot->cacheId = id;
        slot->generation = gen + 1;
        return true;
    }

private:
    /// Value-copy a parsed mesh. The base MeshData points into the
    /// store vectors, so a member-wise copy would alias the source's
    /// arrays — every pointer has to be re-anchored in the copy.
    static void copyParsedMesh(OwnedMeshData &dst, const OwnedMeshData &src)
    {
        dst = src;
        dst.positions = dst.posStore.data();
        dst.normals = src.normals ? dst.normStore.data() : nullptr;
        dst.colors = src.colors ? dst.colorStore.data() : nullptr;
        dst.texCoords = src.texCoords ? dst.uvStore.data() : nullptr;
        dst.triangleIndices =
            src.triangleIndices ? dst.triStore.data() : nullptr;
        dst.lineIndices = src.lineIndices ? dst.lineStore.data() : nullptr;
        dst.pointIndices =
            src.pointIndices ? dst.pointStore.data() : nullptr;
        dst.noSeamLineIndices =
            src.noSeamLineIndices ? dst.noSeamStore.data() : nullptr;
    }

    uint32_t m_version;
    std::shared_ptr<OwnedMeshData> m_identity;
    std::map<std::string, std::shared_ptr<OwnedMeshData>> m_rungs;
};

/// Read one mesh table entry. A deferred entry (v28, streaming) yields
/// an empty MeshData plus the means to fill it once its chunk arrives;
/// the draw calls already alias it, so filling it in place is enough.
std::shared_ptr<const MeshData> readMesh(Reader &r, uint32_t version,
                                         SceneSnapshot &snap)
{
    auto mesh = std::make_shared<OwnedMeshData>();
    if (version >= 28 && r.u8() != 0) {
        SceneSnapshot::DeferredChunk entry;
        if (!readMeshLevels(r, version, entry))
            return nullptr;
        mesh->cacheId = meshIdFromKey(entry.key);
        // The per-rung store (§7, "one rung per instance"); the
        // entry's own closures fill/release the parse-time identity
        // rung, which is all a consumer with no level selection uses.
        // Parse with the version of the snapshot that named the chunk,
        // not this build's: a viewer newer than the payload reads it
        // as it is, and only a payload newer than the viewer makes the
        // viewer reload.
        auto lm = std::make_shared<LevelMeshStore>(mesh, entry.key,
                                                   version);
        const std::string key0 = entry.key;
        entry.levelMeshes = lm;
        // The identity rung is the finest BUILT level (readMeshLevels
        // leaves entry.key on it), and its error is what this mesh
        // draws at -- nonzero whenever the producer published
        // coarse-first. Carrying it onto the mesh is how the
        // producer's own statement of coarseness survives the wire.
        for (size_t i = entry.levels.size(); i-- > 0;) {
            if (entry.levels[i].key == entry.key) {
                mesh->levelError = entry.levels[i].error;
                break;
            }
        }
        entry.fill = [lm, key0](SceneSnapshot &, const void *data,
                                size_t size) {
            return lm->fill(key0, data, size);
        };
        // Kept for the ladder's lifetime (DeferredChunk::refill), like
        // the manifest-layout reader's.
        entry.refill = entry.fill;
        // Geometry has a rung below it, so it is what a viewer short
        // of memory gives back first (§6, phase 4b). The cacheId is
        // the mesh's identity and outlives its contents: the backend
        // keys GPU buffers by it, and the box the draws fall back to
        // has an id of its own.
        entry.release = [lm, key0] { lm->release(key0); };
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
    // The file as authored travels when the producer kept it (v75): it
    // is the same picture at a fortieth of the bytes, and every tier
    // can decode it. The content key is then the key of THOSE bytes,
    // which is what the consumer fetches.
    const bool encoded = !t.encoded.empty();
    const std::vector<uint8_t> &payload = encoded ? t.encoded : t.pixels;
    // An empty texture has nothing to fetch, so it stays inline whatever
    // the transport: deferring it would cost a round trip for no bytes.
    bool defer = bool(blobs) && !payload.empty();
    if (t.contentKey.size() != 40 && !payload.empty())
        t.contentKey = sha1Hex(payload.data(), payload.size());

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
    w.u32(uint32_t(payload.size()));
    if (!defer)
        w.raw(payload.data(), payload.size());
    w.u8(t.wrapS);
    w.u8(t.wrapT);
    w.u8(t.model);
    w.u32(t.blendColor);
    // v63: what one component of the payload IS. Written after the
    // payload, so an older reader stops before it and a newer one knows
    // the bytes it just took were floats.
    w.u8(t.sample);
    // v75: whether the payload is the encoded file rather than pixels.
    w.u8(encoded ? 1 : 0);

    if (defer && (!sent || sent->insert(t.contentKey).second))
        blobs(t.contentKey, std::vector<uint8_t>(payload));
}

/// How large a decoded map may be on this tier. The browser holds its
/// whole scene in one heap and every map at 2k costs it sixteen
/// megabytes decoded; a document's worth is what the chess set is.
/// The desktop keeps what the file says.
#ifdef FC_RENDERER_STANDALONE
constexpr int kDecodeMaxSide = 1024;
#else
constexpr int kDecodeMaxSide = 0;
#endif

/// Turn an encoded payload into the texture's pixels, at the tier's
/// size cap. A payload that will not decode leaves the texture empty,
/// which draws as the map missing -- the same answer a texture that
/// was given up on gets.
static void decodeInto(TextureImage &tex, const uint8_t *bytes, size_t size)
{
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    if (decodeImage(bytes, size, tex.numComponents, kDecodeMaxSide, w, h,
                    pixels)) {
        tex.width = w;
        tex.height = h;
        tex.pixels.swap(pixels);
    }
    else {
        // Said once per payload rather than swallowed: the map then
        // draws as missing, and a picture drawn wrong with no line in
        // the log is the one failure this transport must not have.
        std::fprintf(stderr, "scene: texture payload of %zu bytes would "
                             "not decode (%d components)\n",
                     size, tex.numComponents);
        tex.pixels.clear();
    }
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
    // v63; U8 on anything older, which is all those snapshots could
    // hold.
    tex->sample = version >= 63 ? r.u8()
                                : uint8_t(TextureImage::U8);
    // v75: the payload is the encoded file. Inline, it is decoded here
    // and now; deferred, the fill decodes it when it lands -- the
    // flag rides on the texture until then (encodedPayload).
    if (version >= 75 && r.u8() != 0) {
        if (tex->deferred) {
            tex->encodedPayload = true;
        }
        else {
            std::vector<uint8_t> file;
            file.swap(tex->pixels);
            decodeInto(*tex, file.data(), file.size());
        }
    }
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
    entry.texture = true;
    entry.fill = [tex](SceneSnapshot &, const void *data, size_t size) {
        // Shared across publishes (SceneSnapshot::textureMemo), so two
        // snapshots may each hold an entry for it: the second to land
        // finds it filled and leaves it.
        if (!tex->deferred)
            return true;
        if (data) {
            const uint8_t *bytes = static_cast<const uint8_t *>(data);
            if (tex->encodedPayload)
                decodeInto(*tex, bytes, size);
            else
                tex->pixels.assign(bytes, bytes + size);
        }
        tex->deferred = false;
        return true;
    };
    snap.deferredChunks.push_back(std::move(entry));
}

/// The object a texture with this content key is held as across
/// publishes, when the consumer keeps such a memo: the one the memo
/// holds if anything still holds it, else \a fresh, memoized. The
/// caller queues the deferred fetch for whichever comes back and is
/// still waiting -- a fill is idempotent, so a texture in flight in
/// two snapshots at once is filled by the first to land.
std::shared_ptr<TextureImage> memoTexture(
    SceneSnapshot &snap, const std::shared_ptr<TextureImage> &fresh)
{
    if (!snap.textureMemo || !fresh || fresh->contentKey.empty())
        return fresh;
    auto &slot = (*snap.textureMemo)[fresh->contentKey];
    if (auto held = slot.lock())
        return held;
    slot = fresh;
    return fresh;
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
// binaries (UserShader::Compiled), and (v74) a MaterialX document's
// images with the layout the generated program reads them in.

typedef std::map<const UserShader *, int32_t> ShaderIndex;
typedef std::vector<std::shared_ptr<const UserShader>> ShaderTable;

/// \a texSent, when given, is the writer's set of texture keys already
/// handed to the blob sink (see writeTexture); the images travel as
/// ordinary texture records, deferred under the streaming transport.
void writeUserShader(Writer &w, const UserShader &shader,
                     const SceneSnapshot &snap,
                     std::set<std::string> *texSent)
{
    // The shader's own variants and layout plus whatever the save-side
    // hook makes of it -- the server-compiled viewer binaries, the
    // image layers -- on a copy, so the desktop's own object is never
    // rewritten by a publish. First variant per profile wins on the
    // viewer.
    UserShader s = shader;
    if (snap.shipShader)
        snap.shipShader(s);
    w.u8(uint8_t(s.dialect));
    w.str(s.sourcePath);
    w.str(s.surface);
    w.str(s.stage);
    w.str(s.vertexSource);
    w.str(s.fragmentSource);
    w.str(s.simulateSource);
    w.u32(uint32_t(s.params.size()));
    for (const auto &p : s.params) {
        w.str(p.name);
        w.u32(uint32_t(p.values.size()));
        w.floats(p.values.data(), p.values.size());
    }
    w.u32(uint32_t(s.compiled.size()));
    for (const auto &c : s.compiled) {
        w.str(c.profile);
        w.bytes(c.vsBin);
        w.bytes(c.fsBin);
        w.bytes(c.simBin);
        w.bytes(c.glassBin);
    }
    // v74: the document's images, joined to the generated program's
    // array layers by the ship hook. Each is a whole texture record --
    // header inline, pixels deferred by content key when the transport
    // defers -- so the viewer fetches and caches a map exactly as it
    // does a material's texture.
    w.str(s.imageSampler);
    w.i32(s.imageUnit);
    w.u32(uint32_t(s.images.size()));
    for (const auto &img : s.images) {
        w.str(img.path);
        w.i32(img.layer);
        if (!img.image) {
            w.u8(0);
            continue;
        }
        w.u8(1);
        writeTexture(w, *img.image, snap.textureBlobs, texSent);
    }
}

/// \a textures, when given, is the loader's content-keyed texture memo:
/// an image two shaders name (or a shader and a material) is one
/// object, fetched once. Deferred pixels are queued on \a snap.
std::shared_ptr<const UserShader> readUserShader(
    Reader &r, uint32_t version, SceneSnapshot &snap,
    std::map<std::string, std::shared_ptr<TextureImage>> *textures)
{
    auto s = std::make_shared<UserShader>();
    if (version >= 71) {
        uint8_t d = r.u8();
        // An unknown dialect from a newer writer is not a shader this
        // build can consume; leave it as text and let the backend
        // report the compile failure it would report anyway.
        if (d <= uint8_t(UserShader::Dialect::MaterialX))
            s->dialect = UserShader::Dialect(d);
        r.str(s->sourcePath, 0x1000u);
    }
    if (version >= 72)
        r.str(s->surface, 0x1000u);
    r.str(s->stage, 0x100u);
    r.str(s->vertexSource);
    r.str(s->fragmentSource);
    if (version >= 39)
        r.str(s->simulateSource);
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
        if (version >= 39)
            r.bytes(c.simBin);
        if (version >= 74)
            r.bytes(c.glassBin);
    }
    if (version < 74)
        return s;
    r.str(s->imageSampler, 0x100u);
    s->imageUnit = r.i32();
    uint32_t ni = r.u32();
    if (!r.ok || ni > 0x100u) {
        r.ok = false;
        return s;
    }
    s->images.resize(ni);
    for (auto &img : s->images) {
        r.str(img.path, 0x1000u);
        img.layer = r.i32();
        if (r.u8() == 0)
            continue;
        uint32_t size = 0;
        auto tex = readTexture(r, version, &size);
        if (!tex) {
            r.ok = false;
            return s;
        }
        if (textures && !tex->contentKey.empty()) {
            auto it = textures->find(tex->contentKey);
            if (it != textures->end()) {
                img.image = it->second;
                continue;
            }
            tex = memoTexture(snap, tex);
            textures->emplace(tex->contentKey, tex);
        }
        if (tex->deferred)
            deferTexture(snap, tex, size);
        img.image = tex;
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
    w.u8(m.finish);   // v49
    w.f(m.finishpitch);
    w.f(m.finishdepth);
    w.f(m.finishangle);
    // The finish palette (v50): entry 0 repeats the record above, so a
    // draw without a per-face finish writes an empty one.
    const uint32_t numfinish = m.finishpalette
        ? uint32_t(std::min(m.finishpalette->entries.size(),
                            size_t(MaxFinishPalette)))
        : 0;
    w.u32(numfinish);
    for (uint32_t i = 0; i < numfinish; ++i) {
        const auto &entry = m.finishpalette->entries[i];
        w.u8(entry.pattern);
        w.f(entry.pitch);
        w.f(entry.depth);
        w.f(entry.angle);
    }
    // The projection frames the finish is laid out in (v51). The draw's
    // own frame first -- which is what a reader takes when the palette
    // is empty -- then the palette, whose entry 0 repeats it.
    auto writeFrame = [&w](const SurfaceFrame &f) {
        w.u8(f.kind);
        for (int i = 0; i < 3; ++i)
            w.f(f.origin[i]);
        for (int i = 0; i < 3; ++i)
            w.f(f.axis[i]);
        for (int i = 0; i < 3; ++i)
            w.f(f.xdir[i]);
        w.f(f.radius);
    };
    writeFrame(m.frame);
    const uint32_t numframe = m.framepalette
        ? uint32_t(std::min(m.framepalette->entries.size(),
                            size_t(MaxFramePalette)))
        : 0;
    w.u32(numframe);
    for (uint32_t i = 0; i < numframe; ++i)
        writeFrame(m.framepalette->entries[i]);
    w.b(m.water);
    w.f(m.waterdensity);
    w.b(m.glass);
    w.f(m.glassior);
    w.f(m.glassdensity);
    w.f(m.glassroughness);
    w.b(m.glassmtlx);
    for (int c = 0; c < 3; ++c)
        w.f(m.glasscolor[c]);
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
    // The per-face texture palette (v65): its layer images placed like
    // every other texture reference, then how they are laid out and
    // which layer a draw that cannot read the per-vertex stream uses.
    const uint32_t numface = m.texturepalette
        ? uint32_t(std::min(m.texturepalette->entries.size(),
                            size_t(MaxFaceTexturePalette)))
        : 0;
    w.u32(numface);
    for (uint32_t i = 0; i < numface; ++i)
        texref(m.texturepalette->entries[i]);
    if (numface) {
        w.f(m.facetexscale);
        w.i32(m.facetexlayer);
    }
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
    // v45: SoDrawStyleElement, which is how the Tessellation draw style
    // arrives. Older viewers draw the faces filled, i.e. as Shaded.
    w.u8(m.drawstyle);
    // v57: whether that style is the scene-wide override (the display
    // mode) or a plain SoDrawStyle node. Older viewers take every
    // wireframe for the display mode, which is what they always did.
    w.b(m.drawstyleoverride);
    // v47: per-face material — shade from the mesh's baked stream.
    // Older viewers use the scalars above, the pre-feature look.
    w.b(m.perfacematerial);
    // v48: that stream's alpha slots carry the PBR factor pair.
    w.b(m.perfacepbr);
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
    if (version >= 49) {
        m.finish = r.u8();
        m.finishpitch = r.f();
        m.finishdepth = r.f();
        m.finishangle = r.f();
    }
    if (version >= 50) {
        const uint32_t numfinish = r.u32();
        if (r.ok && numfinish) {
            auto palette = std::make_shared<FinishPalette>();
            palette->entries.reserve(
                    std::min(numfinish, uint32_t(MaxFinishPalette)));
            for (uint32_t i = 0; i < numfinish && r.ok; ++i) {
                FinishPalette::Entry entry;
                entry.pattern = r.u8();
                entry.pitch = r.f();
                entry.depth = r.f();
                entry.angle = r.f();
                if (i < uint32_t(MaxFinishPalette))
                    palette->entries.push_back(entry);
            }
            if (r.ok)
                m.finishpalette = std::move(palette);
        }
    }
    if (version >= 51) {
        auto readFrame = [&r]() {
            SurfaceFrame f;
            const uint8_t kind = r.u8();
            f.kind = kind <= SurfaceFrame::Radial ? kind
                                                  : SurfaceFrame::Unframed;
            for (int i = 0; i < 3; ++i)
                f.origin[i] = r.f();
            for (int i = 0; i < 3; ++i)
                f.axis[i] = r.f();
            for (int i = 0; i < 3; ++i)
                f.xdir[i] = r.f();
            f.radius = r.f();
            return f;
        };
        m.frame = readFrame();
        const uint32_t numframe = r.u32();
        if (r.ok && numframe) {
            auto palette = std::make_shared<FramePalette>();
            palette->entries.reserve(
                    std::min(numframe, uint32_t(MaxFramePalette)));
            for (uint32_t i = 0; i < numframe && r.ok; ++i) {
                const SurfaceFrame f = readFrame();
                if (i < uint32_t(MaxFramePalette))
                    palette->entries.push_back(f);
            }
            if (r.ok)
                m.framepalette = std::move(palette);
        }
    }
    m.water = r.b();
    m.waterdensity = r.f();
    m.glass = r.b();
    m.glassior = r.f();
    m.glassdensity = r.f();
    m.glassroughness = r.f();
    if (version >= 73) {
        m.glassmtlx = r.b();
        for (int c = 0; c < 3; ++c)
            m.glasscolor[c] = r.f();
    }
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
    if (version >= 65) {
        const uint32_t numface = r.u32();
        if (!r.ok || numface > uint32_t(MaxFaceTexturePalette)) {
            r.ok = false;
            return;
        }
        if (numface) {
            auto palette = std::make_shared<TexturePalette>();
            palette->entries.resize(numface);
            for (uint32_t i = 0; i < numface && r.ok; ++i)
                texref(palette->entries[i]);
            m.facetexscale = r.f();
            const int32_t layer = r.i32();
            m.facetexlayer = layer >= -1 && layer < 128 ? int8_t(layer) : -1;
            // A palette whose images did not resolve is no palette: the
            // draw then reads as untextured per face, which is what a
            // viewer that never saw the images can honestly draw.
            bool complete = r.ok;
            for (const auto &e : palette->entries)
                complete = complete && e != nullptr;
            if (complete)
                m.texturepalette = std::move(palette);
        }
    }
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
    // v45: the draw style. Absent means filled, which is what every
    // style but Tessellation asks for anyway.
    if (version >= 45)
        m.drawstyle = r.u8();
    // v57: is that style the display mode's scene-wide override? A dump
    // that does not say was written when only the display mode could
    // produce the style at all, so reading it as the mode is right.
    m.drawstyleoverride = version >= 57
        ? r.b()
        : m.drawstyle == Material::DrawLines;
    // v47: per-face material. Absent means the scalars apply.
    if (version >= 47)
        m.perfacematerial = r.b();
    // v48: the stream's PBR reading. Absent means the Phong one.
    if (version >= 48)
        m.perfacepbr = r.b();
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
    w.b(l.groundAuto);   // v46
    w.f(l.groundSizeX);
    w.f(l.groundSizeY);
    w.b(l.groundAutoPos);
    w.floats(l.groundPos, 3);
    w.floats(l.groundMatrix, 16);
    w.b(l.groundShading);   // v58
    w.b(l.groundBackFaceCull);
    w.f(l.shadowTransparency);   // v66
    w.b(l.groundFollowCamera);   // v68
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
    if (version >= 46) {
        l.groundAuto = r.b();
        l.groundSizeX = r.f();
        l.groundSizeY = r.f();
        l.groundAutoPos = r.b();
        r.floats(l.groundPos, 3);
        r.floats(l.groundMatrix, 16);
    }
    if (version >= 58) {
        l.groundShading = r.b();
        l.groundBackFaceCull = r.b();
    }
    if (version >= 66) {
        l.shadowTransparency = r.f();
    }
    // The struct defaults this ON, which is right for a live config and
    // wrong for a snapshot: a writer older than v68 sized the ground
    // from the scene bounds, so that is what its stream means.
    l.groundFollowCamera = version >= 68 ? r.b() : false;
    // Older streams leave the struct's defaults: auto sizing from the
    // scene bounds, which is what those builds did.
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
    // v67
    w.b(d.skipbounds);
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
    if (version >= 67)
        d.skipbounds = r.b();
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
                writeUserShader(cw, *s, *st.snap, &st.texSent);
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
                ManifestWriter &st, SceneSnapshot::ObjectEntry &entry,
                std::vector<uint8_t> *keep = nullptr)
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
    // A delta serializer needs the bytes again after the sink has
    // them: the changed entries ride inline (v37).
    if (keep)
        *keep = chunk;
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
///
/// \a bytesFor non-null marks the DELTA form (v37): each carried entry
/// is followed by a flag and, when the provider answers, the group
/// manifest's own bytes. The reader keys the flag's presence off
/// baseVersion, so the caller must pass a provider exactly when the
/// payload will say it is a delta — an empty answer for a key is fine
/// (that entry goes by reference), a missing provider on a delta is a
/// format error the reader cannot detect.
template<typename EntryPtr>
void writeObjectSection(Writer &w,
                        const std::vector<uint64_t> &removed,
                        const std::vector<EntryPtr> &carried,
                        const ChunkBytesFor *bytesFor = nullptr)
{
    w.u32(uint32_t(removed.size()));
    for (uint64_t key : removed)
        w.u64(key);
    w.u32(uint32_t(carried.size()));
    for (const auto &ref : carried) {
        const SceneSnapshot::ObjectEntry &e = *ref;
        w.u64(e.objectKey);
        w.floats(e.bbox, 6);
        // v38: the object's document identity.
        w.str(e.info.doc);
        w.str(e.info.obj);
        w.str(e.info.label);
        w.str(e.info.type);
        // v55: whether this publish held part of the object back.
        w.b(e.incomplete);
        writeGroupRef(w, e);
        if (bytesFor) {
            const std::vector<uint8_t> *bytes =
                *bytesFor ? (*bytesFor)(e.key) : nullptr;
            w.u8(bytes ? 1 : 0);
            if (bytes)
                w.bytes(*bytes);
        }
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
///
/// ...except what is deliberately kept out of the key. The v55
/// incomplete mark flips as the producer's capture backlog drains, and
/// in practice it flips WITH the draws that were being waited for --
/// but a delta that carried it only when some other thing moved would
/// depend on that coincidence, and the failure it hides is silent: a
/// consumer holding "incomplete" forever waits for a companion that
/// arrived. Cheaper to compare the bit than to rely on the argument.
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
            if (to[i].key != from[j].key
                    || to[i].incomplete != from[j].incomplete)
                changed.push_back(&to[i]);
            ++i;
            ++j;
        }
    }
}

/// The difference against \a base.
void writeObjectDelta(Writer &w,
                      const std::vector<SceneSnapshot::ObjectEntry> &entries,
                      const std::vector<SceneSnapshot::ObjectEntry> &base,
                      const ChunkBytesFor &bytesFor)
{
    std::vector<uint64_t> removed;
    std::vector<const SceneSnapshot::ObjectEntry *> changed;
    diffObjectPtrs(base, entries, changed, removed);
    writeObjectSection(w, removed, changed, &bytesFor);
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
    /// Where each deferred chunk landed, so that a key named a second
    /// time can add its object to the chunk already covering it. The
    /// memos above answer "is this key slotted"; this answers "which
    /// entry is it", which is what the reverse index needs
    /// (SceneDump.h, docs/SceneStreaming.md §6). Entries are only ever
    /// appended, so an index stays valid for the life of the snapshot
    /// — including across the move that commits it.
    std::map<std::string, size_t> chunkAt;
    /// The scene-level user shader lists, held as the shared objects
    /// their chunks will fill rather than copied into the config at
    /// read time — at read time they are still empty.
    std::vector<std::shared_ptr<const UserShader>> postShaders;
    std::vector<std::shared_ptr<const UserShader>> postSplices;
};

typedef std::shared_ptr<ManifestLoader> LoaderPtr;

RefReader manifestRefReader(const LoaderPtr &st, SceneSnapshot &snap);

/// Record that \a owner needs the chunk under \a key, and remember
/// where that chunk is. Called both where a key is first deferred and
/// where a later group names the same one — sharing is the case the
/// index exists for.
void noteChunkOwner(SceneSnapshot &snap, const LoaderPtr &st,
                    const std::string &key, size_t index, uint64_t owner)
{
    st->chunkAt[key] = index;
    if (!owner || index >= snap.deferredChunks.size())
        return;
    auto &owners = snap.deferredChunks[index].owners;
    // Repeats are common — every draw of a group names its material —
    // and the list is short, so a scan beats a set per chunk.
    if (std::find(owners.begin(), owners.end(), owner) == owners.end())
        owners.push_back(owner);
}

/// The same, for a key that may already have been deferred by an
/// earlier group. Does nothing when this snapshot never deferred it
/// (it rode inline, or it is a fresh key about to be deferred).
void noteExistingOwner(SceneSnapshot &snap, const LoaderPtr &st,
                       const std::string &key, uint64_t owner)
{
    auto it = st->chunkAt.find(key);
    if (it != st->chunkAt.end())
        noteChunkOwner(snap, st, key, it->second, owner);
}

/// One mesh reference inside a group: the counterpart of writeMesh.
std::shared_ptr<const MeshData> readMeshRef(Reader &r, SceneSnapshot &snap,
                                            const LoaderPtr &st,
                                            uint64_t owner)
{
    if (r.u8() == 0) {
        auto mesh = std::make_shared<OwnedMeshData>();
        mesh->cacheId = r.u64();
        readMeshChunk(r, mesh.get(), st->version);
        return r.ok ? mesh : nullptr;
    }
    SceneSnapshot::DeferredChunk c;
    if (!readMeshLevels(r, st->version, c))
        return nullptr;
    // The mesh's identity is its finest built level — the ladder's
    // other rungs are alternatives for the same slot, not meshes of
    // their own — so the dedup across groups keys on that. A copy,
    // because c is moved into the deferred list below and the key is
    // still needed to attribute the owner afterwards.
    const std::string key = c.key;
    auto it = st->meshes.find(key);
    if (it != st->meshes.end()) {
        // Already slotted by another group: nothing to defer, but this
        // object wants it too and its priority may be higher.
        noteExistingOwner(snap, st, key, owner);
        // The ladder can be newer under the same identity: a level
        // generated since this entry was slotted arrives as a keyed
        // entry where a declaration was, and this re-read reference IS
        // the announcement (§7, phase 5c). The entry itself — key,
        // fill, residency — is untouched; only the list of
        // alternatives is refreshed. The resident and asked rungs are
        // indices into that list, so they follow their KEYS into the
        // new one: geometry must stay resident under a renamed ladder
        // (§7, "the ladder owns its fetch state").
        auto at = st->chunkAt.find(key);
        if (at != st->chunkAt.end()
                && at->second < snap.deferredChunks.size()) {
            auto &slot = snap.deferredChunks[at->second];
            const auto remap = [&slot, &c](int rung) -> int {
                if (rung < 0 || size_t(rung) >= slot.levels.size())
                    return rung;
                const std::string &held = slot.levels[size_t(rung)].key;
                if (held.empty())
                    return -1;
                for (size_t i = 0; i < c.levels.size(); ++i) {
                    if (c.levels[i].key == held)
                        return int(i);
                }
                return -1;
            };
            uint16_t mask = 0;
            for (size_t r = 0; r < slot.levels.size() && r < 16; ++r) {
                if (!(slot.residentMask & uint16_t(1u << r)))
                    continue;
                const int to = remap(int(r));
                if (to >= 0 && to < 16)
                    mask |= uint16_t(1u << to);
            }
            slot.residentMask = mask;
            slot.asked = int16_t(remap(slot.asked));
            slot.levels = std::move(c.levels);
        }
        return it->second;
    }
    auto mesh = std::make_shared<OwnedMeshData>();
    mesh->cacheId = meshIdFromKey(key);
    st->meshes.emplace(key, mesh);
    uint32_t version = st->version;
    // The per-rung store (§7, "one rung per instance"). The entry's
    // own closures fill/release the parse-time identity rung — the
    // generic consumer's view of the ladder; a level-selecting
    // consumer talks to levelMeshes directly, one rung at a time.
    auto lm = std::make_shared<LevelMeshStore>(mesh, key, version);
    c.levelMeshes = lm;
    c.fill = [lm, key](SceneSnapshot &, const void *data, size_t size) {
        return lm->fill(key, data, size);
    };
    // The closure parses any rung of this ladder, so the ladder keeps
    // it for life: refetch-after-release and rung changes re-run it
    // (SceneDump.h, DeferredChunk::refill).
    c.refill = c.fill;
    // As in readMesh: geometry is the one payload with a coarser rung
    // to fall back to, so it is the one a viewer can give back (§6).
    c.release = [lm, key] { lm->release(key); };
    snap.deferredChunks.push_back(std::move(c));
    noteChunkOwner(snap, st, key, snap.deferredChunks.size() - 1, owner);
    return mesh;
}

/// The snapshot slot a material key occupies, deferring its chunk the
/// first time the key is seen. Draws carry the slot rather than the
/// material itself, because the chunk may well arrive after them.
int32_t materialSlot(SceneSnapshot &snap, const LoaderPtr &st,
                     const std::string &key, uint32_t size, uint64_t owner)
{
    auto it = st->matSlots.find(key);
    if (it != st->matSlots.end()) {
        noteExistingOwner(snap, st, key, owner);
        return it->second;
    }
    int32_t slot = int32_t(snap.materials.size());
    snap.materials.emplace_back();
    snap.materialFilled.push_back(0);
    st->matSlots.emplace(key, slot);
    SceneSnapshot::DeferredChunk c;
    c.key = key;
    c.size = size;
    // Where this chunk will sit, so that its own parse can hand its
    // owners down to the textures and shaders it names (SceneDump.h).
    // A material is read long after the group that asked for it, and
    // an appearance is wanted by every object wearing it, so nothing
    // else at that moment knows who those are.
    const size_t self = snap.deferredChunks.size();
    c.fill = [st, slot, self](SceneSnapshot &target, const void *data,
                              size_t size) {
        const size_t before = target.deferredChunks.size();
        bool ok = readChunk(data, size, [&](Reader &cr) {
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
            if (cr.ok && size_t(slot) < target.materialFilled.size())
                target.materialFilled[size_t(slot)] = 1;
        });
        if (self < target.deferredChunks.size()) {
            const auto owners = target.deferredChunks[self].owners;
            for (size_t i = before; i < target.deferredChunks.size(); ++i) {
                if (target.deferredChunks[i].owners.empty())
                    target.deferredChunks[i].owners = owners;
            }
        }
        return ok;
    };
    snap.deferredChunks.push_back(std::move(c));
    noteChunkOwner(snap, st, key, snap.deferredChunks.size() - 1, owner);
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
            tex = memoTexture(snap, tex);
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
            c.fill = [sh, st](SceneSnapshot &snap, const void *data,
                              size_t size) {
                return readChunk(data, size, [&](Reader &cr) {
                    if (cr.u32() != kChunkVersion) {
                        cr.ok = false;
                        return;
                    }
                    // The chunk's own layout revision gates the fields
                    // (checked just above), so it always reads current.
                    auto parsed = readUserShader(cr, kVersion, snap,
                                                 &st->textures);
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
                    const LoaderPtr &st, uint64_t owner)
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
        meshes.push_back(readMeshRef(r, snap, st, owner));
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
        slots.push_back(materialSlot(snap, st, key, size, owner));
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
    snap.groupFilled.push_back(0);
    st->targets.push_back(target);
    // Only a scene object has an entry, and only a scene object has a
    // bounding box to be prioritized by; the overlay and auxiliary
    // groups are owned by nobody and are fetched first.
    const uint64_t owner = entry ? entry->objectKey : 0;
    if (r.u8() == 0) {
        // Inline draws still name out-of-band meshes and materials,
        // and those are this object's just as much as a deferred
        // group's are.
        readGroupChunk(r, snap.groups[slot], snap, st, owner);
        snap.groupFilled[slot] = 1;
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
    if (owner)
        c.owners.push_back(owner);
    c.fill = [st, slot, owner](SceneSnapshot &target, const void *data,
                               size_t size) {
        if (slot >= target.groups.size())
            return false;
        DrawCallList draws;
        bool ok = readChunk(data, size, [&](Reader &cr) {
            readGroupChunk(cr, draws, target, st, owner);
        });
        if (ok) {
            target.groups[slot] = std::move(draws);
            if (slot < target.groupFilled.size())
                target.groupFilled[slot] = 1;
        }
        return ok;
    };
    snap.deferredChunks.push_back(std::move(c));
    st->chunkAt[key] = snap.deferredChunks.size() - 1;
}

/// Whether everything \a draws needs in order to look right is in
/// hand. Not whether it can be drawn — it always can — but whether
/// drawing it now would show something the model does not say.
///
/// The appearance is a chain, and every link of it arrives separately:
/// a draw names a material by slot, the material chunk names a texture
/// by key, and the pixels come after that again. A draw taken between
/// any two of those renders in a colour or a surface that is not the
/// object's, and it is taken *once* — so it does not correct itself
/// when the rest lands (docs/SceneStreaming.md §6). Waiting is what the
/// coarse rungs are for.
bool appearanceResident(const DrawCallList &draws,
                        const SceneSnapshot &snap)
{
    for (const DrawCall &d : draws) {
        // Below zero the material rides in the draw, and so does
        // whatever it names: the monolithic layout defers nothing.
        if (d.materialIndex < 0)
            continue;
        size_t i = size_t(d.materialIndex);
        if (i >= snap.materialFilled.size() || !snap.materialFilled[i])
            return false;
        const Material &m = snap.materials[i];
        // A texture that was given up on has cleared its deferred flag
        // and stays empty, which is the draw rendering untextured —
        // the answer the entry already gave. Only one still on its way
        // is worth waiting for.
        if (m.texture && m.texture->deferred && m.texture->pixels.empty())
            return false;
    }
    return true;
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
        // A feed takes a group only once, and only when it has actually
        // arrived: this runs again on every chunk that lands, and a
        // group already handed over is an empty one, not an empty feed.
        // Scene groups are left where they are for the model to take.
        for (size_t i = 0; i < st->targets.size() && i < s.groups.size(); ++i) {
            const GroupTarget &t = st->targets[i];
            if (t.kind == GroupTarget::Scene
                    || i >= s.groupFilled.size() || !s.groupFilled[i])
                continue;
            DrawCallList &draws = s.groups[i];
            // The same rule the scene objects are taken under, for the
            // feeds that have no coarse rung to fall back to: a
            // navigation cube with its labels still in flight is a
            // white block, and it is taken once, so for these "not
            // yet" *is* the rung. The keyless draws are not among
            // them — they stand in on their own bounds like any
            // object, and holding them back would show less than is
            // known (§6).
            switch (t.kind) {
            case GroupTarget::Scene:
                break;
            case GroupTarget::Keyless:
                s.keyless = std::move(draws);
                break;
            case GroupTarget::Overlay:
                if (!appearanceResident(draws, s))
                    continue;
                if (t.index < s.overlays.size())
                    s.overlays[t.index].draws = std::move(draws);
                break;
            case GroupTarget::Selection:
                if (!appearanceResident(draws, s))
                    continue;
                if (t.index < s.selections.size())
                    s.selections[t.index].second = std::move(draws);
                break;
            case GroupTarget::Highlight:
                if (!appearanceResident(draws, s))
                    continue;
                s.highlight = std::move(draws);
                break;
            }
            s.groupFilled[i] = 0;
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
            if (d.material.texturepalette) {
                for (const auto &e : d.material.texturepalette->entries)
                    addTex(e);
            }
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
        // A delta carries its changed manifests inline (v37); the sink
        // has the bytes but never gives them back, so keep a copy of
        // each while it is in hand.
        std::map<std::string, std::vector<uint8_t>> chunkCopies;
        for (const auto &group : byKey) {
            SceneSnapshot::ObjectEntry entry;
            std::vector<uint8_t> copy;
            if (!storeGroup(group.second, group.first, mst, entry,
                            snap.baseVersion ? &copy : nullptr)) {
                w.ok = false;
                break;
            }
            // v55: the object is incomplete if ANY of its draws says so
            // -- the mark is carried by the draws whose companion was
            // deferred, and the contract asks the question of the
            // object. Read off the draws rather than from a map,
            // because it is the draws the producer stamped.
            for (const DrawCall *d : group.second) {
                if (d->objectIncomplete) {
                    entry.incomplete = true;
                    break;
                }
            }
            if (snap.objectInfo) {
                auto it = snap.objectInfo->find(group.first);
                if (it != snap.objectInfo->end())
                    entry.info = it->second;
            }
            // The label a viewer shows, joined on at the one place that
            // needs it. It is not carried by the identity map because it
            // says nothing about geometry: it changes when a user
            // renames something, and never with a mesh. An entry whose
            // identity already carries a label keeps it — a caller may
            // hand the writer a fully resolved map (the dump tests do).
            if (snap.objectMeta && entry.info.label.empty()
                    && !entry.info.doc.empty()) {
                auto d = snap.objectMeta->find(entry.info.doc);
                if (d != snap.objectMeta->end()) {
                    auto o = d->second.find(entry.info.obj);
                    if (o != d->second.end()) {
                        entry.info.label = o->second.label;
                        if (entry.info.type.empty())
                            entry.info.type = o->second.type;
                    }
                }
            }
            if (snap.baseVersion)
                chunkCopies[entry.key] = std::move(copy);
            entries.push_back(std::move(entry));
        }

        if (snap.rootSpans)
            snap.rootSpans->listBegin = w.pos();
        if (!snap.baseVersion)
            writeObjectList(w, entries);
        else
            writeObjectDelta(w, entries, snap.baseObjects,
                             [&](const std::string &key)
                                 -> const std::vector<uint8_t> * {
                                 auto it = chunkCopies.find(key);
                                 return it == chunkCopies.end()
                                     ? nullptr : &it->second;
                             });
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
            writeUserShader(w, *s, snap, nullptr);
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
        // v55: the objects this capture holds only part of. A list of
        // keys rather than a bit per draw: this layout has no object
        // section to hang it on, and the mark is an object's whatever
        // the draws are. Almost always four zero bytes -- a capture is
        // usually taken of a settled scene, and the mark exists for the
        // one that is not.
        std::set<uint64_t> incomplete;
        for (const auto &d : snap.scene) {
            if (d.objectKey && d.objectIncomplete)
                incomplete.insert(d.objectKey);
        }
        w.u32(uint32_t(incomplete.size()));
        for (uint64_t key : incomplete)
            w.u64(key);
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

    w.b(snap.cavityconf.enabled);
    w.f(snap.cavityconf.valley);
    w.f(snap.cavityconf.ridge);
    w.f(snap.cavityconf.radius);

    w.b(snap.matcapconf.enabled);
    w.i32(snap.matcapconf.preset);
    w.f(snap.matcapconf.tint);

    w.b(snap.pbrconf.enabled);
    w.f(snap.pbrconf.metallic);
    w.f(snap.pbrconf.roughness);
    w.i32(snap.pbrconf.envPreset);
    w.f(snap.pbrconf.envIntensity);
    w.b(snap.pbrconf.envBackground);
    // v70: how far out of focus that background is.
    w.f(snap.pbrconf.envBlur);
    refs.tex(w, snap.pbrconf.envImage);
    w.b(snap.pbrconf.fromSpecular);
    // v61: how a Phong shininess becomes a roughness.
    w.i32(snap.pbrconf.shininessMapping);

    w.f(snap.bumpconf.scale);
    w.b(snap.bumpconf.parallax);

    writeLight(w, snap.lightconf, refs);

    // v52: the ordinary Coin lights. Only the filled slots go out --
    // `count` bounds the loop on both sides.
    const ViewLightConfig &vlc = snap.viewlightconf;
    w.b(vlc.fed);
    w.u32(vlc.ambient);
    w.u32(uint32_t(vlc.count));
    for (int i = 0; i < vlc.count && i < MaxViewLights; ++i) {
        const ViewLight &vl = vlc.lights[i];
        w.floats(vl.direction, 3);
        w.floats(vl.position, 3);
        w.b(vl.positional);
        w.floats(vl.attenuation, 3);
        w.u32(vl.color);
        w.f(vl.intensity);
        // v54: the cone of a spot light.
        w.b(vl.spot);
        w.f(vl.cutOffAngle);
        w.f(vl.dropOffRate);
        // v56: camera-relative lights carry the eye-space direction the
        // world one was derived from, because the consumer's camera is
        // not this one's (see ViewLight::eyeSpace).
        w.b(vl.eyeSpace);
        w.floats(vl.eyeDirection, 3);
        w.floats(vl.eyePosition, 3);
    }

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
    // v40: impact rings (the particle impact map, §5.8).
    w.f(wc.impactStrength); w.f(wc.impactLife);

    // v18: bloom.
    const BloomConfig &blc = snap.bloomconf;
    w.b(blc.enabled); w.f(blc.threshold); w.f(blc.intensity);
    w.f(blc.radius);

    // v60: the output colour transform. v62 adds its exposure.
    w.i32(snap.outconf.transform);
    w.f(snap.outconf.exposure);

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
        w.i32(a.subView);   // v69
        writeFeed(ov.draws, 0, true);
    }

    // v10: preselection + selection highlight config (client-side styling).
    for (const PreselHighlightConfig *c : {&snap.preselconf, &snap.selconf}) {
        w.u32(c->color);
        w.f(c->outlineWidth);
        w.b(c->faceOutline);
        w.b(c->outlineOnly);
        w.f(c->pickRadius);  // v11
        w.f(c->loupeLift);   // v41
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
            shaders.push_back(readUserShader(r, version, snap, nullptr));
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

    // v55: the objects the capture holds only part of, stamped back
    // onto their draws -- which is where the contract reads it. An
    // older capture names none, so every object reads complete, which
    // is what a writer of that version believed anyway.
    if (version < 55)
        return;
    uint32_t nincomplete = r.u32();
    if (!r.ok || nincomplete > 0x1000000u) {
        r.ok = false;
        return;
    }
    std::set<uint64_t> incomplete;
    for (uint32_t i = 0; r.ok && i < nincomplete; ++i)
        incomplete.insert(r.u64());
    if (!r.ok || incomplete.empty())
        return;
    for (auto &d : snap.scene)
        d.objectIncomplete = incomplete.count(d.objectKey) != 0;
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
    snap.materialFilled.clear();
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
            if (version >= 38) {
                r.str(up.entry.info.doc, 0x1000u);
                r.str(up.entry.info.obj, 0x1000u);
                r.str(up.entry.info.label, 0x1000u);
                r.str(up.entry.info.type, 0x1000u);
            }
            if (version >= 55)
                up.entry.incomplete = r.b();
            up.group = snap.groups.size();
            GroupTarget t;
            t.kind = GroupTarget::Scene;
            // The group's key is what readGroup consumes next; keep it
            // so the model can tell an unchanged object from a changed
            // one without re-reading the chunk.
            const size_t before = snap.deferredChunks.size();
            readGroup(r, snap, st, t, &up.entry);
            // v37: a delta rides each changed manifest's bytes right
            // behind its reference. They land on the entry readGroup
            // just deferred; the consumer ingests them under the key
            // (store included) instead of fetching a payload that no
            // cache has ever seen.
            if (version >= 37 && snap.baseVersion && r.ok && r.u8()) {
                std::vector<uint8_t> bytes;
                r.bytes(bytes);
                if (r.ok && snap.deferredChunks.size() > before)
                    snap.deferredChunks.back().inlineData =
                        std::move(bytes);
            }
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

    if (version >= 42) {
        snap.cavityconf.enabled = r.b();
        snap.cavityconf.valley = r.f();
        snap.cavityconf.ridge = r.f();
        if (version >= 44)
            snap.cavityconf.radius = r.f();
    }
    if (version >= 43) {
        snap.matcapconf.enabled = r.b();
        snap.matcapconf.preset = r.i32();
        snap.matcapconf.tint = r.f();
    }

    snap.pbrconf.enabled = r.b();
    snap.pbrconf.metallic = r.f();
    snap.pbrconf.roughness = r.f();
    snap.pbrconf.envPreset = version >= 64 ? r.i32() : 1;
    snap.pbrconf.envIntensity = r.f();
    snap.pbrconf.envBackground = version >= 25 ? r.b() : false;
    // 0.375, not the current default: an older snapshot was written by
    // a build that drew the background two levels down a 128 cubemap,
    // and that is the blur which stands for the same softness on the
    // sharper background map this reads it onto.
    snap.pbrconf.envBlur = version >= 70 ? r.f() : 0.375f;
    if (version >= 25)
        refs.tex(r, snap.pbrconf.envImage);
    snap.pbrconf.fromSpecular = version >= 59 ? r.b() : false;
    snap.pbrconf.shininessMapping = version >= 61 ? r.i32() : 0;
    // (0, not the current default: an older snapshot was written
    // under the GL-exponent reading and has to keep being drawn
    // with it.)

    snap.bumpconf.scale = r.f();
    snap.bumpconf.parallax = r.b();

    readLight(r, snap.lightconf, refs, version);

    if (version >= 52) {
        ViewLightConfig &vlc = snap.viewlightconf;
        vlc.fed = r.b();
        if (version >= 53)
            vlc.ambient = r.u32();
        int n = int(r.u32());
        // A writer with a larger MaxViewLights than this build must not
        // desync the stream: read every light it sent, keep the ones
        // there is room for.
        vlc.count = 0;
        for (int i = 0; i < n; ++i) {
            ViewLight vl;
            r.floats(vl.direction, 3);
            r.floats(vl.position, 3);
            vl.positional = r.b();
            r.floats(vl.attenuation, 3);
            vl.color = r.u32();
            vl.intensity = r.f();
            if (version >= 54) {
                vl.spot = r.b();
                vl.cutOffAngle = r.f();
                vl.dropOffRate = r.f();
            }
            if (version >= 56) {
                vl.eyeSpace = r.b();
                r.floats(vl.eyeDirection, 3);
                r.floats(vl.eyePosition, 3);
            }
            if (vlc.count < MaxViewLights)
                vlc.lights[vlc.count++] = vl;
        }
    }

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
    if (version >= 40) {
        wc.impactStrength = r.f(); wc.impactLife = r.f();
    }

    snap.bloomconf = BloomConfig();
    if (version >= 18) {
        BloomConfig &blc = snap.bloomconf;
        blc.enabled = r.b(); blc.threshold = r.f();
        blc.intensity = r.f(); blc.radius = r.f();
    }

    snap.outconf = OutputConfig();
    // Explicitly, not by leaving the struct default: that default is
    // now SRGB, and a snapshot written before v60 was NOT colour
    // managed -- it has to keep being drawn the way it was written.
    snap.outconf.transform = version >= 60
        ? r.i32() : int(OutputConfig::None);
    snap.outconf.exposure = version >= 62 ? r.f() : 1.0f;

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
            a.subView = version >= 69 ? r.i32() : 0;
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
            if (version >= 41)
                c->loupeLift = r.f();
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

size_t Render::SceneObjectModel::unresolved() const
{
    size_t n = 0;
    for (const auto &entry : objects) {
        if (!entry.second.resolved())
            ++n;
    }
    return n;
}

bool Render::SceneObjectModel::boundBox(float *min3, float *max3) const
{
    bool any = false;
    for (const auto &entry : objects) {
        const float *b = entry.second.entry.bbox;
        // The producer writes an empty box as one that is inside out,
        // which would otherwise swallow the origin and pull the fit
        // towards it.
        if (b[0] > b[3] || b[1] > b[4] || b[2] > b[5])
            continue;
        for (int i = 0; i < 3; ++i) {
            min3[i] = any ? std::min(min3[i], b[i]) : b[i];
            max3[i] = any ? std::max(max3[i], b[3 + i]) : b[3 + i];
        }
        any = true;
    }
    return any;
}

const std::shared_ptr<const Render::MeshData> &Render::standInMesh()
{
    struct Box {
        float positions[24 * 3];
        float normals[24 * 3];
        int32_t indices[36];
    };
    static const std::shared_ptr<const MeshData> mesh = [] {
        // Six quads on [0,1]³, each with its own normal so the box
        // shades as a box rather than as a rounded blob. Every face is
        // wound counter-clockwise seen from outside (u × v = n), which
        // is what Material::ccw says a front face is.
        static const float face[6][3][3] = {
            // origin        u              v          (normal = u × v)
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},   // +X
            {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // -X
            {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},   // +Y
            {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}},   // -Y
            {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   // +Z
            {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}},   // -Z
        };
        auto box = std::make_shared<Box>();
        for (int f = 0; f < 6; ++f) {
            const float *o = face[f][0];
            const float *u = face[f][1];
            const float *v = face[f][2];
            const float n[3] = {u[1] * v[2] - u[2] * v[1],
                                u[2] * v[0] - u[0] * v[2],
                                u[0] * v[1] - u[1] * v[0]};
            for (int c = 0; c < 4; ++c) {
                // The quad, walked o → o+u → o+u+v → o+v.
                const float su = (c == 1 || c == 2) ? 1.0f : 0.0f;
                const float sv = (c == 2 || c == 3) ? 1.0f : 0.0f;
                float *p = box->positions + (f * 4 + c) * 3;
                float *q = box->normals + (f * 4 + c) * 3;
                for (int i = 0; i < 3; ++i) {
                    p[i] = o[i] + u[i] * su + v[i] * sv;
                    q[i] = n[i];
                }
            }
            static const int corner[6] = {0, 1, 2, 0, 2, 3};
            for (int i = 0; i < 6; ++i)
                box->indices[f * 6 + i] = f * 4 + corner[i];
        }
        auto data = std::make_shared<MeshData>();
        // An id no streamed or bundled mesh can hold, so the backend
        // uploads the box once and never confuses it for geometry:
        // meshIdFromKey() fills 60 bits below the top one, and an
        // inline cacheId is a counter that sets neither.
        data->cacheId = 0xC000000000000001ull;
        data->owner = box;
        data->numVertices = 24;
        data->positions = box->positions;
        data->normals = box->normals;
        data->triangleIndices = box->indices;
        data->numTriangleIndices = 36;
        return std::shared_ptr<const MeshData>(data);
    }();
    return mesh;
}

/// Whether the mesh a draw names is in hand. A deferred mesh chunk
/// starts life as an empty MeshData and is filled in place when it
/// lands, so a draw can be assembled well before its geometry is
/// (docs/SceneStreaming.md §6) — and an empty mesh has no positions
/// array behind its vertex count, which the backend reaches through
/// the shadow, outline and instancing paths as well as the submit one.
/// Leaving the draw out is the coarsest rung of the ladder: nothing
/// yet. A draw naming no mesh at all is not geometry and passes.
static bool meshResident(const Render::DrawCall &d)
{
    return !d.mesh || (d.mesh->numVertices > 0 && d.mesh->positions);
}

/// Whether every appearance a group's draws name has arrived. Unlike a
/// mesh, a material is copied into the draw and the reference is gone
/// (finalize, SceneDump.cpp), so a group taken before its materials
/// land would keep a default-constructed one for as long as the object
/// lives. Waiting is per group and costs nothing: a material chunk is
/// a few hundred bytes and its objects follow it by one round.
/// A coarse stand-in on the bounds \a bmin … \a bmax, carrying over
/// whatever \a tmpl says about the object it stands for. False when
/// the bounds say nothing: an empty box is written inside out, and
/// there is no coarse answer to give for an extent that is not known.
static bool makeStandIn(Render::DrawCall &box, const Render::DrawCall &tmpl,
                        const float *bmin, const float *bmax)
{
    for (int i = 0; i < 3; ++i) {
        if (bmin[i] > bmax[i])
            return false;
    }
    box = tmpl;
    box.standIn = true;
    box.mesh = Render::standInMesh();
    box.material.type = Render::Material::Triangle;
    box.partIndex = -1;
    box.indexStart = 0;
    box.indexCount = 0;
    // It keeps the object's real appearance — the coarse scene reads as
    // the model in the right colours rather than as grey scaffolding —
    // but not the parts of it that describe a surface the box does not
    // have. Per-vertex colour is the one that would be read off an
    // array the box has no equivalent of; textures are already
    // conditional on texture coordinates.
    box.material.pervertexcolor = false;
    // A stand-in occupies space but is not the shape, and the
    // difference is exactly the passes it drops. It keeps writing
    // depth, including in the prepass: a bounding box is a
    // conservative bound of what it replaces, so the scene behind a
    // coarse object stays hidden instead of showing through and then
    // popping when the geometry lands.
    //
    // What it gives up is everything that describes a surface rather
    // than occupancy. It casts no shadow — a box-shaped shadow reads
    // as a modelling error rather than as progress — while still
    // receiving one, so it is lit like the object it stands for. Nor
    // does it take an outline, a hidden-line or a section-capping
    // pass: those would all draw the box's own edges as if they were
    // the model's.
    box.material.shadowstyle &= uint8_t(~1);
    box.material.outline = false;
    box.material.faceoutline = false;
    box.material.outlineonly = false;
    box.material.solidshape = false;
    // The bounds are world space, so the placement is the box's whole
    // model matrix: scale onto the extent, translate to the corner.
    // Whatever transform produced those bounds is already inside them.
    std::fill(box.model, box.model + 16, 0.0f);
    for (int i = 0; i < 3; ++i) {
        box.model[i * 5] = bmax[i] - bmin[i];
        box.model[12 + i] = bmin[i];
        box.bboxMin[i] = bmin[i];
        box.bboxMax[i] = bmax[i];
    }
    box.model[15] = 1.0f;
    box.identity = false;
    return true;
}

/// Append \a draws to \a scene at the best rung each one has: itself
/// when its mesh is in hand, otherwise a coarse stand-in on its bounds
/// (docs/SceneStreaming.md §6).
///
/// One box per *mesh*, not per draw. A mesh is typically named by one
/// draw per face part, and every one of them carries the bounding box
/// of the same geometry, so a box each would be that many coincident
/// boxes — the overdraw of a solid object drawn many times over, for
/// one silhouette. The first draw naming a mesh is the one that stands
/// in for it, and it stands in whole: no part index, no index range.
///
/// Only surfaces get a stand-in. A box drawn for an object's edge or
/// vertex mesh would add a second, solid silhouette over the one its
/// triangles already gave, and neither reads as the wireframe it
/// replaces; those draws simply wait, as everything did before.
static void appendAtBestRung(Render::DrawCallList &scene,
                             const Render::DrawCallList &draws,
                             Render::SceneObjectModel *model)
{
    std::set<const Render::MeshData *> stoodIn;
    /// Role ordinals, counted over EVERY geometry draw whether or not
    /// it resolved, so the numbering is the same for the list that
    /// recorded an identity and the re-keyed list that asks for it.
    std::map<std::pair<uint64_t, int>, uint32_t> ordinals;
    for (const Render::DrawCall &d : draws) {
        const bool contentId = d.mesh && (d.mesh->cacheId >> 63);
        const uint32_t ordinal = d.mesh
            ? ordinals[{d.objectKey, int(d.material.type)}]++
            : 0;
        const auto role = std::make_tuple(d.objectKey,
                                          int(d.material.type), ordinal);
        // Per-instance rung binding (§7, "one rung per instance"):
        // the consumer may know a better rung for THIS draw's owner
        // than the ladder's identity mesh — the far instance stands
        // on its own coarse rung while a near sibling holds exact.
        // Null falls through to everything below, unchanged.
        if (model && model->rungBinder && d.mesh && d.objectKey) {
            if (auto bound = model->rungBinder(d)) {
                if (contentId) {
                    model->lastGood[d.mesh->cacheId] = bound;
                    auto &rd = model->lastRole[role];
                    rd.mesh = bound;
                    rd.draw = d;
                    rd.draw.mesh.reset();
                }
                Render::DrawCall b = d;
                b.mesh = std::move(bound);
                scene.push_back(std::move(b));
                continue;
            }
        }
        if (meshResident(d)) {
            if (model && contentId) {
                model->lastGood[d.mesh->cacheId] = d.mesh;
                if (d.objectKey) {
                    auto &rd = model->lastRole[role];
                    rd.mesh = d.mesh;
                    rd.draw = d;
                    rd.draw.mesh.reset();
                }
            }
            scene.push_back(d);
            continue;
        }
        // Not filled yet — but the same content (the id is the content
        // key) may still be live from before a re-parse. Substituted
        // into the emitted scene only, never into the model's draws:
        // when the fresh object's fill lands, the next assembly emits
        // it and the bridge ends by itself (SceneObjectModel::lastGood).
        if (model && contentId) {
            auto it = model->lastGood.find(d.mesh->cacheId);
            if (it != model->lastGood.end()) {
                auto held = it->second.lock();
                if (held && held->numVertices > 0 && held->positions) {
                    Render::DrawCall bridged = d;
                    bridged.mesh = std::move(held);
                    scene.push_back(std::move(bridged));
                    continue;
                }
            }
        }
        // Same content gone too — but the same IDENTITY may still be
        // live: an edit re-keys the object's meshes, so the
        // content-addressed bridge cannot answer by construction, and
        // the object used to drop to its box for the length of two
        // fetches. The old draw rides whole — arrays are only correct
        // under their own index ranges and placement — showing the
        // object as it was until the new geometry lands and the next
        // assembly ends the bridge (SceneObjectModel::lastRole).
        if (model && d.objectKey) {
            auto it = model->lastRole.find(role);
            if (it != model->lastRole.end()) {
                auto held = it->second.mesh.lock();
                if (held && held->numVertices > 0 && held->positions) {
                    Render::DrawCall bridged = it->second.draw;
                    bridged.mesh = std::move(held);
                    scene.push_back(std::move(bridged));
                    continue;
                }
            }
        }
        if (d.material.type != Render::Material::Triangle
                || !stoodIn.insert(d.mesh.get()).second)
            continue;
        Render::DrawCall box;
        if (makeStandIn(box, d, d.bboxMin, d.bboxMax))
            scene.push_back(std::move(box));
    }
}

bool Render::applySceneObjects(SceneSnapshot &snap, SceneObjectModel &model)
{
    if (snap.baseVersion && snap.baseVersion != model.version
            && snap.manifestVersion != model.version) {
        // A difference against something this consumer does not hold.
        // There is nothing to be salvaged from it — the entries it does
        // not mention are exactly the ones it assumes are already
        // right — so the caller has to be given a full root. The model
        // already standing at this publish's own version is the other
        // case: that is this delta being assembled a second time as
        // more of it arrives, which is refinement, not a gap.
        return false;
    }
    if (!snap.baseVersion) {
        // A complete list replaces the model — by retiring what it does
        // not name, not by emptying it. The difference matters once
        // this runs more than once for the same publish: clearing would
        // throw away the draws an earlier pass already took, and the
        // chunks that carried them are long since consumed.
        std::set<uint64_t> named;
        for (const auto &up : snap.objectUpdates)
            named.insert(up.entry.objectKey);
        for (auto it = model.objects.begin(); it != model.objects.end();) {
            if (named.count(it->first))
                ++it;
            else
                it = model.objects.erase(it);
        }
    }
    for (uint64_t key : snap.objectsRemoved)
        model.objects.erase(key);
    for (auto &up : snap.objectUpdates) {
        auto &obj = model.objects[up.entry.objectKey];
        obj.entry = up.entry;
        // The entry is what the root said and is always current; the
        // draws are what a chunk carried and may not have arrived yet.
        // Taking them only once, and only when they did, is what lets
        // this run again as the rest of the publish lands — an object
        // still waiting keeps what it was last drawn as rather than
        // being emptied (docs/SceneStreaming.md §6).
        if (up.group < snap.groups.size() && up.group < snap.groupFilled.size()
                && snap.groupFilled[up.group]
                && appearanceResident(snap.groups[up.group], snap)) {
            obj.draws = std::move(snap.groups[up.group]);
            obj.drawsKey = up.entry.key;
            snap.groupFilled[up.group] = 0;
        }
    }
    model.version = snap.manifestVersion;

    // The feed, in objectKey order, then the draws the producer could
    // not name — which belong to no object and are re-sent whole every
    // publish. Rebuilt from the model rather than accumulated, so that
    // building it twice is building it once — which is what lets this
    // run on every arrival and not only the last.
    //
    // First shed the bridge entries that no longer bridge anything:
    // expired (nothing draws that copy), or emptied in place — which
    // is what a release does, so an evicted mesh still shows the box
    // eviction decided on rather than a stale twin.
    for (auto it = model.lastGood.begin(); it != model.lastGood.end();) {
        auto held = it->second.lock();
        if (held && held->numVertices > 0 && held->positions)
            ++it;
        else
            it = model.lastGood.erase(it);
    }
    for (auto it = model.lastRole.begin(); it != model.lastRole.end();) {
        auto held = it->second.mesh.lock();
        if (held && held->numVertices > 0 && held->positions
                && model.objects.count(std::get<0>(it->first)))
            ++it;
        else
            it = model.lastRole.erase(it);
    }
    DrawCallList scene;
    for (const auto &entry : model.objects) {
        const SceneObjectModel::Object &obj = entry.second;
        if (obj.draws.empty() && obj.drawsKey.empty()) {
            // The rung below the per-mesh boxes: the root has been read
            // and this object's manifest has not, so the only thing
            // known about it is the box the root named — one box for
            // the whole object, which is what makes a model appear at
            // all on a cold load rather than after its first group
            // manifests land (docs/SceneStreaming.md §6).
            //
            // Its appearance is the default one, because at this rung
            // there is genuinely no other: a material is named by a
            // group manifest, and that is the thing still missing. One
            // manifest later it is the object's own colour.
            DrawCall whole;
            whole.objectKey = entry.first;
            DrawCall box;
            if (makeStandIn(box, whole, obj.entry.bbox, obj.entry.bbox + 3))
                scene.push_back(std::move(box));
            continue;
        }
        appendAtBestRung(scene, obj.draws, &model);
    }
    appendAtBestRung(scene, snap.keyless, &model);
    // v55: the incomplete mark comes down on the object entry, not in
    // the group chunk, so it is stamped onto the draws here -- at the
    // one point where an object's current entry and its draws are both
    // in hand. Stamped rather than merged, and on every assembly:
    // a draw can outlive the entry that was current when it arrived
    // (an object stands on its old geometry while new geometry is in
    // flight), and a mark left standing after the companion landed is
    // a companion waited for forever.
    std::set<uint64_t> incomplete;
    for (const auto &entry : model.objects) {
        if (entry.second.entry.incomplete)
            incomplete.insert(entry.first);
    }
    const bool anyIncomplete = !incomplete.empty();
    for (auto &d : scene)
        d.objectIncomplete = anyIncomplete
            && incomplete.count(d.objectKey) != 0;
    snap.scene = std::move(scene);
    return true;
}

size_t Render::carryResidentRungs(SceneSnapshot &fresh,
                                  const SceneSnapshot &old)
{
    // Everything the superseded scene holds filled, by content key.
    // Content keys are hashes of the bytes, so a key either names the
    // same mesh in both scenes or appears in only one — identity is
    // exactly what makes the transfer safe across a re-parse that
    // renamed every ladder and re-numbered every rung.
    std::map<std::string, std::shared_ptr<OwnedMeshData>> held;
    for (const auto &entry : old.deferredChunks) {
        if (!entry.release || !entry.residentMask || !entry.levelMeshes)
            continue;
        auto *store = dynamic_cast<LevelMeshStore *>(entry.levelMeshes.get());
        if (!store)
            continue;
        const size_t count = std::min<size_t>(planRungs(entry), 16);
        for (size_t r = 0; r < count; ++r) {
            if (!(entry.residentMask & uint16_t(1u << r)))
                continue;
            const std::string &key = planRungKey(entry, r);
            if (key.empty())
                continue;
            if (auto mesh = store->owned(key))
                held.emplace(key, std::move(mesh));
        }
    }
    if (held.empty())
        return 0;
    // Hand each still-declared rung to the fresh ladder that names it.
    // The fresh entry keeps its own parse-born closures — adoption
    // moves geometry, not machinery — so a later release-and-climb
    // fetches through the new snapshot exactly as if the rung had
    // arrived over the wire.
    size_t adopted = 0;
    for (auto &entry : fresh.deferredChunks) {
        if (!entry.release || !entry.levelMeshes)
            continue;
        auto *store = dynamic_cast<LevelMeshStore *>(entry.levelMeshes.get());
        if (!store)
            continue;
        const size_t count = std::min<size_t>(planRungs(entry), 16);
        for (size_t r = 0; r < count; ++r) {
            if (entry.residentMask & uint16_t(1u << r))
                continue;
            const std::string &key = planRungKey(entry, r);
            if (key.empty())
                continue;
            auto it = held.find(key);
            if (it == held.end())
                continue;
            if (store->adopt(key, it->second)) {
                entry.residentMask |= uint16_t(1u << r);
                ++adopted;
            }
        }
    }
    return adopted;
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
        std::vector<uint8_t> &out,
        const ChunkBytesFor &bytesFor)
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
    writeObjectSection(w, removed, refs, &bytesFor);
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

bool Render::writeObjectSection(
        std::vector<uint8_t> &out,
        const std::vector<uint64_t> &removed,
        const std::vector<SceneSnapshot::ObjectEntry> &entries,
        const ChunkBytesFor *bytesFor)
{
    Writer w;
    w.vec = &out;
    std::vector<const SceneSnapshot::ObjectEntry *> refs;
    refs.reserve(entries.size());
    for (const auto &e : entries)
        refs.push_back(&e);
    ::writeObjectSection(w, removed, refs, bytesFor);
    return w.ok;
}

bool Render::readObjectSection(
        const uint8_t *&p, const uint8_t *end,
        bool deltaForm,
        std::vector<uint64_t> &removed,
        std::vector<ObjectSectionEntry> &entries)
{
    // A bounds-checked cursor over the section bytes. The limits
    // mirror the snapshot reader's (loadSnapshotFp): identity strings
    // 0x1000, chunk keys 128, inline chunks 0x2000000, list sizes
    // 0x1000000.
    bool ok = true;
    auto raw = [&](void *dst, size_t n) {
        if (!ok || size_t(end - p) < n) {
            ok = false;
            return;
        }
        std::memcpy(dst, p, n);
        p += n;
    };
    auto rU8 = [&]() { uint8_t v = 0; raw(&v, 1); return v; };
    auto rU32 = [&]() { uint32_t v = 0; raw(&v, 4); return v; };
    auto rU64 = [&]() { uint64_t v = 0; raw(&v, 8); return v; };
    auto rStr = [&](std::string &s, uint32_t maxLen) {
        uint32_t len = rU32();
        if (!ok || len > maxLen || size_t(end - p) < len) {
            ok = false;
            return;
        }
        s.assign(reinterpret_cast<const char *>(p), len);
        p += len;
    };

    uint32_t nremoved = rU32();
    if (!ok || nremoved > 0x1000000u)
        return false;
    removed.reserve(removed.size() + nremoved);
    for (uint32_t i = 0; ok && i < nremoved; ++i)
        removed.push_back(rU64());

    uint32_t nobj = rU32();
    if (!ok || nobj > 0x1000000u)
        return false;
    entries.reserve(entries.size() + nobj);
    for (uint32_t i = 0; ok && i < nobj; ++i) {
        ObjectSectionEntry e;
        e.entry.objectKey = rU64();
        raw(e.entry.bbox, 6 * sizeof(float));
        rStr(e.entry.info.doc, 0x1000u);
        rStr(e.entry.info.obj, 0x1000u);
        rStr(e.entry.info.label, 0x1000u);
        rStr(e.entry.info.type, 0x1000u);
        e.entry.incomplete = rU8() != 0;
        // Always the out-of-band reference form (writeGroupRef): a
        // format riding this codec has no inline-group alternative.
        if (rU8() != 1)
            return false;
        rStr(e.entry.key, 128);
        e.entry.size = rU32();
        if (deltaForm && rU8()) {
            uint32_t len = rU32();
            if (!ok || len > 0x2000000u || size_t(end - p) < len)
                return false;
            e.inlineData.assign(p, p + len);
            e.hasInline = true;
            p += len;
        }
        if (!ok)
            return false;
        entries.push_back(std::move(e));
    }
    return ok;
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

//////////////////////////////////////////////////////////////////////
// Material identity

/// A material as one value, for ProxyInstance::materialBucket
/// (docs/FarFieldProxies.md §5.1: a proxy is generated per (cell,
/// material bucket), so that every proxy carries exactly one material
/// and nothing is ever averaged).
///
/// It lives here, next to the serializer, for the reason collectMaterials
/// already states about its own table: **equality is the serialized
/// bytes, which is exact by construction** — two materials that write
/// the same bytes restore identically — and needs no hand-written
/// comparison over some sixty fields to stay in step with the format. A
/// field added to writeMaterial is therefore covered here on the same
/// commit, where a separate hash would have silently merged two
/// materials into one bucket and *overstated* what aggregation buys.
///
/// Textures and shaders enter by pointer identity rather than by table
/// index: the bucket asks whether two draws can share one merged mesh,
/// and sharing a texture object is exactly that question.
uint64_t Render::materialIdentity(const Material &m)
{
    std::vector<uint8_t> bytes;
    Writer w;
    w.vec = &bytes;
    RefWriter refs;
    refs.tex = [](Writer &cw, const std::shared_ptr<const TextureImage> &t) {
        cw.u64(uint64_t(reinterpret_cast<uintptr_t>(t.get())));
    };
    refs.shader = [](Writer &cw, const UserShader *s) {
        cw.u64(uint64_t(reinterpret_cast<uintptr_t>(s)));
    };
    writeMaterial(w, m, refs);
    uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (uint8_t byte : bytes) {
        h ^= uint64_t(byte);
        h *= 1099511628211ULL;
    }
    return h;
}

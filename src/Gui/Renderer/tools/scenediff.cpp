/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

/*!
 * \file
 * Structural diff of two scene snapshots — the correctness argument for
 * publishing a document with no 3D view (docs/HeadlessServe.md §4, stage
 * 2c). One publisher is the oracle (a real viewer, drawing under Xvfb),
 * the other is the view-less serve source; both write a dump with
 * `FC_BGFX_DUMP_SCENE`, and this says whether they described the same
 * scene.
 *
 * **Why the dump and not the wire.** A published manifest is
 * content-keyed and delta-encoded per viewer, and carries a session id
 * and a publish version, so two processes legitimately differ byte for
 * byte. A dump is written with no chunk sinks installed: monolithic,
 * self-contained, one document. That is the only comparable form.
 *
 * **Why digests and not a field-by-field comparison.** The comparison
 * has to stay complete as the snapshot grows — a material field added
 * next year and silently not compared is exactly the kind of hole this
 * tool exists to close. So nothing here enumerates fields: each draw is
 * re-serialized alone, through the very writer the format is defined by,
 * and hashed. Whatever the writer records is compared; whatever it does
 * not is not in the scene.
 *
 * What is normalized away first is identity that is process-local by
 * construction: `MeshData::cacheId` and `TextureImage::textureId` are
 * bare counters, and `sourceTag` is a pointer. Two processes never agree
 * on those and are not supposed to. Draw order is normalized too, by
 * grouping on `objectKey` and comparing each group as a multiset — the
 * order draws come out of a traversal is not part of what a scene is.
 *
 * Camera, viewport and overlays are excluded by default because they are
 * expected to differ: a headless source synthesizes a framing, and
 * viewer furniture (axis cross, navigation cube) is the viewer's, not the
 * document's. `--camera` and `--overlays` fold them back in.
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Renderer.h"
#include "SceneDump.h"

using namespace Render;

namespace
{

/// A snapshot carrying nothing but a zeroed camera, so anything hashed
/// through it differs only by what was deliberately put in.
SceneSnapshot blankSnapshot()
{
    SceneSnapshot s;
    std::memset(s.viewMatrix, 0, sizeof(s.viewMatrix));
    std::memset(s.projMatrix, 0, sizeof(s.projMatrix));
    s.width = 0;
    s.height = 0;
    s.clearColor = 0;
    return s;
}

std::string digestOf(const SceneSnapshot &s)
{
    std::vector<uint8_t> bytes;
    if (!saveSceneSnapshot(bytes, s))
        return "<unserializable>";
    return sha1Hex(bytes.data(), bytes.size());
}

/// Replace a texture with a copy whose process-local id is cleared. The
/// content key stays: it is a hash of the pixels, so it agrees across
/// processes by construction.
void normalizeTexture(std::shared_ptr<const TextureImage> &tex)
{
    if (!tex)
        return;
    auto copy = std::make_shared<TextureImage>(*tex);
    copy->textureId = 0;
    tex = copy;
}

/// Strip everything that names a thing rather than describing it. What
/// survives is the draw as content.
DrawCall normalizeDraw(const DrawCall &draw)
{
    DrawCall out = draw;
    out.materialIndex = -1;
    if (draw.mesh) {
        auto mesh = std::make_shared<MeshData>(*draw.mesh);
        mesh->cacheId = 0;
        mesh->generation = 0;
        mesh->sourceTag = nullptr;
        // `owner` is copied along, so the arrays this still points into
        // stay alive for as long as the copy does.
        out.mesh = mesh;
    }
    normalizeTexture(out.material.texture);
    normalizeTexture(out.material.bumpmap);
    normalizeTexture(out.material.emissivemap);
    normalizeTexture(out.material.occlusionmap);
    normalizeTexture(out.material.metallicroughnessmap);
    return out;
}

std::string drawDigest(const DrawCall &draw)
{
    SceneSnapshot one = blankSnapshot();
    one.scene.push_back(normalizeDraw(draw));
    return digestOf(one);
}

struct Group {
    std::vector<std::string> digests;   ///< sorted: order is not content
    std::vector<const DrawCall *> draws;
};

typedef std::map<uint64_t, Group> GroupMap;

GroupMap groupDraws(const DrawCallList &draws)
{
    GroupMap groups;
    for (const auto &draw : draws) {
        auto &group = groups[draw.objectKey];
        group.digests.push_back(drawDigest(draw));
        group.draws.push_back(&draw);
    }
    for (auto &entry : groups)
        std::sort(entry.second.digests.begin(), entry.second.digests.end());
    return groups;
}

size_t countVertices(const DrawCallList &draws)
{
    size_t total = 0;
    for (const auto &draw : draws)
        if (draw.mesh)
            total += size_t(draw.mesh->numVertices);
    return total;
}

const char *materialTypeName(int type)
{
    switch (type) {
        case Material::Triangle: return "triangle";
        case Material::Line: return "line";
        case Material::Point: return "point";
        default: return "?";
    }
}

/// A one-line sketch of a draw, for the report alone. This is a reading
/// aid, never the comparison: the digest already decided.
std::string describe(const DrawCall &draw)
{
    char buf[512];
    const MeshData *mesh = draw.mesh.get();
    std::snprintf(buf, sizeof(buf),
                  "%s diffuse=%08x tri=%d line=%d point=%d verts=%d "
                  "part=%d range=%d+%d%s%s",
                  materialTypeName(draw.material.type),
                  draw.material.diffuse,
                  mesh ? mesh->numTriangleIndices : -1,
                  mesh ? mesh->numLineIndices : -1,
                  mesh ? mesh->numPointIndices : -1,
                  mesh ? mesh->numVertices : -1,
                  draw.partIndex, draw.indexStart, draw.indexCount,
                  draw.material.ontop ? " ontop" : "",
                  draw.wholeObject ? " whole" : "");
    return buf;
}

/// Compare one feed (the scene, a selection, the highlight) and report.
/// Returns the number of objects that differ.
size_t diffFeed(const char *what, const DrawCallList &a,
                const DrawCallList &b, bool verbose)
{
    const GroupMap ga = groupDraws(a);
    const GroupMap gb = groupDraws(b);

    std::vector<uint64_t> onlyA, onlyB, differing;
    for (const auto &entry : ga) {
        auto it = gb.find(entry.first);
        if (it == gb.end())
            onlyA.push_back(entry.first);
        else if (it->second.digests != entry.second.digests)
            differing.push_back(entry.first);
    }
    for (const auto &entry : gb)
        if (!ga.count(entry.first))
            onlyB.push_back(entry.first);

    const size_t bad = onlyA.size() + onlyB.size() + differing.size();
    std::printf("%-10s draws %zu/%zu  objects %zu/%zu  vertices %zu/%zu",
                what, a.size(), b.size(), ga.size(), gb.size(),
                countVertices(a), countVertices(b));
    if (!bad) {
        std::printf("  -- match\n");
        return 0;
    }
    std::printf("  -- DIFFER\n");

    for (uint64_t key : onlyA)
        std::printf("    only in A: object %016llx (%zu draws)\n",
                    (unsigned long long)key, ga.at(key).draws.size());
    for (uint64_t key : onlyB)
        std::printf("    only in B: object %016llx (%zu draws)\n",
                    (unsigned long long)key, gb.at(key).draws.size());
    for (uint64_t key : differing) {
        const Group &A = ga.at(key);
        const Group &B = gb.at(key);
        std::printf("    differs:   object %016llx (%zu draws vs %zu)\n",
                    (unsigned long long)key, A.draws.size(), B.draws.size());
        if (!verbose)
            continue;
        for (size_t i = 0; i < A.draws.size(); ++i)
            std::printf("      A[%zu] %s\n", i, describe(*A.draws[i]).c_str());
        for (size_t i = 0; i < B.draws.size(); ++i)
            std::printf("      B[%zu] %s\n", i, describe(*B.draws[i]).c_str());
    }
    return bad;
}

/// Every config block the snapshot carries, each hashed on its own so a
/// mismatch names the block it is in. Listed once, used twice.
#define FC_SCENEDIFF_CONFIGS(X)                                             \
    X(background) X(hlconfig) X(secconf) X(aoconf) X(pbrconf)               \
    X(bumpconf) X(lightconf) X(volconf) X(waterconf) X(bloomconf)           \
    X(debugconf) X(usershaderconf) X(preselconf) X(selconf)                 \
    X(effectResolution) X(ssaoResolution) X(hatch)

size_t diffConfigs(const SceneSnapshot &a, const SceneSnapshot &b)
{
    size_t bad = 0;
#define FC_SCENEDIFF_ONE(field)                                             \
    {                                                                       \
        SceneSnapshot sa = blankSnapshot();                                 \
        SceneSnapshot sb = blankSnapshot();                                 \
        sa.field = a.field;                                                 \
        sb.field = b.field;                                                 \
        if (digestOf(sa) != digestOf(sb)) {                                 \
            std::printf("    config differs: %s\n", #field);                \
            ++bad;                                                          \
        }                                                                   \
    }
    FC_SCENEDIFF_CONFIGS(FC_SCENEDIFF_ONE)
#undef FC_SCENEDIFF_ONE
    std::printf("%-10s %s\n", "configs", bad ? "-- DIFFER" : "-- match");
    return bad;
}

int usage()
{
    std::fprintf(stderr,
        "usage: fcscenediff [--camera] [--overlays] [-v] A.fcsd B.fcsd\n"
        "\n"
        "Structurally compares two scene dumps (FC_BGFX_DUMP_SCENE).\n"
        "Camera, viewport and overlays are excluded unless asked for:\n"
        "a view-less publisher synthesizes the framing and draws no\n"
        "viewer furniture (docs/HeadlessServe.md).\n"
        "\n"
        "Capture both dumps with FC_BGFX_DUMP_SCENE_SETTLE=<n>. A\n"
        "document does not arrive all at once, so a dump taken a fixed\n"
        "number of frames in records how far the build had got -- three\n"
        "runs of one script were measured producing three different\n"
        "scenes. Nothing here can tell that from a real difference, so\n"
        "getting the capture right is the only defence.\n"
        "\n"
        "Exit: 0 identical, 1 differ, 2 could not read a dump.\n");
    return 2;
}

}  // namespace

int main(int argc, char **argv)
{
    bool withCamera = false;
    bool withOverlays = false;
    bool verbose = false;
    std::vector<const char *> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--camera")
            withCamera = true;
        else if (arg == "--overlays")
            withOverlays = true;
        else if (arg == "-v")
            verbose = true;
        else if (!arg.empty() && arg[0] == '-')
            return usage();
        else
            paths.push_back(argv[i]);
    }
    if (paths.size() != 2)
        return usage();

    SceneSnapshot a;
    SceneSnapshot b;
    if (!loadSceneSnapshot(paths[0], a)) {
        std::fprintf(stderr, "scenediff: cannot read %s\n", paths[0]);
        return 2;
    }
    if (!loadSceneSnapshot(paths[1], b)) {
        std::fprintf(stderr, "scenediff: cannot read %s\n", paths[1]);
        return 2;
    }
    std::printf("A = %s\nB = %s\n", paths[0], paths[1]);

    size_t bad = 0;
    bad += diffFeed("scene", a.scene, b.scene, verbose);
    bad += diffFeed("highlight", a.highlight, b.highlight, verbose);
    if (a.highlightWholeOnTop != b.highlightWholeOnTop) {
        std::printf("    highlightWholeOnTop differs: %d vs %d\n",
                    int(a.highlightWholeOnTop), int(b.highlightWholeOnTop));
        ++bad;
    }

    // Selections are keyed by selection id, which is a flag set rather
    // than a counter (SelIdBits), so the keys themselves compare.
    std::map<int, const DrawCallList *> sa, sb;
    for (const auto &sel : a.selections)
        sa[sel.first] = &sel.second;
    for (const auto &sel : b.selections)
        sb[sel.first] = &sel.second;
    const DrawCallList empty;
    for (const auto &sel : sa) {
        char label[32];
        std::snprintf(label, sizeof(label), "sel:%08x", sel.first);
        auto it = sb.find(sel.first);
        bad += diffFeed(label, *sel.second,
                        it == sb.end() ? empty : *it->second, verbose);
    }
    for (const auto &sel : sb) {
        if (sa.count(sel.first))
            continue;
        char label[32];
        std::snprintf(label, sizeof(label), "sel:%08x", sel.first);
        bad += diffFeed(label, empty, *sel.second, verbose);
    }

    bad += diffConfigs(a, b);

    if (withOverlays) {
        std::map<int, const SceneSnapshot::Overlay *> oa, ob;
        for (const auto &ov : a.overlays)
            oa[ov.id] = &ov;
        for (const auto &ov : b.overlays)
            ob[ov.id] = &ov;
        for (const auto &ov : oa) {
            char label[32];
            std::snprintf(label, sizeof(label), "overlay:%d", ov.first);
            auto it = ob.find(ov.first);
            bad += diffFeed(label, ov.second->draws,
                            it == ob.end() ? empty : it->second->draws,
                            verbose);
        }
        for (const auto &ov : ob)
            if (!oa.count(ov.first)) {
                char label[32];
                std::snprintf(label, sizeof(label), "overlay:%d", ov.first);
                bad += diffFeed(label, empty, ov.second->draws, verbose);
            }
    }
    else {
        std::printf("%-10s %zu/%zu -- skipped (--overlays)\n", "overlays",
                    a.overlays.size(), b.overlays.size());
    }

    // autozoomScale counts as camera, not as config: it is
    // getWorldToScreenScale over the view volume and the viewport aspect
    // (RendererBridge::translateAutoZoomScale), so it moves with the
    // framing and nothing else. Grouping it with the configs made a mere
    // difference of camera read as a difference of scene — which it was
    // reporting until this was traced.
    if (withCamera) {
        const bool same =
            std::memcmp(a.viewMatrix, b.viewMatrix, sizeof(a.viewMatrix)) == 0
            && std::memcmp(a.projMatrix, b.projMatrix,
                           sizeof(a.projMatrix)) == 0
            && a.width == b.width && a.height == b.height
            && a.clearColor == b.clearColor
            && a.autozoomScale == b.autozoomScale;
        std::printf("%-10s %dx%d / %dx%d  autozoom %g/%g %s\n", "camera",
                    a.width, a.height, b.width, b.height,
                    double(a.autozoomScale), double(b.autozoomScale),
                    same ? "-- match" : "-- DIFFER");
        if (!same)
            ++bad;
    }
    else {
        std::printf("%-10s %dx%d / %dx%d  autozoom %g/%g"
                    " -- skipped (--camera)\n", "camera",
                    a.width, a.height, b.width, b.height,
                    double(a.autozoomScale), double(b.autozoomScale));
    }

    std::printf("\n%s\n", bad ? "RESULT: DIFFER" : "RESULT: identical");
    return bad ? 1 : 0;
}

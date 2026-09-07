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

// One LSB of the D24S8 depth buffer, in the NDC depth the vertex stage
// biases. OpenGL and WebGL2 map [-1,1] of NDC z onto [0,1] of window
// depth, so an LSB spans two NDC units there; the [0,1] backends span
// one. The old constant assumed OpenGL and so hit Vulkan, D3D and Metal
// at twice the intended strength.
static float depthLsbNdc()
{
    const bgfx::Caps *caps = bgfx::getCaps();
    return (caps && caps->homogeneousDepth ? 2.0f : 1.0f) / 16777216.0f;
}

float BGFXView::polygonOffsetFactor(const Render::Material &mat,
                                    float decorReach)
{
    // GL's `factor` multiplies the polygon's depth slope in units of one
    // PIXEL, so it reads directly as "how many pixels of slope this fill
    // has to clear". 1 is the right answer for what polygon offset was
    // designed for -- an edge lying ON the surface, coincident with it,
    // where all that is needed is to break a tie.
    //
    // A thick line is not coincident. It is a screen-space quad centred
    // on the edge that carries the EDGE's depth across its whole width
    // (fc_line_vs.sh offsets xy only), so where the surface rises toward
    // the viewer it beats the line everywhere past the pixel this factor
    // cleared. Measured on a 10mm box, an edge whose face climbs toward
    // the camera: the drawn width came up exactly `width/2 - 1` short at
    // every width from 4 to 12 -- the -1 being that one pixel. Coin's GL
    // renderer loses the same half for the same reason, so this is not a
    // backend artifact but the thing polygon offset was never scaled
    // for. It is invisible at widths 1-2, which is why it went unnoticed.
    //
    //  decorReach is how far the decoration drawn over this fill
    // reaches from its own geometry, in pixels -- half a line width plus
    // the analytic feather, or half a point size. It cannot come from
    // the material: SoDrawStyle's line width lives INSIDE the wireframe
    // separator, which ViewProviderExt adds AFTER the faces, so a fill's
    // own material always reports linewidth 1 no matter how thick its
    // edges are. The frame resolves it per object from the draw list
    // instead (BGFXView::decorReachFor).
    //
    // WHAT IT COSTS, measured (scripts/fill_pullback_slope.py, which
    // bisects the depth at which a neighbour starts winning): the fill
    // moves back by `(reach - 1) * gradient * 2 / height` NDC against a
    // neighbour whose edges are thin, tracking that arithmetic at ratio
    // 1.00 over gradients 0.5 to 19, and saturating at the ceiling
    // exactly where kPolyOffsetMaxSlope says it should. On a 621px
    // viewport that ceiling is 0.071 NDC for a 12px line -- 3.5% of the
    // depth range -- and 0.006 NDC, 0.3%, for the default width 2. So
    // where two solids touch and one carries thick edges, the neighbour
    // wins any surface lying within that slice behind it. That is the
    // risk 34461d03b7 left untested: confirmed, bounded, and kept,
    // because the defect it replaces -- every thick line losing its
    // face-side half -- is both larger and always on screen.
    if (!mat.polygonoffset)
        return 0.0f;
    return std::max(mat.polygonoffsetfactor, decorReach);
}

float BGFXView::decorReachFor(uint64_t objectKey) const
{
    auto it = decorReach.find(objectKey);
    return it == decorReach.end() ? 1.0f : it->second;
}

float BGFXView::polygonOffsetBias(const Render::Material &mat)
{
    // The constant half of GL's `factor * m + units * r` — the slope
    // half m lives in the vertex stage, which needs a surface normal to
    // compute it. GL's r is a single depth LSB; this uses sixteen, and
    // folds factor into the constant as well as passing it on to the
    // slope. Both are deliberate floors: the slope comes from the
    // shading normal, which does not describe the triangle's plane on a
    // mesh with smoothed or missing normals, and this is what those
    // cases fall back on. It is also exactly the bias the backend
    // applied before the slope term existed, so nothing that already
    // resolved stops resolving.
    constexpr float kUnitLsb = 16.0f;
    return mat.polygonoffset
        ? (mat.polygonoffsetfactor + mat.polygonoffsetunits)
            * kUnitLsb * depthLsbNdc()
        : 0.0f;
}

float BGFXView::polygonOffsetMaxBias(const Render::Material &mat,
                                     uint64_t objectKey) const
{
    // What the vertex stage's slope term can reach for this material:
    // the gradient ceiling, converted to NDC depth by the size of a
    // pixel on the shorter viewport axis (the axis that gives the
    // larger step, which is the one the shader's max() picks).
    if (!mat.polygonoffset)
        return 0.0f;
    const float px = 2.0f
        / float(std::max<int>(1, std::min<int>(width, height)));
    return polygonOffsetFactor(mat, decorReachFor(objectKey))
        * kPolyOffsetMaxSlope * px;
}

void BGFXView::setPolygonOffsetUniform(const Render::Material *mat,
                                       uint64_t objectKey)
{
    // Bound at every site that submits a program built on vs_fc_mesh:
    // a bgfx uniform keeps its last value across draws, so a site that
    // left it alone would inherit the previous draw's offset.
    float po[4] = {0.0f, kPolyOffsetMaxSlope, 0.0f, 0.0f};
    if (mat && mat->polygonoffset
            && mat->type == Render::Material::Triangle)
        po[0] = polygonOffsetFactor(*mat, decorReachFor(objectKey));
    bgfx::setUniform(u_polyOffset, po);
}

void BGFXView::submitTessellation(const Render::DrawCall &draw,
                                  const float *viewMatrix, uint16_t viewId)
{
    // The Tessellation draw style: SoFCUnifiedSelection overrides
    // SoDrawStyleElement to LINES and lets the shapes draw their faces,
    // which the GL renderer turns into a wireframe with glPolygonMode.
    // No modern API has that state, so the triangle edges are drawn as
    // geometry instead — the same per-triangle instance buffer the
    // stencil outline passes use, over its whole range rather than where
    // a stencil differs.
    //
    // Coin does not get hidden-line removal from the draw style either:
    // it comes from SoRenderManager::HIDDEN_LINE, a second pass that
    // fills the scene in the background colour first. That pass is
    // reproduced here rather than left out, because a tessellation
    // wireframe with every back face showing through is unreadable on
    // anything with more than one closed solid in it.
    //
    // But only for the display MODE, which is what that render mode is
    // switched on for. A lone SoDrawStyle node asking for a wireframe
    // (PartGui's geometry-check bounding box) gets no such pass from
    // Coin, and giving it one filled the box with the background colour
    // and hid the shape it was drawn around.
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;
    gpu->geom->ensureOutline(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->triEdgeInst))
        return;

    const Render::Material &mat = draw.material;
    const bool clipped = clipActiveFor(mat);
    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Pass 1: the faces, in the background colour, depth only as far as
    // the eye is concerned — they exist to occlude, not to be seen.
    // Unlit and untextured for the same reason.
    if (mat.drawstyleoverride) {
        float fill[4];
        unpackAuthoredColor(bgFillColor, fill, colorManaged());
        fill[3] = 1.0f;
        float fillParams[4] = {0.0f, 0.0f, 1.0f, polygonOffsetBias(mat)};
        bgfx::setUniform(u_matColor, fill);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, fillParams);
        setPolygonOffsetUniform(&mat, draw.objectKey);
        setTriangleFrameState(mat, PassNormal, false, false);
        if (clipped)
            setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix,
                         (float)height);
        setMeshVertexBuffers(gpu, *draw.mesh);
        if (draw.indexCount > 0)
            bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                                 uint32_t(draw.indexCount));
        else
            bgfx::setIndexBuffer(gpu->geom->tri);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_WRITE_Z | BGFX_STATE_MSAA
                       | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(vid(viewId), clipped ? m_progMeshClip : m_progMesh);
        ++drawcount;
    }

    // Pass 2: the edges, in the material's own colour.
    //
    // Which edges depends on the same distinction pass 1 makes. The
    // display MODE is showing the tessellation, so it draws every
    // triangle edge -- the instance range is the index range, the
    // buffer holding one segment per triangle index position. A lone
    // SoDrawStyle asking for a wireframe is not: GL replays the shape
    // itself under glPolygonMode, and the shapes that reach here that
    // way (SoCube, SoFCBoundingBox) are polygons, so the diagonals the
    // triangulation introduced are edges GL never draws. Those come off
    // in ensureCreaseEdges, whose per-triangle offsets keep a partial
    // index range mapping onto an instance range.
    bgfx::VertexBufferHandle edgeInst = gpu->geom->triEdgeInst;
    uint32_t start = draw.indexCount > 0 ? uint32_t(draw.indexStart) : 0;
    uint32_t count = draw.indexCount > 0
        ? uint32_t(draw.indexCount)
        : uint32_t(draw.mesh->numTriangleIndices);
    if (!mat.drawstyleoverride) {
        gpu->geom->ensureCreaseEdges(*draw.mesh);
        const auto &first = gpu->geom->creaseEdgeFirst;
        if (bgfx::isValid(gpu->geom->creaseEdgeInst) && !first.empty()) {
            const size_t from = size_t(start) / 3;
            const size_t to = size_t(start + count) / 3;
            if (to < first.size()) {
                edgeInst = gpu->geom->creaseEdgeInst;
                start = first[from];
                count = first[to] - start;
            }
        }
    }
    if (count == 0)
        return;

    // The pattern of a dashed wireframe (PartGui's geometry check asks
    // for one) lives in the quad fragment shader, as it does for any
    // other patterned line.
    const uint32_t linepattern = mat.linepattern;
    const bool patterned = (linepattern & 0xffff) != 0xffff;

    float color[4];
    unpackColor(mat.linecolor ? mat.linecolor : mat.diffuse, color);
    color[3] = 1.0f;
    float lineParams[4] = {0.0f, qMax(1.0f, mat.linewidth), 0.0f, 1.0f};
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, lineParams);
    if (patterned) {
        // glLineStipple clamps the repeat factor to [1, 256].
        uint32_t factor = linepattern >> 16;
        factor = factor < 1 ? 1 : factor > 256 ? 256 : factor;
        float patParams[4] = {float(linepattern & 0xffff),
                              float(factor), 0.0f, 0.0f};
        bgfx::setUniform(u_linePattern, patParams);
    }
    if (clipped)
        setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix,
                     (float)height);
    LineQuadVertex::init();
    bgfx::setVertexBuffer(0, m_lineQuadVb);
    bgfx::setIndexBuffer(m_lineQuadIb);
    bgfx::setInstanceDataBuffer(edgeInst, start, count);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z | BGFX_STATE_MSAA
                   | BGFX_STATE_DEPTH_TEST_LEQUAL
                   | BGFX_STATE_BLEND_FUNC_SEPARATE(
                       BGFX_STATE_BLEND_SRC_ALPHA,
                       BGFX_STATE_BLEND_INV_SRC_ALPHA,
                       BGFX_STATE_BLEND_ONE,
                       BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(vid(viewId),
                 patterned ? (clipped ? m_progLinePatClip : m_progLinePat)
                           : (clipped ? m_progLineClip : m_progLine));
    ++drawcount;
}

void BGFXView::submitVertexPoints(const Render::DrawCall &draw,
                                  const float *viewMatrix, uint16_t viewId)
{
    // The Points draw style: filled triangles carrying
    // SoDrawStyleElement::POINTS -- Mesh's "Points" display mode
    // re-styles its face set -- draw as their corner points, which the
    // GL renderer gets from glPolygonMode POINT. Reuses the outline
    // passes' per-corner instance buffer: one instance per triangle
    // index, so a shared vertex draws once per corner -- coincident
    // dots, invisible and cheap at point sizes.
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh))
        return;
    gpu->geom->ensureOutline(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->triCornerInst))
        return;

    const Render::Material &mat = draw.material;
    const bool clipped = clipActiveFor(mat);
    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float color[4];
    unpackAuthoredColor(mat.diffuse, color, colorManaged());
    color[3] = 1.0f;
    float pointParams[4] = {0.0f,
                            qMax(1.0f, std::floor(mat.pointsize + 0.5f)),
                            0.0f, 1.0f};
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, pointParams);
    if (clipped)
        setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix,
                     (float)height);
    LineQuadVertex::init();
    bgfx::setVertexBuffer(0, m_lineQuadVb);
    bgfx::setIndexBuffer(m_lineQuadIb);
    // A partial index range maps 1:1 onto the corner instances (one
    // per triangle index position), like the tessellation edges.
    uint32_t start = draw.indexCount > 0 ? uint32_t(draw.indexStart) : 0;
    uint32_t count = draw.indexCount > 0
        ? uint32_t(draw.indexCount)
        : uint32_t(draw.mesh->numTriangleIndices);
    if (count == 0)
        return;
    bgfx::setInstanceDataBuffer(gpu->geom->triCornerInst, start, count);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z | BGFX_STATE_MSAA
                   | BGFX_STATE_DEPTH_TEST_LEQUAL);
    bgfx::submit(vid(viewId), clipped ? m_progPointClip : m_progPoint);
    ++drawcount;
}

void BGFXView::submitOutline(const Render::DrawCall &draw, uint32_t refCounter,
                   const OutlineSpec &spec)
{
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return;
    // An outline is shading, so it belongs to whatever the per-object
    // style resolution decided about the draw itself
    // (docs/CoinRetirement.md 5.13): a draw this sub-view does not
    // show must not leave its silhouette behind, and the ADDITIVELY
    // captured copy of an overridden object must not outline the same
    // object twice. submit() asks the same question of the fill.
    if (!styleAdmits(draw))
        return;
    // Validate the edge passes up front so a mesh that cannot draw
    // them leaves no stray stencil marks.
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;
    gpu->geom->ensureOutline(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->triEdgeInst))
        return;
    if (!submitOutlineMark(draw, refCounter, spec.view, spec.depthTest,
                           spec.start, spec.count))
        return;
    submitOutlineEdges(draw, refCounter, spec);
}

bool BGFXView::clipActiveFor(const Render::Material &mat) const
{
    return mat.numclipplanes > 0 || reflClipActive(mat);
}

bool BGFXView::reflClipActive(const Render::Material &mat) const
{
    return reflPass && reflClip && !mat.clipconcave;
}

void BGFXView::setClipUniforms(const Render::Material &mat)
{
    const bool refl = reflClipActive(mat);
    if (mat.numclipplanes == 0 && !refl)
        return;
    int n = mat.numclipplanes;
    float planes[Render::Material::MaxClipPlanes][4];
    if (n > 0)
        std::memcpy(planes, mat.clipplanes, sizeof(float) * 4 * n);
    if (refl && n < Render::Material::MaxClipPlanes) {
        std::memcpy(planes[n], reflClipPlane, sizeof(float) * 4);
        ++n;
    }
    float clipParams[4] = {float(n),
                           mat.clipconcave ? 1.0f : 0.0f,
                           0.0f, 0.0f};
    bgfx::setUniform(u_clipParams, clipParams);
    bgfx::setUniform(u_clipPlanes, planes, uint16_t(n));
}

bool BGFXView::submitOutlineMark(const Render::DrawCall &draw,
                       uint32_t refCounter, uint16_t view,
                       bool depthTest, int start, int count)
{
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return false;
    // Also reached directly, for the whole-scene silhouette: the same
    // rule, and the same answer as the edges pass below, so a dropped
    // draw leaves no stencil mark for edges that never come.
    if (!styleAdmits(draw))
        return false;
    const Render::MeshData &mesh = *draw.mesh;
    if (count <= 0) {
        start = 0;
        count = mesh.numTriangleIndices;
    }
    if (count < 3)
        return false;
    GpuMesh *gpu = getMesh(mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return false;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    uint32_t ref = ((refCounter - 1) % 255) + 1;
    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bgfx::setUniform(u_matColor, zero);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    setMeshVertexBuffers(gpu, mesh);
    bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(start), uint32_t(count));
    bgfx::setState(BGFX_STATE_MSAA
        | (depthTest ? BGFX_STATE_DEPTH_TEST_LEQUAL : 0));
    bgfx::setStencil(BGFX_STENCIL_TEST_ALWAYS
        | BGFX_STENCIL_FUNC_REF(ref) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_REPLACE
        | BGFX_STENCIL_OP_PASS_Z_REPLACE);
    bgfx::submit(vid(view),
                 clipped ? m_progFlatClip : m_progFlat);
    ++drawcount;
    return true;
}

void BGFXView::submitOutlineEdges(const Render::DrawCall &draw,
                        uint32_t refCounter, const OutlineSpec &spec)
{
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return;
    if (!styleAdmits(draw))
        return;
    const Render::MeshData &mesh = *draw.mesh;
    int start = spec.start;
    int count = spec.count;
    if (count <= 0) {
        start = 0;
        count = mesh.numTriangleIndices;
    }
    if (count < 3)
        return;
    GpuMesh *gpu = getMesh(mesh);
    if (!bgfx::isValid(gpu->geom->vbh))
        return;
    gpu->geom->ensureOutline(mesh);
    if (!bgfx::isValid(gpu->geom->triEdgeInst))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    uint32_t ref = ((refCounter - 1) % 255) + 1;
    const uint64_t depthtest =
        spec.depthTest ? BGFX_STATE_DEPTH_TEST_LEQUAL : 0;
    const uint64_t depthstate = depthtest
        | (spec.depthWrite ? BGFX_STATE_WRITE_Z : 0);
    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // Shared uniforms of the edge and corner passes: flat outline
    // color, forced opaque. When the edges write depth, they are biased
    // past anything the owning fill can reach, so that fill (biased away
    // from the viewer) still passes LEQUAL and blends over its outline
    // like GL's ordered draw does, while fills of objects genuinely
    // behind the outline stay depth-killed. The fill's own bias is not a
    // single number any more — the vertex stage adds a per-vertex slope
    // term on top of the constant — so the outline clears the ceiling on
    // that term rather than doubling a constant that no longer bounds it.
    float color[4];
    // A picked outline colour into the mesh program's base slot, which
    // is light: decoded like every authored colour the beauty frame
    // takes, or the output transform paints it 1.5x too bright.
    unpackAuthoredColor((spec.color & 0xffffff00) | 0xff, color, colorManaged());
    params[1] = qMax(1.0f, spec.width);
    params[2] = spec.depthWrite
        ? 2.0f * polygonOffsetBias(draw.material)
            + polygonOffsetMaxBias(draw.material, draw.objectKey)
        : 0.0f;
    const uint64_t outlinestate = BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA | depthstate
        | BGFX_STATE_BLEND_FUNC_SEPARATE(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
            BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    const uint32_t outlinestencil = BGFX_STENCIL_TEST_NOTEQUAL
        | BGFX_STENCIL_FUNC_REF(ref) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_KEEP
        | BGFX_STENCIL_OP_PASS_Z_KEEP;
    LineQuadVertex::init();

    // Pass 2: the triangle edges as instanced thick lines where the
    // stencil differs — the boundary outline. Instances map 1:1 onto
    // triangle index positions, so the index range is the instance
    // range.
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, m_lineQuadVb);
    bgfx::setIndexBuffer(m_lineQuadIb);
    bgfx::setInstanceDataBuffer(gpu->geom->triEdgeInst, uint32_t(start),
                                uint32_t(count));
    bgfx::setState(outlinestate);
    bgfx::setStencil(outlinestencil);
    bgfx::submit(vid(spec.view),
                 clipped ? m_progLineClip : m_progLine);
    ++drawcount;

    // Pass 3: point-sprite corner caps (GL's GL_POINT polygon-mode
    // pass), patching the notches thick quads leave at corners.
    if (!spec.caps)
        return;
    if (spec.capWidth > 0.0f)
        params[1] = qMax(1.0f, std::floor(spec.capWidth + 0.5f));
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, m_lineQuadVb);
    bgfx::setIndexBuffer(m_lineQuadIb);
    bgfx::setInstanceDataBuffer(gpu->geom->triCornerInst, uint32_t(start),
                                uint32_t(count));
    bgfx::setState(outlinestate);
    bgfx::setStencil(outlinestencil);
    bgfx::submit(vid(spec.view),
                 clipped ? m_progPointClip : m_progPoint);
    ++drawcount;
}

void BGFXView::updateHatchTexture(uint64_t version, const uint8_t *rgba,
                        int width, int height)
{
    if (version == m_hatchVersion)
        return;
    m_hatchVersion = version;
    if (bgfx::isValid(m_hatchTex)) {
        bgfx::destroy(m_hatchTex);
        m_hatchTex = BGFX_INVALID_HANDLE;
    }
    if (!rgba || width <= 0 || height <= 0)
        return;
    m_hatchTex = bgfx::createTexture2D(uint16_t(width), uint16_t(height),
        false, 1, bgfx::TextureFormat::RGBA8, 0,
        bgfx::copy(rgba, uint32_t(width) * uint32_t(height) * 4));
}

bool BGFXView::submitCapMark(const Render::DrawCall &draw,
                   const float plane[4], uint16_t view)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return false;
    const Render::MeshData &mesh = *draw.mesh;
    GpuMesh *gpu = getMesh(mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return false;

    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float clipParams[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const uint32_t markstencil = BGFX_STENCIL_TEST_ALWAYS
        | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_KEEP
        | BGFX_STENCIL_OP_PASS_Z_INVERT;

    auto submitRange = [&](int start, int count) {
        bgfx::setUniform(u_matColor, zero);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, params);
        bgfx::setUniform(u_clipParams, clipParams);
        bgfx::setUniform(u_clipPlanes, plane, 1);
        setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
        setMeshVertexBuffers(gpu, mesh);
        if (count > 0)
            bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(start),
                                 uint32_t(count));
        else
            bgfx::setIndexBuffer(gpu->geom->tri);
        bgfx::setState(BGFX_STATE_MSAA);
        bgfx::setStencil(markstencil);
        bgfx::submit(vid(view), m_progFlatClip);
        ++drawcount;
    };

    if (mesh.solidParts.empty())
        submitRange(0, 0);
    else
        for (const auto &part : mesh.solidParts)
            submitRange(part.first, part.second);
    return true;
}

void BGFXView::submitCapQuad(const CapVertex verts[4], uint32_t color,
                   const float (*otherPlanes)[4], int numOther,
                   bool hatch, bool blend, uint16_t view)
{
    if (bgfx::getAvailTransientVertexBuffer(6, CapVertex::ms_layout) < 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, CapVertex::ms_layout);
    auto *v = reinterpret_cast<CapVertex *>(tvb.data);
    v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
    v[3] = verts[0]; v[4] = verts[2]; v[5] = verts[3];

    float col[4];
    // The cap fill is the material's own (inverted or not) diffuse,
    // written straight to the colour target: authored, so decoded, or
    // the cap of a 0.5 grey solid encodes out as 0.73.
    unpackAuthoredColor(color, col, colorManaged());
    bgfx::setUniform(u_matColor, col);
    if (numOther > 0) {
        float clipParams[4] = {float(numOther), 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_clipParams, clipParams);
        bgfx::setUniform(u_clipPlanes, otherPlanes, numOther);
    }
    // GL only textures the cap when hatching is enabled; the white
    // stand-in keeps the shader uniform otherwise.
    bgfx::setTexture(0, s_texHatch,
        hatch && bgfx::isValid(m_hatchTex) ? m_hatchTex : m_whiteTex);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA
        | (blend ? BGFX_STATE_BLEND_ALPHA : 0));
    bgfx::setStencil(BGFX_STENCIL_TEST_EQUAL
        | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0x01)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_KEEP
        | BGFX_STENCIL_OP_PASS_Z_KEEP);
    bgfx::submit(vid(view),
                 numOther > 0 ? m_progCapClip : m_progCap);
    ++drawcount;
}

void BGFXView::submitCapPrepass(const CapVertex verts[4],
                   const float (*otherPlanes)[4], int numOther,
                   uint16_t view)
{
    // The same quad as submitCapQuad(), into the depth+normal prepass
    // target instead of the scene. Without this the cap exists only in
    // the scene framebuffer, while every screen-space pass that reads
    // aoNormalZ still sees the geometry the cap hides -- so the cavity
    // pass darkens the solid's inside corners and paints them back over
    // the finished cap, and GTAO occludes against a cavity that is not
    // visible. The prepass programs read a_position + a_normal, both of
    // which CapVertex now carries.
    if (!bgfx::isValid(m_progPrepass))
        return;
    if (bgfx::getAvailTransientVertexBuffer(6, CapVertex::ms_layout) < 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, CapVertex::ms_layout);
    auto *v = reinterpret_cast<CapVertex *>(tvb.data);
    v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
    v[3] = verts[0]; v[4] = verts[2]; v[5] = verts[3];

    const bool clipped = numOther > 0 && bgfx::isValid(m_progPrepassClip);
    if (clipped) {
        float clipParams[4] = {float(numOther), 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_clipParams, clipParams);
        bgfx::setUniform(u_clipPlanes, otherPlanes, numOther);
    }
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::setStencil(BGFX_STENCIL_TEST_EQUAL
        | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0x01)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_KEEP
        | BGFX_STENCIL_OP_PASS_Z_KEEP);
    bgfx::submit(vid(view), clipped ? m_progPrepassClip : m_progPrepass);
    ++drawcount;
}

void BGFXView::submitCapCleanup(const CapVertex verts[4], uint16_t view)
{
    if (bgfx::getAvailTransientVertexBuffer(6, CapVertex::ms_layout) < 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, CapVertex::ms_layout);
    auto *v = reinterpret_cast<CapVertex *>(tvb.data);
    v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
    v[3] = verts[0]; v[4] = verts[2]; v[5] = verts[3];

    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_matColor, zero);
    bgfx::setTexture(0, s_texHatch, m_whiteTex);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_MSAA);
    bgfx::setStencil(BGFX_STENCIL_TEST_ALWAYS
        | BGFX_STENCIL_FUNC_REF(0) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_REPLACE
        | BGFX_STENCIL_OP_PASS_Z_REPLACE);
    bgfx::submit(vid(view), m_progCap);
    ++drawcount;
}

void BGFXView::submitComposite()
{
    TransientVertex::init();
    if (bgfx::getAvailTransientVertexBuffer(3, TransientVertex::ms_layout)
            < 3)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 3, TransientVertex::ms_layout);
    auto *v = reinterpret_cast<TransientVertex *>(tvb.data);
    // Clip-space triangle covering the viewport.
    v[0] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[1] = { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[2] = {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    bgfx::setTexture(0, s_texAccum, oitAccum);
    bgfx::setTexture(1, s_texReveal, oitReveal);
    bgfx::setVertexBuffer(0, &tvb);
    // The shader's alpha is the coverage (fs_fc_comp.sc): a plain
    // "over", split so that the alpha channel accumulates coverage
    // rather than coverage squared. Only a capture over a transparent
    // background can tell the difference, and it did.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
                                         BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                         BGFX_STATE_BLEND_ONE,
                                         BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(vid(ViewOITComposite), m_progComp);
    ++drawcount;
}

void BGFXView::submitDebug(const Render::RenderDebugConfig &conf, float maxDepth,
                 int aoMethod, bool shadowValid, float impactLife)
{
    if (!bgfx::isValid(m_progDebug) || !bgfx::isValid(aoNormalZ))
        return;
    float params[4] = {float(conf.viewMode),
                       maxDepth > 0.0f ? 1.0f / maxDepth : 1.0f,
                       shadowValid ? 1.0f : 0.0f,
                       float(shadowSize)};
    bgfx::setUniform(u_debugParams, params);
    // Dynamically bound named uniforms (docs/RenderDebug.md §2.5),
    // set against this pass's draw: uniform updates recorded before
    // an EMPTY submit (bgfx::touch, the view clears) are discarded
    // with the dropped draw, so the push must precede a real draw.
    // The u_userParams bootstrap pool merges the user value onto
    // its identity default (x = output scale, y = bias) and is set
    // exactly once — a second setUniform of one handle before the
    // same submit is fatal in bgfx debug builds.
    {
        float pool[16] = {1.0f};
        for (const auto &p : conf.userParams) {
            if (p.name == "u_userParams") {
                std::memcpy(pool, p.values.data(),
                            std::min(p.values.size(), size_t(16))
                                * sizeof(float));
                continue;
            }
            _BGFXLib.setUserUniform(p.name, p.values.data(),
                                    uint16_t(p.values.size() / 4));
        }
        _BGFXLib.setUserUniform("u_userParams", pool, 4);
    }
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    // The finished AO term: GTAO denoises back into aoTex, the
    // classic blur lands in aoBlurTex (see submitAOResolve).
    bgfx::TextureHandle ao = aoMethod == 1 ? aoTex : aoBlurTex;
    bgfx::setTexture(2, s_texAO,
                     bgfx::isValid(ao) ? ao : m_whiteTex);
    if (shadowValid) {
        // The shadow-map sampling state of fc_volume_shadow.sh —
        // the same set the volumetric/caustics passes bind.
        float lightDir[4] = {lightDirView[0], lightDirView[1],
                             lightDirView[2], 1.0f};
        float evsm[4] = {shadowWarpFrame, shadowThreshold,
                         shadowSpreadUv, shadowSpreadMode};
        bgfx::setUniform(u_evsm, evsm);
        bgfx::setUniform(u_lightDir, lightDir);
        bgfx::setUniform(u_lightPos, lightPosView);
        bgfx::setUniform(u_lightColor, lightColorI);
        bgfx::setUniform(u_shadowMatrix, shadowMtx);
        bgfx::setTexture(1, s_texShadow, shadowTex);
    }
    else {
        bgfx::setTexture(1, s_texShadow, m_whiteTex);
    }
    // The tile-coverage mode (5) replicates the mesh receivers'
    // bulb-tile selection: it needs the local-light state and the
    // atlas matrices with this draw (frame-global pushes recorded
    // against other submits do not reach this program).
    if (bgfx::isValid(u_bulbShadowMtx)) {
        bgfx::setUniform(u_localLight, localLightView, kLocalLights);
        bgfx::setUniform(u_localLightColor, localLightColorI,
                         kLocalLights);
        bgfx::setUniform(u_bulbShadowMtx, bulbShadowMtx,
                         kBulbShadowTiles);
        bgfx::setUniform(u_bulbShadowConf, bulbShadowConf,
                         kLocalLights - kMediumSlots);
        bgfx::setUniform(u_bulbShadowRot, bulbShadowRotMtx);
        bgfx::setTexture(3, s_texBulbShadow,
                         bgfx::isValid(bulbShadowTex)
                             ? bulbShadowTex : m_whiteTex);
    }
    if (bgfx::isValid(s_texDebugScene))
        bgfx::setTexture(4, s_texDebugScene,
                         bgfx::isValid(debugSceneTex)
                             ? debugSceneTex : m_whiteTex);
    // Mode 9 shows the mirror target itself. Binding the black
    // texture when there is none keeps "no reflection pass ran"
    // distinguishable from "the pass ran and mirrored nothing":
    // the mode tints zero coverage, and a black bind reads as
    // coverage 0 everywhere.
    if (bgfx::isValid(s_texRefl))
        bgfx::setTexture(5, s_texRefl,
                         bgfx::isValid(reflTex) ? reflTex : m_blackTex);
    // Mode 10 shows the particle impact map. Black when there is
    // none, which the mode reads as "no cell was ever struck" — the
    // same picture a map that nothing has reported into gives, and
    // the distinction that matters (map vs surface) is elsewhere.
    if (bgfx::isValid(s_texImpact)) {
        bgfx::setTexture(6, s_texImpact,
                         bgfx::isValid(impactTex) ? impactTex
                                                  : m_blackTex);
        // Recorded with this draw: a uniform pushed for the water
        // pass belongs to that draw, not to the frame.
        const float cfg[4] = {float(kImpactRes), impactNow,
                              impactLife, 1.0f};
        bgfx::setUniform(u_waterImpactCfg, cfg);
    }
    fullscreen(ViewDebug, m_progDebug,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

bool BGFXView::ensureDebugScene()
{
    if (!bgfx::isValid(m_progDebugScene))
        return false;
    if (bgfx::isValid(debugSceneFbo) && debugSceneW == width
            && debugSceneH == height)
        return true;
    if (bgfx::isValid(debugSceneFbo)) {
        bgfx::destroy(debugSceneFbo);
        debugSceneFbo = BGFX_INVALID_HANDLE;
    }
    for (auto tex : {&debugSceneTex, &debugSceneDepth}) {
        if (bgfx::isValid(*tex)) {
            bgfx::destroy(*tex);
            *tex = BGFX_INVALID_HANDLE;
        }
    }
    const bgfx::Caps *caps = bgfx::getCaps();
    if (!(caps->formats[bgfx::TextureFormat::RGBA16F]
          & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER))
        return false;
    // RGBA16F: the overdraw counts accumulate additively well past
    // 8-bit range, and the UV mode wants more than 8-bit texcoords.
    // POINT-sampled — the blit reads pixel centers 1:1.
    const uint64_t flags = 0
        | BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP;
    debugSceneTex = bgfx::createTexture2D(width, height, false, 1,
        bgfx::TextureFormat::RGBA16F, flags);
    debugSceneDepth = bgfx::createTexture2D(width, height, false, 1,
        bgfx::TextureFormat::D24S8,
        flags | BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle att[2] = {debugSceneTex, debugSceneDepth};
    debugSceneFbo = bgfx::createFrameBuffer(2, att, false);
    debugSceneW = width;
    debugSceneH = height;
    return bgfx::isValid(debugSceneFbo);
}

bool BGFXView::ensureCaptureTargets()
{
    if (!idReadbackSupported() || !bgfx::isValid(m_progDepthEnc)
            || !bgfx::isValid(bgfxColor))
        return false;
    // A blit requires matching formats, so the staging texture follows
    // whatever readbackCapture will actually copy FROM -- and that is
    // not always the scene colour. With a colour transform selected the
    // finished image is the present pass's RGBA8 output, even though
    // the scene colour behind it is RGBA16F; sizing this off hdrScene
    // alone would ask bgfx to blit RGBA8 into an RGBA16F texture.
    const bool encoded = outputTransform != Render::OutputConfig::None
        && bgfx::isValid(presentTex);
    const bgfx::TextureFormat::Enum want = (!encoded && hdrScene)
        ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
    if (bgfx::isValid(captureDepthFbo) && captureW == width
            && captureH == height && captureColorFormat == want)
        return true;
    if (bgfx::isValid(captureDepthFbo)) {
        bgfx::destroy(captureDepthFbo);
        captureDepthFbo = BGFX_INVALID_HANDLE;
    }
    for (auto tex : {&captureDepthTex, &captureColorRead, &captureDepthRead}) {
        if (bgfx::isValid(*tex)) {
            bgfx::destroy(*tex);
            *tex = BGFX_INVALID_HANDLE;
        }
    }
    const bgfx::Caps *caps = bgfx::getCaps();
    // R32F for the encoded depth: geometryPixels counts against a 0.999
    // threshold, and eight bits of a non-linear depth buffer put far
    // more than a thousandth of the range in the last code -- an RGBA8
    // target would report the whole background as geometry. A backend
    // that cannot render R32F simply has no portable capture; the
    // caller falls back to the GL readback, which is what every such
    // backend is anyway.
    if (!(caps->formats[bgfx::TextureFormat::R32F]
          & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER))
        return false;
    // POINT-sampled, CLAMPed: the encode reads depth texel centers 1:1
    // and the blit that follows does too.
    const uint64_t flags = 0
        | BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP;
    captureDepthTex = bgfx::createTexture2D(width, height, false, 1,
        bgfx::TextureFormat::R32F, flags);
    bgfx::TextureHandle att[1] = {captureDepthTex};
    captureDepthFbo = bgfx::createFrameBuffer(1, att, false);
    captureColorRead = bgfx::createTexture2D(width, height, false, 1, want,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    captureDepthRead = bgfx::createTexture2D(width, height, false, 1,
        bgfx::TextureFormat::R32F,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    captureW = width;
    captureH = height;
    captureColorFormat = want;
    return bgfx::isValid(captureDepthFbo)
        && bgfx::isValid(captureColorRead)
        && bgfx::isValid(captureDepthRead);
}

uint32_t BGFXView::readbackCapture(void *color, void *depth)
{
    if (!bgfx::isValid(captureColorRead) || !bgfx::isValid(captureDepthRead))
        return 0;
    // The image the frame actually finished with: the encoded present
    // output when a colour transform is selected, the scene colour
    // otherwise. Exactly the choice the GL composite makes, so the two
    // capture routes cannot disagree about what the frame was.
    const bool encoded = outputTransform != Render::OutputConfig::None
        && bgfx::isValid(presentTex);
    const bgfx::TextureHandle source = encoded ? presentTex : bgfxColor;
    if (!bgfx::isValid(source))
        return 0;
    bgfx::blit(vid(ViewCapture), captureColorRead, 0, 0, source);
    bgfx::blit(vid(ViewCapture), captureDepthRead, 0, 0, captureDepthTex);
    // Two reads, one frame: bgfx returns the frame each will be ready
    // at, and they are asked in the same frame, so the later of the two
    // is when BOTH are filled. Taking the first would hand the caller a
    // half-written depth buffer.
    const uint32_t c = bgfx::readTexture(captureColorRead, color);
    const uint32_t d = bgfx::readTexture(captureDepthRead, depth);
    return std::max(c, d);
}


void BGFXView::submitDebugScene(const Render::DrawCall &draw, int mode)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    setClipUniforms(mat);
    float params[4] = {float(mode), 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_debugParams, params);
        // ⚠️ The debug-scene vertex shader reads u_params.w as an NDC
        // depth bias, and a bgfx uniform keeps whatever the last draw
        // left in it. These modes want none, but they must SAY so: a
        // line draw leaves its dim alpha (1.0) there, and one NDC unit
        // of bias is past the far plane, which rasterizes nothing at all
        // (docs/FarFieldProxies.md §12.6 — the same trap, once already).
        float bias[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_params, bias);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix,
                     (float)height);
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (mode == 6)
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE);
    else
        state |= BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    if (mat.culling && !mat.twoside)
        state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
    bgfx::setState(state);
    bgfx::submit(vid(ViewDebugScene),
                 clipped ? m_progDebugSceneClip : m_progDebugScene);
    ++drawcount;
}

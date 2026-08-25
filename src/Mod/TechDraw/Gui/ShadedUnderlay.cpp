/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
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

#include "PreCompiled.h"
#ifndef _PreComp_
#include <cmath>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <algorithm>

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Geom_Surface.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <Inventor/SbRotation.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoFrustumCamera.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#endif// #ifndef _PreComp_

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Inventor/SoFCSwitch.h>
#include <Gui/Renderer/Renderer.h>
#include <Gui/SoFCOffscreenRenderer.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewProvider.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/TechDraw/App/DrawComplexSection.h>
#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/DrawViewDetail.h>
#include <Mod/TechDraw/App/DrawViewPart.h>
#include <Mod/TechDraw/App/DrawViewSection.h>
#include <Mod/TechDraw/App/Preferences.h>
#include <Mod/TechDraw/App/ShapeUtils.h>

#include "ShadedUnderlay.h"

using namespace TechDrawGui;
using TechDraw::DrawViewPart;
using DU = TechDraw::DrawUtil;

namespace
{

// The capture density cap. A raster bigger than this per side re-rates
// the density instead of growing without bound (the rect is unchanged,
// so registration does not move; the image is merely softer).
constexpr int kMaxUnderlayPixels = 4096;

// Silhouette anti-aliasing needs a little empty border or the boundary
// pixels are clipped by the raster edge; rect and camera window grow
// together, so registration is preserved.
constexpr double kMarginPx = 2.0;

std::vector<App::DocumentObject*> sourcesOf(DrawViewPart* dvp)
{
    std::vector<App::DocumentObject*> result = dvp->Source.getValues();
    const auto& xsource = dvp->XSource.getValues();
    result.insert(result.end(), xsource.begin(), xsource.end());
    return result;
}

// Whether a re-capture is the same picture. GL renders of an unchanged
// projection are not byte-stable -- shading gradients jitter by a few
// counts between render paths (measured max channel delta 16 on a
// curved face) -- so idempotency is judged with a tolerance: any pixel
// moving far, or the whole image drifting, is a real change; sub-
// threshold jitter is not, and must not dirty the document.
bool sameCapture(const QImage& a, const QImage& b)
{
    if (a.size() != b.size())
        return false;
    qint64 total = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int dr = std::abs(qRed(pa[x]) - qRed(pb[x]));
            const int dg = std::abs(qGreen(pa[x]) - qGreen(pb[x]));
            const int db = std::abs(qBlue(pa[x]) - qBlue(pb[x]));
            const int da = std::abs(qAlpha(pa[x]) - qAlpha(pb[x]));
            const int dmax = std::max(std::max(dr, dg), std::max(db, da));
            if (dmax > 48)
                return false;
            total += dmax;
        }
    }
    const qint64 pixels = qint64(a.width()) * a.height();
    return pixels == 0 || double(total) / double(pixels) <= 1.0;
}

// The base view of a section or detail. BaseView is declared on each
// derived class separately, so resolve it by name.
DrawViewPart* baseViewOf(const DrawViewPart* dvp)
{
    auto* prop = dynamic_cast<App::PropertyLink*>(
        const_cast<DrawViewPart*>(dvp)->getPropertyByName("BaseView"));
    return prop ? dynamic_cast<DrawViewPart*>(prop->getValue()) : nullptr;
}

// The uniform fallback color for a derived (cut/clipped) shape: the
// first source object's ShapeColor (following BaseView for views that
// carry no Source of their own). Used whole when no face->color
// resolver is available (doc sec 30), per-face as its last resort.
SbColor shadeColor(DrawViewPart* dvp)
{
    for (int depth = 0; dvp && depth < 8; ++depth) {
        for (auto* obj : sourcesOf(dvp)) {
            auto* vp = Gui::Application::Instance->getViewProvider(obj);
            if (!vp)
                continue;
            auto* prop =
                dynamic_cast<App::PropertyColor*>(vp->getPropertyByName("ShapeColor"));
            if (prop) {
                const auto c = prop->getValue();
                return {c.r, c.g, c.b};
            }
        }
        dvp = baseViewOf(dvp);
    }
    return {0.8F, 0.8F, 0.8F};
}

// One source object's geometry and colors, for mapping the faces of a
// derived (cut/clipped) shape back to their origins. The shape is the
// object's own (global frame, placement applied) -- the same frame the
// cut input was built from.
struct SourceColorEntry
{
    TopoDS_Shape shape;
    Bnd_Box box;// enlarged by the probe tolerance
    SbColor uniform;
    bool hasFaceColors = false;
    TopTools_IndexedMapOfShape faceMap;
    std::vector<SbColor> faceColors;// by faceMap index - 1
};

// Maps a face of the derived shape to a color by geometry, not by
// history: the cut/clip runs in a worker with a plain BRepAlgoAPI (no
// element map survives it), but every face of the result either lies
// ON a source's boundary (a surviving piece of a source face -- same
// surface, so distance is essentially zero) or strictly INSIDE a
// source solid (a face born of the cut). One exact probe point per
// face decides which, robust against TrimAfterCut and per-solid cut
// failures re-ordering the piece compound (which breaks any index
// scheme).
struct DerivedColorResolver
{
    std::vector<SourceColorEntry> entries;
    gp_Trsf toGlobal;// derived-frame point -> source global frame
    double tol = 1e-3;
    SbColor fallback{0.8F, 0.8F, 0.8F};
    bool useCutColor = false;
    SbColor cutColor{0.8F, 0.8F, 0.8F};

    SbColor colorOf(const gp_Pnt& probeLocal) const
    {
        const gp_Pnt probe = probeLocal.Transformed(toGlobal);
        BRepBuilderAPI_MakeVertex mkVertex(probe);
        const SourceColorEntry* inner = nullptr;
        for (const auto& entry : entries) {
            if (entry.box.IsOut(probe))
                continue;
            BRepExtrema_DistShapeShape dss(mkVertex.Vertex(), entry.shape);
            if (!dss.IsDone() || dss.NbSolution() < 1)
                continue;
            // A probe strictly inside the solid reports distance zero
            // as an "inner solution", NOT a boundary hit -- that is
            // precisely a face born of the cut. Hold the owner and
            // keep looking: another source may own the surface itself.
            if (dss.InnerSolution()) {
                if (!inner)
                    inner = &entry;
                continue;
            }
            if (dss.Value() > tol)
                continue;
            if (entry.hasFaceColors) {
                for (int i = 1; i <= dss.NbSolution(); ++i) {
                    const TopoDS_Shape& support = dss.SupportOnShape2(i);
                    if (support.ShapeType() != TopAbs_FACE)
                        continue;
                    const int idx = entry.faceMap.FindIndex(support);
                    if (idx > 0)
                        return entry.faceColors[size_t(idx) - 1];
                }
            }
            return entry.uniform;
        }
        // Born of the cut: the section's cut surface color when the
        // view displays one; otherwise the owning solid's body color.
        if (inner)
            return useCutColor ? cutColor : inner->uniform;
        return fallback;
    }

    // A point ON the face, interior: the UV centroid of a triangle
    // evaluated on the exact surface. The mesh nodes themselves sit up
    // to a deflection off a curved surface -- far beyond the probe
    // tolerance -- so only fall back to a node average (and likely the
    // fallback color) when the triangulation carries no UV.
    SbColor faceColor(const TopoDS_Face& face, const Handle(Poly_Triangulation)& tri,
                      const TopLoc_Location& loc) const
    {
        if (tri.IsNull() || tri->NbTriangles() < 1)
            return fallback;
        int n1{}, n2{}, n3{};
        tri->Triangle(1).Get(n1, n2, n3);
        if (tri->HasUVNodes()) {
            TopLoc_Location surfLoc;
            Handle(Geom_Surface) surf = BRep_Tool::Surface(face, surfLoc);
            if (!surf.IsNull()) {
                const gp_Pnt2d uv1 = tri->UVNode(n1);
                const gp_Pnt2d uv2 = tri->UVNode(n2);
                const gp_Pnt2d uv3 = tri->UVNode(n3);
                const double u = (uv1.X() + uv2.X() + uv3.X()) / 3.0;
                const double v = (uv1.Y() + uv2.Y() + uv3.Y()) / 3.0;
                return colorOf(surf->Value(u, v).Transformed(surfLoc.Transformation()));
            }
        }
        const gp_Trsf& trsf = loc.Transformation();
        const gp_Pnt p1 = tri->Node(n1).Transformed(trsf);
        const gp_Pnt p2 = tri->Node(n2).Transformed(trsf);
        const gp_Pnt p3 = tri->Node(n3).Transformed(trsf);
        return colorOf(gp_Pnt((p1.X() + p2.X() + p3.X()) / 3.0,
                              (p1.Y() + p2.Y() + p3.Y()) / 3.0,
                              (p1.Z() + p2.Z() + p3.Z()) / 3.0));
    }
};

// The classification cap: a derived shape with more faces than this
// keeps the uniform color rather than pay a distance query per face.
constexpr int kMaxResolverFaces = 500;

// Solids only: the cut input is solids (doSectionCut explores
// TopAbs_SOLID) and the detail clip intersects solids/shells, so a 2D
// source (a sketch lying on a model face would false-hit the boundary
// probe) contributes nothing and is skipped.
bool appendSourceEntry(App::DocumentObject* obj, double tol,
                       std::vector<SourceColorEntry>& out)
{
    TopoDS_Shape shape = Part::Feature::getShape(obj);
    if (shape.IsNull() || !TopExp_Explorer(shape, TopAbs_SOLID).More())
        return false;
    SourceColorEntry entry;
    entry.shape = shape;
    BRepBndLib::Add(shape, entry.box, /*useTriangulation*/ false);
    if (entry.box.IsVoid())
        return false;
    entry.box.Enlarge(tol);
    auto* vp = Gui::Application::Instance->getViewProvider(obj);
    if (auto* prop = vp ? dynamic_cast<App::PropertyColor*>(
                         vp->getPropertyByName("ShapeColor"))
                        : nullptr) {
        const auto c = prop->getValue();
        entry.uniform = SbColor(c.r, c.g, c.b);
    }
    else {
        entry.uniform = SbColor(0.8F, 0.8F, 0.8F);
    }
    if (auto* prop = vp ? dynamic_cast<App::PropertyColorList*>(
                         vp->getPropertyByName("DiffuseColor"))
                        : nullptr) {
        TopExp::MapShapes(shape, TopAbs_FACE, entry.faceMap);
        const auto& colors = prop->getValues();
        if (int(colors.size()) == entry.faceMap.Extent() && colors.size() > 1) {
            entry.faceColors.reserve(colors.size());
            for (const auto& c : colors)
                entry.faceColors.emplace_back(c.r, c.g, c.b);
            entry.hasFaceColors = true;
        }
    }
    out.push_back(std::move(entry));
    return true;
}

// The resolver for a section's cut shape or a detail's clipped region.
// False when the mapping back to the sources is not available (a
// detail in the base-view chain puts the shape in a frame this cannot
// invert exactly) or not worth building (single plain-colored source
// and no cut surface color to place); the caller then shades uniform.
bool buildDerivedResolver(DrawViewPart* dvp, const TopoDS_Shape& derivedShape,
                          const SbColor& fallback, DerivedColorResolver& resolver)
{
    int nFaces = 0;
    for (TopExp_Explorer expl(derivedShape, TopAbs_FACE); expl.More(); expl.Next())
        ++nFaces;
    if (nFaces > kMaxResolverFaces) {
        Base::Console().Log("ShadedUnderlay: %d faces exceed the per-face color cap\n",
                            nFaces);
        return false;
    }

    DrawViewPart* sourceView = nullptr;
    if (auto* dvs = dynamic_cast<TechDraw::DrawViewSection*>(dvp)) {
        // The cut input is the BaseView chain's source shape, in the
        // global frame all the way down a section chain. A detail
        // ancestor hands over its own centered frame instead -- skip.
        DrawViewPart* base = baseViewOf(dvs);
        while (dynamic_cast<TechDraw::DrawViewSection*>(base))
            base = baseViewOf(base);
        if (!base || dynamic_cast<TechDraw::DrawViewDetail*>(base))
            return false;
        sourceView = base;
        resolver.tol = 1e-3;
        resolver.useCutColor = dvs->CutSurfaceDisplay.isValue("Color");
        if (resolver.useCutColor) {
            auto* vp = Gui::Application::Instance->getViewProvider(dvs);
            auto* prop = vp ? dynamic_cast<App::PropertyColor*>(
                             vp->getPropertyByName("CutSurfaceColor"))
                            : nullptr;
            if (prop) {
                const auto c = prop->getValue();
                resolver.cutColor = SbColor(c.r, c.g, c.b);
            }
            else {
                resolver.useCutColor = false;
            }
        }
    }
    else if (auto* dvd = dynamic_cast<TechDraw::DrawViewDetail*>(dvp)) {
        // detailExec hands makeDetailShape the base's shape rotated by
        // the base Rotation and centered on its bbox centroid; invert
        // that here. The centroid is re-derived (it is not stored
        // alone), and bbox centers read triangulation-dependent bounds
        // that can drift a few hundredths -- so the detail probe runs
        // with a matching looser tolerance, degrading at worst to the
        // owner's uniform color, never to a wrong owner's.
        auto* base = dynamic_cast<DrawViewPart*>(dvd->BaseView.getValue());
        if (!base || dynamic_cast<TechDraw::DrawViewSection*>(base)
            || dynamic_cast<TechDraw::DrawViewDetail*>(base))
            return false;
        sourceView = base;
        TopoDS_Shape src = base->getSourceShape();
        if (src.IsNull())
            return false;
        const double rotDeg = base->Rotation.getValue();
        TopoDS_Shape rotated = src;
        gp_Trsf unrotate;
        if (!TechDraw::DrawUtil::fpCompare(rotDeg, 0.0)) {
            // The detail was built from getShapeForDetail's FUSED
            // sources; the unfused compound has the identical bbox, so
            // the centroid matches without paying the fuse.
            rotated = TechDraw::ShapeUtils::rotateShape(src, base->getProjectionCS(),
                                                        rotDeg);
            unrotate.SetRotation(base->getProjectionCS().Axis(),
                                 -rotDeg * M_PI / 180.0);
        }
        const gp_Pnt center =
            TechDraw::ShapeUtils::findCentroid(rotated, base->Direction.getValue());
        gp_Trsf uncenter;
        uncenter.SetTranslation(gp_Vec(center.X(), center.Y(), center.Z()));
        resolver.toGlobal = unrotate.Multiplied(uncenter);
        resolver.tol = 0.1;
    }
    else {
        return false;
    }

    for (auto* obj : sourcesOf(sourceView))
        appendSourceEntry(obj, resolver.tol, resolver.entries);
    if (resolver.entries.empty())
        return false;
    resolver.fallback = fallback;
    if (resolver.entries.size() == 1 && !resolver.entries.front().hasFaceColors
        && !resolver.useCutColor)
        return false;// uniform would resolve identically, for free
    return true;
}

// The resolver for a single object's own shape (the hidden-source
// tessellation path): identity frame, every face on its own boundary.
// Only worth building when the object carries per-face colors.
bool buildObjectResolver(App::DocumentObject* obj, const SbColor& fallback,
                         DerivedColorResolver& resolver)
{
    if (!appendSourceEntry(obj, resolver.tol, resolver.entries))
        return false;
    if (!resolver.entries.front().hasFaceColors)
        return false;
    int nFaces = resolver.entries.front().faceMap.Extent();
    if (nFaces > kMaxResolverFaces)
        return false;
    resolver.fallback = fallback;
    return true;
}

// A derived shape as a shaded Coin scene: the shape's triangulation as
// one indexed face set. Normals are Coin-generated with a crease angle
// -- tessellated curvature shades smooth, real corners crease. With a
// resolver, faces take their mapped colors through a per-face-indexed
// material palette; without one, the single color covers everything.
SoSeparator* buildMeshNode(const TopoDS_Shape& shape, const SbColor& color,
                           const DerivedColorResolver* resolver = nullptr)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds, /*useTriangulation*/ false);
    if (bounds.IsVoid())
        return nullptr;
    const double diag = std::sqrt(bounds.SquareExtent());
    // Relative deflection; the raster caps at kMaxUnderlayPixels per
    // side, where 0.25% of the diagonal is around a pixel.
    BRepMesh_IncrementalMesh mesher(shape, std::max(diag * 0.0025, 1e-4), Standard_False, 0.5,
                                    Standard_True);
    (void)mesher;

    std::vector<SbVec3f> points;
    std::vector<int32_t> indices;
    std::vector<SbColor> palette;
    std::vector<int32_t> matIndices;// per triangle, parallel to indices
    for (TopExp_Explorer expl(shape, TopAbs_FACE); expl.More(); expl.Next()) {
        const TopoDS_Face& face = TopoDS::Face(expl.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull())
            continue;
        int32_t matIndex = 0;
        if (resolver) {
            const SbColor c = resolver->faceColor(face, tri, loc);
            auto found = std::find(palette.begin(), palette.end(), c);
            matIndex = int32_t(found - palette.begin());
            if (found == palette.end())
                palette.push_back(c);
        }
        const gp_Trsf& trsf = loc.Transformation();
        const bool reversed = (face.Orientation() == TopAbs_REVERSED);
        const int base = int(points.size());
        const int nbNodes = tri->NbNodes();
        for (int i = 1; i <= nbNodes; ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            points.emplace_back(float(p.X()), float(p.Y()), float(p.Z()));
        }
        const int nbTris = tri->NbTriangles();
        for (int i = 1; i <= nbTris; ++i) {
            int n1{}, n2{}, n3{};
            tri->Triangle(i).Get(n1, n2, n3);
            if (reversed)
                std::swap(n2, n3);
            indices.push_back(base + n1 - 1);
            indices.push_back(base + n2 - 1);
            indices.push_back(base + n3 - 1);
            indices.push_back(-1);
            matIndices.push_back(matIndex);
        }
    }
    if (indices.empty())
        return nullptr;

    auto sep = new SoSeparator;
    auto hints = new SoShapeHints;
    hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
    // Two-sided lighting: cut and clipped shapes expose interior faces.
    hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
    hints->creaseAngle = 0.6F;
    sep->addChild(hints);
    auto material = new SoMaterial;
    if (palette.size() > 1)
        material->diffuseColor.setValues(0, int(palette.size()), palette.data());
    else
        material->diffuseColor = palette.empty() ? color : palette.front();
    sep->addChild(material);
    if (palette.size() > 1) {
        auto binding = new SoMaterialBinding;
        binding->value = SoMaterialBinding::PER_FACE_INDEXED;
        sep->addChild(binding);
    }
    auto coords = new SoCoordinate3;
    coords->point.setValues(0, int(points.size()), points.data());
    sep->addChild(coords);
    auto faceSet = new SoIndexedFaceSet;
    faceSet->coordIndex.setValues(0, int(indices.size()), indices.data());
    if (palette.size() > 1)
        faceSet->materialIndex.setValues(0, int(matIndices.size()), matIndices.data());
    sep->addChild(faceSet);
    return sep;
}

// The bgfx-quality capture -- doc sec 26.2's recorded follow-up. When
// a 3D view of the source document runs a backend renderer, capture
// through it instead of the private Coin scene: the backend's
// PBR/AO/matcap-grade shading, with the per-capture object filter
// (Renderer::setCaptureFilter) keeping selection, overlays and every
// other object out of the frame. Registration is identical by
// construction: the same camera volume, handed over as matrices. Any
// refusal -- no backend, a 3D-hidden source (no scene draws), a
// backend without the filter -- falls back to the Coin capture.
bool captureViaBackend(DrawViewPart* dvp, SoCamera* camera, int wPx, int hPx,
                       QImage& image)
{
    if (!TechDraw::Preferences::getPreferenceGroup("General")
             ->GetBool("ShadedUnderlayBackend", true))
        return false;
    const std::vector<App::DocumentObject*> sources = sourcesOf(dvp);
    if (sources.empty())
        return false;
    Gui::Document* gdoc =
        Gui::Application::Instance->getDocument(sources.front()->getDocument());
    if (!gdoc)
        return false;
    Render::Renderer* renderer = nullptr;
    Gui::View3DInventorViewer* viewer = nullptr;
    for (auto* view :
         gdoc->getMDIViewsOfType(Gui::View3DInventor::getClassTypeId())) {
        auto* v3d = static_cast<Gui::View3DInventor*>(view);
        if (auto* r = v3d->getViewer()->getExternalRenderer()) {
            renderer = r;
            viewer = v3d->getViewer();
            break;
        }
    }
    if (!renderer || !viewer)
        return false;
    std::vector<std::pair<std::string, std::string>> ids;
    ids.reserve(sources.size());
    for (auto* obj : sources)
        ids.emplace_back(obj->getDocument()->getName(), obj->getNameInDocument());
    if (!renderer->setCaptureFilter(ids))
        return false;

    SbMatrix viewMat, projMat;
    camera->getViewVolume(0.0F).getMatrices(viewMat, projMat);

    auto* glWidget = qobject_cast<QOpenGLWidget*>(viewer->viewport());
    if (!glWidget) {
        renderer->clearCaptureFilter();
        return false;
    }
    QOpenGLContext* previous = QOpenGLContext::currentContext();
    QSurface* previousSurface = previous ? previous->surface() : nullptr;
    glWidget->makeCurrent();
    bool ok = false;
    std::vector<unsigned char> pixels;
    {
        QOpenGLFramebufferObject fbo(wPx, hPx, QOpenGLFramebufferObject::Depth);
        fbo.bind();
        QOpenGLFunctions* gl = nullptr;
        if (QOpenGLContext* ctx = QOpenGLContext::currentContext())
            gl = ctx->functions();
        if (gl)
            gl->glViewport(0, 0, wPx, hPx);
        ok = renderer->renderOffscreen(QColor(255, 255, 255, 255),
                                       &viewMat.getValue(), &projMat.getValue(),
                                       wPx, hPx);
        if (ok && gl) {
            // Read the pixels directly: the engine's finished frame
            // does not maintain a meaningful alpha channel, and
            // QOpenGLFramebufferObject::toImage's premultiplied
            // conversion would zero every color under it. Re-bind
            // first -- the engine's frame leaves its own readback FBO
            // on the READ binding, which is what glReadPixels reads.
            fbo.bind();
            pixels.resize(size_t(wPx) * hPx * 4);
            gl->glReadPixels(0, 0, wPx, hPx, GL_RGBA, GL_UNSIGNED_BYTE,
                             pixels.data());
        }
        fbo.release();
    }
    glWidget->doneCurrent();
    if (previous && previousSurface)
        previous->makeCurrent(previousSurface);
    renderer->clearCaptureFilter();
    if (!ok || pixels.empty()) {
        Base::Console().Log("ShadedUnderlay: backend render refused\n");
        return false;
    }
    // GL reads bottom-up; the image is top-down. Alpha comes from
    // keying the exact clear color, like the Coin offscreen renderer
    // does (the anti-aliased fringe blends toward white -- invisible
    // on paper).
    image = QImage(wPx, hPx, QImage::Format_ARGB32);
    for (int y = 0; y < hPx; ++y) {
        const unsigned char* src = pixels.data() + size_t(hPx - 1 - y) * wPx * 4;
        QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < wPx; ++x) {
            const unsigned char r = src[size_t(x) * 4];
            const unsigned char g = src[size_t(x) * 4 + 1];
            const unsigned char b = src[size_t(x) * 4 + 2];
            dst[x] = (r == 255 && g == 255 && b == 255) ? qRgba(255, 255, 255, 0)
                                                        : qRgba(r, g, b, 255);
        }
    }
    Base::Console().Log("ShadedUnderlay: backend capture used\n");
    return true;
}

}// namespace

bool ShadedUnderlay::capture(DrawViewPart* dvp, QImage& image, QRectF& rect)
{
    if (!dvp || !Gui::Application::Instance)
        return false;

    // What to render and through which frame. A plain part view
    // renders its sources' ViewProvider roots; a section or a detail
    // projects a DERIVED shape (the cut solid / the clipped region)
    // that exists nowhere in the 3D scene, so its capture tessellates
    // that shape instead -- through the same window math, mirroring
    // each type's own centering step for step.
    TopoDS_Shape shape;    // bounds (and, derived, the scene) in frame F
    gp_Ax2 viewCS;         // the camera frame, in F
    gp_Pnt gCentroid;      // the view's 2D origin, in F
    bool derived = false;  // scene = tessellated shape, not VP roots

    if (auto* dvd = dynamic_cast<TechDraw::DrawViewDetail*>(dvp)) {
        auto* baseDvp = dynamic_cast<DrawViewPart*>(dvd->BaseView.getValue());
        if (!baseDvp)
            return false;
        // Null until the async intersection lands; the paint that
        // follows it captures.
        shape = dvd->getDetailShape();
        if (shape.IsNull())
            return false;
        // Mirror detailExec: the clipped shape lives in the base
        // view's centered frame; the 2D origin is the anchor lifted to
        // R3, and the camera is the BASE view's projection CS (a
        // detail of a rotated section gets that rotation baked into
        // the shape it is handed) rotated by the detail's own
        // Rotation.
        const gp_Ax2 baseCS = baseDvp->getProjectionCS();
        gCentroid = DU::togp_Pnt(DU::toR3(baseCS, dvd->AnchorPoint.getValue()));
        const gp_Ax1 rotationAxis(gp_Pnt(0.0, 0.0, 0.0), baseCS.Direction());
        viewCS = baseCS.Rotated(rotationAxis, -dvd->Rotation.getValue() * M_PI / 180.0);
        derived = true;
    }
    else if (auto* dvs = dynamic_cast<TechDraw::DrawViewSection*>(dvp)) {
        // An aligned complex section projects an unfolded fiction
        // through centerShapeXY, not the centroid chain -- no raster
        // can register against that; it stays unshaded.
        auto* dcs = dynamic_cast<TechDraw::DrawComplexSection*>(dvs);
        if (dcs && dcs->ProjectionStrategy.getValue() != 0)
            return false;
        // Null until the async cut lands.
        shape = dvs->getCutShapeRaw();
        if (shape.IsNull())
            return false;
        // Virtual dispatch gives the section CS and the centroid its
        // prepareShape computed for the cut shape -- the same members
        // the base-view math reads.
        viewCS = dvp->getRotatedCS();
        gCentroid = DU::togp_Pnt(dvp->getOriginalCentroid());
        derived = true;
    }
    else {
        shape = dvp->getSourceShape();
        if (shape.IsNull())
            return false;
        // The frame the projected geometry is computed in: the
        // projection CS rotated by -Rotation, which is how
        // makeGeometryForShape's rotating of the shape by +Rotation
        // reads from the camera's side.
        viewCS = dvp->getRotatedCS();
        // The exact centroid the HLR chain centered the shape on. Not
        // recomputed here: ShapeUtils::findCentroid reads
        // triangulation-dependent bounds, and a recompute after the 3D
        // view tessellates would drift the rect (measured 0.02mm) and
        // re-capture an unchanged projection.
        gCentroid = DU::togp_Pnt(dvp->getOriginalCentroid());
    }

    gp_Trsf toView;
    toView.SetTransformation(gp_Ax3(viewCS));
    gp_Pnt vCentroid = gCentroid.Transformed(toView);

    // Source bounds in the view frame. Conservative bounds only pad the
    // rect with empty pixels -- the camera window and the rect move
    // together, so looseness cannot shift registration.
    Bnd_Box bounds;
    // Geometry bounds, never triangulation: the mesh appears (and
    // refines) as the 3D view tessellates, and bounds that follow it
    // would re-rect -- and re-capture -- an unchanged projection.
    BRepBndLib::Add(shape.Moved(TopLoc_Location(toView)), bounds,
                    /*useTriangulation*/ false);
    if (bounds.IsVoid())
        return false;
    Standard_Real xMin{}, yMin{}, zMin{}, xMax{}, yMax{}, zMax{};
    bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);

    const double scale = dvp->getScale();
    double resolution = dvp->UnderlayResolution.getValue();
    if (scale <= 0.0 || resolution <= 0.0)
        return false;

    // The 2D window in page mm, centroid origin, +Y up -- the same
    // frame the projected edges live in before invertY.
    const double marginMm = kMarginPx / resolution;
    const bool perspective = dvp->Perspective.getValue();
    // The HLR projects the SCALED shape with focal length Focus (eye
    // on the view axis at +Focus over the centroid plane, projecting
    // onto that plane: u = x * f / (f - z)); from the unscaled side
    // that reads as a focal distance of Focus/scale.
    const double focusM = dvp->Focus.getValue() / scale;
    double x0, y0, w, h;
    if (perspective) {
        if (!(focusM > 0.0))
            return false;
        // a shape reaching the eye cannot project
        if (zMax - vCentroid.Z() >= focusM * 0.99)
            return false;
        // The projection of the bounds box is the hull of its
        // projected corners (central projection preserves convexity
        // in front of the eye), so the corner extremes bound the
        // whole projection.
        double uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0;
        bool first = true;
        for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
                for (int iz = 0; iz < 2; ++iz) {
                    const double dx = (ix ? xMax : xMin) - vCentroid.X();
                    const double dy = (iy ? yMax : yMin) - vCentroid.Y();
                    const double dz = (iz ? zMax : zMin) - vCentroid.Z();
                    const double q = focusM / (focusM - dz);
                    const double u = scale * dx * q;
                    const double v = scale * dy * q;
                    if (first) {
                        uMin = uMax = u;
                        vMin = vMax = v;
                        first = false;
                    }
                    else {
                        uMin = std::min(uMin, u);
                        uMax = std::max(uMax, u);
                        vMin = std::min(vMin, v);
                        vMax = std::max(vMax, v);
                    }
                }
        x0 = uMin - marginMm;
        y0 = vMin - marginMm;
        w = uMax - uMin + 2.0 * marginMm;
        h = vMax - vMin + 2.0 * marginMm;
    }
    else {
        x0 = (xMin - vCentroid.X()) * scale - marginMm;
        y0 = (yMin - vCentroid.Y()) * scale - marginMm;
        w = (xMax - xMin) * scale + 2.0 * marginMm;
        h = (yMax - yMin) * scale + 2.0 * marginMm;
    }
    if (w <= 0.0 || h <= 0.0)
        return false;
    rect = QRectF(x0, y0, w, h);

    const double maxDim = std::max(w, h) * resolution;
    if (maxDim > kMaxUnderlayPixels)
        resolution *= kMaxUnderlayPixels / maxDim;
    const int wPx = std::max(16, int(std::lround(w * resolution)));
    const int hPx = std::max(16, int(std::lround(h * resolution)));

    // The camera: centered on the window, backed off along the view
    // direction far enough to clear the shape.
    const SbVec3f right(float(viewCS.XDirection().X()), float(viewCS.XDirection().Y()),
                        float(viewCS.XDirection().Z()));
    const SbVec3f up(float(viewCS.YDirection().X()), float(viewCS.YDirection().Y()),
                     float(viewCS.YDirection().Z()));
    const SbVec3f back(float(viewCS.Direction().X()), float(viewCS.Direction().Y()),
                       float(viewCS.Direction().Z()));

    const double depth = std::max(zMax - zMin, 1.0);

    SoCamera* camera = nullptr;
    if (perspective) {
        // The eye of the HLR's perspective projector, mirrored: on
        // the view axis through the centroid, focusM over the
        // centroid plane. The window is off-axis in general, so the
        // camera is an asymmetric frustum whose section at the
        // centroid plane is exactly the rect (in model units).
        auto frustum = new SoFrustumCamera;
        const double eyeZ = vCentroid.Z() + focusM;
        frustum->position = right * float(vCentroid.X()) + up * float(vCentroid.Y())
            + back * float(eyeZ);
        const double distToShape = eyeZ - zMax;// > 0 by the guard above
        const double nearD = std::max(focusM * 1e-3, distToShape * 0.5);
        frustum->nearDistance = float(nearD);
        frustum->farDistance = float(eyeZ - zMin + depth);
        // glFrustum semantics: the window edges are given at the near
        // plane; scale the centroid-plane window down by near/focusM.
        const double ratio = nearD / focusM;
        frustum->left = float(x0 / scale * ratio);
        frustum->right = float((x0 + w) / scale * ratio);
        frustum->bottom = float(y0 / scale * ratio);
        frustum->top = float((y0 + h) / scale * ratio);
        camera = frustum;
    }
    else {
        const double centerX = vCentroid.X() + rect.center().x() / scale;
        const double centerY = vCentroid.Y() + rect.center().y() / scale;
        const double camZ = zMax + depth;
        auto ortho = new SoOrthographicCamera;
        ortho->position =
            right * float(centerX) + up * float(centerY) + back * float(camZ);
        ortho->height = float(h / scale);
        ortho->aspectRatio = float(w / h);
        ortho->nearDistance = float(depth * 0.5);
        ortho->farDistance = float(camZ - zMin + depth);
        camera = ortho;
    }
    // Rows are the images of the camera axes under Coin's row-vector
    // convention: right, up, back(+N) -- the camera looks along -N.
    SbMatrix orient(right[0], right[1], right[2], 0.0F,
                    up[0], up[1], up[2], 0.0F,
                    back[0], back[1], back[2], 0.0F,
                    0.0F, 0.0F, 0.0F, 1.0F);
    camera->orientation = SbRotation(orient);
    // The raster maps the window to the full image exactly; the
    // viewport's rounded pixel aspect must not re-adjust the window.
    camera->viewportMapping = SoCamera::LEAVE_ALONE;

    auto light = new SoDirectionalLight;
    light->direction.setValue(-back);

    auto root = new SoSeparator;
    root->ref();
    root->addChild(camera);
    root->addChild(light);
    int fed = 0;
    if (derived) {
        // Per-face color fidelity: map each face of the cut/clipped
        // shape back to its source (piece colors, per-face DiffuseColor,
        // the cut surface color for faces born of the cut). When no
        // mapping is available the first source's color covers all.
        const SbColor uniform = shadeColor(dvp);
        DerivedColorResolver resolver;
        const bool perFace = buildDerivedResolver(dvp, shape, uniform, resolver);
        if (SoSeparator* node =
                buildMeshNode(shape, uniform, perFace ? &resolver : nullptr)) {
            root->addChild(node);
            ++fed;
        }
    }
    else {
        for (auto* obj : sourcesOf(dvp)) {
            auto* vp = Gui::Application::Instance->getViewProvider(obj);
            if (!vp || !vp->getRoot())
                continue;
            if (!vp->isShow()) {
                // A 3D-hidden source may have nothing behind its root
                // to draw: render-cache mode 3 evicts a hidden
                // object's caches, and with a backend attached the
                // switch override renders blank. Tessellate its shape
                // instead -- same frame (own placement), its own
                // ShapeColor.
                TopoDS_Shape hiddenShape = Part::Feature::getShape(obj);
                SbColor color(0.8F, 0.8F, 0.8F);
                if (auto* prop = dynamic_cast<App::PropertyColor*>(
                        vp->getPropertyByName("ShapeColor"))) {
                    const auto c = prop->getValue();
                    color = SbColor(c.r, c.g, c.b);
                }
                DerivedColorResolver resolver;
                const bool perFace = buildObjectResolver(obj, color, resolver);
                if (SoSeparator* node = buildMeshNode(hiddenShape, color,
                                                      perFace ? &resolver : nullptr)) {
                    root->addChild(node);
                    ++fed;
                }
                continue;
            }
            // The root carries the same frame the HLR shape resolves
            // to: an object's own placement (containers do not project
            // into a member's root, and Part::Feature::getShape does
            // not apply them either), a container's or Link's root its
            // own transform plus its children -- verified for nested,
            // container and Link sources by registration probes.
            root->addChild(vp->getRoot());
            ++fed;
        }
    }
    if (!fed) {
        root->unref();
        return false;
    }

    // Prefer the backend renderer's capture when one is running --
    // quality follows the 3D view. Derived shapes (cut solid, clipped
    // region) exist nowhere in the backend's scene feed, so they
    // always render the private Coin scene.
    if (!derived && captureViaBackend(dvp, camera, wPx, hPx, image)) {
        root->unref();
        return true;
    }

    Gui::SoQtOffscreenRenderer renderer{SbViewportRegion(short(wPx), short(hPx))};
    // White at alpha 0: the offscreen renderer keys the exact clear
    // color transparent, and the anti-aliased fringe blends toward
    // white -- invisible on paper.
    renderer.setBackgroundColor(SbColor4f(1.0F, 1.0F, 1.0F, 0.0F));
    // The renderer's default internal format is a float FBO
    // (GL_RGB32F_ARB), which comes back as dithered garbage on this
    // stack; a plain 8-bit RGBA target reads back exactly.
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
    renderer.setInternalTextureFormat(GL_RGBA8);
    // Sources hidden in the 3D view still have a drawing: override
    // their mode switches for this traversal only.
    SoFCSwitch::switchOverride(renderer.getGLRenderAction());
    const bool ok = renderer.render(root);
    root->unref();
    if (!ok)
        return false;

    renderer.writeToImage(image);
    if (image.isNull())
        return false;
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);
    return true;
}

bool ShadedUnderlay::update(DrawViewPart* dvp)
{
    if (!dvp || !dvp->Shaded.getValue())
        return false;

    QImage image;
    QRectF rect;
    if (!capture(dvp, image, rect))
        return false;

    const std::vector<double> rectValues{rect.x(), rect.y(), rect.width(), rect.height()};
    bool sameRect = dvp->UnderlayRect.getValues() == rectValues;

    const char* current = dvp->UnderlayImage.getValue();
    if (sameRect && current && current[0]) {
        QImage existing;
        if (existing.load(QString::fromUtf8(current), "PNG")
            && sameCapture(existing.convertToFormat(QImage::Format_ARGB32), image))
            return false;
    }

    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return false;
    }

    // PropertyFileIncluded keeps the base name of the file it is handed;
    // a stable name keeps the document's payload name stable across
    // captures.
    QString tempPath = QDir::temp().filePath(
        QStringLiteral("%1_ShadedUnderlay.png").arg(QString::fromUtf8(dvp->getNameInDocument())));
    {
        QFile tempFile(tempPath);
        if (!tempFile.open(QIODevice::WriteOnly) || tempFile.write(png) != png.size())
            return false;
    }
    dvp->UnderlayImage.setValue(tempPath.toUtf8().constData());
    QFile::remove(tempPath);
    if (!sameRect)
        dvp->UnderlayRect.setValues(rectValues);
    return true;
}

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

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <Inventor/SbRotation.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#endif// #ifndef _PreComp_

#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Inventor/SoFCSwitch.h>
#include <Gui/SoFCOffscreenRenderer.h>
#include <Gui/ViewProvider.h>
#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/DrawViewPart.h>

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

}// namespace

bool ShadedUnderlay::capture(DrawViewPart* dvp, QImage& image, QRectF& rect)
{
    if (!dvp || !Gui::Application::Instance)
        return false;
    // Perspective registration against perspective HLR is its own math
    // and its own test; the underlay stays off for it (doc sec 26.3).
    if (dvp->Perspective.getValue())
        return false;

    TopoDS_Shape shape = dvp->getSourceShape();
    if (shape.IsNull())
        return false;

    // The frame the projected geometry is computed in: the projection
    // CS rotated by -Rotation, which is how makeGeometryForShape's
    // rotating of the shape by +Rotation reads from the camera's side.
    gp_Ax2 viewCS = dvp->getRotatedCS();
    gp_Trsf toView;
    toView.SetTransformation(gp_Ax3(viewCS));

    // The exact centroid the HLR chain centered the shape on. Not
    // recomputed here: ShapeUtils::findCentroid reads triangulation-
    // dependent bounds, and a recompute after the 3D view tessellates
    // would drift the rect (measured 0.02mm) and re-capture an
    // unchanged projection.
    gp_Pnt gCentroid = DU::togp_Pnt(dvp->getOriginalCentroid());
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
    const double x0 = (xMin - vCentroid.X()) * scale - marginMm;
    const double y0 = (yMin - vCentroid.Y()) * scale - marginMm;
    const double w = (xMax - xMin) * scale + 2.0 * marginMm;
    const double h = (yMax - yMin) * scale + 2.0 * marginMm;
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

    const double centerX = vCentroid.X() + rect.center().x() / scale;
    const double centerY = vCentroid.Y() + rect.center().y() / scale;
    const double depth = std::max(zMax - zMin, 1.0);
    const double camZ = zMax + depth;

    auto camera = new SoOrthographicCamera;
    camera->position =
        right * float(centerX) + up * float(centerY) + back * float(camZ);
    // Rows are the images of the camera axes under Coin's row-vector
    // convention: right, up, back(+N) -- the camera looks along -N.
    SbMatrix orient(right[0], right[1], right[2], 0.0F,
                    up[0], up[1], up[2], 0.0F,
                    back[0], back[1], back[2], 0.0F,
                    0.0F, 0.0F, 0.0F, 1.0F);
    camera->orientation = SbRotation(orient);
    camera->height = float(h / scale);
    camera->aspectRatio = float(w / h);
    // The raster maps the window to the full image exactly; the
    // viewport's rounded pixel aspect must not re-adjust the window.
    camera->viewportMapping = SoCamera::LEAVE_ALONE;
    camera->nearDistance = float(depth * 0.5);
    camera->farDistance = float(camZ - zMin + depth);

    auto light = new SoDirectionalLight;
    light->direction.setValue(-back);

    auto root = new SoSeparator;
    root->ref();
    root->addChild(camera);
    root->addChild(light);
    int fed = 0;
    for (auto* obj : sourcesOf(dvp)) {
        auto* vp = Gui::Application::Instance->getViewProvider(obj);
        if (!vp || !vp->getRoot())
            continue;
        // The root is the object as the 3D view shows it, in world
        // coordinates for a top-level object. A source nested in a
        // transformed container is captured untransformed for now --
        // out of registration until container transforms are resolved.
        root->addChild(vp->getRoot());
        ++fed;
    }
    if (!fed) {
        root->unref();
        return false;
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

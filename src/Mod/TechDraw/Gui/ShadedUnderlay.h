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

#ifndef TECHDRAWGUI_SHADEDUNDERLAY_H
#define TECHDRAWGUI_SHADEDUNDERLAY_H

#include <QImage>
#include <QRectF>

#include <Mod/TechDraw/TechDrawGlobal.h>

namespace TechDraw
{
class DrawViewPart;
}

namespace TechDrawGui
{

/** The shaded-view hybrid's raster underlay
 * (docs/TechDrawPortAndSection.md sec 26).
 *
 * A shaded drawing view is the view's exact-HLR vector edges drawn
 * over a shaded raster of the same projection. The raster is captured
 * here: a private Coin scene -- the source objects' ViewProvider roots
 * under an orthographic camera built from the view's rotated
 * projection CS -- rendered offscreen, never grabbed from an
 * interactive viewport. Registration mirrors the HLR input chain
 * (center on centroid, scale, rotate) step for step, so the captured
 * rect and the projected edges land in the same 2D frame by
 * construction.
 */
class ShadedUnderlay
{
public:
    /** Capture the underlay for \a dvp.
     *
     * On success \a image is the shaded raster (transparent
     * background) and \a rect its registration rect in the view's 2D
     * coordinates: page mm, centroid origin, +X right, +Y up (the
     * pre-invertY frame the projected geometry is computed in).
     * Fails on perspective views, empty sources, or a failed
     * offscreen render.
     */
    static bool capture(TechDraw::DrawViewPart* dvp, QImage& image, QRectF& rect);

    /** Capture and write back into UnderlayImage/UnderlayRect.
     *
     * Returns true when the properties actually changed. The capture
     * is deterministic, so identical inputs re-encode to identical
     * PNG bytes and the write is skipped -- a repaint or a restore
     * never dirties the document.
     */
    static bool update(TechDraw::DrawViewPart* dvp);
};

}// namespace TechDrawGui

#endif// TECHDRAWGUI_SHADEDUNDERLAY_H

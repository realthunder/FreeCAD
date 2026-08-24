/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
 ***************************************************************************/

#ifndef TECHDRAWGUI_PAGEFEED_H
#define TECHDRAWGUI_PAGEFEED_H

/// The TechDraw feed of the 2D page engine
/// (docs/TechDrawPortAndSection.md sec 16, milestone M2): converts a
/// DrawPage's views -- DrawViewPart edges, vertices and faces -- into
/// Render::Page2D items. Coordinates follow the QGSPage scene
/// convention: Rez units (mm x 10), y down, each view placed at
/// (guiX(X), -guiX(Y)); DrawViewPart geometry arrives from the App side
/// already scaled, rotated and y-inverted.

#include <cstdint>

#include <Mod/TechDraw/TechDrawGlobal.h>

namespace TechDraw {
class DrawPage;
class DrawViewPart;
}

namespace Render {
class Page2D;
}

namespace TechDrawGui {

class QGIView;

class TechDrawGuiExport PageFeed
{
public:
    /// Fallback appearance. The feed reads the real appearance from the
    /// view provider and the TechDraw preferences (line widths, face
    /// color, per-edge cosmetic/GeomFormat formats, dash patterns);
    /// these values apply only where no view provider exists.
    struct Style
    {
        uint32_t edgeColor = 0x000000ff;   // visible (hard/outline) edges
        float edgeWidth = 6.0f;            // Rez units (0.6mm)
        uint32_t hiddenColor = 0x808080ff; // hidden edges when shown
        float hiddenWidth = 3.0f;
        uint32_t vertexColor = 0x000000ff;
        float vertexRadius = 3.0f;
        uint32_t faceColor = 0xf2f2f2ff;   // alpha 0 skips face fills
        /// Flattening tolerance for curves the feed discretizes (Rez
        /// units). Circles stay exact vg primitives; arcs, ellipses and
        /// anything else without a native op become polylines at this
        /// deflection for now.
        float deflection = 0.5f;
    };

    /// Feed every DrawViewPart of the page, and the template. Item ids
    /// derive from the view's document name, so re-feeding an edited
    /// view damages only its items. Views land on layers >= 1; layer 0
    /// is the template's.
    static void feedPage(TechDraw::DrawPage* page, Render::Page2D& out);
    static void feedPage(TechDraw::DrawPage* page, Render::Page2D& out,
                         const Style& style,
                         float templateRasterScale = 1.0f);

    /// The page's SVG template, rasterized (QSvgRenderer) into the
    /// page's image registry at rasterScale pixels per Rez unit --
    /// pass the current zoom band so the sheet stays sharp; capped at
    /// 4096 px, so deep zoom stops sharpening rather than exploding
    /// the texture. Re-feeding replaces the raster (an edit of the
    /// template's editable texts, or a band crossing); the item id is
    /// stable and the sheet always draws on layer 0, below every view.
    static void feedTemplate(TechDraw::DrawPage* page, Render::Page2D& out,
                             float rasterScale);

    /// Feed one view's edges/vertices/faces as items at the given layer.
    static void feedViewPart(TechDraw::DrawViewPart* dvp, Render::Page2D& out,
                             const Style& style, uint32_t layer);

    /// The annotation tier (dimensions, balloons, annotations, leaders,
    /// ...): capture the view's already-laid-out Qt scene item subtree
    /// as one Annotation item -- shape items become path ops, text items
    /// become fontstash text runs (rotated text rides a transform op).
    /// The Qt tier stays the single layout implementation; this converts
    /// its result. Nested QGIViews are skipped (they feed under their
    /// own ids). A hidden view records an empty item, clearing stale
    /// content.
    static void feedViewCapture(QGIView* qgiv, Render::Page2D& out,
                                uint32_t layer);

    /// The decorations a part view carries as Qt-side children with no
    /// App-side geometry (section lines, detail highlights, view center
    /// lines): captured like feedViewCapture but restricted to
    /// QGIDecoration children -- everything else of a part view is fed
    /// from App data by feedViewPart.
    static void feedViewDecorations(QGIView* qgiv, Render::Page2D& out,
                                    uint32_t layer);
};

} // namespace TechDrawGui

#endif // TECHDRAWGUI_PAGEFEED_H

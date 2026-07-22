/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
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

#ifndef GUI_SOTEXTIMAGE_H
#define GUI_SOTEXTIMAGE_H

#include <Inventor/SbVec2s.h>
#include <Inventor/fields/SoMFString.h>
#include <Inventor/fields/SoSFBool.h>
#include <Inventor/fields/SoSFEnum.h>
#include <Inventor/fields/SoSFFloat.h>
#include <Inventor/fields/SoSFImage.h>
#include <Inventor/fields/SoSFName.h>
#include <Inventor/nodes/SoShape.h>
#include <FCGlobal.h>

class SoSeparator;
class SoTexture2;
class SoText2;
class SoFont;

namespace Gui {

class SoAutoZoomTranslation;

/**
 * Reusable companion that ports screen-space text (SoText2 and its FreeCAD
 * subclasses) into the render-cache backend (bgfx / WASM), where the classic
 * raw-GL text pass is bypassed and would otherwise leave the label invisible.
 *
 * It rasterizes its multi-line string with Qt into a *white* glyph atlas
 * (rgb=1, alpha = coverage) and emits it as a textured quad during render-cache
 * capture (SoCallbackAction). The companion sub-graph pairs the quad with a
 * SoTexture2 in MODULATE mode, so the current material colour tints the glyph —
 * text follows selection/pre-selection highlighting exactly like the GL path,
 * without baking a colour. An SoAutoZoomTranslation keeps the quad a constant
 * screen size (emitted in native glyph pixels), matching SoText2's pixel size
 * across zoom. The node is inert on the GL and ray-pick paths: on the desktop
 * the owning SoText2 still draws the text itself.
 *
 * Use createSubGraph() to get a ready-to-insert separator, then drive the
 * returned node's fields (string / fontName / fontSize / justification) from the
 * owner. Insert it where the owner's material is in scope so MODULATE picks up
 * the right colour.
 */
class GuiExport SoTextImage : public SoShape {
    using inherited = SoShape;

    SO_NODE_HEADER(SoTextImage);

public:
    // Mirrors SoText2::Justification values (LEFT=1, RIGHT=2, CENTER=3).
    enum Justification {
        LEFT = 1,
        RIGHT,
        CENTER
    };

    static void initClass();
    SoTextImage();

    SoMFString string;
    SoSFName   fontName;
    SoSFFloat  fontSize;      // pixel size (matches SoText2's on-screen height)
    SoSFEnum   justification; // SoText2::LEFT / RIGHT / CENTER
    SoSFFloat  spacing;       // inter-line spacing multiplier (SoText2::spacing)
    // When TRUE the glyph block is centred vertically on the origin instead of
    // growing upward from it (SoText2's default). Used by the overlay axis
    // labels so a single letter sits ON its axis endpoint rather than above it
    // (which clipped the top-most label out of the tight corner overlay).
    SoSFBool   vcenter;
    // Read by SoFCVertexCache (by field name) so the explicit UVs are captured
    // even though no texture unit is enabled on the capture traversal.
    SoSFBool   forceTexCoords;

    /**
     * Build a self-contained sub-graph the caller can drop into the scene:
     *   SoSeparator [ SoTexture2(MODULATE) -> SoAutoZoomTranslation -> SoTextImage ]
     * The returned separator is *not* referenced; add it as a child of a node
     * you own. @a outImage receives the SoTextImage whose fields you drive.
     */
    static SoSeparator* createSubGraph(SoTextImage** outImage);

    /**
     * Build a companion for an existing SoText2 (or subclass) that tracks its
     * text live via field connections (string / spacing). Insert the returned
     * separator as a sibling right AFTER @a label so it shares the label's
     * coordinate space and material scope. @a fontSize is the on-screen pixel
     * size the label renders at (from the governing SoFont / SoFontSizeElement);
     * @a fontName defaults to Helvetica when null. This is the one-liner most
     * call sites want to port an in-scene SoText2 to the render-cache backend.
     */
    static SoSeparator* createFor(SoText2* label, float fontSize,
                                  const char* fontName = nullptr);

    /**
     * Like createFor(), but also tracks a live SoFont: the companion's pixel
     * size and font name are connected to @a font->size / @a font->name, so a
     * font/size change on the owning ViewProvider re-rasterizes automatically.
     */
    static SoSeparator* createFor(SoText2* label, SoFont* font);

protected:
    ~SoTextImage() override;
    void generatePrimitives(SoAction* action) override;
    void computeBBox(SoAction* action, SbBox3f& box, SbVec3f& center) override;
    void notify(SoNotList* list) override;
    // Inert on the classic GL path (the owning SoText2 draws the text there).
    void GLRender(SoGLRenderAction*) override {}

private:
    void updateImage();
    void syncAutoZoom(SoAction* action);
    void emitQuad(SoAction* action);

    // Set by createSubGraph so the node can feed its texture and calibrate the
    // screen-constant scale factor.
    SoTexture2*           imageTexture = nullptr;
    SoAutoZoomTranslation* imageZoom = nullptr;

    SoSFImage image;      // rasterized bottom-up RGBA glyph block
    SbVec2s   imgSize {0, 0};
    float     offX = 0.f; // block bottom-left offset from the origin, in pixels
    float     offY = 0.f;
    bool      imageDirty = true;
};

} // namespace Gui

#endif // GUI_SOTEXTIMAGE_H

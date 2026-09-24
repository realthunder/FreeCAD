/***************************************************************************
 *   Copyright (c) 2011-2012 Luke Parry <l.parry@warwick.ac.uk>            *
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

#ifndef GUI_SODATUMLABEL_H
#define GUI_SODATUMLABEL_H

#include <Inventor/SbBox3f.h>
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/fields/SoSFEnum.h>
#include <Inventor/fields/SoSFFloat.h>
#include <Inventor/fields/SoSFImage.h>
#include <Inventor/fields/SoSFInt32.h>
#include <Inventor/fields/SoSFName.h>
#include <Inventor/fields/SoSFVec3f.h>
#include <Inventor/fields/SoMFString.h>
#include <Inventor/fields/SoMFVec3f.h>
#include <Inventor/nodes/SoShape.h>

#include <FCGlobal.h>

class SoTexture2;
class SoSeparator;
class SoLightModel;
class SoMaterial;
class SoDrawStyle;

namespace Gui {

class SoDatumLabelImage;
class SoDatumLabelLeader;
class SoDatumLabelAnchor;
class SoAutoZoomTranslation;

class GuiExport SoDatumLabel : public SoShape {
    using inherited = SoShape;

    SO_NODE_HEADER(SoDatumLabel);

    friend class SoDatumLabelImage;
    friend class SoDatumLabelLeader;
    friend class SoDatumLabelAnchor;

public:
    enum Type
    {
        ANGLE,
        DISTANCE,
        DISTANCEX,
        DISTANCEY,
        RADIUS,
        DIAMETER,
        SYMMETRIC
    };

    static void initClass();
    SoDatumLabel();

    /*The points have to be on XY plane, ie they need to be 2D points.
    To draw on other planes, you need to attach a SoTransform to the SoDatumLabel (or parent).*/
    void setPoints(SbVec3f p1, SbVec3f p2);

    /* returns the center point of the text of the label */
    SbVec3f getLabelTextCenter();

    /* When true, GLRender() draws nothing. Set by the viewer around the Coin GL
     * pass when an external backend (render-cache mode 3) is already rendering
     * the editing overlay these labels live in, so the datum is not drawn twice
     * (raw GL + backend). Left false everywhere else so the classic GL path is
     * unchanged. */
    static bool SuppressGLRender;

    /* Returns the companion sub-graph that renders this datum for the
     * render-cache bridge (bgfx/WASM backend), where the raw-GL GLRender pass
     * is bypassed: the leader lines, arrows and arcs, drawn unlit in textColor
     * at lineWidth as GLRender draws them, then the text glyph as a textured
     * quad. Add it as a sibling right after this label in the scene graph; it
     * is inert on the classic GL path (this node's GLRender draws everything
     * there). Without it the capture sees no datum at all. Built lazily,
     * owned here. */
    SoNode* getImageNode();

    SoMFString string;
    SoSFColor  textColor;
    SoSFEnum   datumtype;
    SoSFName   name;
    SoSFInt32  size;
    SoSFFloat  param1;
    SoSFFloat  param2;
    SoSFFloat  param3;
    SoSFFloat  param4;
    SoSFFloat  param5;
    SoMFVec3f  pnts;
    SoSFVec3f  norm;
    SoSFImage  image;
    SoSFFloat  lineWidth;
    bool       useAntialiasing;

protected:
    ~SoDatumLabel() override;
    void GLRender(SoGLRenderAction *action) override;
    void computeBBox(SoAction *, SbBox3f &box, SbVec3f &center) override;
    void generatePrimitives(SoAction * action) override;
    void notify(SoNotList * l) override;

private:
    float getScaleFactor(SoState*) const;
    void generateDistancePrimitives(SoAction * action, const SbVec3f&, const SbVec3f&);
    void generateDiameterPrimitives(SoAction * action, const SbVec3f&, const SbVec3f&);
    void generateAnglePrimitives(SoAction * action, const SbVec3f&);
    void generateSymmetricPrimitives(SoAction * action, const SbVec3f&, const SbVec3f&);
    // Emit the leader lines / arrows / arcs as cache-visible primitives so the
    // render-cache bridge (and the bgfx/WASM backend) can draw the datum
    // without the raw-GL GLRender pass. Called only during SoCallbackAction
    // traversal (render-cache capture), never during ray picking.
    void generateLeaderPrimitives(SoAction * action);
    bool updateImageSize(SoState * state, int & srcw, int & srch);
    SbVec3f getLabelTextCenterDistance(const SbVec3f&, const SbVec3f&);
    SbVec3f getLabelTextCenterDiameter(const SbVec3f&, const SbVec3f&);
    SbVec3f getLabelTextCenterAngle(const SbVec3f&);

    // Emit the text glyph as a textured quad (2 triangles + UVs) so the
    // render-cache bridge captures the datum number; called by the companion
    // SoDatumLabelImage during render-cache capture. Uses textOffset/textAngle
    // and imgWidth/imgHeight computed by the leader pass.
    void generateTextQuad(SoAction * action);

private:
    void drawImage();
    // Keep the companion texture's image in sync with this->image (called after
    // drawImage() regenerates the glyph bitmap).
    void syncImageTexture();
    float imgWidth;
    float imgHeight;
    bool glimagevalid;
    // Whether imageTexture mirrors the current glyph bitmap. Distinct from
    // glimagevalid because GLRender validates the bitmap without touching the
    // companion texture; reset together with glimagevalid on content change.
    bool imagesynced;

    // Text placement in local (sketch-plane) coordinates, recomputed by the
    // leader pass (generateLeaderPrimitives) for the text-quad companion.
    SbVec3f textOffset;
    float textAngle;

    // Lazily built companion sub-graph: the leaders under their own style
    // (SoSeparator[light model, material, draw style, leader, ...]), then the
    // text glyph quad, screen constant via the autozoom node ([..., texture,
    // anchor, zoom, quad]).
    SoSeparator* imageRoot;
    SoTexture2* imageTexture;
    Gui::SoDatumLabelAnchor* imageAnchor;
    Gui::SoAutoZoomTranslation* imageZoom;
    Gui::SoDatumLabelImage* imageShape;
    Gui::SoDatumLabelLeader* leaderShape;
};

}


#endif // GUI_SODATUMLABEL_H

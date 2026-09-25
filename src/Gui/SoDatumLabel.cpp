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

#include "PreCompiled.h"
#ifndef _PreComp_
# ifdef FC_OS_WIN32
# include <windows.h>
# undef min
# undef max
# endif
# ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
# else
# include <GL/gl.h>
# endif

# include <algorithm>
# include <cfloat>
# include <cmath>
# include <QFontMetrics>
# include <QPainter>

# include <Inventor/SoPrimitiveVertex.h>
# include <Inventor/actions/SoCallbackAction.h>
# include <Inventor/actions/SoGLRenderAction.h>
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/actions/SoGetMatrixAction.h>
# include <Inventor/actions/SoPickAction.h>
# include <Inventor/elements/SoModelMatrixElement.h>
# include <Inventor/elements/SoViewingMatrixElement.h>
# include <Inventor/elements/SoViewportRegionElement.h>
# include <Inventor/elements/SoViewVolumeElement.h>
# include <Inventor/fields/SoSFBool.h>
# include <Inventor/misc/SoState.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoLightModel.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoTexture2.h>
# include <Inventor/nodes/SoTransformation.h>
#endif // _PreComp_

#include <Gui/BitmapFactory.h>
#include <Gui/Tools.h>

#include "SoDatumLabel.h"
#include "Inventor/SoAutoZoomTranslation.h"


#define ZCONSTR 0.006f

using namespace Gui;

namespace {
// Which way the glyph's own y axis points relative to the outward normal at
// the middle of an ARCLENGTH dimension arc: +1 when textShift moves the
// number away from the centre.
float arcLengthShiftSign(float textAngle, float midAngle)
{
    const SbVec3f up(-sin(textAngle), cos(textAngle), 0.f);
    const SbVec3f out(cos(midAngle), sin(midAngle), 0.f);
    return up.dot(out) >= 0.f ? 1.f : -1.f;
}
}

// ------------------------------------------------------

// ------------------------------------------------------
// Companion node that renders only the datum text glyph as a textured quad.
// SoDatumLabel keeps its leader lines/arrows in its own vertex cache (untextured
// datum colour); this sibling gets its own render-cache scope so the glyph
// texture applies to the quad alone. It is inert on the classic GL path — the
// owner's GLRender still draws the glyph there — and only emits geometry during
// render-cache capture (SoCallbackAction). The forceTexCoords field makes the
// vertex cache keep the explicit UVs even though no texture element is active on
// the capture traversal.
// ------------------------------------------------------

namespace Gui {
// Places the text quad at the datum's textOffset/textAngle (both in local
// sketch-plane coordinates, recomputed by the leader pass). Kept as a live
// transform that reads the owner rather than baked fields so it needs no
// notify plumbing. Paired with an SoAutoZoomTranslation that follows it, so
// the quad renders screen-constant: postAutoZoom resets the model matrix to
// identity and the backend replays this placement + a per-frame scale each
// frame (see setDrawTransform), which is why the quad is emitted at the origin.
class SoDatumLabelAnchor : public SoTransformation {
    using inherited = SoTransformation;
    SO_NODE_HEADER(SoDatumLabelAnchor);

public:
    static void initClass();
    SoDatumLabelAnchor();
    SoDatumLabel* owner = nullptr;

protected:
    ~SoDatumLabelAnchor() override = default;
    void doAction(SoAction* action) override
    {
        if (!this->owner)
            return;
        SoState* state = action->getState();
        SoModelMatrixElement::translateBy(state, this, this->owner->textOffset);
        SoModelMatrixElement::rotateBy(
            state, this, SbRotation(SbVec3f(0.f, 0.f, 1.f), this->owner->textAngle));
    }
    void callback(SoCallbackAction* action) override { doAction(action); }
    void GLRender(SoGLRenderAction* action) override { doAction(action); }
    void getBoundingBox(SoGetBoundingBoxAction* action) override { doAction(action); }
    void pick(SoPickAction* action) override { doAction(action); }
    void getMatrix(SoGetMatrixAction* action) override;
};

class SoDatumLabelImage : public SoShape {
    using inherited = SoShape;
    SO_NODE_HEADER(SoDatumLabelImage);

public:
    static void initClass();
    SoDatumLabelImage();

    SoSFBool forceTexCoords;
    SoDatumLabel* owner = nullptr;

protected:
    ~SoDatumLabelImage() override = default;
    // The glyph is drawn by SoDatumLabel::GLRender on the classic GL path.
    void GLRender(SoGLRenderAction*) override {}
    void computeBBox(SoAction*, SbBox3f& box, SbVec3f& center) override;
    void generatePrimitives(SoAction* action) override;
};

// Companion node that carries the datum's leader lines, arrows and arcs into
// the render-cache capture. They cannot come from SoDatumLabel itself: the
// capture snapshots a shape's material before the shape runs, and the style
// GLRender draws them in -- unlit, textColor, lineWidth -- has to be in the
// traversal state by then. Placing a style node in front of the label is not
// an option either; the Sketcher finds each label as child 0 of its
// constraint node. So the leaders move here, behind their own style nodes in
// the companion sub-graph, and the label emits nothing to the capture.
class SoDatumLabelLeader : public SoShape {
    using inherited = SoShape;
    SO_NODE_HEADER(SoDatumLabelLeader);

public:
    static void initClass();
    SoDatumLabelLeader();

    /// Read by SoFCVertexCache: the leader vertices carry screen-space
    /// offsets in their texture coordinates (Render::MeshData::
    /// screenOffsets), since GL sizes arrowheads and gaps in pixels.
    SoSFBool screenOffsets;
    SoDatumLabel* owner = nullptr;

protected:
    ~SoDatumLabelLeader() override = default;
    // GLRender of the owner draws the leaders on the classic GL path.
    void GLRender(SoGLRenderAction*) override {}
    void computeBBox(SoAction* action, SbBox3f& box, SbVec3f& center) override
    {
        if (this->owner)
            this->owner->computeBBox(action, box, center);
    }
    void generatePrimitives(SoAction* action) override
    {
        if (this->owner && action->isOfType(SoCallbackAction::getClassTypeId()))
            this->owner->generateLeaderPrimitives(action);
    }
};
}  // namespace Gui

SO_NODE_SOURCE(SoDatumLabelAnchor)

void SoDatumLabelAnchor::initClass()
{
    SO_NODE_INIT_CLASS(SoDatumLabelAnchor, SoTransformation, "Transformation");
}

SoDatumLabelAnchor::SoDatumLabelAnchor()
{
    SO_NODE_CONSTRUCTOR(SoDatumLabelAnchor);
}

void SoDatumLabelAnchor::getMatrix(SoGetMatrixAction* action)
{
    if (!this->owner)
        return;
    SbMatrix t;
    t.setTranslate(this->owner->textOffset);
    SbMatrix m;
    m.setRotate(SbRotation(SbVec3f(0.f, 0.f, 1.f), this->owner->textAngle));
    m.multRight(t);
    action->getMatrix().multLeft(m);
    action->getInverse().multRight(m.inverse());
}

SO_NODE_SOURCE(SoDatumLabelLeader)

void SoDatumLabelLeader::initClass()
{
    SO_NODE_INIT_CLASS(SoDatumLabelLeader, SoShape, "Shape");
}

SoDatumLabelLeader::SoDatumLabelLeader()
{
    SO_NODE_CONSTRUCTOR(SoDatumLabelLeader);
    SO_NODE_ADD_FIELD(screenOffsets, (TRUE));
}

SO_NODE_SOURCE(SoDatumLabelImage)

void SoDatumLabelImage::initClass()
{
    SO_NODE_INIT_CLASS(SoDatumLabelImage, SoShape, "Shape");
}

SoDatumLabelImage::SoDatumLabelImage()
{
    SO_NODE_CONSTRUCTOR(SoDatumLabelImage);
    // Read by SoFCVertexCache to force unit-0 UV capture from our primitives.
    SO_NODE_ADD_FIELD(forceTexCoords, (TRUE));
}

void SoDatumLabelImage::computeBBox(SoAction*, SbBox3f& box, SbVec3f& center)
{
    // The quad is emitted at the origin (native pixel units) and placed by the
    // anchor + screen-constant autozoom; contribute a tiny box at the origin so
    // it neither dominates fitAll nor leaves an invalid bbox. The datum's real
    // extent is already covered by the leader shape (SoDatumLabel).
    box.setBounds(SbVec3f(0.f, 0.f, 0.f), SbVec3f(0.f, 0.f, 0.f));
    center = SbVec3f(0.f, 0.f, 0.f);
}

void SoDatumLabelImage::generatePrimitives(SoAction* action)
{
    // Only feed the render-cache capture (SoCallbackAction); stay invisible to
    // ray picking (the owner's text box handles selection) and every other
    // action.
    if (this->owner && action->isOfType(SoCallbackAction::getClassTypeId()))
        this->owner->generateTextQuad(action);
}

// ------------------------------------------------------

SO_NODE_SOURCE(SoDatumLabel)

bool SoDatumLabel::SuppressGLRender = false;

void SoDatumLabel::initClass()
{
    SO_NODE_INIT_CLASS(SoDatumLabel, SoShape, "Shape");
    SoDatumLabelAnchor::initClass();
    SoDatumLabelImage::initClass();
    SoDatumLabelLeader::initClass();
}


SoDatumLabel::SoDatumLabel()
{
    SO_NODE_CONSTRUCTOR(SoDatumLabel);
    SO_NODE_ADD_FIELD(string, (""));
    SO_NODE_ADD_FIELD(textColor, (SbVec3f(1.0f,1.0f,1.0f)));
    SO_NODE_ADD_FIELD(pnts, (SbVec3f(.0f,.0f,.0f)));
    SO_NODE_ADD_FIELD(norm, (SbVec3f(.0f,.0f,1.f)));

    SO_NODE_ADD_FIELD(name, ("Helvetica"));
    SO_NODE_ADD_FIELD(size, (10.f));
    SO_NODE_ADD_FIELD(lineWidth, (2.f));

    SO_NODE_ADD_FIELD(datumtype, (SoDatumLabel::DISTANCE));

    SO_NODE_DEFINE_ENUM_VALUE(Type, DISTANCE);
    SO_NODE_DEFINE_ENUM_VALUE(Type, DISTANCEX);
    SO_NODE_DEFINE_ENUM_VALUE(Type, DISTANCEY);
    SO_NODE_DEFINE_ENUM_VALUE(Type, ANGLE);
    SO_NODE_DEFINE_ENUM_VALUE(Type, RADIUS);
    SO_NODE_DEFINE_ENUM_VALUE(Type, DIAMETER);
    SO_NODE_DEFINE_ENUM_VALUE(Type, ARCLENGTH);
    SO_NODE_SET_SF_ENUM_TYPE(datumtype, Type);

    SO_NODE_ADD_FIELD(param1, (0.f));
    SO_NODE_ADD_FIELD(param2, (0.f));
    // An angle's range; unregistered it notified nobody when it changed.
    SO_NODE_ADD_FIELD(param3, (0.f));
    SO_NODE_ADD_FIELD(param4, (0.f));
    SO_NODE_ADD_FIELD(param5, (0.f));

    useAntialiasing = true;

    this->imgWidth = 0;
    this->imgHeight = 0;
    this->glimagevalid = false;
    this->imagesynced = false;

    this->textOffset = SbVec3f(0.f, 0.f, 0.f);
    this->textAngle = 0.f;
    this->imageRoot = nullptr;
    this->imageTexture = nullptr;
    this->imageAnchor = nullptr;
    this->imageZoom = nullptr;
    this->imageShape = nullptr;
    this->leaderShape = nullptr;
}

SoDatumLabel::~SoDatumLabel()
{
    if (this->imageRoot)
        this->imageRoot->unref();
}

SoNode* SoDatumLabel::getImageNode()
{
    if (!this->imageRoot) {
        this->imageTexture = new SoTexture2;
        // The glyph bitmap already bakes in the text colour, so replace the
        // fragment colour with the texel (alpha-blended); no material tint.
        this->imageTexture->model = SoTexture2::REPLACE;

        // Anchor places the quad at textOffset/textAngle; the autozoom that
        // follows makes it screen-constant (native glyph pixels), matching the
        // GL path and avoiding the minification that darkened the baked glyph.
        this->imageAnchor = new SoDatumLabelAnchor;
        this->imageAnchor->owner = this;
        this->imageZoom = new SoAutoZoomTranslation;
        // Per-frame auto-flip so the number reads upright from any viewpoint,
        // matching GLRender's projected-axis + backfacing test. flipNormal
        // tracks the datum's (world-space) plane normal.
        this->imageZoom->datumFlip = TRUE;
        this->imageZoom->flipNormal.connectFrom(&this->norm);
        // One screen pixel per glyph pixel, resolved by the backend against
        // the view that draws it -- which is not always the one this was
        // captured in (a browser, a served capture). The scaleFactor below
        // stays for a view with a camera of its own.
        this->imageZoom->pixelScale = 1.0f;

        this->imageShape = new SoDatumLabelImage;
        this->imageShape->owner = this;

        // The leaders' style, as GLRender sets it by hand: no lighting, the
        // label's colour, its line width. Connected rather than copied, so a
        // selection or preselection recolour -- which writes textColor only
        // -- reaches the capture too.
        auto lightModel = new SoLightModel;
        lightModel->model = SoLightModel::BASE_COLOR;
        auto material = new SoMaterial;
        material->diffuseColor.connectFrom(&this->textColor);
        auto drawStyle = new SoDrawStyle;
        drawStyle->lineWidth.connectFrom(&this->lineWidth);
        this->leaderShape = new SoDatumLabelLeader;
        this->leaderShape->owner = this;

        this->imageRoot = new SoSeparator;
        // Hold our own reference so the sub-graph outlives its scene parent
        // (the companion reads back into this label during capture).
        this->imageRoot->ref();
        this->imageRoot->renderCaching = SoSeparator::OFF;
        this->imageRoot->boundingBoxCaching = SoSeparator::OFF;
        // Leaders first: they compute textOffset/textAngle, which the glyph
        // reads, and are captured before the texture is in the state.
        this->imageRoot->addChild(lightModel);
        this->imageRoot->addChild(material);
        this->imageRoot->addChild(drawStyle);
        this->imageRoot->addChild(this->leaderShape);
        this->imageRoot->addChild(this->imageTexture);
        this->imageRoot->addChild(this->imageAnchor);
        this->imageRoot->addChild(this->imageZoom);
        this->imageRoot->addChild(this->imageShape);

        syncImageTexture();
    }
    return this->imageRoot;
}

void SoDatumLabel::syncImageTexture()
{
    if (!this->imageTexture)
        return;
    SbVec2s size;
    int nc;
    const unsigned char* bytes = this->image.getValue(size, nc);
    if (bytes && size[0] > 0 && size[1] > 0)
        this->imageTexture->image.setValue(size, nc, bytes);
    else
        this->imageTexture->image.setValue(SbVec2s(0, 0), 0, nullptr);
}

void SoDatumLabel::drawImage()
{
    const SbString* s = string.getValues(0);
    int num = string.getNum();
    if (num == 0) {
        this->image = SoSFImage();
        return;
    }

    QFont font(QString::fromUtf8(name.getValue().getString()), size.getValue());
    QFontMetrics fm(font);
    QString str = QString::fromUtf8(s[0].getString());

    int w = Gui::QtTools::horizontalAdvance(fm, str);
    int h = fm.height();

    // No Valid text
    if (!w) {
        this->image = SoSFImage();
        return;
    }

    const SbColor& t = textColor.getValue();
    QColor front;
    front.setRgbF(t[0],t[1], t[2]);

    QImage image(w, h,QImage::Format_ARGB32_Premultiplied);
    image.fill(0x00000000);

    QPainter painter(&image);
    if(useAntialiasing)
        painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(front);
    painter.setFont(font);
    painter.drawText(0, 0, w, h, Qt::AlignLeft, str);
    painter.end();

    Gui::BitmapFactory().convert(image, this->image);
}

namespace {
// Helper class to determine the bounding box of a datum label
class DatumLabelBox
{
public:
    DatumLabelBox(float scale, SoDatumLabel* label)
        : scale{scale}
        , label{label}
    {

    }
    void computeBBox(SbBox3f& box, SbVec3f& center) const
    {
        std::vector<SbVec3f> corners;
        if (label->datumtype.getValue() == SoDatumLabel::DISTANCE ||
            label->datumtype.getValue() == SoDatumLabel::DISTANCEX ||
            label->datumtype.getValue() == SoDatumLabel::DISTANCEY ) {
            corners = computeDistanceBBox();
        }
        else if (label->datumtype.getValue() == SoDatumLabel::RADIUS ||
                 label->datumtype.getValue() == SoDatumLabel::DIAMETER) {
            corners = computeRadiusDiameterBBox();
        }
        else if (label->datumtype.getValue() == SoDatumLabel::ANGLE) {
            corners = computeAngleBBox();
        }
        else if (label->datumtype.getValue() == SoDatumLabel::SYMMETRIC) {
            corners = computeSymmetricBBox();
        }
        else if (label->datumtype.getValue() == SoDatumLabel::ARCLENGTH) {
            corners = computeArcLengthBBox();
        }

        getBBox(corners, box, center);
    }

private:
    void getBBox(const std::vector<SbVec3f>& corners, SbBox3f& box, SbVec3f& center) const
    {
        if (corners.size() > 1) {
            float minX = FLT_MAX;
            float minY = FLT_MAX;
            float maxX = -FLT_MAX;
            float maxY = -FLT_MAX;
            for (SbVec3f it : corners) {
                minX = (it[0] < minX) ? it[0] : minX;
                minY = (it[1] < minY) ? it[1] : minY;
                maxX = (it[0] > maxX) ? it[0] : maxX;
                maxY = (it[1] > maxY) ? it[1] : maxY;
            }

            // Store the bounding box
            box.setBounds(SbVec3f(minX, minY, 0.0F), SbVec3f (maxX, maxY, 0.0F));
            center = box.getCenter();
        }
    }
    std::vector<SbVec3f> computeDistanceBBox() const
    {
        SbVec2s imgsize;
        int nc;
        int srcw = 1;
        int srch = 1;

        const unsigned char * dataptr = label->image.getValue(imgsize, nc);
        if (dataptr) {
            srcw = imgsize[0];
            srch = imgsize[1];
        }

        float aspectRatio =  (float) srcw / (float) srch;
        float imgHeight = scale * (float) (srch);
        float imgWidth  = aspectRatio * imgHeight;

        // Get the points stored in the pnt field
        const SbVec3f *points = label->pnts.getValues(0);
        if (label->pnts.getNum() < 2) {
            return {};
        }

        SbVec3f textOffset;

        float length = label->param1.getValue();
        float length2 = label->param2.getValue();

        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir;
        SbVec3f normal;
        if (label->datumtype.getValue() == SoDatumLabel::DISTANCE) {
            dir = (p2-p1);
        }
        else if (label->datumtype.getValue() == SoDatumLabel::DISTANCEX) {
            dir = SbVec3f( (p2[0] - p1[0] >= FLT_EPSILON) ? 1 : -1, 0, 0);
        }
        else if (label->datumtype.getValue() == SoDatumLabel::DISTANCEY) {
            dir = SbVec3f(0, (p2[1] - p1[1] >= FLT_EPSILON) ? 1 : -1, 0);
        }

        dir.normalize();
        normal = SbVec3f (-dir[1], dir[0], 0);

        // when the datum line is not parallel to p1-p2 the projection of
        // p1-p2 on normal is not zero, p2 is considered as reference and p1
        // is replaced by its projection p1_
        float normproj12 = (p2 - p1).dot(normal);
        SbVec3f p1_ = p1 + normproj12 * normal;

        SbVec3f midpos = (p1_ + p2)/2;

        float offset1 = ((length + normproj12 < 0.0F) ? -1.0F  : 1.0F) * float(srch);
        float offset2 = ((length < 0.0F) ? -1.0F  : 1.0F) * float(srch);

        textOffset = midpos + normal * length + dir * length2;
        float margin = imgHeight / 4.0F;

        SbVec3f perp1 = p1_ + normal * (length + offset1 * scale);
        SbVec3f perp2 = p2  + normal * (length + offset2 * scale);

        // Finds the mins and maxes
        std::vector<SbVec3f> corners;
        corners.push_back(p1);
        corners.push_back(p2);
        corners.push_back(perp1);
        corners.push_back(perp2);

        // Make sure that the label is inside the bounding box
        corners.push_back(textOffset + dir * (imgWidth / 2.0F + margin) + normal * (srch + margin));
        corners.push_back(textOffset - dir * (imgWidth / 2.0F + margin) + normal * (srch + margin));
        corners.push_back(textOffset + dir * (imgWidth / 2.0F + margin) - normal * margin);
        corners.push_back(textOffset - dir * (imgWidth / 2.0F + margin) - normal * margin);

        return corners;
    }

    std::vector<SbVec3f> computeRadiusDiameterBBox() const
    {
        SbVec2s imgsize;
        int nc;
        int srcw = 1;
        int srch = 1;

        const unsigned char * dataptr = label->image.getValue(imgsize, nc);
        if (dataptr) {
            srcw = imgsize[0];
            srch = imgsize[1];
        }

        float aspectRatio =  (float) srcw / (float) srch;
        float imgHeight = scale * (float) (srch);
        float imgWidth  = aspectRatio * imgHeight;

        // Get the points stored in the pnt field
        const SbVec3f *points = label->pnts.getValues(0);
        if (label->pnts.getNum() < 2) {
            return {};
        }

        // Get the Points
        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir = p2 - p1;
        dir.normalize();
        SbVec3f normal (-dir[1], dir[0], 0);

        float length = label->param1.getValue();
        SbVec3f pos = p2 + length*dir;

        float margin = imgHeight / 4.0F;

        SbVec3f p3 = pos +  dir * (imgWidth / 2.0F + margin);
        if ((p3-p1).length() > (p2-p1).length()) {
            p2 = p3;
        }

        // Calculate the points
        SbVec3f pnt1 = pos - dir * (margin + imgWidth / 2.0F);
        SbVec3f pnt2 = pos + dir * (margin + imgWidth / 2.0F);

        // Finds the mins and maxes
        std::vector<SbVec3f> corners;
        corners.push_back(p1);
        corners.push_back(p2);
        corners.push_back(pnt1);
        corners.push_back(pnt2);

        return corners;
    }

    std::vector<SbVec3f> computeAngleBBox() const
    {
        SbVec2s imgsize;
        int nc;
        int srcw = 1;
        int srch = 1;

        const unsigned char * dataptr = label->image.getValue(imgsize, nc);
        if (dataptr) {
            srcw = imgsize[0];
            srch = imgsize[1];
        }

        float aspectRatio =  (float) srcw / (float) srch;
        float imgHeight = scale * (float) (srch);
        float imgWidth  = aspectRatio * imgHeight;

        // Get the points stored in the pnt field
        const SbVec3f *points = label->pnts.getValues(0);
        if (label->pnts.getNum() < 1) {
            return {};
        }

        // Only the angle intersection point is needed
        SbVec3f p0 = points[0];

        // Load the Parameters
        float length     = label->param1.getValue();
        float startangle = label->param2.getValue();
        float range      = label->param3.getValue();
        float endangle   = startangle + range;


        float len2 = 2.0F * length;

        // Useful Information
        // v0 - vector for text position
        // p0 - vector for angle intersect
        SbVec3f v0(cos(startangle+range/2), sin(startangle+range/2), 0);

        SbVec3f textOffset = p0 + v0 * len2;

        float margin = imgHeight / 4.0F;

        // Direction vectors for start and end lines
        SbVec3f v1(cos(startangle), sin(startangle), 0);
        SbVec3f v2(cos(endangle), sin(endangle), 0);

        SbVec3f pnt1 = p0+(len2-margin)*v1;
        SbVec3f pnt2 = p0+(len2+margin)*v1;
        SbVec3f pnt3 = p0+(len2-margin)*v2;
        SbVec3f pnt4 = p0+(len2+margin)*v2;

        // Finds the mins and maxes
        // We may need to include the text position too

        SbVec3f img1 = SbVec3f(-imgWidth / 2.0F, -imgHeight / 2, 0.0F);
        SbVec3f img2 = SbVec3f(-imgWidth / 2.0F,  imgHeight / 2, 0.0F);
        SbVec3f img3 = SbVec3f( imgWidth / 2.0F, -imgHeight / 2, 0.0F);
        SbVec3f img4 = SbVec3f( imgWidth / 2.0F,  imgHeight / 2, 0.0F);

        img1 += textOffset;
        img2 += textOffset;
        img3 += textOffset;
        img4 += textOffset;

        std::vector<SbVec3f> corners;
        corners.push_back(pnt1);
        corners.push_back(pnt2);
        corners.push_back(pnt3);
        corners.push_back(pnt4);
        corners.push_back(img1);
        corners.push_back(img2);
        corners.push_back(img3);
        corners.push_back(img4);

        return corners;
    }

    std::vector<SbVec3f> computeSymmetricBBox() const
    {
        // Get the points stored in the pnt field
        const SbVec3f *points = label->pnts.getValues(0);
        if (label->pnts.getNum() < 2) {
            return {};
        }

        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        // Finds the mins and maxes
        std::vector<SbVec3f> corners;
        corners.push_back(p1);
        corners.push_back(p2);

        return corners;
    }

    std::vector<SbVec3f> computeArcLengthBBox() const
    {
        SoDatumLabel::ArcLengthGeometry geom;
        if (!label->arcLengthGeometry(geom)) {
            return {};
        }

        SbVec2s imgsize;
        int nc;
        int srcw = 1;
        int srch = 1;
        const unsigned char * dataptr = label->image.getValue(imgsize, nc);
        if (dataptr) {
            srcw = imgsize[0];
            srch = imgsize[1];
        }
        float imgHeight = scale * (float) (srch);
        float imgWidth  = imgHeight * (float) srcw / (float) srch;

        std::vector<SbVec3f> corners {geom.pnt1, geom.pnt2, geom.pnt3, geom.pnt4};
        // the dimension arc, closely enough for a box
        const int steps = 8;
        for (int i = 0; i <= steps; ++i) {
            float a = geom.startangle + (geom.endangle - geom.startangle) * float(i) / steps;
            corners.push_back(geom.arcCenter + geom.arcRadius * SbVec3f(cos(a), sin(a), 0.f));
        }

        // the number, turned along the chord and shifted outward
        const float mid = (geom.startangle + geom.endangle) / 2;
        const float shift = arcLengthShiftSign(geom.textAngle, mid) * imgHeight;
        const float s = sin(geom.textAngle);
        const float c = cos(geom.textAngle);
        for (float x : {-imgWidth / 2, imgWidth / 2}) {
            for (float y : {-imgHeight / 2, imgHeight / 2}) {
                float yy = y + shift;
                corners.push_back(geom.textOffset + SbVec3f(x * c - yy * s, x * s + yy * c, 0.f));
            }
        }
        return corners;
    }

private:
    float scale;
    SoDatumLabel* label;
};
}

void SoDatumLabel::computeBBox(SoAction * action, SbBox3f &box, SbVec3f &center)
{
    SoState *state = action->getState();
    float scale = getScaleFactor(state);

    DatumLabelBox datumBox(scale, this);
    datumBox.computeBBox(box, center);
}

SbVec3f SoDatumLabel::getLabelTextCenter()
{
    // Get the points stored
    const SbVec3f* points = this->pnts.getValues(0);
    SbVec3f p1 = points[0];
    SbVec3f p2 = points[1];

    if (datumtype.getValue() == SoDatumLabel::DISTANCE ||
        datumtype.getValue() == SoDatumLabel::DISTANCEX ||
        datumtype.getValue() == SoDatumLabel::DISTANCEY) {
        return getLabelTextCenterDistance(p1, p2);
    }
    else if (datumtype.getValue() == SoDatumLabel::RADIUS ||
        datumtype.getValue() == SoDatumLabel::DIAMETER) {
        return getLabelTextCenterDiameter(p1, p2);

    }
    else if (datumtype.getValue() == SoDatumLabel::ANGLE) {
        return getLabelTextCenterAngle(p1);
    }
    else if (datumtype.getValue() == SoDatumLabel::ARCLENGTH) {
        ArcLengthGeometry geom;
        if (arcLengthGeometry(geom)) {
            const float mid = (geom.startangle + geom.endangle) / 2;
            const float shift = arcLengthShiftSign(geom.textAngle, mid) * this->imgHeight;
            return geom.textOffset
                + SbVec3f(-sin(geom.textAngle), cos(geom.textAngle), 0.f) * shift;
        }
    }

    return p1;
}

SbVec3f SoDatumLabel::getLabelTextCenterDistance(const SbVec3f& p1, const SbVec3f& p2)
{
    float length = param1.getValue();
    float length2 = param2.getValue();

    SbVec3f dir;
    SbVec3f normal;
    if (datumtype.getValue() == SoDatumLabel::DISTANCE) {
        dir = (p2 - p1);
    }
    else if (datumtype.getValue() == SoDatumLabel::DISTANCEX) {
        dir = SbVec3f((p2[0] - p1[0] >= FLT_EPSILON) ? 1 : -1, 0, 0);
    }
    else if (datumtype.getValue() == SoDatumLabel::DISTANCEY) {
        dir = SbVec3f(0, (p2[1] - p1[1] >= FLT_EPSILON) ? 1 : -1, 0);
    }

    dir.normalize();
    normal = SbVec3f(-dir[1], dir[0], 0);

    float normproj12 = (p2 - p1).dot(normal);
    SbVec3f p1_ = p1 + normproj12 * normal;

    SbVec3f midpos = (p1_ + p2) / 2;

    SbVec3f textCenter = midpos + normal * length + dir * length2;
    return textCenter;
}

SbVec3f SoDatumLabel::getLabelTextCenterDiameter(const SbVec3f& p1, const SbVec3f& p2)
{
    SbVec3f dir = (p2 - p1);
    dir.normalize();

    float length = this->param1.getValue();
    SbVec3f textCenter = p2 + length * dir;
    return textCenter;
}

SbVec3f SoDatumLabel::getLabelTextCenterAngle(const SbVec3f& p0)
{
    // Load the Parameters
    float length = param1.getValue();
    float startangle = param2.getValue();
    float range = param3.getValue();
    float len2 = 2.0F * length;

    // Useful Information
    // v0 - vector for text position
    // p0 - vector for angle intersect
    SbVec3f v0(cos(startangle + range / 2), sin(startangle + range / 2), 0);

    SbVec3f textCenter = p0 + v0 * len2;
    return textCenter;
}

// Taken from upstream's calculateArcLengthGeometry (646b4381f9, reworked
// since), with one change: the number is placed ON the dimension arc and
// textShift carries it outward, where upstream puts it one text height out in
// world units -- see textShift.
bool SoDatumLabel::arcLengthGeometry(ArcLengthGeometry& geom) const
{
    if (this->pnts.getNum() < 3) {
        return false;
    }
    const SbVec3f* points = this->pnts.getValues(0);
    const SbVec3f ctr = points[0];
    const SbVec3f p1 = points[1];
    const SbVec3f p2 = points[2];
    const float length = this->param1.getValue();

    SbVec3f vc1 = p1 - ctr;
    SbVec3f vc2 = p2 - ctr;
    const float radius = vc1.length();
    if (radius <= FLT_EPSILON) {
        return false;
    }

    // The sweep from the start point to the end point, counter-clockwise.
    auto sweepEnd = [](float start, float end) {
        constexpr float tau = 2.0f * float(M_PI);
        const float delta = end - start;
        return delta >= 0.f ? end : end + tau * std::ceil(-delta / tau);
    };
    float startangle = atan2f(vc1[1], vc1[0]);
    float endangle = sweepEnd(startangle, atan2f(vc2[1], vc2[0]));
    const float range = endangle - startangle;

    // The extension lines run along the chord's normal, away from the centre.
    // A half circle has its chord through the centre; its middle direction
    // is the same line.
    SbVec3f vm = (p1 + p2) / 2 - ctr;
    if (vm.length() <= radius * 1e-4f) {
        const float mid = startangle + range / 2;
        vm = SbVec3f(cos(mid), sin(mid), 0.f);
    }
    vm.normalize();

    geom.pnt1 = p1;
    geom.pnt3 = p2;
    if (range > float(M_PI)) {
        // More than half a circle: the chord's side is the short side, so
        // the lines go that way, out to a circle about the same centre.
        const float desiredRadius = std::max(length, radius);
        const float proj = std::clamp(
            0.5f * (vc1.dot(vm) + vc2.dot(vm)) / radius, -1.0f, 1.0f);
        const float offset = -radius * proj
            + std::sqrt(std::max(0.0f,
                                 desiredRadius * desiredRadius
                                     - radius * radius * (1.0f - proj * proj)));
        SbVec3f o1 = p1 + offset * vm - ctr;
        SbVec3f o2 = p2 + offset * vm - ctr;
        o1.normalize();
        o2.normalize();

        geom.arcCenter = ctr;
        geom.arcRadius = desiredRadius;
        geom.pnt2 = ctr + desiredRadius * o1;
        geom.pnt4 = ctr + desiredRadius * o2;
        startangle = atan2f(o1[1], o1[0]);
        endangle = sweepEnd(startangle, atan2f(o2[1], o2[0]));
    }
    else {
        // The arc itself, moved along its middle direction: out, in, or past
        // the centre when length is negative.
        const float offset = length - radius;
        geom.pnt2 = p1 + offset * vm;
        geom.pnt4 = p2 + offset * vm;
        geom.arcCenter = ctr + offset * vm;
        geom.arcRadius = radius;
    }
    geom.startangle = startangle;
    geom.endangle = endangle;

    const float mid = (startangle + endangle) / 2;
    geom.textOffset = geom.arcCenter + geom.arcRadius * SbVec3f(cos(mid), sin(mid), 0.f);

    // Along the chord, kept upright as the distance labels are.
    SbVec3f dir = p2 - p1;
    dir.normalize();
    float angle = atan2f(dir[1], dir[0]);
    if (angle > float(M_PI_2 + M_PI / 12)) {
        angle -= float(M_PI);
    }
    else if (angle <= float(-M_PI_2 + M_PI / 12)) {
        angle += float(M_PI);
    }
    geom.textAngle = angle;
    return true;
}

void SoDatumLabel::generateArcLengthPrimitives(SoAction * action)
{
    ArcLengthGeometry geom;
    if (!arcLengthGeometry(geom)) {
        return;
    }
    // The number only, as for the other types: that is what picks the label.
    const float mid = (geom.startangle + geom.endangle) / 2;
    const float s = sin(geom.textAngle);
    const float c = cos(geom.textAngle);
    const float shift = arcLengthShiftSign(geom.textAngle, mid) * this->imgHeight;
    auto corner = [&](float x, float y) {
        y += shift;
        return geom.textOffset + SbVec3f(x * c - y * s, x * s + y * c, 0.f);
    };
    const float hw = this->imgWidth / 2;
    const float hh = this->imgHeight / 2;

    SoPrimitiveVertex pv;
    this->beginShape(action, QUADS);
    pv.setNormal(SbVec3f(0.f, 0.f, 1.f));
    for (const SbVec3f& p : {corner(-hw, -hh), corner(-hw, hh), corner(hw, hh), corner(hw, -hh)}) {
        pv.setPoint(p);
        shapeVertex(&pv);
    }
    this->endShape();
}

void SoDatumLabel::generateDistancePrimitives(SoAction * action, const SbVec3f& p1, const SbVec3f& p2)
{
    SbVec3f dir;
    if (this->datumtype.getValue() == DISTANCE) {
        dir = (p2-p1);
    } else if (this->datumtype.getValue() == DISTANCEX) {
        dir = SbVec3f( (p2[0] - p1[0] >= FLT_EPSILON) ? 1 : -1, 0, 0);
    } else if (this->datumtype.getValue() == DISTANCEY) {
        dir = SbVec3f(0, (p2[1] - p1[1] >= FLT_EPSILON) ? 1 : -1, 0);
    }

    dir.normalize();

    // Get magnitude of angle between horizontal
    float angle = atan2f(dir[1],dir[0]);

    SbVec3f img1 = SbVec3f(-this->imgWidth / 2, -this->imgHeight / 2, 0.f);
    SbVec3f img2 = SbVec3f(-this->imgWidth / 2,  this->imgHeight / 2, 0.f);
    SbVec3f img3 = SbVec3f( this->imgWidth / 2, -this->imgHeight / 2, 0.f);
    SbVec3f img4 = SbVec3f( this->imgWidth / 2,  this->imgHeight / 2, 0.f);

    // Rotate through an angle
    float s = sin(angle);
    float c = cos(angle);

    img1 = SbVec3f((img1[0] * c) - (img1[1] * s), (img1[0] * s) + (img1[1] * c), 0.f);
    img2 = SbVec3f((img2[0] * c) - (img2[1] * s), (img2[0] * s) + (img2[1] * c), 0.f);
    img3 = SbVec3f((img3[0] * c) - (img3[1] * s), (img3[0] * s) + (img3[1] * c), 0.f);
    img4 = SbVec3f((img4[0] * c) - (img4[1] * s), (img4[0] * s) + (img4[1] * c), 0.f);

    SbVec3f textOffset = getLabelTextCenterDistance(p1, p2);

    img1 += textOffset;
    img2 += textOffset;
    img3 += textOffset;
    img4 += textOffset;

    // Primitive Shape is only for text as this should only be selectable
    SoPrimitiveVertex pv;

    this->beginShape(action, QUADS);

    pv.setNormal( SbVec3f(0.f, 0.f, 1.f) );

    // Set coordinates
    pv.setPoint( img1 );
    shapeVertex(&pv);

    pv.setPoint( img2 );
    shapeVertex(&pv);

    pv.setPoint( img3 );
    shapeVertex(&pv);

    pv.setPoint( img4 );
    shapeVertex(&pv);

    this->endShape();
}

void SoDatumLabel::generateDiameterPrimitives(SoAction * action, const SbVec3f& p1, const SbVec3f& p2)
{
    SbVec3f dir = (p2-p1);
    dir.normalize();

    float angle = atan2f(dir[1],dir[0]);

    SbVec3f img1 = SbVec3f(-this->imgWidth / 2, -this->imgHeight / 2, 0.f);
    SbVec3f img2 = SbVec3f(-this->imgWidth / 2,  this->imgHeight / 2, 0.f);
    SbVec3f img3 = SbVec3f( this->imgWidth / 2, -this->imgHeight / 2, 0.f);
    SbVec3f img4 = SbVec3f( this->imgWidth / 2,  this->imgHeight / 2, 0.f);

    // Rotate through an angle
    float s = sin(angle);
    float c = cos(angle);

    img1 = SbVec3f((img1[0] * c) - (img1[1] * s), (img1[0] * s) + (img1[1] * c), 0.f);
    img2 = SbVec3f((img2[0] * c) - (img2[1] * s), (img2[0] * s) + (img2[1] * c), 0.f);
    img3 = SbVec3f((img3[0] * c) - (img3[1] * s), (img3[0] * s) + (img3[1] * c), 0.f);
    img4 = SbVec3f((img4[0] * c) - (img4[1] * s), (img4[0] * s) + (img4[1] * c), 0.f);

    SbVec3f textOffset = getLabelTextCenterDiameter(p1, p2);

    img1 += textOffset;
    img2 += textOffset;
    img3 += textOffset;
    img4 += textOffset;

    // Primitive Shape is only for text as this should only be selectable
    SoPrimitiveVertex pv;

    this->beginShape(action, QUADS);

    pv.setNormal( SbVec3f(0.f, 0.f, 1.f) );

    // Set coordinates
    pv.setPoint( img1 );
    shapeVertex(&pv);

    pv.setPoint( img2 );
    shapeVertex(&pv);

    pv.setPoint( img3 );
    shapeVertex(&pv);

    pv.setPoint( img4 );
    shapeVertex(&pv);

    this->endShape();
}

void SoDatumLabel::generateAnglePrimitives(SoAction * action, const SbVec3f& p0)
{
    SbVec3f textOffset = getLabelTextCenterAngle(p0);

    SbVec3f img1 = SbVec3f(-this->imgWidth / 2, -this->imgHeight / 2, 0.f);
    SbVec3f img2 = SbVec3f(-this->imgWidth / 2,  this->imgHeight / 2, 0.f);
    SbVec3f img3 = SbVec3f( this->imgWidth / 2, -this->imgHeight / 2, 0.f);
    SbVec3f img4 = SbVec3f( this->imgWidth / 2,  this->imgHeight / 2, 0.f);

    img1 += textOffset;
    img2 += textOffset;
    img3 += textOffset;
    img4 += textOffset;

    // Primitive Shape is only for text as this should only be selectable
    SoPrimitiveVertex pv;

    this->beginShape(action, QUADS);

    pv.setNormal( SbVec3f(0.f, 0.f, 1.f) );

    // Set coordinates
    pv.setPoint( img1 );
    shapeVertex(&pv);

    pv.setPoint( img2 );
    shapeVertex(&pv);

    pv.setPoint( img3 );
    shapeVertex(&pv);

    pv.setPoint( img4 );
    shapeVertex(&pv);

    this->endShape();
}

void SoDatumLabel::generateSymmetricPrimitives(SoAction * action, const SbVec3f& p1, const SbVec3f& p2)
{
    SbVec3f dir = (p2-p1);
    dir.normalize();
    SbVec3f normal (-dir[1],dir[0],0);

    float margin = this->imgHeight / 4.0;

    // Calculate coordinates for the first arrow
    SbVec3f ar0, ar1, ar2;
    ar0  = p1 + dir * 5 * margin ;
    ar1  = ar0 - dir * 0.866f * 2 * margin; // Base Point of Arrow
    ar2  = ar1 + normal * margin; // Triangular corners
    ar1 -= normal * margin;

    // Calculate coordinates for the second arrow
    SbVec3f ar3, ar4, ar5;
    ar3  = p2 - dir * 5 * margin ;
    ar4  = ar3 + dir * 0.866f * 2 * margin; // Base Point of 2nd Arrow

    ar5  = ar4 + normal * margin; // Triangular corners
    ar4 -= normal * margin;

    SoPrimitiveVertex pv;

    this->beginShape(action, TRIANGLES);

    pv.setNormal( SbVec3f(0.f, 0.f, 1.f) );

    // Set coordinates
    pv.setPoint( ar0 );
    shapeVertex(&pv);

    pv.setPoint( ar1 );
    shapeVertex(&pv);

    pv.setPoint( ar2 );
    shapeVertex(&pv);

    // Set coordinates
    pv.setPoint( ar3 );
    shapeVertex(&pv);

    pv.setPoint( ar4 );
    shapeVertex(&pv);

    pv.setPoint( ar5 );
    shapeVertex(&pv);

    this->endShape();
}

bool SoDatumLabel::updateImageSize(SoState * state, int & srcw, int & srch)
{
    // Recompute imgWidth/imgHeight the same way GLRender does at its top. This
    // is needed during render-cache capture because GLRender never runs then
    // (in render-cache modes SoFCSelectionRoot draws through the render cache,
    // not the per-shape GL traversal), so the members would otherwise be stale.
    bool hasText = computeImageSize(state, srcw, srch);

    // Keep the companion quad's texture in step with the current bitmap.
    // Gated on imagesynced (not glimagevalid) because GLRender may have
    // already validated the bitmap without ever feeding the companion.
    if (this->string.getValues(0)->getLength() > 0 && this->imageTexture
        && !this->imagesynced) {
        syncImageTexture();
        this->imagesynced = true;
    }

    // Calibrate the companion autozoom so the glyph quad (emitted in native
    // pixels) renders one screen pixel per glyph pixel, as the GL path draws
    // it. The backend applies scaleFactor * getWorldToScreenScale(0, 0.1) /
    // (5 * aspect) (RendererBridge::translateAutoZoomScale), and the 0.1 there
    // is a tenth of the view WIDTH, so one screen pixel -- width / vpWidth --
    // takes scaleFactor = 50 / vpHeight. This used to read 7.5 / vpHeight,
    // fitted by eye while the capture traversal still saw Coin's 100 px default
    // viewport: 7.5 / 100 is 50 / 667, right for the window it was tuned in and
    // nearly seven times too small once the capture got the real viewport. Set
    // only on change so it does not thrash the render cache each frame.
    if (this->imageZoom) {
        const SbViewportRegion& vp = SoViewportRegionElement::get(state);
        float vph = (float)vp.getViewportSizePixels()[1];
        if (vph > 0.f) {
            float sf = 50.0f / vph;
            if (this->imageZoom->scaleFactor.getValue() != sf)
                this->imageZoom->scaleFactor.setValue(sf);
        }
    }

    return hasText;
}

bool SoDatumLabel::computeImageSize(SoState * state, int & srcw, int & srch)
{
    float scale = getScaleFactor(state);

    const SbString* s = string.getValues(0);
    bool hasText = (s->getLength() > 0);
    srcw = 1;
    srch = 1;

    if (hasText) {
        if (!this->glimagevalid) {
            drawImage();
            this->glimagevalid = true;
        }
        SbVec2s imgsize;
        int nc;
        const unsigned char* dataptr = this->image.getValue(imgsize, nc);
        if (!dataptr) {
            hasText = false;
        }
        else {
            srcw = imgsize[0];
            srch = imgsize[1];
            float aspectRatio = (float)srcw / (float)srch;
            this->imgHeight = scale * (float)srch;
            this->imgWidth  = aspectRatio * (float)this->imgHeight;
        }
    }

    if (this->datumtype.getValue() == SYMMETRIC) {
        this->imgHeight = scale * 25.0f;
        this->imgWidth  = scale * 25.0f;
    }

    return hasText;
}

void SoDatumLabel::generateLeaderPrimitives(SoAction * action)
{
    // Mirror of GLRender()'s line/arrow/arc drawing, emitted as cache-visible
    // primitives (via beginShape/shapeVertex) instead of immediate-mode GL, so
    // the render-cache bridge captures the datum for the bgfx / WASM backend.
    // The text glyph quad is handled separately (it needs its own textured
    // material scope).
    //
    // GL sizes the arrowheads, the gap left for the number and the
    // extension-line overshoot in screen pixels, recomputing them from the
    // view every frame. A capture is taken once and drawn under any camera --
    // the capture traversal does not even have the viewer's -- so each vertex
    // here is a WORLD point plus an offset in PIXELS along a direction in this
    // node's coordinates, carried in the texture coordinate (the screenOffsets
    // field tells the capture so). The backend resolves the offsets against
    // the view it draws with (Render::MeshData::screenOffsets). The pixel
    // sizes are the glyph's own, which is what GLRender multiplies by its
    // world-per-pixel scale.
    //
    // What that representation cannot say is a choice that depends on the
    // zoom. Two are made here from world quantities instead: the arrows turn
    // outward when the number's CENTRE lies past an extension line (GL: when
    // any of it does), and an angle's end lines are the pixel minimum only
    // when no length was set. The arc trimmed around an angle's number is
    // linearised along each vertex's tangent.
    SoState* state = action->getState();

    int srcw = 1, srch = 1;
    updateImageSize(state, srcw, srch);

    const SbVec3f* points = this->pnts.getValues(0);
    int npts = this->pnts.getNum();
    int dt = this->datumtype.getValue();

    const float textW = float(srcw);
    const float margin = (dt == SYMMETRIC ? 25.0f : float(srch)) / 4.0f;
    const SbVec3f none(0.f, 0.f, 0.f);
    this->textShift = 0.f;

    struct Pt {
        SbVec3f p;   // world, in this node's coordinates
        SbVec3f px;  // screen-space offset, pixels
    };
    auto at = [&](const SbVec3f& p) { return Pt{p, none}; };

    SoPrimitiveVertex pv;
    pv.setNormal(SbVec3f(0.f, 0.f, 1.f));
    pv.setMaterialIndex(0);

    auto put = [&](const Pt& v) {
        pv.setPoint(v.p);
        pv.setTextureCoords(SbVec4f(v.px[0], v.px[1], v.px[2], 1.f));
        shapeVertex(&pv);
    };
    auto emitLine = [&](const Pt& a, const Pt& b) {
        this->beginShape(action, LINES);
        put(a);
        put(b);
        this->endShape();
    };
    auto emitTri = [&](const Pt& a, const Pt& b, const Pt& c) {
        this->beginShape(action, TRIANGLES);
        put(a);
        put(b);
        put(c);
        this->endShape();
    };
    // An arrowhead with its tip at `tip`, pointing along -`back`.
    auto emitArrow = [&](const SbVec3f& tip, const SbVec3f& back, const SbVec3f& side) {
        SbVec3f base = back * (0.866f * 2 * margin);
        emitTri(at(tip), Pt{tip, base - side * margin}, Pt{tip, base + side * margin});
    };

    // Text label rotation matching GLRender's normalisation (keep upright).
    auto textAngleFromDir = [](const SbVec3f& d) -> float {
        float a = atan2f(d[1], d[0]);
        if (a > float(M_PI_2 + M_PI / 12))
            a -= float(M_PI);
        else if (a <= float(-M_PI_2 + M_PI / 12))
            a += float(M_PI);
        return a;
    };

    if (dt == DISTANCE || dt == DISTANCEX || dt == DISTANCEY) {
        if (npts < 2)
            return;
        float length = this->param1.getValue();
        float length2 = this->param2.getValue();

        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir, normal;
        if (dt == DISTANCE) {
            dir = (p2 - p1);
        } else if (dt == DISTANCEX) {
            dir = SbVec3f((p2[0] - p1[0] >= FLT_EPSILON) ? 1 : -1, 0, 0);
        } else {
            dir = SbVec3f(0, (p2[1] - p1[1] >= FLT_EPSILON) ? 1 : -1, 0);
        }
        dir.normalize();
        normal = SbVec3f(-dir[1], dir[0], 0);

        float normproj12 = (p2 - p1).dot(normal);
        SbVec3f p1_ = p1 + normproj12 * normal;
        SbVec3f midpos = (p1_ + p2) / 2;

        float offset1 = ((length + normproj12 < 0) ? -1.f : 1.f) * srch;
        float offset2 = ((length < 0) ? -1.f : 1.f) * srch;

        this->textOffset = midpos + normal * length + dir * length2;
        this->textAngle = textAngleFromDir(dir);

        Pt perp1{p1_ + normal * length, normal * offset1};
        Pt perp2{p2 + normal * length, normal * offset2};

        SbVec3f par1 = p1_ + normal * length;
        SbVec3f par4 = p2 + normal * length;
        const float halfGap = textW / 2 + margin;
        Pt P2{this->textOffset, dir * -halfGap};
        Pt P3{this->textOffset, dir * halfGap};
        Pt P1 = at(par1);
        Pt P4 = at(par4);

        bool flipTriang = false;
        float t = (this->textOffset - par1).dot(dir);
        float span = (par4 - par1).length();
        float tmpMargin = srch / 0.75f;
        if (t > span) {
            P3 = P2;
            P2 = Pt{par1, dir * -tmpMargin};
            flipTriang = true;
        }
        else if (t < 0.f) {
            P2 = P3;
            P3 = Pt{par4, dir * tmpMargin};
            flipTriang = true;
        }

        if (length != 0.) {
            emitLine(at(p1), perp1);
            emitLine(at(p2), perp2);
        }
        emitLine(P1, P2);
        emitLine(P3, P4);

        float s = flipTriang ? -1.f : 1.f;
        emitArrow(par1, dir * s, normal);
        emitArrow(par4, dir * -s, normal);
    }
    else if (dt == RADIUS || dt == DIAMETER) {
        if (npts < 2)
            return;
        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir = (p2 - p1);
        SbVec3f center = p1;
        double radius = (p2 - p1).length();
        if (dt == DIAMETER) {
            center = (p1 + p2) / 2;
            radius = radius / 2;
        }
        dir.normalize();
        SbVec3f normal(-dir[1], dir[0], 0);

        float length = this->param1.getValue();
        SbVec3f pos = p2 + length * dir;

        this->textOffset = pos;
        this->textAngle = textAngleFromDir(dir);

        const float halfGap = textW / 2 + margin;
        // The line runs on past the number when the number sits outside.
        Pt end = length >= 0.f ? Pt{pos, dir * halfGap} : at(p2);

        emitLine(at(p1), Pt{pos, dir * -halfGap});
        emitLine(Pt{pos, dir * halfGap}, end);
        emitArrow(p2, dir * -1.f, normal);
        if (dt == DIAMETER)
            emitArrow(p1, dir, normal);

        float startangle = this->param3.getValue();
        float range = this->param4.getValue();
        if (range != 0.0) {
            int countSegments = std::max(6, abs(int(50.0 * range / (2 * M_PI))));
            double segment = range / (countSegments - 1);
            this->beginShape(action, LINE_STRIP);
            for (int i = 0; i < countSegments; i++) {
                double theta = startangle + segment * i;
                put(at(center + SbVec3f(radius * cos(theta), radius * sin(theta), 0)));
            }
            this->endShape();
        }
    }
    else if (dt == ANGLE) {
        if (npts < 1)
            return;
        SbVec3f p0 = points[0];

        float length     = this->param1.getValue();
        float startangle = this->param2.getValue();
        float range      = this->param3.getValue();
        float endangle   = startangle + range;
        float param4     = this->param4.getValue();
        float param5     = this->param5.getValue();

        float r = 2 * length;

        // Text sits on the mid-angle ray, upright (matches GLRender).
        this->textOffset =
            p0 + SbVec3f(cos(startangle + range / 2), sin(startangle + range / 2), 0) * r;
        this->textAngle = 0.f;

        // Each arc runs from its end toward the middle and stops short of
        // the number: GL trims the range by textW / (2r) in world terms, a
        // quarter of the number's width on each side. Vertex i of 2c-2
        // moves back along its tangent by i / (2c - 2) of half that width.
        int countSegments = std::max(6, abs(int(50.0 * range / (2 * M_PI))));
        double segment = range / (2 * countSegments - 2);
        // Toward the start along the arc is -tangent for a positive radius;
        // a negative one (the number past the centre) draws the arc turned
        // half way round, and the tangent with it.
        float sgn = (range >= 0 ? 1.f : -1.f) * (r >= 0 ? 1.f : -1.f);
        float step = textW / (2.f * float(2 * countSegments - 2));

        this->beginShape(action, LINE_STRIP);
        for (int i = 0; i < countSegments; i++) {
            double theta = startangle + segment * i;
            SbVec3f tangent(-sin(theta), cos(theta), 0);
            put(Pt{p0 + SbVec3f(r * cos(theta), r * sin(theta), 0),
                   tangent * (-sgn * step * i)});
        }
        this->endShape();

        this->beginShape(action, LINE_STRIP);
        for (int i = 0; i < countSegments; i++) {
            double theta = endangle - segment * i;
            SbVec3f tangent(-sin(theta), cos(theta), 0);
            put(Pt{p0 + SbVec3f(r * cos(theta), r * sin(theta), 0),
                   tangent * (sgn * step * i)});
        }
        this->endShape();

        // End lines: the set length (world) where there is one, else the
        // pixel minimum GL clamps them to.
        auto endLine = [&](const SbVec3f& v, float param) {
            SbVec3f onArc = p0 + v * r;
            Pt inner = param > 0.f ? at(onArc - v * param) : Pt{onArc, v * -margin};
            Pt outer = param < 0.f ? at(onArc - v * param) : Pt{onArc, v * margin};
            emitLine(inner, outer);
        };
        endLine(SbVec3f(cos(startangle), sin(startangle), 0), param4);
        endLine(SbVec3f(cos(endangle), sin(endangle), 0), param5);
    }
    else if (dt == SYMMETRIC) {
        if (npts < 2)
            return;
        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir = (p2 - p1);
        dir.normalize();
        SbVec3f normal(-dir[1], dir[0], 0);

        SbVec3f zc(0, 0, ZCONSTR);
        SbVec3f head = dir * (4 * margin);
        SbVec3f base = dir * (4 * margin - 0.866f * 2 * margin);
        SbVec3f side = normal * margin;

        emitLine(at(p1 + zc), Pt{p1 + zc, head});
        emitLine(Pt{p1 + zc, head}, Pt{p1 + zc, base - side});
        emitLine(Pt{p1 + zc, head}, Pt{p1 + zc, base + side});

        emitLine(at(p2 + zc), Pt{p2 + zc, head * -1.f});
        emitLine(Pt{p2 + zc, head * -1.f}, Pt{p2 + zc, base * -1.f - side});
        emitLine(Pt{p2 + zc, head * -1.f}, Pt{p2 + zc, base * -1.f + side});
    }
    else if (dt == ARCLENGTH) {
        ArcLengthGeometry geom;
        if (!arcLengthGeometry(geom))
            return;

        const float mid = (geom.startangle + geom.endangle) / 2;
        this->textOffset = geom.textOffset;
        this->textAngle = geom.textAngle;
        this->textShift = arcLengthShiftSign(geom.textAngle, mid) * float(srch);

        const float range = geom.endangle - geom.startangle;
        int countSegments = std::max(6, abs(int(50.0 * range / (2 * M_PI))));
        double segment = range / (countSegments - 1);
        this->beginShape(action, LINE_STRIP);
        for (int i = 0; i < countSegments; i++) {
            double theta = geom.startangle + segment * i;
            put(at(geom.arcCenter
                   + SbVec3f(geom.arcRadius * cos(theta), geom.arcRadius * sin(theta), 0)));
        }
        this->endShape();

        emitLine(at(geom.pnt1), at(geom.pnt2));
        emitLine(at(geom.pnt3), at(geom.pnt4));

        // Tips on the extension lines, bodies along the arc.
        auto radial = [](float a) { return SbVec3f(cos(a), sin(a), 0.f); };
        auto tangent = [](float a) { return SbVec3f(-sin(a), cos(a), 0.f); };
        emitArrow(geom.pnt2, tangent(geom.startangle), radial(geom.startangle));
        emitArrow(geom.pnt4, tangent(geom.endangle) * -1.f, radial(geom.endangle));
    }
}

void SoDatumLabel::generateTextQuad(SoAction * action)
{
    // Emit the glyph as a textured quad (two triangles + UVs) for the companion
    // SoDatumLabelImage. The quad is emitted at the ORIGIN in native glyph
    // pixels; the sub-graph's SoDatumLabelAnchor moves it to textOffset/textAngle
    // and the SoAutoZoomTranslation scales it to a constant screen size each
    // frame (native pixels => sampled at mip 0 => the baked colour is preserved,
    // fixing both the minification darkening and the fixed-world-size problem).
    if (this->string.getNum() == 0 || this->string[0].getLength() == 0)
        return;

    SbVec2s imgsize;
    int nc;
    if (!this->image.getValue(imgsize, nc) || imgsize[0] <= 0 || imgsize[1] <= 0)
        return;
    float hw = imgsize[0] * 0.5f;
    float hh = imgsize[1] * 0.5f;

    // Local corners at the origin with matching UVs. The glyph bitmap is stored
    // bottom-up (GL convention), so v=0 is the bottom row.
    struct Corner { float x, y, u, v; };
    const float dy = this->textShift;
    const Corner corners[4] = {
        {-hw, dy - hh, 0.f, 0.f},
        { hw, dy - hh, 1.f, 0.f},
        { hw, dy + hh, 1.f, 1.f},
        {-hw, dy + hh, 0.f, 1.f},
    };

    SoPrimitiveVertex pv;
    pv.setNormal(SbVec3f(0.f, 0.f, 1.f));
    pv.setMaterialIndex(0);

    auto emit = [&](int i) {
        pv.setPoint(SbVec3f(corners[i].x, corners[i].y, 0.f));
        pv.setTextureCoords(SbVec4f(corners[i].u, corners[i].v, 0.f, 1.f));
        shapeVertex(&pv);
    };

    this->beginShape(action, TRIANGLES);
    emit(0); emit(1); emit(2);
    emit(0); emit(2); emit(3);
    this->endShape();
}

void SoDatumLabel::generatePrimitives(SoAction * action)
{
    // Render-cache capture (SoCallbackAction): nothing from here. The
    // companion sub-graph (getImageNode()) carries the whole datum into the
    // capture -- the leaders from SoDatumLabelLeader under their own style,
    // the glyph from SoDatumLabelImage under its texture.
    if (action->isOfType(SoCallbackAction::getClassTypeId()))
        return;

    // Ray-pick path: keep only the text label box selectable (unchanged).
    // The box is sized for the view picked in. The members were last set by
    // whatever traversed before -- GLRender for this view in mode 0, but in
    // the render-cache modes GLRender never runs, and the number of a
    // dimension could not be picked at all.
    int srcw = 1, srch = 1;
    computeImageSize(action->getState(), srcw, srch);
    // Initialisation check (needs something more sensible) prevents an infinite loop bug
    if (this->imgHeight <= FLT_EPSILON || this->imgWidth <= FLT_EPSILON)
        return;

    // Get the points stored
    const SbVec3f *points = this->pnts.getValues(0);
    SbVec3f p1 = points[0];
    SbVec3f p2 = points[1];

    // Change the offset and bounding box parameters depending on Datum Type
    if (this->datumtype.getValue() == DISTANCE ||
        this->datumtype.getValue() == DISTANCEX ||
        this->datumtype.getValue() == DISTANCEY) {

        generateDistancePrimitives(action, p1, p2);
    }
    else if (this->datumtype.getValue() == RADIUS ||
             this->datumtype.getValue() == DIAMETER) {

        generateDiameterPrimitives(action, p1, p2);
    }
    else if (this->datumtype.getValue() == ANGLE) {

        generateAnglePrimitives(action, p1);
    }
    else if (this->datumtype.getValue() == SYMMETRIC) {

        generateSymmetricPrimitives(action, p1, p2);
    }
    else if (this->datumtype.getValue() == ARCLENGTH) {

        generateArcLengthPrimitives(action);
    }
}

void SoDatumLabel::notify(SoNotList * l)
{
    SoField * f = l->getLastField();
    if (f == &this->string || f == &this->textColor || f == &this->name
        || f == &this->size || f == &this->image) {
        this->glimagevalid = false;
        // The glyph bitmap changed; the companion texture must be re-fed.
        this->imagesynced = false;
    }
    // The companion shapes draw this label for the render-cache capture, from
    // these fields, but are not below this node: nothing tells the capture
    // they changed, and it kept drawing the label as it first found it -- the
    // old number, at the old place. textColor and lineWidth reach it through
    // their connected style nodes; image is written by the capture itself, as
    // is what the anchor reads.
    if (this->leaderShape && f && f != &this->textColor && f != &this->lineWidth
        && f != &this->image) {
        this->leaderShape->touch();
        this->imageShape->touch();
    }
    inherited::notify(l);
}

float SoDatumLabel::getScaleFactor(SoState* state) const
{
    const SbViewVolume & vv = SoViewVolumeElement::get(state);

    /**Remark from Stefan Tröger:
    * The scale calculation is based on knowledge of SbViewVolume::getWorldToScreenScale
    * implementation internals. The factor returned from this function is calculated from the view frustums
    * nearplane width, height is not taken into account, and hence we divide it with the viewport width
    * to get the exact pixel scale factor.
    * This is not documented and therefore may change on later coin versions!
    */
#if 0
    // As reference use the center point the camera is looking at on the focal plane
    // because then independent of the camera we get a constant scale factor when panning.
    // If we used (0,0,0) instead then the scale factor would change heavily in perspective
    // rendering mode. See #0002921 and #0002922.
    // It's important to use the distance to the focal plane an not near or far plane because
    // depending on additionally displayed objects they may change heavily and thus impact the
    // scale factor. See #7082 and #7860.
    float focal = SoFocalDistanceElement::get(state);
    SbVec3f center = vv.getSightPoint(focal);
    float scale = vv.getWorldToScreenScale(center, 1.f);
    const SbViewportRegion & vp = SoViewportRegionElement::get(state);
    SbVec2s vp_size = vp.getViewportSizePixels();
    scale /= vp_size[0];
#else

    // The above comment about potential scale change in perspective view does
    // not seem to be true. On the contrary, it produces unusable scale factor.
    // Using center 0 below seems to be just fine.
    SbVec3f center(0,0,0);
    float scale = vv.getWorldToScreenScale(center, 0.1f);
    const SbViewportRegion & vp = SoViewportRegionElement::get(state);
    SbVec2s vp_size = vp.getViewportSizePixels();
    scale /= vp_size[0]/10.f;
#endif

    return scale;
}

void SoDatumLabel::GLRender(SoGLRenderAction * action)
{
    // An external backend is already drawing this datum (leaders + glyph) from
    // the captured editing overlay; skip the raw-GL draw to avoid doubling.
    if (SuppressGLRender)
        return;

    SoState *state = action->getState();

    if (!shouldGLRender(action))
        return;
    if (action->handleTransparency(true))
        return;

    float scale = getScaleFactor(state);

    const SbString* s = string.getValues(0);
    bool hasText = (s->getLength() > 0) ? true : false;

    SbVec2s imgsize;
    int nc;
    int srcw=1, srch=1;

    if (hasText) {
        if (!this->glimagevalid) {
            drawImage();
            this->glimagevalid = true;
        }

        const unsigned char * dataptr = this->image.getValue(imgsize, nc);
        if (!dataptr) // no image
            return;

        srcw = imgsize[0];
        srch = imgsize[1];

        float aspectRatio =  (float) srcw / (float) srch;
        this->imgHeight = scale * (float) (srch);
        this->imgWidth  = aspectRatio * (float) this->imgHeight;
    }

    if (this->datumtype.getValue() == SYMMETRIC) {
        this->imgHeight = scale*25.0f;
        this->imgWidth = scale*25.0f;
    }

    // Get the points stored in the pnt field
    const SbVec3f *points = this->pnts.getValues(0);

    state->push();

    //Set General OpenGL Properties
    glPushAttrib(GL_ENABLE_BIT | GL_PIXEL_MODE_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING);

    //Enable Anti-alias
    if (action->isSmoothing()) {
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);
    }

    // Position for Datum Text Label
    float angle = 0;

    // Get the colour
    const SbColor& t = textColor.getValue();

    // Set GL Properties
    glLineWidth(this->lineWidth.getValue());
    glColor3f(t[0], t[1], t[2]);

    SbVec3f textOffset;

    if (this->datumtype.getValue() == DISTANCE ||
        this->datumtype.getValue() == DISTANCEX ||
        this->datumtype.getValue() == DISTANCEY ) {
        float length = this->param1.getValue();
        float length2 = this->param2.getValue();

        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir, normal;
        if (this->datumtype.getValue() == DISTANCE) {
            dir = (p2-p1);
        } else if (this->datumtype.getValue() == DISTANCEX) {
            dir = SbVec3f( (p2[0] - p1[0] >= FLT_EPSILON) ? 1 : -1, 0, 0);
        } else if (this->datumtype.getValue() == DISTANCEY) {
            dir = SbVec3f(0, (p2[1] - p1[1] >= FLT_EPSILON) ? 1 : -1, 0);
        }

        dir.normalize();
        normal = SbVec3f (-dir[1],dir[0],0);

        // when the datum line is not parallel to p1-p2 the projection of
        // p1-p2 on normal is not zero, p2 is considered as reference and p1
        // is replaced by its projection p1_
        float normproj12 = (p2-p1).dot(normal);
        SbVec3f p1_ = p1 + normproj12 * normal;

        SbVec3f midpos = (p1_ + p2)/2;

        float offset1 = ((length + normproj12 < 0) ? -1.  : 1.) * srch;
        float offset2 = ((length < 0) ? -1  : 1)*srch;

        // Get magnitude of angle between horizontal
        angle = atan2f(dir[1],dir[0]);
        if (angle > M_PI_2+M_PI/12) {
            angle -= (float)M_PI;
        } else if (angle <= -M_PI_2+M_PI/12) {
            angle += (float)M_PI;
        }

        textOffset = midpos + normal * length + dir * length2;

        // Get the colour
        const SbColor& t = textColor.getValue();

        // Set GL Properties
        glLineWidth(this->lineWidth.getValue());
        glColor3f(t[0], t[1], t[2]);
        float margin = this->imgHeight / 4.0;


        SbVec3f perp1 = p1_ + normal * (length + offset1 * scale);
        SbVec3f perp2 = p2  + normal * (length + offset2 * scale);

        // Calculate the coordinates for the parallel datum lines
        SbVec3f par1 = p1_ + normal * length;
        SbVec3f par2 = midpos + normal * length + dir * (length2 - this->imgWidth / 2 - margin);
        SbVec3f par3 = midpos + normal * length + dir * (length2 + this->imgWidth / 2 + margin);
        SbVec3f par4 = p2  + normal * length;

        bool flipTriang = false;

        if ((par3-par1).dot(dir) > (par4 - par1).length()) {
            // Increase Margin to improve visibility
            float tmpMargin = this->imgHeight /0.75;
            par3 = par4;
            if ((par2-par1).dot(dir) > (par4 - par1).length()) {
                par3 = par2;
                par2 = par1 - dir * tmpMargin;
                flipTriang = true;
            }
        }
        else if ((par2-par1).dot(dir) < 0.f) {
            float tmpMargin = this->imgHeight /0.75;
            par2 = par1;
            if((par3-par1).dot(dir) < 0.f) {
                par2 = par3;
                par3 = par4 + dir * tmpMargin;
                flipTriang = true;
            }
        }
        // Perp Lines
        glBegin(GL_LINES);
            if (length != 0.) {
                glVertex2f(p1[0], p1[1]);
                glVertex2f(perp1[0], perp1[1]);

                glVertex2f(p2[0], p2[1]);
                glVertex2f(perp2[0], perp2[1]);
            }

            glVertex2f(par1[0], par1[1]);
            glVertex2f(par2[0], par2[1]);

            glVertex2f(par3[0], par3[1]);
            glVertex2f(par4[0], par4[1]);
        glEnd();

        SbVec3f ar1 = par1 + ((flipTriang) ? -1 : 1) * dir * 0.866f * 2 * margin;
        SbVec3f ar2 = ar1 + normal * margin;
                ar1 -= normal * margin;

        SbVec3f ar3 = par4 - ((flipTriang) ? -1 : 1) * dir * 0.866f * 2 * margin;
        SbVec3f ar4 = ar3 + normal * margin ;
                ar3 -= normal * margin;

        //Draw a pretty arrowhead (Equilateral) (Eventually could be improved to other shapes?)
        glBegin(GL_TRIANGLES);
            glVertex2f(par1[0], par1[1]);
            glVertex2f(ar1[0], ar1[1]);
            glVertex2f(ar2[0], ar2[1]);

            glVertex2f(par4[0], par4[1]);
            glVertex2f(ar3[0], ar3[1]);
            glVertex2f(ar4[0], ar4[1]);
        glEnd();
    }
    else if (this->datumtype.getValue() == RADIUS || this->datumtype.getValue() == DIAMETER) {
        // Get the Points
        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir = (p2-p1);
        SbVec3f center = p1;
        double radius = (p2 - p1).length();
        if (this->datumtype.getValue() == DIAMETER) {
            center = (p1 + p2) / 2;
            radius = radius / 2;
        }

        dir.normalize();
        SbVec3f normal (-dir[1],dir[0],0);

        float length = this->param1.getValue();
        SbVec3f pos = p2 + length*dir;

        // Get magnitude of angle between horizontal
        angle = atan2f(dir[1],dir[0]);
        if (angle > M_PI_2+M_PI/12) {
            angle -= (float)M_PI;
        } else if (angle <= -M_PI_2+M_PI/12) {
            angle += (float)M_PI;
        }

        textOffset = pos;

        float margin = this->imgHeight / 4.0;

        // Create the arrowhead
        SbVec3f ar0  = p2;
        SbVec3f ar1  = p2 - dir * 0.866f * 2 * margin;
        SbVec3f ar2  = ar1 + normal * margin;
        ar1 -= normal * margin;

        SbVec3f p3 = pos +  dir * (this->imgWidth / 2 + margin);
        if ((p3-p1).length() > (p2-p1).length())
            p2 = p3;

        // Calculate the points
        SbVec3f pnt1 = pos - dir * (margin + this->imgWidth / 2);
        SbVec3f pnt2 = pos + dir * (margin + this->imgWidth / 2);

        // Draw the Lines
        glBegin(GL_LINES);
            glVertex2f(p1[0], p1[1]);
            glVertex2f(pnt1[0], pnt1[1]);

            glVertex2f(pnt2[0], pnt2[1]);
            glVertex2f(p2[0], p2[1]);
        glEnd();

        glBegin(GL_TRIANGLES);
            glVertex2f(ar0[0], ar0[1]);
            glVertex2f(ar1[0], ar1[1]);
            glVertex2f(ar2[0], ar2[1]);
        glEnd();

        if (this->datumtype.getValue() == DIAMETER) {
            // create second arrowhead
            SbVec3f ar0_1  = p1;
            SbVec3f ar1_1  = p1 + dir * 0.866f * 2 * margin;
            SbVec3f ar2_1  = ar1_1 + normal * margin;
            ar1_1 -= normal * margin;

            glBegin(GL_TRIANGLES);
                glVertex2f(ar0_1[0], ar0_1[1]);
                glVertex2f(ar1_1[0], ar1_1[1]);
                glVertex2f(ar2_1[0], ar2_1[1]);
            glEnd();
        }

        // Draw arc helper if needed
        float startangle = this->param3.getValue();
        float range = this->param4.getValue();
        if (range != 0.0) {
            int countSegments = std::max(6, abs(int(50.0 * range / (2 * M_PI))));
            double segment = range / (countSegments - 1);

            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < countSegments; i++) {
                double theta = startangle + segment * i;
                SbVec3f v1 = center + SbVec3f(radius * cos(theta), radius * sin(theta), 0);
                glVertex2f(v1[0], v1[1]);
            }
            glEnd();
        }

    }
    else if (this->datumtype.getValue() == ANGLE) {
        // Only the angle intersection point is needed
        SbVec3f p0 = points[0];

        float margin = this->imgHeight / 4.0;

        // Load the Parameters
        float length     = this->param1.getValue();
        float startangle = this->param2.getValue();
        float range      = this->param3.getValue();
        float endangle   = startangle + range;
        float endLineLength1 = std::max(this->param4.getValue(), margin);
        float endLineLength2 = std::max(this->param5.getValue(), margin);
        float endLineLength12 = std::max(- this->param4.getValue(), margin);
        float endLineLength22 = std::max(- this->param5.getValue(), margin);


        float r = 2*length;

        // Set the Text label angle to zero
        angle = 0.f;

        // Useful Information
        // v0 - vector for text position
        // p0 - vector for angle intersect
        SbVec3f v0(cos(startangle+range/2),sin(startangle+range/2),0);

        // leave some space for the text; r is negative when the number is
        // past the centre, and the gap is the same size then
        if (range >= 0)
            range = std::max(0.2f*range, range - this->imgWidth/(2*std::fabs(r)));
        else
            range = std::min(0.2f*range, range + this->imgWidth/(2*std::fabs(r)));

        int countSegments = std::max(6, abs(int(50.0 * range / (2 * M_PI))));
        double segment = range / (2*countSegments-2);

        textOffset = p0 + v0 * r;


        // Draw
        glBegin(GL_LINE_STRIP);

        for (int i=0; i < countSegments; i++) {
            double theta = startangle + segment*i;
            SbVec3f v1 = p0+SbVec3f(r*cos(theta),r*sin(theta),0);
            glVertex2f(v1[0],v1[1]);
        }
        glEnd();

        glBegin(GL_LINE_STRIP);
        for (int i=0; i < countSegments; i++) {
            double theta = endangle - segment*i;
            SbVec3f v1 = p0+SbVec3f(r*cos(theta),r*sin(theta),0);
            glVertex2f(v1[0],v1[1]);
        }
        glEnd();

        // Direction vectors for start and end lines
        SbVec3f v1(cos(startangle),sin(startangle),0);
        SbVec3f v2(cos(endangle),sin(endangle),0);

        SbVec3f pnt1 = p0 + (r - endLineLength1) * v1;
        SbVec3f pnt2 = p0 + (r + endLineLength12) * v1;
        SbVec3f pnt3 = p0 + (r - endLineLength2) * v2;
        SbVec3f pnt4 = p0 + (r + endLineLength22) * v2;

        glBegin(GL_LINES);
            glVertex2f(pnt1[0],pnt1[1]);
            glVertex2f(pnt2[0],pnt2[1]);

            glVertex2f(pnt3[0],pnt3[1]);
            glVertex2f(pnt4[0],pnt4[1]);
        glEnd();

    }
    else if (this->datumtype.getValue() == SYMMETRIC) {

        SbVec3f p1 = points[0];
        SbVec3f p2 = points[1];

        SbVec3f dir = (p2-p1);
        dir.normalize();
        SbVec3f normal (-dir[1],dir[0],0);

        float margin = this->imgHeight / 4.0;

        // Calculate coordinates for the first arrow
        SbVec3f ar0, ar1, ar2;
        ar0  = p1 + dir * 4 * margin; // Tip of Arrow
        ar1  = ar0 - dir * 0.866f * 2 * margin;
        ar2  = ar1 + normal * margin;
        ar1 -= normal * margin;

        glBegin(GL_LINES);
            glVertex3f(p1[0], p1[1], ZCONSTR);
            glVertex3f(ar0[0], ar0[1], ZCONSTR);
            glVertex3f(ar0[0], ar0[1], ZCONSTR);
            glVertex3f(ar1[0], ar1[1], ZCONSTR);
            glVertex3f(ar0[0], ar0[1], ZCONSTR);
            glVertex3f(ar2[0], ar2[1], ZCONSTR);
        glEnd();

        // Calculate coordinates for the second arrow
        SbVec3f ar3, ar4, ar5;
        ar3  = p2 - dir * 4 * margin; // Tip of 2nd Arrow
        ar4  = ar3 + dir * 0.866f * 2 * margin;
        ar5  = ar4 + normal * margin;
        ar4 -= normal * margin;

        glBegin(GL_LINES);
            glVertex3f(p2[0], p2[1], ZCONSTR);
            glVertex3f(ar3[0], ar3[1], ZCONSTR);
            glVertex3f(ar3[0], ar3[1], ZCONSTR);
            glVertex3f(ar4[0], ar4[1], ZCONSTR);
            glVertex3f(ar3[0], ar3[1], ZCONSTR);
            glVertex3f(ar5[0], ar5[1], ZCONSTR);
        glEnd();
    }
    else if (this->datumtype.getValue() == ARCLENGTH) {
        ArcLengthGeometry geom;
        if (arcLengthGeometry(geom)) {
            const float mid = (geom.startangle + geom.endangle) / 2;
            angle = geom.textAngle;
            const float shift = arcLengthShiftSign(angle, mid) * this->imgHeight;
            textOffset = geom.textOffset + SbVec3f(-sin(angle), cos(angle), 0.f) * shift;

            const float range = geom.endangle - geom.startangle;
            int countSegments = std::max(6, abs(int(50.0 * range / (2 * M_PI))));
            double segment = range / (countSegments - 1);
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < countSegments; i++) {
                double theta = geom.startangle + segment * i;
                SbVec3f v = geom.arcCenter
                    + SbVec3f(geom.arcRadius * cos(theta), geom.arcRadius * sin(theta), 0);
                glVertex2f(v[0], v[1]);
            }
            glEnd();

            glBegin(GL_LINES);
                glVertex2f(geom.pnt1[0], geom.pnt1[1]);
                glVertex2f(geom.pnt2[0], geom.pnt2[1]);
                glVertex2f(geom.pnt3[0], geom.pnt3[1]);
                glVertex2f(geom.pnt4[0], geom.pnt4[1]);
            glEnd();

            // Tips on the extension lines, bodies along the arc, sized as
            // the distance labels' are.
            float margin = this->imgHeight / 4.0f;
            auto arrow = [&](const SbVec3f& tip, const SbVec3f& back, const SbVec3f& side) {
                SbVec3f base = tip + back * (0.866f * 2 * margin);
                SbVec3f a = base + side * margin;
                SbVec3f b = base - side * margin;
                glVertex2f(tip[0], tip[1]);
                glVertex2f(a[0], a[1]);
                glVertex2f(b[0], b[1]);
            };
            glBegin(GL_TRIANGLES);
                arrow(geom.pnt2,
                      SbVec3f(-sin(geom.startangle), cos(geom.startangle), 0.f),
                      SbVec3f(cos(geom.startangle), sin(geom.startangle), 0.f));
                arrow(geom.pnt4,
                      SbVec3f(sin(geom.endangle), -cos(geom.endangle), 0.f),
                      SbVec3f(cos(geom.endangle), sin(geom.endangle), 0.f));
            glEnd();
        }
    }

    if (hasText) {
        //Get the camera z-direction
        const SbViewVolume & vv = SoViewVolumeElement::get(state);
        SbVec3f z = vv.zVector();
        bool backfacing = norm.getValue().dot(z) < 0.f;

        const unsigned char * dataptr = this->image.getValue(imgsize, nc);

        static bool init = false;
        static bool npot = false;
        if (!init) {
            init = true;
            std::string ext = (const char*)(glGetString(GL_EXTENSIONS));
            npot = (ext.find("GL_ARB_texture_non_power_of_two") != std::string::npos);
        }

        int w = srcw;
        int h = srch;
        if (!npot) {
            // make power of two
            if ((w & (w-1)) != 0) {
                int i=1;
                while (i < 8) {
                    if ((w >> i) == 0)
                        break;
                    i++;
                }
                w = (1 << i);
            }
            // make power of two
            if ((h & (h-1)) != 0) {
                int i=1;
                while (i < 8) {
                    if ((h >> i) == 0)
                        break;
                    i++;
                }
                h = (1 << i);
            }
        }

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D); // Enable Textures
        glEnable(GL_BLEND);

        // glGenTextures/glBindTexture was commented out but it must be active, see:
        // #0000971: Tracing over a background image in Sketcher: image is overwritten by first dimensional constraint text
        // #0001185: Planer image changes to number graphic when a part design constraint is made after the planar image
        //
        // Copy the text bitmap into memory and bind
        GLuint myTexture;
        // generate a texture
        glGenTextures(1, &myTexture);
        glBindTexture(GL_TEXTURE_2D, myTexture);

        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        if (!npot) {
            QImage imagedata(w, h,QImage::Format_ARGB32_Premultiplied);
            imagedata.fill(0x00000000);
            int sx = (w - srcw)/2;
            int sy = (h - srch)/2;
            glTexImage2D(GL_TEXTURE_2D, 0, nc, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const GLvoid*)imagedata.bits());
            glTexSubImage2D(GL_TEXTURE_2D, 0, sx, sy, srcw, srch, GL_RGBA, GL_UNSIGNED_BYTE,(const GLvoid*)  dataptr);
        }
        else {
            glTexImage2D(GL_TEXTURE_2D, 0, nc, srcw, srch, 0, GL_RGBA, GL_UNSIGNED_BYTE,(const GLvoid*)  dataptr);
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();

        // Apply a rotation and translation matrix
        glTranslatef(textOffset[0],textOffset[1], textOffset[2]);
        glRotatef((GLfloat) angle * 180 / M_PI, 0,0,1);
        glBegin(GL_QUADS);

        glColor3f(1.f, 1.f, 1.f);

        SbVec3f img1 = SbVec3f(-this->imgWidth / 2, -this->imgHeight / 2, 0.f);
        SbVec3f img2 = SbVec3f(-this->imgWidth / 2,  this->imgHeight / 2, 0.f);
        SbVec3f img3 = SbVec3f( this->imgWidth / 2, -this->imgHeight / 2, 0.f);

        // Rotate through an angle
        float s = sin(angle);
        float c = cos(angle);

        img1 = SbVec3f((img1[0] * c) - (img1[1] * s), (img1[0] * s) + (img1[1] * c), 0.f);
        img2 = SbVec3f((img2[0] * c) - (img2[1] * s), (img2[0] * s) + (img2[1] * c), 0.f);
        img3 = SbVec3f((img3[0] * c) - (img3[1] * s), (img3[0] * s) + (img3[1] * c), 0.f);

        SbBool identity;
        const SbMatrix & mm = SoModelMatrixElement::get(state, identity);
        if (!identity) {
            mm.multVecMatrix(img1, img1);
            mm.multVecMatrix(img2, img2);
            mm.multVecMatrix(img3, img3);
        }
        const SbMatrix & vm = SoViewingMatrixElement::get(state);
        vm.multVecMatrix(img1, img1);
        vm.multVecMatrix(img2, img2);
        vm.multVecMatrix(img3, img3);

        float det = mm.det3();
        float margin = det < 0.f ? -1e-3f : 1e-3f;

        float xfactor = img1[0] - img3[0] < margin ? 0.5f : -0.5f;
        float yfactor = img1[1] - img2[1] < margin ? 0.5f : -0.5f;

        bool flip = backfacing ? xfactor*yfactor > 0.f : xfactor*yfactor < 0.f;
        if (det < 0.f) // To check if there's any reflection. Is it reliable?
            flip = !flip;
        if (flip)
            xfactor = -xfactor;
            
        glTexCoord2f(0.f, 1.f); glVertex2f( -this->imgWidth * xfactor,  this->imgHeight * yfactor);
        glTexCoord2f(0.f, 0.f); glVertex2f( -this->imgWidth * xfactor, -this->imgHeight * yfactor);
        glTexCoord2f(1.f, 0.f); glVertex2f( this->imgWidth * xfactor, -this->imgHeight * yfactor);
        glTexCoord2f(1.f, 1.f); glVertex2f( this->imgWidth * xfactor,  this->imgHeight * yfactor);

        glEnd();

        // Reset the Mode
        glPopMatrix();

        // wmayer: see bug report below which is caused by generating but not
        // deleting the texture.
        // #0000721: massive memory leak when dragging an unconstrained model
        glDeleteTextures(1, &myTexture);
    }

    glPopAttrib();
    state->pop();
}

void SoDatumLabel::setPoints(SbVec3f p1, SbVec3f p2)
{
    pnts.setNum(2);
    SbVec3f* verts = pnts.startEditing();
    verts[0] = p1;
    verts[1] = p2;
    pnts.finishEditing();
}

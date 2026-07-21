/***************************************************************************
 *   Copyright (c) 2010 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
#  include <windows.h>
# endif
# ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
# else
# include <GL/gl.h>
# endif

# include <Inventor/actions/SoCallbackAction.h>
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/actions/SoGLRenderAction.h>
# include <Inventor/bundles/SoMaterialBundle.h>
# include <Inventor/bundles/SoTextureCoordinateBundle.h>
# include <Inventor/elements/SoLazyElement.h>
# include <Inventor/elements/SoModelMatrixElement.h>
# include <Inventor/elements/SoViewportRegionElement.h>
# include <Inventor/elements/SoViewVolumeElement.h>
# include <Inventor/nodekits/SoShapeKit.h>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoCone.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoCube.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoFontStyle.h>
# include <Inventor/nodes/SoLineSet.h>
# include <Inventor/nodes/SoPointSet.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoText2.h>
# include <Inventor/nodes/SoTranslation.h>
#endif

#include <Inventor/engines/SoCalculator.h>

#include "SoAxisCrossKit.h"
#include "Inventor/SoAutoZoomTranslation.h"
#include "SoTextImage.h"


using namespace Gui;


SO_KIT_SOURCE(SoShapeScale)

//  Constructor.
SoShapeScale::SoShapeScale()
{
    SO_KIT_CONSTRUCTOR(SoShapeScale);

    SO_KIT_ADD_FIELD(active, (true));
    SO_KIT_ADD_FIELD(scaleFactor, (1.0f));

    SO_KIT_ADD_CATALOG_ENTRY(topSeparator, SoSeparator, false, this, "", false);
    SO_KIT_ADD_CATALOG_ABSTRACT_ENTRY(shape, SoNode, SoCube, true, topSeparator, "", true);
    SO_KIT_ADD_CATALOG_ENTRY(scale, SoAutoZoomTranslation, false, topSeparator, shape, false);

    SO_KIT_INIT_INSTANCE();

    auto scale = static_cast<SoAutoZoomTranslation*>(this->getAnyPart(SbName("scale"), true));
    scale->scaleFactor = 0.2f;

    SoCalculator *engine = new SoCalculator();
    engine->a.connectFrom(&scaleFactor);
    engine->expression.set1Value(0, "oa = 0.2 * a");
    scale->scaleFactor.connectFrom(&engine->oa);
}

// Destructor.
SoShapeScale::~SoShapeScale() = default;

void
SoShapeScale::initClass()
{
    SO_KIT_INIT_CLASS(SoShapeScale, SoBaseKit, "BaseKit");
}

void
SoShapeScale::GLRender(SoGLRenderAction * action)
{
    // Auto scale is now done with SoAutoZoomTranslation
#if 0
    SoState * state = action->getState();

    SoScale * scale = static_cast<SoScale*>(this->getAnyPart(SbName("scale"), true));
    if (!this->active.getValue()) {
        SbVec3f v(1.0f, 1.0f, 1.0f);
        if (scale->scaleFactor.getValue() != v)
            scale->scaleFactor = v;
    }
    else {
        const SbViewportRegion & vp = SoViewportRegionElement::get(state);
        const SbViewVolume & vv = SoViewVolumeElement::get(state);
        SbVec3f center(0.0f, 0.0f, 0.0f);
        float nsize = this->scaleFactor.getValue() / float(vp.getViewportSizePixels()[1]);
        SoModelMatrixElement::get(state).multVecMatrix(center, center); // world coords
        float sf = vv.getWorldToScreenScale(center, nsize);
        SbVec3f v(sf, sf, sf);
        if (scale->scaleFactor.getValue() != v)
            scale->scaleFactor = v;
    }
#endif

    inherited::GLRender(action);
}

// --------------------------------------------------------------

SO_KIT_SOURCE(SoAxisCrossKit)

void
SoAxisCrossKit::initClass()
{
   SO_KIT_INIT_CLASS(SoAxisCrossKit,SoBaseKit, "BaseKit");
}

SoAxisCrossKit::SoAxisCrossKit()
{
   SO_KIT_CONSTRUCTOR(SoAxisCrossKit);

   // Add the parts to the catalog...
   SO_KIT_ADD_CATALOG_ENTRY(xAxis, SoShapeKit,
                            true, this,"", true);
   SO_KIT_ADD_CATALOG_ENTRY(xHead, SoShapeKit,
                            true, this,"", true);
   SO_KIT_ADD_CATALOG_ENTRY(yAxis, SoShapeKit,
                            true, this,"", true);
   SO_KIT_ADD_CATALOG_ENTRY(yHead, SoShapeKit,
                            true, this,"", true);
   SO_KIT_ADD_CATALOG_ENTRY(zAxis, SoShapeKit,
                            true, this,"", true);
   SO_KIT_ADD_CATALOG_ENTRY(zHead, SoShapeKit,
                            true, this,"", true);

   SO_KIT_INIT_INSTANCE();

   createAxes();
}

SoAxisCrossKit::~SoAxisCrossKit() = default;

// This kit is made up entirely of SoShapeKits.
// Since SoShapeKits do not affect state, neither does this.
SbBool
SoAxisCrossKit::affectsState() const
{
   return false;
}

void SoAxisCrossKit::addWriteReference(SoOutput * /*out*/, SbBool /*isfromfield*/)
{
    // this node should not be written out to a file
}

void SoAxisCrossKit::getBoundingBox(SoGetBoundingBoxAction * action)
{
    inherited::getBoundingBox(action);
    action->resetCenter();
    action->setCenter(SbVec3f(0,0,0), false);
}

// Set up parts for default configuration of the jumping jack
void
SoAxisCrossKit::createAxes()
{
   // Create the heads.
   auto head = new SoCone;
   head->bottomRadius.setValue(5);
   head->height.setValue(10);
   setPart("xHead.shape", head);
   setPart("yHead.shape", head);
   setPart("zHead.shape", head);

   // Create the axes.
   auto coords = new SoCoordinate3;
   coords->point.set1Value(0, SbVec3f(0,0,0));
   coords->point.set1Value(1, SbVec3f(90,0,0));
   setPart("xAxis.coordinate3", coords);
   setPart("yAxis.coordinate3", coords);
   setPart("zAxis.coordinate3", coords);

   auto shape = new SoLineSet;
   setPart("xAxis.shape", shape);
   setPart("yAxis.shape", shape);
   setPart("zAxis.shape", shape);

   // Place the axes and heads
   set("yAxis.transform", "rotation 0 0 1 1.5707999");
   set("zAxis.transform", "rotation 0 1 0 -1.5707999");

   set("xHead.transform", "translation 95 0 0");
   set("xHead.transform", "scaleFactor 0.5 1.5 0.5");
   set("xHead.transform", "rotation 0 0 -1  1.5707999");

   set("yHead.transform", "translation 0 95 0");
   set("yHead.transform", "scaleFactor 0.5 1.5 0.5");
   set("yHead.transform", "rotation 0 0 1 0");

   set("zHead.transform", "translation 0 0 95");
   set("zHead.transform", "scaleFactor 0.5 1.5 0.5");
   set("zHead.transform", "rotation 1 0 0  1.5707999");

   // Set colors & styles
   set("xAxis.appearance.lightModel", "model BASE_COLOR");
   set("xHead.appearance.lightModel", "model BASE_COLOR");
   set("yAxis.appearance.lightModel", "model BASE_COLOR");
   set("yHead.appearance.lightModel", "model BASE_COLOR");
   set("zAxis.appearance.lightModel", "model BASE_COLOR");
   set("zHead.appearance.lightModel", "model BASE_COLOR");
   set("xAxis.appearance.drawStyle", "lineWidth 1");
   set("yAxis.appearance.drawStyle", "lineWidth 1");
   set("zAxis.appearance.drawStyle", "lineWidth 1");
   set("xAxis.appearance.material", "diffuseColor 0.5 0.125 0.125");
   set("xHead.appearance.material", "diffuseColor 0.5 0.125 0.125");
   set("yAxis.appearance.material", "diffuseColor 0.125 0.5 0.125");
   set("yHead.appearance.material", "diffuseColor 0.125 0.5 0.125");
   set("zAxis.appearance.material", "diffuseColor 0.125 0.125 0.5");
   set("zHead.appearance.material", "diffuseColor 0.125 0.125 0.5");

   // Make unpickable
   set("xAxis.pickStyle", "style UNPICKABLE");
   set("xHead.pickStyle", "style UNPICKABLE");
   set("yAxis.pickStyle", "style UNPICKABLE");
   set("yHead.pickStyle", "style UNPICKABLE");
   set("zAxis.pickStyle", "style UNPICKABLE");
   set("zHead.pickStyle", "style UNPICKABLE");
}

// --------------------------------------------------------------

SO_NODE_SOURCE(SoRegPoint)

void SoRegPoint::initClass()
{
    SO_NODE_INIT_CLASS(SoRegPoint, SoShape, "Shape");
}

SoRegPoint::SoRegPoint()
{
    SO_NODE_CONSTRUCTOR(SoRegPoint);

    SO_NODE_ADD_FIELD(base, (SbVec3f(0,0,0)));
    SO_NODE_ADD_FIELD(normal, (SbVec3f(1,1,1)));
    SO_NODE_ADD_FIELD(length, (3.0));
    SO_NODE_ADD_FIELD(color, (1.0f, 0.447059f, 0.337255f));
    SO_NODE_ADD_FIELD(text, (""));

    SbVec3f p1 = base.getValue();
    SbVec3f p2 = p1 + normal.getValue() * length.getValue();

    root = new SoSeparator();
    root->ref();
    root->renderCaching = SoSeparator::OFF;
    root->boundingBoxCaching = SoSeparator::OFF;

    // Probe colour for the leader line and the two point markers (real Coin
    // geometry so the render-cache backend captures them like any other node,
    // instead of the old raw-GL GLRender that produced nothing for bgfx/WASM).
    probeColor = new SoBaseColor();
    probeColor->rgb.setValue(this->color.getValue());
    root->addChild(probeColor);

    // leader line p1 -> p2
    auto lineStyle = new SoDrawStyle();
    lineStyle->lineWidth = 1.0f;
    root->addChild(lineStyle);
    lineCoords = new SoCoordinate3();
    lineCoords->point.setNum(2);
    lineCoords->point.set1Value(0, p1);
    lineCoords->point.set1Value(1, p2);
    root->addChild(lineCoords);
    auto lineSet = new SoLineSet();
    lineSet->numVertices.setValue(2);
    root->addChild(lineSet);

    // base point marker (larger)
    auto baseStyle = new SoDrawStyle();
    baseStyle->pointSize = 5.0f;
    root->addChild(baseStyle);
    baseCoords = new SoCoordinate3();
    baseCoords->point.setNum(1);
    baseCoords->point.set1Value(0, p1);
    root->addChild(baseCoords);
    root->addChild(new SoPointSet());

    // tip point marker (smaller)
    auto tipStyle = new SoDrawStyle();
    tipStyle->pointSize = 2.0f;
    root->addChild(tipStyle);
    tipCoords = new SoCoordinate3();
    tipCoords->point.setNum(1);
    tipCoords->point.set1Value(0, p2);
    root->addChild(tipCoords);
    root->addChild(new SoPointSet());

    // text label at the tip
    textMove = new SoTranslation();
    textMove->translation.setValue(p2);
    root->addChild(textMove);

    auto font = new SoFontStyle;
    font->size = 14;

    auto sub = new SoSeparator();
    sub->addChild(font);
    label = new SoText2();
    sub->addChild(label);
    // Companion glyph quad so the probe text is drawn by the render-cache backend
    // (bgfx / WASM) too; captured via generatePrimitives() traversing root below.
    sub->addChild(SoTextImage::createFor(label, font));
    root->addChild(sub);
}

SoRegPoint::~SoRegPoint()
{
    root->unref();
}

/**
 * Renders the probe (leader line + point markers + text label) — all now real
 * Coin geometry under root, so GL and the render-cache backend draw the same.
 */
void SoRegPoint::GLRender(SoGLRenderAction *action)
{
    if (shouldGLRender(action))
        root->GLRender(action);
}

void SoRegPoint::generatePrimitives(SoAction* action)
{
    // Feed the render-cache capture (and any other traversal): the line, points
    // and text companion under root emit real primitives for the bgfx/WASM
    // backend. Inert on the ray-pick path (root has no pickable text box).
    if (action->isOfType(SoCallbackAction::getClassTypeId()))
        root->doAction(action);
}

/**
 * Sets the bounding box of the probe to \a box and its center to \a center.
 */
void SoRegPoint::computeBBox(SoAction * /*action*/, SbBox3f &box, SbVec3f &center)
{
    // The leader endpoints define the extent; the screen-space text adds a
    // negligible amount. (Do NOT run the bbox action over root here — its real
    // line/point geometry would set the action's centre, tripping the
    // single-setCenter assertion in SoGetBoundingBoxAction.)
    SbVec3f p1 = base.getValue();
    SbVec3f p2 = p1 + normal.getValue() * length.getValue();

    box.setBounds(p1, p1);
    box.extendBy(p2);

    center = box.getCenter();
}

void SoRegPoint::notify(SoNotList * node)
{
    SoField * f = node->getLastField();
    if ((f == &this->base || f == &this->normal || f == &this->length)
        && lineCoords) {
        SbVec3f p1 = base.getValue();
        SbVec3f p2 = p1 + normal.getValue() * length.getValue();
        lineCoords->point.set1Value(0, p1);
        lineCoords->point.set1Value(1, p2);
        baseCoords->point.set1Value(0, p1);
        tipCoords->point.set1Value(0, p2);
        textMove->translation.setValue(p2);
    }
    else if (f == &this->color && probeColor) {
        probeColor->rgb = this->color.getValue();
    }
    else if (f == &this->text && label) {
        label->string = this->text.getValue();
    }

    SoShape::notify(node);
}

/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
#include <cfloat>
#include <cmath>

#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoGetMatrixAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoCamera.h>
#endif

#include <Gui/Inventor/SoFCZoomOffsetElement.h>

#include "SoZoomTranslation.h"


// *************************************************************************

using namespace SketcherGui;

// ------------------------------------------------------

SO_NODE_SOURCE(SoZoomTranslation)

void SoZoomTranslation::initClass()
{
    SO_NODE_INIT_CLASS(SoZoomTranslation, SoTranslation, "Translation");

    // Enable elements for SoGetMatrixAction (#0002268)
    // SoCamera::initClass() enables the SoViewVolumeElement for
    // * SoGLRenderAction
    // * SoGetBoundingBoxAction
    // * SoRayPickAction
    // * SoCallbackAction
    // * SoGetPrimitiveCountAction
    // The element SoViewportRegionElement is enabled by the
    // above listed actions.
    // Additionally, SoViewVolumeElement is enabled for
    // * SoAudioRenderAction
    // * SoHandleEventAction
    // And SoViewportRegionElement is enabled for
    // * SoHandleEventAction
    // * SoGetMatrixAction
    SO_ENABLE(SoGetMatrixAction, SoViewVolumeElement);
}

float SoZoomTranslation::calculateScaleFactor(SoAction* action) const
{
    // Dividing by 5 seems to work well
    SbViewVolume vv = SoViewVolumeElement::get(action->getState());
    float aspectRatio = SoViewportRegionElement::get(action->getState()).getViewportAspectRatio();
    scaleFactor = vv.getWorldToScreenScale(SbVec3f(0.f, 0.f, 0.f), 0.1f) / (5 * aspectRatio);
    return scaleFactor;
}

SoZoomTranslation::SoZoomTranslation()
    : scaleFactor(0)
{
    SO_NODE_CONSTRUCTOR(SoZoomTranslation);
    SO_NODE_ADD_FIELD(abPos, (SbVec3f(0.f, 0.f, 0.f)));
    SO_NODE_ADD_FIELD(pixelOffset, (SbVec2f(0.f, 0.f)));
}

SbVec3f SoZoomTranslation::pixelVector(SoAction* action) const
{
    const SbVec2f px = this->pixelOffset.getValue();
    if (px == SbVec2f(0.f, 0.f)) {
        return SbVec3f(0.f, 0.f, 0.f);
    }
    // the model length of one pixel: the view volume's width over the
    // viewport's, at the origin as calculateScaleFactor takes it
    SbViewVolume vv = SoViewVolumeElement::get(action->getState());
    const SbVec2s size =
        SoViewportRegionElement::get(action->getState()).getViewportSizePixels();
    if (size[0] <= 0) {
        return SbVec3f(0.f, 0.f, 0.f);
    }
    const float perPixel = vv.getWorldToScreenScale(SbVec3f(0.f, 0.f, 0.f), 1.f) / size[0];
    return SbVec3f(px[0] * perPixel, px[1] * perPixel, 0.f);
}

void SoZoomTranslation::GLRender(SoGLRenderAction* action)
{
    SoZoomTranslation::doAction((SoAction*)action);
}

// Doc in superclass.
void SoZoomTranslation::doAction(SoAction* action)
{
    SbVec3f v;
    if (this->translation.getValue() == SbVec3f(0.0f, 0.0f, 0.0f)
        && this->abPos.getValue() == SbVec3f(0.0f, 0.0f, 0.0f)
        && this->pixelOffset.getValue() == SbVec2f(0.0f, 0.0f)) {
        return;
    }

    SbVec3f absVtr = this->abPos.getValue();
    SbVec3f relVtr = this->translation.getValue();

    // Render-cache capture (bgfx/WASM backends): the scale factor is
    // recomputed from the view every GL frame, but a capture bakes it into
    // static caches where the offset then scales WITH the camera zoom.
    // Deposit the zoom-scaled part in the state instead — the SoImage
    // capture companion folds it into its quad as a constant pixel offset
    // — and apply only the absolute part plus the unscaled z layer here.
    if (SoFCZoomOffsetElement::isCapturing()
        && action->isOfType(SoCallbackAction::getClassTypeId())) {
        SoFCZoomOffsetElement::add(action->getState(),
                                        SbVec2f(relVtr[0], relVtr[1]));
        SoFCZoomOffsetElement::addPixels(action->getState(), this->pixelOffset.getValue());
        v = absVtr + SbVec3f(0.f, 0.f, relVtr[2]);
        SoModelMatrixElement::translateBy(action->getState(), this, v);
        return;
    }

    float sf = this->calculateScaleFactor(action);
    // For Sketcher Keep Z value the same
    relVtr[0] = (relVtr[0] != 0) ? sf * relVtr[0] : 0;
    relVtr[1] = (relVtr[1] != 0) ? sf * relVtr[1] : 0;

    v = absVtr + relVtr + pixelVector(action);

    SoModelMatrixElement::translateBy(action->getState(), this, v);
}

void SoZoomTranslation::getMatrix(SoGetMatrixAction* action)
{
    SbVec3f v;
    if (this->translation.getValue() == SbVec3f(0.0f, 0.0f, 0.0f)
        && this->abPos.getValue() == SbVec3f(0.0f, 0.0f, 0.0f)
        && this->pixelOffset.getValue() == SbVec2f(0.0f, 0.0f)) {
        return;
    }
    else {
        SbVec3f absVtr = this->abPos.getValue();
        SbVec3f relVtr = this->translation.getValue();

        float sf = this->calculateScaleFactor(action);
        // For Sketcher Keep Z value the same
        relVtr[0] = (relVtr[0] != 0) ? sf * relVtr[0] : 0;
        relVtr[1] = (relVtr[1] != 0) ? sf * relVtr[1] : 0;

        v = absVtr + relVtr + pixelVector(action);
    }

    SbMatrix m;
    m.setTranslate(v);
    action->getMatrix().multLeft(m);
    m.setTranslate(-v);
    action->getInverse().multRight(m);
}

void SoZoomTranslation::callback(SoCallbackAction* action)
{
    SoZoomTranslation::doAction((SoAction*)action);
}

void SoZoomTranslation::getBoundingBox(SoGetBoundingBoxAction* action)
{
    SoZoomTranslation::doAction((SoAction*)action);
}

void SoZoomTranslation::pick(SoPickAction* action)
{
    SoZoomTranslation::doAction((SoAction*)action);
}

// Doc in superclass.
void SoZoomTranslation::getPrimitiveCount(SoGetPrimitiveCountAction* action)
{
    SoZoomTranslation::doAction((SoAction*)action);
}

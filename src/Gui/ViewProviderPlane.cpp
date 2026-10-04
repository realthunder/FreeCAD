/***************************************************************************
 *   Copyright (c) 2012 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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
# include <Inventor/nodes/SoAsciiText.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoFaceSet.h>
# include <Inventor/nodes/SoIndexedLineSet.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoShapeHints.h>
# include <Inventor/nodes/SoSwitch.h>
# include <Inventor/nodes/SoTranslation.h>
# include <Inventor/nodes/SoVertexProperty.h>
# include <Inventor/SbColor.h>
#endif

#include <App/Document.h>

#include "SoFCUnifiedSelection.h"
#include "SoFCSelection.h"
#include "ViewProviderPlane.h"
#include "ViewProviderCoordinateSystem.h"


using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderPlane, Gui::ViewProviderDatum)


ViewProviderPlane::ViewProviderPlane()
    : SelectionObserver(false)
{
    sPixmap = "Std_Plane";
}

ViewProviderPlane::~ViewProviderPlane() = default;

void ViewProviderPlane::attach ( App::DocumentObject *obj ) {
    ViewProviderDatum::attach ( obj );

    // indexes used to create the edges
    static const int32_t lines[6] = { 0, 1, 2, 3, 0, -1 };

    SoSeparator *sep = getDatumRoot ();

    pCoords = new SoCoordinate3 ();
    sep->addChild ( pCoords );

    auto pLines  = new SoIndexedLineSet ();
    pLines->coordIndex.setNum(6);
    pLines->coordIndex.setValues(0, 6, lines);
    sep->addChild ( pLines );

    // add semi transparent face
    auto faceSeparator = new SoFCLatePickGroup();
    pHighlight->addChild(faceSeparator);

    auto material = new SoMaterial();
    material->transparency.setValue(0.95f);
    SbColor color;
    float alpha = 0.0f;
    color.setPackedValue(ViewProviderCoordinateSystem::defaultColor, alpha);
    material->ambientColor.setValue(color);
    material->diffuseColor.setValue(color);
    faceSeparator->addChild(material);

    // disable backface culling and render with two-sided lighting
    auto shapeHints = new SoShapeHints();
    shapeHints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
    shapeHints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
    faceSeparator->addChild(shapeHints);

    auto faceSet = new SoFaceSet();
    pFaceVertices = new SoVertexProperty();
    faceSet->vertexProperty.setValue(pFaceVertices);
    faceSeparator->addChild(faceSet);

    pTextTranslation = new SoTranslation ();
    sep->addChild ( pTextTranslation );

    auto ps = new SoPickStyle();
    ps->style.setValue(SoPickStyle::BOUNDING_BOX);
    sep->addChild(ps);

    sep->addChild ( pLabelSwitch );

    updateDatumSize();
    attachSelection();
}

void ViewProviderPlane::updateDatumSize()
{
    if (!pCoords)
        return;

    SbVec3f verts[4];
    if (!screenSize) {
        const float size = ViewProviderCoordinateSystem::baseSize ();
        verts[0] = SbVec3f(size, size, 0);
        verts[1] = SbVec3f(size, -size, 0);
        verts[2] = SbVec3f(-size, -size, 0);
        verts[3] = SbVec3f(-size, size, 0);
        pTextTranslation->translation.setValue(SbVec3f(-size * 49.f / 50.f, size * 9.f / 10.f, 0));
        pLabel->justification = SoAsciiText::LEFT;
    }
    else {
        // upstream's layout, in screen units (upstream b942275957)
        const float size = screenPlaneSize();
        const float offset = 8.0f;
        if (!getRole().empty() && !isSelected && !isHovered) {
            verts[0] = SbVec3f(size, size, 0);
            verts[1] = SbVec3f(size, offset, 0);
            verts[2] = SbVec3f(offset, offset, 0);
            verts[3] = SbVec3f(offset, size, 0);
        }
        else {
            verts[0] = SbVec3f(size, size, 0);
            verts[1] = SbVec3f(size, -size, 0);
            verts[2] = SbVec3f(-size, -size, 0);
            verts[3] = SbVec3f(-size, size, 0);
        }
        pTextTranslation->translation.setValue(verts[0] / 2 - SbVec3f(2, 6, 0));
        pLabel->justification = SoAsciiText::RIGHT;
    }
    pCoords->point.setNum(4);
    pCoords->point.setValues(0, 4, verts);
    pFaceVertices->vertex.setNum(4);
    pFaceVertices->vertex.setValues(0, 4, verts);
}

void ViewProviderPlane::onSelectionChanged(const SelectionChanges& msg)
{
    if (!screenSize || getRole().empty())
        return;

    auto obj = getObject();
    if (!obj || !obj->isAttachedToDocument())
        return;

    bool before = isSelected || isHovered;
    if (msg.Type == SelectionChanges::ClrSelection) {
        isSelected = false;
    }
    else if (msg.pDocName && msg.pObjectName
            && strcmp(msg.pDocName, obj->getDocument()->getName()) == 0
            && strcmp(msg.pObjectName, obj->getNameInDocument()) == 0) {
        isSelected = Selection().isSelected(obj);
    }
    const auto &presel = Selection().getPreselection();
    isHovered = presel.Object.getSubObject() == obj || presel.Object.getObject() == obj;

    if (before != (isSelected || isHovered))
        updateDatumSize();
}

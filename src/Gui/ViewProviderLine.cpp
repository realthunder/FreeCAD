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
# include <Inventor/nodes/SoIndexedLineSet.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoSwitch.h>
# include <Inventor/nodes/SoTranslation.h>
#endif

#include "ViewProviderLine.h"
#include "ViewProviderCoordinateSystem.h"


using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderLine, Gui::ViewProviderDatum)


ViewProviderLine::ViewProviderLine()
{
    sPixmap = "Std_Axis";
}

ViewProviderLine::~ViewProviderLine() = default;

void ViewProviderLine::attach ( App::DocumentObject *obj ) {
    ViewProviderDatum::attach ( obj );

    // indexes used to create the edges
    static const int32_t lines[4] = { 0, 1, -1 };

    SoSeparator *sep = getDatumRoot ();

    pCoords = new SoCoordinate3 ();
    sep->addChild ( pCoords );

    auto pLines  = new SoIndexedLineSet ();
    pLines->coordIndex.setNum(3);
    pLines->coordIndex.setValues(0, 3, lines);
    sep->addChild ( pLines );

    pTextTranslation = new SoTranslation ();
    sep->addChild ( pTextTranslation );

    auto ps = new SoPickStyle();
    ps->style.setValue(SoPickStyle::BOUNDING_BOX);
    sep->addChild(ps);

    sep->addChild ( pLabelSwitch );

    updateDatumSize();
}

void ViewProviderLine::updateDatumSize()
{
    if (!pCoords)
        return;

    SbVec3f verts[2];
    if (!screenSize) {
        const float size = ViewProviderCoordinateSystem::baseSize ();
        verts[0] = SbVec3f(size, 0, 0);
        verts[1] = SbVec3f(-size, 0, 0);
        pTextTranslation->translation.setValue(SbVec3f(-size * 49.f / 50.f, size / 30.f, 0));
        pLabel->justification = SoAsciiText::LEFT;
    }
    else {
        // upstream's layout, in screen units: an axis of a coordinate system
        // starts off its origin, a lone line starts at it
        const float size = screenLineSize();
        // App::Line runs along its local X (see Attacher's readLinks())
        const SbVec3f dir(1, 0, 0);
        if (getRole().empty()) {
            verts[0] = dir * 2 * size;
            verts[1] = SbVec3f(0, 0, 0);
        }
        else {
            verts[0] = dir * size;
            verts[1] = dir * 0.2f * size;
        }
        pTextTranslation->translation.setValue(dir * 1.2f * size);
        pLabel->justification = SoAsciiText::CENTER;
    }
    pCoords->point.setNum(2);
    pCoords->point.setValues(0, 2, verts);
}

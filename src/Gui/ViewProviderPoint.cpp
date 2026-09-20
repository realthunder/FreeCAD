// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 Ondsel (PL Boyer) <development@ondsel.com>         *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <Inventor/nodes/SoAsciiText.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoSphere.h>
# include <Inventor/nodes/SoTranslation.h>
#endif

#include <App/Application.h>

#include "ViewProviderPoint.h"
#include "ViewProviderOrigin.h"


using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderPoint, Gui::ViewProviderDatum)


ViewProviderPoint::ViewProviderPoint()
{
    sPixmap = "Std_Point";
}

ViewProviderPoint::~ViewProviderPoint() = default;

void ViewProviderPoint::attach ( App::DocumentObject *obj ) {
    ViewProviderDatum::attach ( obj );

    static const float size = ViewProviderOrigin::baseSize ();

    SoSeparator *sep = getDatumRoot ();

    auto pCoords = new SoCoordinate3 ();
    pCoords->point.setNum (1);
    pCoords->point.setValue ( SbVec3f (0, 0, 0) );
    sep->addChild ( pCoords );

    // A marker rather than a point primitive: an SoPointSet of one vertex
    // draws a single pixel, which is not pickable in practice.
    static const double radius = App::GetApplication()
            .GetParameterGroupByPath ("User parameter:BaseApp/Preferences/View")
            ->GetFloat ("DatumPointSize", 2.5);
    auto sphere = new SoSphere ();
    sphere->radius.setValue ( static_cast<float>(radius) );
    sep->addChild ( sphere );

    auto textTranslation = new SoTranslation ();
    textTranslation->translation.setValue ( SbVec3f ( size / 30., size / 30., 0 ) );
    sep->addChild ( textTranslation );

    auto ps = new SoPickStyle();
    ps->style.setValue(SoPickStyle::BOUNDING_BOX);
    sep->addChild(ps);

    sep->addChild ( getLabel () );
}

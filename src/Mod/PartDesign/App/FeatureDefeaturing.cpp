// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Max Wilfinger
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <Standard_Failure.hxx>
#endif

#include <App/Document.h>
#include <Base/Exception.h>
#include <Mod/Part/App/TopoShape.h>

#include "FeatureDefeaturing.h"


using namespace PartDesign;

PROPERTY_SOURCE(PartDesign::Defeaturing, PartDesign::DressUp)

Defeaturing::Defeaturing() = default;

App::DocumentObjectExecReturn *Defeaturing::execute()
{
    Part::TopoShape baseShape;
    try {
        baseShape = getBaseShape();
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    // In the base's local frame, as the other dress-ups
    baseShape.setTransform(Base::Matrix4D());
    this->positionByBaseFeature();

    auto faces = getFaces(baseShape);
    if (faces.empty()) {
        // Nothing picked yet: the base as it is
        this->Shape.setValue(getSolid(baseShape));
        return App::DocumentObject::StdReturn;
    }

    try {
        Part::TopoShape shape(0, getDocument()->getStringHasher());
        shape.makEDefeaturing(baseShape, faces);
        if (shape.isNull())
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Defeaturing failed: result is null"));
        shape = refineShapeIfActive(shape);
        this->Shape.setValue(getSolid(shape));
        return App::DocumentObject::StdReturn;
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
}

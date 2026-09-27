/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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

#include "FeatureRevolution.h"

using namespace PartDesign;

namespace PartDesign {

/* TRANSLATOR PartDesign::Revolution */

// TwoAngles stays, so that the indices hold, and becomes Two sides with both
// sides Angle (Revolved::onChanged)
const char* Revolution::TypeEnums[]= {"Angle", "UpToLast", "UpToFirst", "UpToFace", "TwoAngles", nullptr};

PROPERTY_SOURCE(PartDesign::Revolution, PartDesign::Revolved)

Revolution::Revolution()
{
    ADD_PROPERTY_TYPE(SideType, (0L), "Revolution", App::Prop_None,
            "How the revolution goes from the profile: to one side, to each side\n"
            "by its own type, or by half the angle to each side");
    SideType.setEnums(SideTypeEnums);
    ADD_PROPERTY_TYPE(Type, (0L), "Revolution", App::Prop_None, "Revolution type");
    Type.setEnums(TypeEnums);
    ADD_PROPERTY_TYPE(Type2, (0L), "Revolution", App::Prop_None, "Revolution type of the second side");
    Type2.setEnums(TypeEnums);
    ADD_PROPERTY_TYPE(Base, (Base::Vector3d(0.0,0.0,0.0)), "Revolution", App::Prop_ReadOnly, "Base");
    ADD_PROPERTY_TYPE(Axis, (Base::Vector3d(0.0,1.0,0.0)), "Revolution", App::Prop_ReadOnly, "Axis");
    ADD_PROPERTY_TYPE(Angle, (360.0), "Revolution", App::Prop_None, "Angle");
    ADD_PROPERTY_TYPE(Angle2, (60.0), "Revolution", App::Prop_None, "Revolution angle of the second side");
    ADD_PROPERTY_TYPE(UpToFace2, (nullptr), "Revolution", App::Prop_None,
                      "Face where the second side of the revolution will end");
    ADD_PROPERTY_TYPE(ReferenceAxis, (nullptr), "Revolution", App::Prop_None, "Reference axis of revolution");
    Type2.setReadOnly(true);
    Angle2.setReadOnly(true);
    UpToFace.setReadOnly(true);
    UpToFace2.setReadOnly(true);
}

App::DocumentObjectExecReturn *Revolution::execute()
{
    return executeRevolved();
}

bool Revolution::suggestReversedAngle(double angle) const
{
    return angle < 0.0;
}

}

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
#ifndef _PreComp_
#endif

#include <boost/algorithm/string/predicate.hpp>

#include "FeaturePocket.h"
#include <Mod/Part/App/PartParams.h>

using namespace PartDesign;

/* TRANSLATOR PartDesign::Pocket */

const char* Pocket::TypeEnums[]= {"Length", "ThroughAll", "UpToFirst", "UpToFace", "TwoLengths", "UpToShape", nullptr};

PROPERTY_SOURCE(PartDesign::Pocket, PartDesign::FeatureExtrude)

Pocket::Pocket()
{
    initAddSubType(FeatureAddSub::Subtractive);
    ADD_PROPERTY_TYPE(Type, (0L), "Pocket", App::Prop_None, "Pocket type");
    Type.setEnums(TypeEnums);
    ADD_PROPERTY_TYPE(_Version,(0),"Part Design",(App::PropertyType)(App::Prop_Hidden), 0);
    initProperties("Pocket");
}

App::DocumentObjectExecReturn *Pocket::execute()
{
    // MakeFace|MakeFuse: because we want a solid.
    // InverseDirection: to inverse the auto detected extrusion direction for
    // backward compatibility to upstream
    ExtrudeOptions options(ExtrudeOption::MakeFace | ExtrudeOption::MakeFuse
                                                   | ExtrudeOption::InverseDirection);
    if (_Version.getValue() < 1) {
        // LegacyPocket to use PrismMode::CutFromBase, i.e. remove material
        // with extrusion, which has poor support for element history. Newer
        // version uses normal boolean cut.
        options |= ExtrudeOption::LegacyPocket;
    }
    return buildExtrusion(options);
}

void Pocket::setupObject()
{
    ProfileBased::setupObject();
    UsePipeForDraft.setValue(Part::PartParams::getUsePipeForExtrusionDraft());
    _Version.setValue(1);
}

void Pocket::setPauseRecompute(bool enable)
{
    if (_Version.getValue() < 1
            && (boost::equals(Type.getValueAsString(), "UpToFirst")
                || boost::equals(Type.getValueAsString(), "UpToFace"))) {
        enable = false;
    }
    inherited::setPauseRecompute(enable);
}


Base::Vector3d Pocket::getProfileNormal() const
{
    auto res = inherited::getProfileNormal();
    // turn around for pockets
    return res * -1;
}

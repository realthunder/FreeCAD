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
# include <BRepBuilderAPI_Copy.hxx>
# include <TopoDS.hxx>
#endif

#include <Base/Placement.h>
#include <App/Document.h>

#include "FeatureFace.h"
#include "FaceMaker.h"
#include "WireJoiner.h"


using namespace Part;


PROPERTY_SOURCE(Part::Face, Part::Feature)

Face::Face()
{
    ADD_PROPERTY(Sources,(nullptr));
    ADD_PROPERTY(FaceMakerClass,("Part::FaceMakerCheese"));//default value here is for legacy documents. Default for new objects is set in setupObject.
    Sources.setSize(0);
    ADD_PROPERTY_TYPE(SplitEdges, (false), "Wire Construction",  App::Prop_None,
            "Whether to split edge if it intersects with other edges");
    ADD_PROPERTY_TYPE(MergeEdges, (true), "Wire Construction",  App::Prop_None,
            "Merge connected edges without branches into wire before searching for closed wires.");
    ADD_PROPERTY_TYPE(TightBound, (false), "Wire Construction",  App::Prop_None,
            "Create only wires that cannot be further splitted into smaller wires");
    ADD_PROPERTY_TYPE(Outline, (false), "Wire Construction",  App::Prop_None,
            "Whether to remove any edges that are shared by more than one wire, effectively creating an outline.");
    ADD_PROPERTY_TYPE(EdgeTolerance, (1e-6), "Wire Construction",  App::Prop_None,
            "Wire construction tolerance");
}

short Face::mustExecute() const
{
    if (FaceMakerClass.isTouched())
        return 1;
    if (Sources.isTouched())
        return 1;
    return Part::Feature::mustExecute();
}

void Face::setupObject()
{
    this->FaceMakerClass.setValue("Part::FaceMakerUnified");
    Feature::setupObject();
}

App::DocumentObjectExecReturn *Face::execute()
{
    std::vector<App::DocumentObject*> links = Sources.getValues();
    if (links.empty())
        return new App::DocumentObjectExecReturn("No shapes linked");

    std::vector<TopoShape> shapes;
    for (std::vector<App::DocumentObject*>::iterator it = links.begin(); it != links.end(); ++it) {
        auto shape = Feature::getTopoShape(*it);
        if (shape.isNull())
            return new App::DocumentObjectExecReturn("Linked shape object is empty");
        shapes.push_back(shape);
    }

    if (SplitEdges.getValue() || Outline.getValue()) {
        WireJoiner joiner;
        joiner.setTolerance(EdgeTolerance.getValue());
        joiner.setTightBound(TightBound.getValue());
        joiner.setOutline(Outline.getValue());
        joiner.setSplitEdges(SplitEdges.getValue());
        joiner.addShape(shapes);
        TopoShape result(0, getDocument()->getStringHasher());
        joiner.getResultWires(result);
        shapes.clear();
        shapes.push_back(result);
    }

    if (FaceMakerClass.getStrValue() == "None") {
        Shape.setValue(TopoShape(0, getDocument()->getStringHasher()).makECompound(shapes, nullptr, false));
        return Part::Feature::execute();
    }

    std::unique_ptr<FaceMaker> facemaker = FaceMaker::ConstructFromType(this->FaceMakerClass.getValue());
    facemaker->MyHasher = getDocument()->getStringHasher();

    for (const auto &shape : shapes) {
        if(links.size() == 1 && shape.getShape().ShapeType() == TopAbs_COMPOUND)
            facemaker->useTopoCompound(shape);
        else
            facemaker->addTopoShape(shape);
    }

    facemaker->Build();
    TopoShape ret = facemaker->getTopoShape();
    if (ret.isNull())
        return new App::DocumentObjectExecReturn("Creating face failed (null shape result)");
    this->Shape.setValue(ret);
    return Part::Feature::execute();
}


/***************************************************************************
 *   Copyright (c) 2008 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <BRepAlgo.hxx>
# include <BRepFilletAPI_MakeFillet.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopTools_ListOfShape.hxx>
# include <ShapeFix_Shape.hxx>
# include <ShapeFix_ShapeTolerance.hxx>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <App/Document.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>

#include "FeatureFillet.h"

FC_LOG_LEVEL_INIT("PartDesign",true,true)

using namespace PartDesign;


PROPERTY_SOURCE(PartDesign::Fillet, PartDesign::DressUp)

const App::PropertyQuantityConstraint::Constraints floatRadius = {0.0,FLT_MAX,0.1};

Fillet::Fillet()
{
    ADD_PROPERTY_TYPE(Radius, (1.0), "Fillet", App::Prop_None, "Fillet radius.");
    ADD_PROPERTY(Segments,());
    Radius.setUnit(Base::Unit::Length);
    Radius.setConstraints(&floatRadius);

    Segments.connectLinkProperty(Base);

    ADD_PROPERTY_TYPE(Corners, (), "Fillet", App::Prop_None,
      "Setback corners, by vertex: each fillet ending at the vertex stops a distance from it,\n"
      "measured along its edge, and one patch tangent to them closes the opening.\n"
      "A corner holds the setback of all its fillets (less than 0 for none) and the setbacks\n"
      "of single fillets by edge, and the depths of faces by face: the patch's boundary on\n"
      "the face bows into it, away from the vertex, that far at its middle. Needs the OCCT fork.");
    Corners.connectLinkProperty(Base);

    ADD_PROPERTY_TYPE(CornerFaces, (nullptr), "Fillet", App::Prop_Hidden,
      "The faces Corners gives depths, kept by the fillet so that their names follow the topology");
    Corners.connectFaceLinkProperty(CornerFaces);

    ADD_PROPERTY_TYPE(UseAllEdges, (false), "Fillet", App::Prop_None,
      "Fillet all edges if true, else use only those edges in Base property.\n"
      "If true, then this overrides any edge changes made to the Base property or in the dialog.\n");
}

short Fillet::mustExecute() const
{
    if (Placement.isTouched()
            || Radius.isTouched()
            || Segments.isTouched()
            || Corners.isTouched())
        return 1;
    return DressUp::mustExecute();
}

App::DocumentObjectExecReturn *Fillet::execute()
{
    Part::TopoShape baseShape;
    try {
        baseShape = getBaseShape();
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    baseShape.setTransform(Base::Matrix4D());

    auto edges = UseAllEdges.getValue() ? baseShape.getSubTopoShapes(TopAbs_EDGE)
                                        : getContinuousEdges(baseShape);
    if (edges.empty())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Fillet not possible on selected shapes"));

    double radius = Radius.getValue();

    if(radius <= 0)
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Fillet radius must be greater than zero"));

    this->positionByBaseFeature();

    try {
        TopoShape shape(0,getDocument()->getStringHasher());

        std::vector<TopoShape::FilletSegments> segmentList;
        std::string sub;
        for (const auto &e : edges) {
            int index = baseShape.findShape(e.getShape());
            segmentList.emplace_back();
            auto &conf = segmentList.back();
            if (index == 0)
                continue;
            sub = "Edge";
            sub += std::to_string(index);
            const auto &segments = Segments.getValue(sub);
            for (const auto &segment : segments)
                conf.emplace_back(segment.param, segment.radius, segment.length);
        }

        shape.makEFillet(baseShape,edges,segmentList,Radius.getValue(),nullptr,getCorners(baseShape));
        if (shape.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Resulting shape is null"));

        TopTools_ListOfShape aLarg;
        aLarg.Append(baseShape.getShape());
        bool failed = false;
        if (!BRepAlgo::IsValid(aLarg, shape.getShape(), Standard_False, Standard_False)) {
            ShapeFix_ShapeTolerance aSFT;
            aSFT.LimitTolerance(shape.getShape(), Precision::Confusion(), Precision::Confusion(), TopAbs_SHAPE);
            // For backward compatibility
            if (FixShape.getValue() == 0) {
                shape.fix();
                if (!BRepAlgo::IsValid(aLarg, shape.getShape(), Standard_False, Standard_False))
                    failed = true;
            }
        }

        if (!failed) {
            shape = refineShapeIfActive(shape);
            shape = getSolid(shape);
        }
        this->Shape.setValue(shape);
        // An invalid result that a fix "repairs" by dropping faces -- the one
        // above, or the property's own (FixShape) as it takes the value --
        // leaves an open shell: valid, and no solid. Features after it would
        // take it for a missing base -- a Pocket then hands back its own tool.
        if (!failed && baseShape.hasSubShape(TopAbs_SOLID)
                && !Shape.getShape().hasSubShape(TopAbs_SOLID))
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Resulting shape is not a solid"));

        if (failed)
            return new App::DocumentObjectExecReturn("Resulting shape is invalid");
        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
}

Part::TopoShape::FilletCorners Fillet::getCorners(const Part::TopoShape &baseShape) const
{
    Part::TopoShape::FilletCorners corners;
    if (Corners.getValue().empty())
        return corners;

    // The names of Corners are Base's, its faces CornerFaces'; resolve them
    // by their mapped names
    std::map<std::string, std::string> mappedNames;
    for (const auto *link : {&Base, &CornerFaces}) {
        for (const auto &shadow : link->getShadowSubs()) {
            if (!shadow.first.empty())
                mappedNames[shadow.second] = shadow.first;
        }
    }
    auto find = [&](const std::string &name, TopAbs_ShapeEnum type) {
        auto it = mappedNames.find(name);
        const auto &ref = it == mappedNames.end() ? name : it->second;
        TopoDS_Shape shape = baseShape.getSubShape(ref.c_str(), true);
        if (shape.IsNull() || shape.ShapeType() != type) {
            FC_WARN(getFullName() << ": skip fillet corner reference " << name);
            return TopoDS_Shape();
        }
        return shape;
    };

    for (const auto &v : Corners.getValue()) {
        const auto &setting = v.second;
        if (setting.setback < 0.0 && setting.edges.empty())
            continue;
        TopoDS_Shape vertex = find(v.first, TopAbs_VERTEX);
        if (vertex.IsNull())
            continue;
        corners.emplace_back();
        auto &corner = corners.back();
        corner.vertex = vertex;
        corner.setback = setting.setback;
        // A corner that no longer ends a fillet is kept, and skipped
        corner.optional = true;
        for (const auto &e : setting.edges) {
            if (Part::PropertyFilletCorners::isFaceName(e.first)) {
                TopoDS_Shape face = find(e.first, TopAbs_FACE);
                if (!face.IsNull())
                    corner.faces.emplace_back(face, e.second);
                continue;
            }
            TopoDS_Shape edge = find(e.first, TopAbs_EDGE);
            if (!edge.IsNull())
                corner.edges.emplace_back(edge, e.second);
        }
    }
    return corners;
}

void Fillet::onChanged(const App::Property *prop)
{
    // A corner's vertex goes in Base, so that its name follows the topology
    // as the edges' names do (PropertyFilletCorners::connectLinkProperty)
    if (prop == &Corners && Base.getValue() && getDocument()
            && !isRestoring() && !getDocument()->isPerformingTransaction()) {
        auto subs = Base.getSubValues(false);
        bool added = false;
        for (const auto &v : Corners.getValue()) {
            if (std::find(subs.begin(), subs.end(), v.first) != subs.end())
                continue;
            Part::TopoShape vertex;
            try {
                vertex = Part::Feature::getTopoShape(Base.getValue(), v.first.c_str(), true);
            }
            catch (Base::Exception &) {
            }
            if (vertex.isNull() || vertex.shapeType(true) != TopAbs_VERTEX)
                continue;
            subs.push_back(v.first);
            added = true;
        }
        if (added)
            Base.setValue(Base.getValue(), std::move(subs));
    }
    // ... and the faces it gives depths go in CornerFaces, for the same
    if ((prop == &Corners || prop == &Base) && getDocument()
            && !isRestoring() && !getDocument()->isPerformingTransaction())
        syncCornerFaces();
    DressUp::onChanged(prop);
}

void Fillet::syncCornerFaces()
{
    std::vector<std::string> faces;
    for (const auto &v : Corners.getValue()) {
        for (const auto &e : v.second.edges) {
            if (Part::PropertyFilletCorners::isFaceName(e.first)
                    && std::find(faces.begin(), faces.end(), e.first) == faces.end())
                faces.push_back(e.first);
        }
    }
    // with no base the faces are left as they are: unlinking them would drop
    // their depths
    App::DocumentObject *base = Base.getValue();
    if (!base)
        return;
    if (faces.empty())
        base = nullptr;
    // the faces already linked keep their mapped names
    auto subs = CornerFaces.getSubValues(false);
    std::vector<std::string> sortedSubs(subs), sortedFaces(faces);
    std::sort(sortedSubs.begin(), sortedSubs.end());
    std::sort(sortedFaces.begin(), sortedFaces.end());
    if (CornerFaces.getValue() == base && sortedSubs == sortedFaces)
        return;
    CornerFaces.setValue(base, std::move(faces));
}

void Fillet::handleChangedPropertyType(Base::XMLReader &reader, const char * TypeName, App::Property * prop)
{
    if (prop && strcmp(TypeName,"App::PropertyFloatConstraint") == 0 &&
        strcmp(prop->getTypeId().getName(), "App::PropertyQuantityConstraint") == 0) {
        App::PropertyFloatConstraint p;
        p.Restore(reader);
        static_cast<App::PropertyQuantityConstraint*>(prop)->setValue(p.getValue());
    }
    else {
        DressUp::handleChangedPropertyType(reader, TypeName, prop);
    }
}

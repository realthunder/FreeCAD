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
# include <BRepAlgo.hxx>
# include <BRepFilletAPI_MakeChamfer.hxx>
# include <TopExp.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
# include <TopTools_ListOfShape.hxx>
# include <ShapeFix_Shape.hxx>
# include <ShapeFix_ShapeTolerance.hxx>
# include <Standard_Version.hxx>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/ProgramVersion.h>
#include <Base/Reader.h>
#include <App/Document.h>
#include <Base/Tools.h>
#include <Mod/Part/App/TopoShape.h>

#include "FeatureChamfer.h"


using namespace PartDesign;


PROPERTY_SOURCE(PartDesign::Chamfer, PartDesign::DressUp)

const char* ChamferTypeEnums[] = {"Equal distance", "Two distances", "Distance and Angle", nullptr};
const App::PropertyQuantityConstraint::Constraints Chamfer::floatSize = {0.0, FLT_MAX, 0.1};
const App::PropertyAngle::Constraints Chamfer::floatAngle = {0.0, 180.0, 1.0};

static App::DocumentObjectExecReturn *validateParameters(int chamferType, double size, double size2, double angle);

Chamfer::Chamfer()
{
    ADD_PROPERTY_TYPE(ChamferType, (0L), "Chamfer", App::Prop_None, "Type of chamfer");
    ChamferType.setEnums(ChamferTypeEnums);

    ADD_PROPERTY_TYPE(Size, (1.0), "Chamfer", App::Prop_None, "Size of chamfer");
    Size.setUnit(Base::Unit::Length);
    Size.setConstraints(&floatSize);

    ADD_PROPERTY_TYPE(Size2, (1.0), "Chamfer", App::Prop_None, "Second size of chamfer");
    Size2.setUnit(Base::Unit::Length);
    Size2.setConstraints(&floatSize);

    ADD_PROPERTY_TYPE(Angle, (45.0), "Chamfer", App::Prop_None, "Angle of chamfer");
    Angle.setUnit(Base::Unit::Angle);
    Angle.setConstraints(&floatAngle);

    ADD_PROPERTY_TYPE(FlipDirection, (false), "Chamfer", App::Prop_None, "Flip direction");
    ADD_PROPERTY_TYPE(UseAllEdges, (false), "Chamfer", App::Prop_None,
             "Chamfer all edges if true, else use only those edges in Base property.\n"
             "If true, then this overrides any edge changes made to the Base property or in the dialog.\n");

    ADD_PROPERTY(ChamferInfo,());
    ChamferInfo.connectLinkProperty(Base);

    updateProperties();
}

short Chamfer::mustExecute() const
{
    bool touched = false;

    auto chamferType = ChamferType.getValue();

    switch (chamferType) {
        case 0: // "Equal distance"
            touched = Size.isTouched() || ChamferType.isTouched();
            break;
        case 1: // "Two distances"
            touched = Size.isTouched() || ChamferType.isTouched() || Size2.isTouched();
            break;
        case 2: // "Distance and Angle"
            touched = Size.isTouched() || ChamferType.isTouched() || Angle.isTouched();
            break;
    }

    if (Placement.isTouched() || ChamferInfo.isTouched() || touched)
        return 1;
    return DressUp::mustExecute();
}

App::DocumentObjectExecReturn *Chamfer::execute()
{
    // NOTE: Normally the Base property and the BaseFeature property should point to the same object.
    // The only difference is that the Base property also stores the edges that are to be chamfered
    Part::TopoShape baseShape;
    try {
        baseShape = getBaseShape();
    } catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    baseShape.setTransform(Base::Matrix4D());

    auto edges = UseAllEdges.getValue() ? baseShape.getSubTopoShapes(TopAbs_EDGE)
                                        : getContinuousEdges(baseShape);

    if (edges.empty())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "No edges specified"));

    const int chamferType = ChamferType.getValue();
    const double size = Size.getValue();
    const double size2 = Size2.getValue();
    const double angle = Angle.getValue();
    const bool flipDirection = FlipDirection.getValue();

    auto res = validateParameters(chamferType, size, size2, angle);
    if (res != App::DocumentObject::StdReturn) {
        return res;
    }

    Part::TopoShape::ChamferInfo defaultChamfer;
    defaultChamfer.size = size;
    defaultChamfer.size2 = chamferType == 1 ? size2 : size;
    defaultChamfer.angle = chamferType == 2 ? angle : 0.0;
    defaultChamfer.flip = flipDirection;

    if (migrateFaceRule) {
        migrateFaceRule = false;
        if (chamferType != 0)
            migrateSelectedFaceRule(baseShape, edges);
    }

    std::string name;
    std::vector<Part::TopoShape::ChamferInfo> chamferInfo;
    for (const auto &edge : edges) {
        int idx = baseShape.findShape(edge.getShape());
        name = "Edge";
        name += std::to_string(idx);
        chamferInfo.push_back(defaultChamfer);
        ChamferInfo.getValue(name.c_str(),chamferInfo.back());
    }

    this->positionByBaseFeature();
    try {
        TopoShape shape(0,getDocument()->getStringHasher());
        shape.makEChamfer(baseShape, edges, chamferInfo);
        if (shape.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Failed to create chamfer"));

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
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Resulting shape is invalid"));
        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
}

void Chamfer::handleChangedPropertyType(Base::XMLReader &reader, const char * TypeName, App::Property * prop)
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

void Chamfer::onChanged(const App::Property* prop)
{
    if (prop == &ChamferType) {
        updateProperties();
    }
    else if (prop == &ChamferInfo && isRestoring()) {
        chamferInfoRestored = true;
    }

    DressUp::onChanged(prop);
}

void Chamfer::Restore(Base::XMLReader &reader)
{
    chamferInfoRestored = false;
    DressUp::Restore(reader);
    // Upstream before 1.0 read a selected face its own way (upstream
    // 4d712f44c2). Its files have neither a string hasher nor ChamferInfo,
    // which every chamfer this fork writes has -- and the fork writes a 0.22
    // ProgramVersion, so the version alone does not tell.
    migrateFaceRule = !chamferInfoRestored && !reader.HasStringHasher
        && Base::getVersion(reader.ProgramVersion) < Base::Version::v1_0;
}

void Chamfer::onDocumentRestored()
{
    DressUp::onDocumentRestored();
    if (!migrateFaceRule)
        return;
    // Now, while the shapes are those of the file, so that saving the file
    // unrecomputed keeps it; the recompute tries again if this cannot
    try {
        auto baseShape = getBaseShape();
        baseShape.setTransform(Base::Matrix4D());
        if (ChamferType.getValue() != 0 && !UseAllEdges.getValue())
            migrateSelectedFaceRule(baseShape, getContinuousEdges(baseShape));
        migrateFaceRule = false;
    }
    catch (const Base::Exception &) {
    }
    catch (const Standard_Failure &) {
    }
}

void Chamfer::migrateSelectedFaceRule(const Part::TopoShape &baseShape,
                                      const std::vector<Part::TopoShape> &edges)
{
    // For an edge of a selected face, upstream before 1.0 measured Size on
    // the selected face, or on the edge's other face when flipped; for any
    // other edge, on its first face, or its last when flipped. The fork,
    // as upstream since, always does the latter, so an upstream 0.21
    // chamfer made on a face came out with its sizes swapped wherever the
    // selected face is the last. The rule is stated as the flip of each
    // edge it decides differently.
    std::vector<TopoDS_Shape> faces;
    for (const auto &v : Base.getShadowSubs()) {
        const auto &ref = v.first.size() ? v.first : v.second;
        auto subshape = baseShape.getSubShape(ref.c_str(), true);
        if (!subshape.IsNull() && subshape.ShapeType() == TopAbs_FACE)
            faces.push_back(subshape);
    }
    if (faces.empty())
        return;

    const bool flip = FlipDirection.getValue();
    Part::TopoShape::ChamferInfo info;
    info.size = Size.getValue();
    info.size2 = ChamferType.getValue() == 1 ? Size2.getValue() : info.size;
    info.angle = ChamferType.getValue() == 2 ? Angle.getValue() : 0.0;

    auto values = ChamferInfo.getValue();
    int count = 0;
    for (const auto &edge : edges) {
        auto ancestors = baseShape.findAncestorsShapes(edge.getShape(), TopAbs_FACE);
        if (ancestors.size() < 2)
            continue;
        const TopoDS_Shape &first = ancestors.front();
        const TopoDS_Shape &last = ancestors.back();
        const TopoDS_Shape *face = flip ? &last : &first;
        for (const auto &selected : faces) {
            if (last.IsSame(selected))
                face = flip ? &first : &last;
            else if (first.IsSame(selected))
                face = flip ? &last : &first;
        }
        info.flip = !face->IsSame(first);
        if (info.flip == flip)
            continue;
        std::string name("Edge");
        name += std::to_string(baseShape.findShape(edge.getShape()));
        values[name] = info;
        ++count;
    }
    if (!count)
        return;
    ChamferInfo.setValue(std::move(values));
    Base::Console().Warning("%s: %d edge(s) of a chamfer made on a face by FreeCAD before 1.0 "
                            "flipped to keep its sizes where they were.\n",
                            getFullName().c_str(), count);
}

void Chamfer::updateProperties()
{
    auto chamferType = ChamferType.getValue();

    auto disableproperty = [](App::Property * prop, bool on) {
        prop->setStatus(App::Property::ReadOnly, on);
    };

    switch (chamferType) {
    case 0: // "Equal distance"
        disableproperty(&this->Angle, true);
        disableproperty(&this->Size2, true);
        break;
    case 1: // "Two distances"
        disableproperty(&this->Angle, true);
        disableproperty(&this->Size2, false);
        break;
    case 2: // "Distance and Angle"
        disableproperty(&this->Angle, false);
        disableproperty(&this->Size2, true);
        break;
    }
}

static App::DocumentObjectExecReturn *validateParameters(int chamferType, double size, double size2, double angle)
{
    // Size is common to all chamfer types.
    if (size <= 0) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Size must be greater than zero"));
    }

    switch (chamferType) {
        case 0: // Equal distance
            // Nothing to do.
            break;
        case 1: // Two distances
            if (size2 <= 0) {
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Size2 must be greater than zero"));
            }
            break;
        case 2: // Distance and angle
            if (angle <= 0 || angle >= 180.0) {
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Angle must be greater than 0 and less than 180"));
            }
            break;
    }

    return App::DocumentObject::StdReturn;
}



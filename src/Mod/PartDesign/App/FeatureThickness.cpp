/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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
# include <cmath>
# include <functional>
# include <BRepOffset_Mode.hxx>
# include <Precision.hxx>
# include <TopoDS.hxx>
#endif

#include <Base/Exception.h>
#include <Base/Parameter.h>
#include <Base/Console.h>
#include <App/Application.h>
#include <App/Document.h>
#include "FeatureThickness.h"

FC_LOG_LEVEL_INIT("PartDesign",true,true)

using namespace PartDesign;
using Part::TopoShape;

namespace {

void ensureValidWall(const TopoShape &wall, const char *message)
{
    if (wall.isNull() || !wall.isValid() || wall.countSubShapes(TopAbs_SOLID) != 1)
        throw Base::CADKernelError(message);
}

/** A wall of the given total thickness centred on a solid's faces (upstream
 * f4a9a68df2, 1f0127b4cc, 903ab41a37). OCCT's recto-verso offset mode made
 * the one-sided skin here, the same as Skin: this makes a skin half as thick
 * each way and fuses the two across the faces they share. With no face to
 * open, the wall is the solid grown by half the thickness less the solid
 * shrunk by as much.
 */
TopoShape makeRectoVersoThickness(const TopoShape &solid, const std::vector<TopoShape> &faces,
                                  double thickness, double tol, bool intersection,
                                  TopoShape::JoinType join,
                                  const std::function<void(TopoShape &)> &fixShape)
{
    const double distance = std::fabs(thickness) / 2.0;
    if (distance <= tol)
        throw Base::CADKernelError("Recto-verso half-thickness must exceed the modeling tolerance");

    // Signed offsets need a consistently oriented solid, which an imported
    // one need not be; this keeps the element names
    TopoShape oriented = solid;
    oriented.fixSolidOrientation();

    constexpr auto skin = static_cast<short>(BRepOffset_Skin);
    TopoShape recto = oriented.makEThickSolid(faces, distance, tol, intersection, false, skin,
                                              join, "RectoVersoRecto");
    TopoShape verso = oriented.makEThickSolid(faces, -distance, tol, intersection, false, skin,
                                              join, "RectoVersoVerso");
    // As the feature fixes a skin: OCCT 8.0.1 makes the inner Arc-joined
    // skin of an open cylinder invalid, and the fix mends it. A solid grown
    // with no face open comes back inside out.
    for (auto wall : {&recto, &verso}) {
        fixShape(*wall);
        if (faces.empty())
            wall->fixSolidOrientation();
    }
    ensureValidWall(recto, "Recto-verso positive-side wall is invalid");
    ensureValidWall(verso, "Recto-verso negative-side wall is invalid");

    TopoShape result(0, solid.Hasher);
    if (faces.empty())
        result.makECut({recto, verso}, "RectoVerso", tol);
    else
        result.makEFuse({recto, verso}, "RectoVerso", tol);
    if (result.isNull() || !result.isValid() || result.countSubShapes(TopAbs_SOLID) != 1)
        throw Base::CADKernelError("Recto-verso thickness produced an invalid solid");
    return result;
}

} // namespace

const char *PartDesign::Thickness::ModeEnums[] = {"Skin", "Pipe", "RectoVerso", nullptr};
const char *PartDesign::Thickness::JoinEnums[] = {"Arc", "Intersection", nullptr};

PROPERTY_SOURCE(PartDesign::Thickness, PartDesign::DressUp)

Thickness::Thickness()
{
    ADD_PROPERTY_TYPE(Value, (1.0), "Thickness", App::Prop_None, "Thickness value");
    ADD_PROPERTY_TYPE(Mode, (long(0)), "Thickness", App::Prop_None, "Mode");
    Mode.setEnums(ModeEnums);
    ADD_PROPERTY_TYPE(Join, (long(0)), "Thickness", App::Prop_None, "Join type");
    Join.setEnums(JoinEnums);
    ADD_PROPERTY_TYPE(Reversed, (true), "Thickness", App::Prop_None,
                      "Apply the thickness towards the solids interior");
    ADD_PROPERTY_TYPE(Intersection, (false), "Thickness", App::Prop_None,
                      "Enable intersection-handling");
    ADD_PROPERTY_TYPE(MakeOffset,(false),"Thickness",App::Prop_None,"Make a thicken or shrunken solid instead of a thin shell");
}

short Thickness::mustExecute() const
{
    if (Placement.isTouched() ||
        Value.isTouched() ||
        Mode.isTouched() ||
        Join.isTouched())
        return 1;
    return DressUp::mustExecute();
}

App::DocumentObjectExecReturn *Thickness::execute()
{
    // Base shape
    Part::TopoShape baseShape;
    try {
        baseShape = getBaseShape();
    } catch (Base::Exception &e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    // In the base's local frame, as Fillet does: making a thick solid of a
    // rotated filleted body fails in global coordinates (upstream d8d85f05ff,
    // issue 5829)
    baseShape.setTransform(Base::Matrix4D());
    this->positionByBaseFeature();

    std::map<int,std::vector<TopoShape> > closeFaces;
    const std::vector<std::string>& subStrings = Base.getSubValues(true);
    for (std::vector<std::string>::const_iterator it = subStrings.begin(); it != subStrings.end(); ++it) {
        TopoDS_Shape face;
        try {
            face = baseShape.getSubShape(it->c_str());
        }catch(...){}
        if(face.IsNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Invalid face reference"));
        int index = baseShape.findAncestor(face,TopAbs_SOLID);
        if(!index) {
            FC_WARN(getFullName() << ": Ignore non-solid face  " << *it);
            continue;
        }
        closeFaces[index].emplace_back(face);
    }

    bool reversed = Reversed.getValue();
    bool intersection = Intersection.getValue();
    double thickness =  (reversed ? -1. : 1. )*Value.getValue();
    double tol = Precision::Confusion();
    short mode = (short)Mode.getValue();
    short join = (short)Join.getValue();

    std::vector<TopoShape> shapes;
    int count = baseShape.countSubShapes(TopAbs_SOLID);
    if(!count)
        return new App::DocumentObjectExecReturn("No solid");

    if (fabs(thickness) > 2*tol) {
        auto it = closeFaces.begin();
        for(int i=1;i<=count;++i) {
            std::vector<TopoShape> dummy;
            const auto *faces = &dummy;
            TopoShape solid = baseShape;
            if(it!=closeFaces.end() && i>=it->first) {
                faces = &it->second;
                solid = baseShape.getSubTopoShape(TopAbs_SOLID,it->first);
            }
            TopoShape res(0,getDocument()->getStringHasher());
            try {
                if (mode == BRepOffset_RectoVerso) {
                    // Centred on the faces, so neither Reversed nor
                    // MakeOffset has a side to take
                    res = makeRectoVersoThickness(solid, *faces, thickness, tol, intersection,
                            static_cast<Part::TopoShape::JoinType>(join),
                            [this](TopoShape &s) { this->fixShape(s); });
                    this->fixShape(res);
                    shapes.push_back(res);
                    if (it != closeFaces.end())
                        ++it;
                    continue;
                }
                res = solid.makEThickSolid(*faces, thickness, tol, intersection, false, mode,
                                           static_cast<Part::TopoShape::JoinType>(join));
                this->fixShape(res);

                // When no face to remove, OCC makeThickSolid actually behave
                // the same as makeOffset. We slightly change that behavior to
                // always create thick shell unless `MakeOffset` is active. So if
                // face is empty, and MakeOffset is not active, we'll do a cut to
                // make the shell.
                if (faces->empty() && !MakeOffset.getValue()) {
                    if (thickness < 0.)
                        res = solid.makECut(res);
                    else
                        res = res.makECut(solid);
                }
                else if (!faces->empty() && MakeOffset.getValue()) {
                    if (thickness < 0.)
                        res = solid.makECut(res);
                    else
                        res = solid.makEFuse(res);
                }
                this->fixShape(res);
                shapes.push_back(res);
            }catch(Standard_Failure &e) {
                FC_ERR("Exception on making thick solid: " << e.GetMessageString());
                return new App::DocumentObjectExecReturn("Failed to make thick solid");
            }
            if (it !=closeFaces.end()) {
                ++it;
            }
        }
    }

    TopoShape result(0,getDocument()->getStringHasher());
    if(shapes.size()>1) {
        result.makEFuse(shapes);
    }else if (shapes.empty())
        result = baseShape;
    else
        result = shapes.front();
    result = refineShapeIfActive(result);
    this->Shape.setValue(getSolid(result));
    return App::DocumentObject::StdReturn;
}

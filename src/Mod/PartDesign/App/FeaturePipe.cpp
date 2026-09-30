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
# include <BRepAlgoAPI_Cut.hxx>
# include <BRepAlgoAPI_Fuse.hxx>
# include <BRepBndLib.hxx>
# include <BRepBuilderAPI_Sewing.hxx>
# include <BRepBuilderAPI_MakeSolid.hxx>
# include <BRepBuilderAPI_MakeWire.hxx>
# include <BRepClass3d_SolidClassifier.hxx>
# include <BRepOffsetAPI_MakePipeShell.hxx>
# include <gp_Ax2.hxx>
# include <gp_Pln.hxx>
# include <Law_Function.hxx>
# include <Precision.hxx>
# include <ShapeAnalysis_FreeBounds.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Wire.hxx>
# include <TopTools_HSequenceOfShape.hxx>
#endif

#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <App/Document.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include "FeatureLoft.h"
#include "FeaturePipe.h"

FC_LOG_LEVEL_INIT("PartDesign",true,true);

using namespace PartDesign;
using namespace Part;

const char* Pipe::TypeEnums[] = {"FullPath", "UpToFace", nullptr};
const char* Pipe::TransitionEnums[] = {"Transformed", "Right corner", "Round corner", nullptr};
const char* Pipe::ModeEnums[] = {"Standard", "Fixed", "Frenet", "Auxiliary", "Binormal", nullptr};
const char* Pipe::TransformEnums[] = {
    "Constant", "Multisection", "Linear", "S-shape", "Interpolation", nullptr};


PROPERTY_SOURCE(PartDesign::Pipe, PartDesign::ProfileBased)

Pipe::Pipe()
{
    ADD_PROPERTY_TYPE(Sections, (nullptr), "Sweep", App::Prop_None, "List of sections");
    Sections.setValue(nullptr);
    ADD_PROPERTY_TYPE(Spine, (nullptr), "Sweep", App::Prop_None, "Path to sweep along");
    ADD_PROPERTY_TYPE(SpineTangent, (false), "Sweep", App::Prop_None,
        "Include tangent edges into path");
    ADD_PROPERTY_TYPE(AuxiliarySpine, (nullptr), "Sweep", App::Prop_None,
        "Secondary path to orient sweep");
    ADD_PROPERTY_TYPE(AuxiliarySpineTangent, (false), "Sweep", App::Prop_None,
        "Include tangent edges into secondary path");
    ADD_PROPERTY_TYPE(AuxiliaryCurvilinear, (true), "Sweep", App::Prop_None,
        "Calculate normal between equidistant points on both spines");
    ADD_PROPERTY_TYPE(Mode, (long(0)), "Sweep", App::Prop_None, "Profile mode");
    ADD_PROPERTY_TYPE(Binormal, (Base::Vector3d()), "Sweep", App::Prop_None,
        "Binormal vector for corresponding orientation mode");
    ADD_PROPERTY_TYPE(Transition, (long(0)), "Sweep", App::Prop_None, "Transition mode");
    ADD_PROPERTY_TYPE(Transformation, (long(0)), "Sweep", App::Prop_None,
        "Section transformation mode");
    ADD_PROPERTY_TYPE(MoveProfile, (false), "Sweep", App::Prop_None,
        "Auto move profile to be in contact with sweep path");
    ADD_PROPERTY_TYPE(RotateProfile, (false), "Sweep", App::Prop_None,
        "Auto rotate profile to be orthogonal to sweep path");
    Mode.setEnums(ModeEnums);
    Transition.setEnums(TransitionEnums);
    Transition.setValue(1);
    Transformation.setEnums(TransformEnums);
}

void Pipe::setupObject()
{
    ProfileBased::setupObject();
    _ProfileBasedVersion.setValue(2);
}

short Pipe::mustExecute() const
{
    if (Sections.isTouched())
        return 1;
    if (Spine.isTouched())
        return 1;
    if (Mode.isTouched())
        return 1;
    if (Transition.isTouched())
        return 1;
    return ProfileBased::mustExecute();
}

App::DocumentObjectExecReturn *Pipe::execute()
{
    TopoShape path, auxpath;
    TopLoc_Location invObjLoc;
    try {
        invObjLoc = positionByPrevious();

        //build the paths
        path = buildPipePath(Spine,invObjLoc);
        if(path.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception","Invalid spine"));

        // auxiliary
        if(Mode.getValue()==3) {
            auxpath = buildPipePath(AuxiliarySpine,invObjLoc);
            if(auxpath.isNull())
                return new App::DocumentObjectExecReturn(
                        QT_TRANSLATE_NOOP("Exception", "invalid auxiliary spine"));
        }
    } catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    } catch (const Base::Exception & e) {
        e.ReportException();
        return new App::DocumentObjectExecReturn(e.what());
    } catch (...) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Unknown error"));
    }

    return _execute(this,
                    path,
                    invObjLoc,
                    Transition.getValue(),
                    auxpath,
                    AuxiliaryCurvilinear.getValue(),
                    Mode.getValue(),
                    Binormal.getValue(),
                    Transformation.getValue(),
                    Sections.getSubListValues(),
                    MoveProfile.getValue(),
                    RotateProfile.getValue());
}

App::DocumentObjectExecReturn *Pipe::_execute(ProfileBased *feat,
                                              const TopoShape &path,
                                              const TopLoc_Location &invObjLoc,
                                              int transition,
                                              const TopoShape &auxpath,
                                              bool auxCurveLinear,
                                              int mode,
                                              const Base::Vector3d &binormalVector,
                                              int transformation,
                                              const std::vector<App::PropertyLinkSubList::SubSet> &multisections,
                                              bool moveProfile,
                                              bool rotateProfile)
{
    // if the Base property has a valid shape, fuse the pipe into it
    TopoShape base;
    try {
        base = feat->getBaseShape();
    } catch (const Base::Exception&) {
    }

    try {
        auto wires = Loft::getSectionShape("Profile", feat->Profile.getValue(), feat->Profile.getSubValues());

        // If base is null, it means we are creating a new shape, and we shall
        // allow non closed wire to create face from sweeping wire.
        bool closed = wires.size()>0 && wires.front().isClosed();

        //setup the location
        if(!base.isNull())
            base.move(invObjLoc);

        //build up multisections
        std::vector<std::vector<TopoShape>> wiresections;
        wiresections.reserve(wires.size());
        for(TopoShape& wire : wires)
            wiresections.emplace_back(1, wire);
        //maybe we need a sacling law
        Handle(Law_Function) scalinglaw;

        //see if we shall use multiple sections
        if(transformation == 1) {
            //TODO: we need to order the sections to prevent occ from crahsing, as makepieshell connects
            //the sections in the order of adding
            for (auto &subSet : multisections) {
                int i=0;
                for (const auto &s : Loft::getSectionShape("Section", subSet.first, subSet.second, wiresections.size()))
                    wiresections[i++].push_back(s);
            }
        }
        /*//build the law functions instead
        else if(transformation == 2) {
            if(ScalingData.getValues().size()<1)
                return new App::DocumentObjectExecReturn("No valid data given for linear scaling mode");

            Handle(Law_Linear) lin = new Law_Linear();
            lin->Set(0, 1, 1, ScalingData[0].x);

            scalinglaw = lin;
        }
        else if (transformation == 3) {
            if(ScalingData.getValues().size()<1)
                return new App::DocumentObjectExecReturn("No valid data given for S-shape scaling mode");

            Handle(Law_S) s = new Law_S();
            s->Set(0, 1, ScalingData[0].y, 1, ScalingData[0].x, ScalingData[0].z);

            scalinglaw = s;
        }*/

        //build all shells
        std::vector<TopoShape> shells;
        std::vector<TopoShape> frontwires, backwires;
        for(auto& wires : wiresections) {

            BRepOffsetAPI_MakePipeShell mkPS(TopoDS::Wire(path.getShape()));
            setupAlgorithm(mkPS, mode, binormalVector, transition, auxpath, auxCurveLinear);

            if(!scalinglaw) {
                for(TopoShape& wire : wires) {
                    wire.move(invObjLoc);
                    mkPS.Add(wire.getShape(),
                             moveProfile?Standard_True:Standard_False,
                             rotateProfile?Standard_True:Standard_False);
                }
            }
            else {
                for(TopoShape& wire : wires)  {
                    wire.move(invObjLoc);
                    mkPS.SetLaw(wire.getShape(), scalinglaw,
                                moveProfile?Standard_True:Standard_False,
                                rotateProfile?Standard_True:Standard_False);
                }
            }

            if (!mkPS.IsReady())
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Shape could not be built"));

            TopoShape shell(0, feat->getDocument()->getStringHasher());
            shell.makEShape(mkPS,wires);
            shells.push_back(shell);

            if (closed && !mkPS.Shape().Closed()) {
                // shell is not closed - use simulate to get the end wires
                TopTools_ListOfShape sim;
                mkPS.Simulate(2, sim);

                if (wires.front().shapeType() != TopAbs_VERTEX) {
                    TopoShape front(sim.First());
                    if(front.countSubShapes(TopAbs_EDGE)==wires.front().countSubShapes(TopAbs_EDGE)) {
                        front = wires.front();
                        front.setShape(sim.First(),false);
                    }else
                        front.Tag = -wires.front().Tag;
                    frontwires.push_back(front);
                }

                if (wires.back().shapeType() != TopAbs_VERTEX) {
                    TopoShape back(sim.Last());
                    if(back.countSubShapes(TopAbs_EDGE)==wires.back().countSubShapes(TopAbs_EDGE)) {
                        back = wires.back();
                        back.setShape(sim.Last(),false);
                    }else
                        back.Tag = -wires.back().Tag;
                    backwires.push_back(back);
                }
            }
        }

        TopoShape result(0,feat->getDocument()->getStringHasher());

        if (!frontwires.empty() || !backwires.empty()) {
            BRepBuilderAPI_Sewing sewer;
            sewer.SetTolerance(Precision::Confusion());
            for(auto& s : shells)
                sewer.Add(s.getShape());

            TopoShape frontface, backface;
            gp_Pln pln;

            if (!frontwires.empty() && frontwires.front().hasSubShape(TopAbs_EDGE)) {
                if (!TopoShape(-1).makECompound(frontwires).findPlane(pln)) {
                    try {
                        frontface.makEBSplineFace(frontwires);
                    } catch (Base::Exception &) {
                        frontface.makEFilledFace(frontwires, TopoShape::BRepFillingParams());
                    }
                }
                else
                    frontface.makEFace(frontwires);
                sewer.Add(frontface.getShape());
            }

            if (!backwires.empty() && backwires.front().hasSubShape(TopAbs_EDGE)) {
                // Explicitly set op code when making face to generate different
                // topo name than the front face.
                if (!TopoShape(-1).makECompound(backwires).findPlane(pln)) {
                    try {
                        backface.makEBSplineFace(backwires,
                                                /*style*/TopoShape::FillingStyle::FillingStyle_Strech,
                                                /*keepBezier*/false,
                                                /*op*/OpCodes::Sewing);
                    } catch (Base::Exception &) {
                        backface.makEFilledFace(backwires,
                                                TopoShape::BRepFillingParams(),
                                                /*op*/OpCodes::Sewing);
                    }
                }
                else
                    backface.makEFace(backwires,/*op*/OpCodes::Sewing);
                sewer.Add(backface.getShape());
            }

            sewer.Perform();
            result = result.makEShape(sewer,shells).makESolid();
        } else {
            // shells are already closed - add them directly
            result.makESolid(shells);
        }

        if (closed) {
            BRepClass3d_SolidClassifier SC(result.getShape());
            SC.PerformInfinitePoint(Precision::Confusion());
            if (SC.State() == TopAbs_IN) {
                result.setShape(result.getShape().Reversed(),false);
            }
            if (feat->Linearize.getValue())
                result.linearize(true, false);
        }

        //result.Move(invObjLoc);
        feat->AddSubShape.setValue(result);
        if (feat->isRecomputePaused())
            return App::DocumentObject::StdReturn;

        if(base.isNull()) {
            result = feat->refineShapeIfActive(result);
            feat->Shape.setValue(closed ? feat->getSolid(result) : result);
            return App::DocumentObject::StdReturn;
        }

        result.Tag = -feat->getID();

        TopoShape boolOp(0,feat->getDocument()->getStringHasher());

        const char *maker;
        switch(feat->getAddSubType()) {
        case Additive:
            maker = Part::OpCodes::Fuse;
            break;
        case Subtractive:
            maker = Part::OpCodes::Cut;
            break;
        case Intersecting:
            maker = Part::OpCodes::Common;
            break;
        default:
            return new App::DocumentObjectExecReturn("Unknown operation type");
        }
        try {
            feat->fixShape(result);
            boolOp.makEBoolean(maker, {base,result}, nullptr, feat->FuzzyTolerance.getValue());
        }catch(Standard_Failure &e) {
            FC_ERR(feat->getFullName() << ": " << e.GetMessageString());
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                        "Failed to perform boolean operation"));
        }
        boolOp = feat->getSolid(boolOp);
        // lets check if the result is a solid
        if (boolOp.isNull())
            return new App::DocumentObjectExecReturn("Resulting shape is not a solid");

        boolOp = feat->refineShapeIfActive(boolOp);
        feat->Shape.setValue(feat->getSolid(boolOp));
        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    } catch (const Base::Exception & e) {
        e.ReportException();
        return new App::DocumentObjectExecReturn(e.what());
    } catch (...) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "A fatal error occurred when making the pipe"));
    }
}

void Pipe::setupAlgorithm(BRepOffsetAPI_MakePipeShell& mkPipeShell,
                          int mode,
                          const Base::Vector3d &bVec,
                          int transition,
                          const TopoShape& auxshape,
                          bool auxCurveLinear)
{
    mkPipeShell.SetTolerance(Precision::Confusion());

    switch(transition) {
        case 0:
            mkPipeShell.SetTransitionMode(BRepBuilderAPI_Transformed);
            break;
        case 1:
            mkPipeShell.SetTransitionMode(BRepBuilderAPI_RightCorner);
            break;
        case 2:
            mkPipeShell.SetTransitionMode(BRepBuilderAPI_RoundCorner);
            break;
    }

    bool auxiliary = false;
    switch(mode) {
        case 1:
            mkPipeShell.SetMode(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
            break;
        case 2:
            mkPipeShell.SetMode(true);
            break;
        case 3:
            auxiliary = true;
            break;
        case 4:
            mkPipeShell.SetMode(gp_Dir(bVec.x, bVec.y, bVec.z));
            break;
    }

    if(auxiliary) {
        mkPipeShell.SetMode(TopoDS::Wire(auxshape.getShape()), auxCurveLinear);
        //mkPipeShell.SetMode(TopoDS::Wire(auxshape), AuxiliaryCurvilinear.getValue(), BRepFill_ContactOnBorder);
    }
}


void Pipe::getContinuousEdges(Part::TopoShape /*TopShape*/, std::vector<std::string>& /*SubNames*/)
{
    /*
    TopTools_IndexedMapOfShape mapOfEdges;
    TopTools_IndexedDataMapOfShapeListOfShape mapEdgeEdge;
    TopExp::MapShapesAndAncestors(TopShape.getShape(), TopAbs_EDGE, TopAbs_EDGE, mapEdgeEdge);
    TopExp::MapShapes(TopShape.getShape(), TopAbs_EDGE, mapOfEdges);

    Base::Console().Message("Initial edges:\n");
    for (int i=0; i<SubNames.size(); ++i)
        Base::Console().Message("Subname: %s\n", SubNames[i].c_str());

    unsigned int i = 0;
    while(i < SubNames.size())
    {
        std::string aSubName = static_cast<std::string>(SubNames.at(i));

        if (aSubName.compare(0, 4, "Edge") == 0) {
            TopoDS_Edge edge = TopoDS::Edge(TopShape.getSubShape(aSubName.c_str()));
            const TopTools_ListOfShape& los = mapEdgeEdge.FindFromKey(edge);

            if (los.Extent() != 2)
            {
                SubNames.erase(SubNames.begin()+i);
                continue;
            }

            const TopoDS_Shape& face1 = los.First();
            const TopoDS_Shape& face2 = los.Last();
            GeomAbs_Shape cont = BRep_Tool::Continuity(TopoDS::Edge(edge),
                                                       TopoDS::Face(face1),
                                                       TopoDS::Face(face2));
            if (cont != GeomAbs_C0) {
                SubNames.erase(SubNames.begin()+i);
                continue;
            }

            i++;
        }
        // empty name or any other sub-element
        else {
            SubNames.erase(SubNames.begin()+i);
        }
    }

    Base::Console().Message("Final edges:\n");
    for (int i=0; i<SubNames.size(); ++i)
        Base::Console().Message("Subname: %s\n", SubNames[i].c_str());
    */
}

TopoShape Pipe::buildPipePath(const App::PropertyLinkSub &link, const TopLoc_Location &invObjLoc) {
    TopoShape result(0,getDocument()->getStringHasher());
    auto obj = link.getValue();
    if(!obj) return result;
    std::vector<TopoShape> shapes;
    const auto &subs = link.getSubValues(true);
    if(subs.empty()) {
        shapes.push_back(getTopoShape(obj));
        if(shapes.back().isNull())
            return result;
    }else{
        for(auto &sub : subs) {
            shapes.push_back(getTopoShape(obj,sub.c_str(),true));
            if(shapes.back().isNull())
                return result;
        }
    }
    if (_ProfileBasedVersion.getValue() >= 2)
        result.makEOrderedWires(shapes);
    else
        result.makEWires(shapes);
    if(result.countSubShapes(TopAbs_WIRE)>1)
        FC_WARN("Sweep path contain more than one wire");
    result = result.getSubTopoShape(TopAbs_WIRE,1);
    result.move(invObjLoc);
    return result;
}

void Pipe::handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName,
                                     App::Property* prop)
{
    // property Sections had the App::PropertyLinkList and was changed to App::PropertyLinkSubList
    if (prop == &Sections && strcmp(TypeName, "App::PropertyLinkList") == 0) {
        Sections.upgrade(reader, TypeName);
    }
    else {
        ProfileBased::handleChangedPropertyType(reader, TypeName, prop);
    }
}

void Pipe::handleChangedPropertyName(Base::XMLReader& reader, const char* TypeName,
                                     const char* PropName)
{
    // The auxiliary spine properties were misspelled before (upstream
    // fa3c6e1068): AuxillerySpine, AuxillerySpineTangent, AuxilleryCurvelinear
    Base::Type type = Base::Type::fromName(TypeName);
    if (type == AuxiliarySpine.getClassTypeId() && strcmp(PropName, "AuxillerySpine") == 0) {
        AuxiliarySpine.Restore(reader);
    }
    else if (type == AuxiliarySpineTangent.getClassTypeId()
             && strcmp(PropName, "AuxillerySpineTangent") == 0) {
        AuxiliarySpineTangent.Restore(reader);
    }
    else if (type == AuxiliaryCurvilinear.getClassTypeId()
             && strcmp(PropName, "AuxilleryCurvelinear") == 0) {
        AuxiliaryCurvilinear.Restore(reader);
    }
    else {
        ProfileBased::handleChangedPropertyName(reader, TypeName, PropName);
    }
}

PROPERTY_SOURCE(PartDesign::AdditivePipe, PartDesign::Pipe)
AdditivePipe::AdditivePipe() {
}

PROPERTY_SOURCE(PartDesign::SubtractivePipe, PartDesign::Pipe)
SubtractivePipe::SubtractivePipe() {
    initAddSubType(FeatureAddSub::Subtractive);
}

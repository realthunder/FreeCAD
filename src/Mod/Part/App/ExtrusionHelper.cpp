/***************************************************************************
 *   Copyright (c) 2022 Uwe Stöhr <uwestoehr@lyx.org>                      *
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
# include <BRepBuilderAPI_MakeFace.hxx>
# include <BRepBuilderAPI_MakeWire.hxx>
# include <BRepBuilderAPI_Sewing.hxx>
# include <BRepGProp.hxx>
# include <BRepOffsetAPI_ThruSections.hxx>
# include <BRepOffsetAPI_MakePipeShell.hxx>
# include <BRepPrimAPI_MakePrism.hxx>
# include <BRepClass3d_SolidClassifier.hxx>
# include <gp_Ax1.hxx>
# include <gp_Dir.hxx>
# include <gp_Pln.hxx>
# include <gp_Trsf.hxx>
# include <GProp_GProps.hxx>
# include <Precision.hxx>
# include <ShapeFix_Wire.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>

#include "ExtrusionHelper.h"
#include "Geometry.h"
#include "PartParams.h"


using namespace Part;

ExtrusionHelper::Parameters::Parameters()
    : lengthFwd(0)
    , lengthRev(0)
    , solid(false)
    , innertaper(false)
    , usepipe(false)
    , linearize(PartParams::getLinearizeExtrusionDraft())
    , taperAngleFwd(0)
    , taperAngleRev(0)
    , innerTaperAngleFwd(0)
    , innerTaperAngleRev(0)
{
}

ExtrusionHelper::ExtrusionHelper() = default;

static TopoShape makeDraftUsingPipe(const std::vector<TopoShape> &_wires,
                                    App::StringHasherRef hasher)
{
    std::vector<TopoShape> shells;
    std::vector<TopoShape> frontwires, backwires;
    
    if (_wires.size() < 2)
        throw Base::CADKernelError("Not enough wire section");

    std::vector<TopoShape> wires;
    wires.reserve(_wires.size());
    for (auto &wire : _wires) {
        // Make a copy to work around OCCT bug on offset circular shapes
        wires.push_back(wire.makECopy());
    }
    GeomLineSegment line;
    Base::Vector3d pstart, pend;
    wires.front().getCenterOfGravity(pstart);
    gp_Pln pln;
    if (wires.back().findPlane(pln)) {
        auto dir = pln.Position().Direction();
        auto base = pln.Location();
        pend = pstart;
        pend.ProjectToPlane(Base::Vector3d(base.X(), base.Y(), base.Z()),
                            Base::Vector3d(dir.X(), dir.Y(), dir.Z()));
    } else
        wires.back().getCenterOfGravity(pend);
    line.setPoints(pstart, pend);

    BRepBuilderAPI_MakeWire mkWire(TopoDS::Edge(line.toShape()));
    BRepOffsetAPI_MakePipeShell mkPS(mkWire.Wire());
    mkPS.SetTolerance(Precision::Confusion());
    mkPS.SetTransitionMode(BRepBuilderAPI_Transformed);
    mkPS.SetMode(false);

    for (auto &wire : wires)
        mkPS.Add(TopoDS::Wire(wire.getShape()));

    if (!mkPS.IsReady())
        throw Base::CADKernelError("Shape could not be built");

    TopoShape result(0,hasher);
    result.makEShape(mkPS,wires);

    if (!mkPS.Shape().Closed()) {
        // shell is not closed - use simulate to get the end wires
        TopTools_ListOfShape sim;
        mkPS.Simulate(2, sim);

        TopoShape front(sim.First());
        if(front.countSubShapes(TopAbs_EDGE)==wires.front().countSubShapes(TopAbs_EDGE)) {
            front = wires.front();
            front.setShape(sim.First(),false);
        }else
            front.Tag = -wires.front().Tag;
        TopoShape back(sim.Last());
        if(back.countSubShapes(TopAbs_EDGE)==wires.back().countSubShapes(TopAbs_EDGE)) {
            back = wires.back();
            back.setShape(sim.Last(),false);
        }else
            back.Tag = -wires.back().Tag;

        // build the end faces, sew the shell and build the final solid
        front = front.makEFace();
        back = back.makEFace();

        BRepBuilderAPI_Sewing sewer;
        sewer.SetTolerance(Precision::Confusion());
        sewer.Add(front.getShape());
        sewer.Add(back.getShape());
        sewer.Add(result.getShape());

        sewer.Perform();
        result = result.makEShape(sewer);
    }

    result = result.makESolid();

    BRepClass3d_SolidClassifier SC(result.getShape());
    SC.PerformInfinitePoint(Precision::Confusion());
    if (SC.State() == TopAbs_IN) {
        result.setShape(result.getShape().Reversed(),false);
    }
    return result;
}

void ExtrusionHelper::makeDraft(const Parameters& params,
                                const TopoShape& _shape, 
                                std::vector<TopoShape>& drafts,
                                App::StringHasherRef hasher)
{
    double distanceFwd = tan(params.taperAngleFwd)*params.lengthFwd;
    double distanceRev = tan(params.taperAngleRev)*params.lengthRev;

    gp_Vec vecFwd = gp_Vec(params.dir)*params.lengthFwd;
    gp_Vec vecRev = gp_Vec(params.dir.Reversed())*params.lengthRev;

    bool bFwd = fabs(params.lengthFwd) > Precision::Confusion();
    bool bRev = fabs(params.lengthRev) > Precision::Confusion();
    bool bMid = !bFwd || !bRev || params.lengthFwd*params.lengthRev > 0.0; //include the source shape as loft section?

    TopoShape shape = _shape;
    TopoShape sourceWire;
    if (shape.isNull())
        Standard_Failure::Raise("Not a valid shape");

    if (params.solid)
        shape = shape.makEFace(nullptr, params.faceMakerClass.c_str());

    shape = shape.makERefine();

    if (shape.shapeType() == TopAbs_FACE) {
        std::vector<TopoShape> wires;
        TopoShape outerWire = shape.splitWires(&wires, TopoShape::ReorientForward);
        if (outerWire.isNull())
            Standard_Failure::Raise("Missing outer wire");
        if (wires.empty())
            shape = outerWire.getShape();
        else {
            unsigned pos = drafts.size();
            makeDraft(params, outerWire, drafts, hasher);
            if (drafts.size() != pos+1)
                Standard_Failure::Raise("Failed to make drafted extrusion");
            std::vector<TopoShape> inner;
            TopoShape innerWires(0, hasher);
            innerWires.makECompound(wires,"",false);
            Parameters copy = params;
            copy.taperAngleFwd = params.innerTaperAngleFwd;
            copy.taperAngleRev = params.innerTaperAngleRev;
            makeDraft(copy, innerWires, inner, hasher);
            if (inner.empty())
                Standard_Failure::Raise("Failed to make drafted extrusion with inner hole");
            inner.insert(inner.begin(), drafts.back());
            drafts.back().makECut(inner);
            return;
        }
    }

    if (shape.shapeType() == TopAbs_WIRE) {
        ShapeFix_Wire aFix;
        aFix.Load(TopoDS::Wire(shape.getShape()));
        aFix.FixReorder();
        aFix.FixConnected();
        aFix.FixClosed();
        sourceWire.setShape(aFix.Wire());
        sourceWire.Tag = shape.Tag;
        sourceWire.mapSubElement(shape);
    }
    else if (shape.shapeType() == TopAbs_COMPOUND) {
        for(auto &s : shape.getSubTopoShapes())
            makeDraft(params, s, drafts, hasher);
    }
    else {
        Standard_Failure::Raise("Only a wire or a face is supported");
    }

    if (!sourceWire.isNull()) {
        std::vector<TopoShape> list_of_sections;
        auto makeOffset = [&sourceWire](const gp_Vec& translation, double offset) -> TopoShape {
            gp_Trsf mat;
            mat.SetTranslation(translation);
            TopoShape offsetShape(sourceWire.makETransform(mat,"RV"));
            if (fabs(offset)>Precision::Confusion())
                offsetShape = offsetShape.makEOffset2D(offset, TopoShape::JoinType::Intersection);
            return offsetShape;
        };

        //first. add wire for reversed part of extrusion
        if (bRev){
            auto offsetShape = makeOffset(vecRev, distanceRev);
            if (offsetShape.isNull())
                Standard_Failure::Raise("Tapered shape is empty");
            TopAbs_ShapeEnum type = offsetShape.getShape().ShapeType();
            if (type == TopAbs_WIRE) {
                list_of_sections.push_back(offsetShape);
            }
            else if (type == TopAbs_EDGE) {
                list_of_sections.push_back(offsetShape.makEWires());
            }
            else {
                Standard_Failure::Raise("Tapered shape type is not supported");
            }
        }

        //next. Add source wire as middle section. Order is important.
        if (bMid){
            list_of_sections.push_back(sourceWire);
        }

        //finally. Forward extrusion offset wire.
        if (bFwd){
            auto offsetShape = makeOffset(vecFwd, distanceFwd);
            if (offsetShape.isNull())
                Standard_Failure::Raise("Tapered shape is empty");
            TopAbs_ShapeEnum type = offsetShape.getShape().ShapeType();
            if (type == TopAbs_WIRE) {
                list_of_sections.push_back(offsetShape);
            }
            else if (type == TopAbs_EDGE) {
                list_of_sections.push_back(offsetShape.makEWires());
            }
            else {
                Standard_Failure::Raise("Tapered shape type is not supported");
            }
        }

        try {
#if defined(__GNUC__) && defined (FC_OS_LINUX)
            Base::SignalException se;
#endif
            if (params.usepipe) {
                drafts.push_back(makeDraftUsingPipe(list_of_sections, hasher));
                if (params.linearize)
                    drafts.back().linearize(true, false);
                return;
            }

            //make loft
            BRepOffsetAPI_ThruSections mkGenerator(
                    params.solid ? Standard_True : Standard_False, /*ruled=*/Standard_True);
            for(auto &shape : list_of_sections)
                mkGenerator.AddWire(TopoDS::Wire(shape.getShape()));

            mkGenerator.Build();
            drafts.push_back(TopoShape(0,hasher).makEShape(mkGenerator,list_of_sections));
            if (params.linearize)
                drafts.back().linearize(true, false);
        }
        catch (Standard_Failure &){
            throw;
        }
        catch (...) {
            throw Base::CADKernelError("Unknown exception from BRepOffsetAPI_ThruSections");
        }
    }
}

/***************************************************************************
 *   Copyright (c) 2016 Victor Titov (DeepSOIC) <vv.titov@gmail.com>       *
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
# include <BRep_Builder.hxx>
# include <BRep_Tool.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <BRepBndLib.hxx>
# include <BRepBuilderAPI_Copy.hxx>
# include <BRepBuilderAPI_MakeFace.hxx>
# include <BRepClass_FaceClassifier.hxx>
# include <BRepLib_FindSurface.hxx>
# include <Geom_Plane.hxx>
# include <GeomAPI_ProjectPointOnSurf.hxx>
# include <Precision.hxx>
# include <Standard_Failure.hxx>
# include <TopoDS.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS_Iterator.hxx>
# include <TopTools_MapOfShape.hxx>
# include <QtGlobal>
#endif

#include "FaceMakerBullseye.h"
#include "FaceMakerCheese.h"

#include "TopoShape.h"
#include "WireJoiner.h"


using namespace Part;

TYPESYSTEM_SOURCE(Part::FaceMakerBullseye, Part::FaceMakerPublic)

void FaceMakerBullseye::setPlane(const gp_Pln &plane)
{
    this->myPlane = gp_Pln(plane);
    this->planeSupplied = true;
}

std::string FaceMakerBullseye::getUserFriendlyName() const
{
    return {QT_TRANSLATE_NOOP("Part_FaceMaker","Bull's-eye facemaker")};
}

std::string FaceMakerBullseye::getBriefExplanation() const
{
    return {QT_TRANSLATE_NOOP("Part_FaceMaker","Supports making planar faces with holes with islands.")};
}

bool FaceMakerBullseye::WireInfo::operator<(const WireInfo &other) const
{
    return extent - other.extent > Precision::Confusion();
}

// A wire that travels an edge both ways is a face boundary walk that went
// out over a bridge, around whatever hangs off it, and back: a hole tied to
// the outer boundary by a slit, say. WireJoiner's angle traversal emits such
// a walk as one wire, in walk order (makeDartWire()), the way it does for a
// hole touching the boundary at a vertex. An edge walked both ways bounds
// nothing, so it is dropped -- as the joiner already drops one it goes over
// and straight back -- and the walk falls apart into the loops the bridges
// connected: the stretch between going out over an edge and coming back over
// it is a loop on the far side of it, and what is left at the end is one
// more. The wire is a cycle and may begin anywhere in it, even on the far
// side of a bridge, so that nesting does not say which loop is the outer
// one: it is the one enclosing the rest, which the caller picks by extent.
// The loops behind the bridges are holes of the face the walk bounds; the
// region inside each is a face of its own, which the walk that enumerated
// this one has enumerated as well.
//
// Returns false, leaving the wire alone, if the wire has no such edge, or if
// what remains does not close as loops -- an unordered wire, or something
// other than a walk.
static bool splitDoubledWire(const TopoShape &wire, std::vector<TopoShape> &loops)
{
    TopTools_MapOfShape seen;
    TopTools_MapOfShape doubled;
    for (TopoDS_Iterator it(wire.getShape()); it.More(); it.Next()) {
        if (!seen.Add(it.Value()))
            doubled.Add(it.Value());
    }
    if (doubled.IsEmpty())
        return false;

    std::vector<std::vector<TopoDS_Edge>> found;
    std::vector<std::vector<TopoDS_Edge>> stack(1);
    TopTools_MapOfShape open;
    for (TopoDS_Iterator it(wire.getShape()); it.More(); it.Next()) {
        const auto &e = TopoDS::Edge(it.Value());
        if (!doubled.Contains(e)) {
            stack.back().push_back(e);
            continue;
        }
        if (open.Add(e)) {
            stack.emplace_back();
            continue;
        }
        // the way back: what was walked in between is a loop of its own
        if (stack.size() < 2)
            return false;
        found.push_back(std::move(stack.back()));
        stack.pop_back();
    }
    if (stack.size() != 1)
        return false;
    found.push_back(std::move(stack.front()));

    std::vector<TopoShape> result;
    BRep_Builder builder;
    for (const auto &edges : found) {
        if (edges.empty())
            continue;
        // with cumulative orientation, so that a loop walked backwards still
        // meets itself
        TopoDS_Vertex first = TopExp::FirstVertex(edges.front(), Standard_True);
        TopoDS_Vertex last = TopExp::LastVertex(edges.back(), Standard_True);
        if (first.IsNull() || last.IsNull() || !first.IsSame(last))
            return false;
        TopoDS_Wire w;
        builder.MakeWire(w);
        for (const auto &e : edges)
            builder.Add(w, e);
        w.Closed(true);
        result.emplace_back(wire.Tag, wire.Hasher, w);
        result.back().mapSubElement(wire);
    }
    if (result.empty())
        return false;
    loops = std::move(result);
    return true;
}

void FaceMakerBullseye::Build_Essence()
{
    if (myWires.empty())
        return;

    //validity check
    for (TopoDS_Wire& w : myWires) {
        if (!BRep_Tool::IsClosed(w))
            THROWM(Base::ValueError, "Wire is not closed.")
    }


    //find plane (at the same time, test that all wires are on the same plane)
    gp_Pln plane;
    if (this->planeSupplied) {
        plane = this->myPlane;
    }
    else {
        TopoDS_Builder builder;
        TopoDS_Compound comp;
        builder.MakeCompound(comp);
        for (TopoDS_Wire& w : myWires) {
            builder.Add(comp, BRepBuilderAPI_Copy(w).Shape());
        }
        BRepLib_FindSurface planeFinder(comp, -1, /*OnlyPlane=*/Standard_True);
        if (!planeFinder.Found())
            THROWM(Base::ValueError, "Wires are not coplanar.")
        plane = GeomAdaptor_Surface(planeFinder.Surface()).Plane();
    }

    std::vector<WireInfo> wireInfos;
    auto addWireInfo = [&](const TopoShape &w, bool cut) {
        Bnd_Box box;
        if (w.isNull())
            return;
        BRepBndLib::AddOptimal(w.getShape(), box, Standard_False);
        if (box.IsVoid())
            return;
        wireInfos.emplace_back(w, box);
        wireInfos.back().cut = cut;
    };
    for (const auto &w : this->myTopoWires) {
        std::vector<TopoShape> loops;
        if (!splitDoubledWire(w, loops)) {
            addWireInfo(w, false);
            continue;
        }
        // the outer loop encloses the rest; every other one is a hole
        std::size_t first = wireInfos.size();
        for (const auto &loop : loops)
            addWireInfo(loop, true);
        if (first == wireInfos.size())
            continue;
        auto outer = first;
        for (auto k = first + 1; k < wireInfos.size(); ++k) {
            if (wireInfos[k].extent > wireInfos[outer].extent)
                outer = k;
        }
        wireInfos[outer].cut = false;
    }
        
    // Sort wires by length of diagonal of bounding box.
    std::stable_sort(wireInfos.begin(), wireInfos.end());

    for (int i=0; i < (reuseInnerWire ? 2 : 1); ++i) {
        //add wires one by one to current set of faces.
        std::vector< std::unique_ptr<FaceDriller> > faces;
        for (auto it = wireInfos.begin(); it != wireInfos.end(); ) {

            //test if this wire is on any of existing faces (if yes, it's a hole;
            // if no, it's a beginning of a new face).
            FaceDriller* foundFace = nullptr;
            bool hitted = false;
            bool bounded = false;
            for(auto rit=faces.rbegin(); rit!=faces.rend(); ++rit){
                if ((*rit)->hasEdges(it->wire)) {
                    // every edge of the wire bounds that face already: the
                    // wire is the boundary of the face on the other side of
                    // one of its holes
                    bounded = true;
                    break;
                }
                switch((*rit)->hitTest(it->wire)) {
                case FaceDriller::HitTest::Hit:
                    foundFace = rit->get();
                    hitted = true;
                    break;
                case FaceDriller::HitTest::HitOuter:
                    // Shape in outer wire but not on face, which means it is
                    // within a hole. So it's a hit and we shall make a new face
                    // with the wire.
                    hitted = true;
                    break;
                default:
                    break;
                }
            }

            if (it->cut && (bounded || i > 0)) {
                // a loop cut out of a walk is a hole of the walk's face and
                // nothing else: the region inside it is on the list as a wire
                // of its own, and that wire makes the face there
                it = wireInfos.erase(it);
                continue;
            }

            TopoDS_Wire w = TopoDS::Wire(it->wire.getShape());

            if(foundFace){
                //wire is on a face.
                if (reuseInnerWire)
                    foundFace->addHole(*it, mySourceShapes);
                else
                    foundFace->addHole(w);
            } else {
                //wire is not on a face. Start a new face.
                faces.push_back(std::unique_ptr<FaceDriller>(
                                    new FaceDriller(plane, w)
                            ));
            }

            if (i==0 && reuseInnerWire && !hitted) {
                // If reuseInnerWire, then discard the outer-most wire, and
                // retry so that the previous hole (and nested hole) wires can
                // become outer wire for new faces.
                it = wireInfos.erase(it);
            } else
                ++it;
        }

        //and we are done!
        for(std::unique_ptr<FaceDriller> &ff : faces){
            this->myShapesToReturn.push_back(ff->Face());
        }
    }
}


FaceMakerBullseye::FaceDriller::FaceDriller(const gp_Pln& plane, TopoDS_Wire outerWire)
{
    this->myPlane = plane;
    this->myFace = TopoDS_Face();

    //Ensure correct orientation of the wire.
    if (getWireDirection(myPlane, outerWire) < 0)
        outerWire.Reverse();

    myHPlane = new Geom_Plane(this->myPlane);
    BRep_Builder builder;
    builder.MakeFace(this->myFace, myHPlane, Precision::Confusion());
    builder.Add(this->myFace, outerWire);
    this->myTopoFace = TopoShape(this->myFace);
    for (TopExp_Explorer xp(outerWire, TopAbs_EDGE); xp.More(); xp.Next())
        myEdges.Add(xp.Current());
}

bool FaceMakerBullseye::FaceDriller::hasEdges(const TopoShape &shape) const
{
    TopExp_Explorer xp(shape.getShape(), TopAbs_EDGE);
    if (!xp.More())
        return false;
    for (; xp.More(); xp.Next()) {
        if (!myEdges.Contains(xp.Current()))
            return false;
    }
    return true;
}

FaceMakerBullseye::FaceDriller::HitTest
FaceMakerBullseye::FaceDriller::hitTest(const TopoShape &shape) const
{
    auto vertex = TopoDS::Vertex(shape.getSubShape(TopAbs_VERTEX, 1));
    if (!myFaceBound.IsNull()) {
        if (myTopoFaceBound.findShape(vertex) > 0)
            return HitTest::HitNone;
        for (const auto &info : myHoles) {
            if (info.wire.findShape(vertex))
                return HitTest::Hit;
        }
    } else if (myTopoFace.findShape(vertex) > 0)
        return HitTest::HitNone;

    double tol = BRep_Tool::Tolerance(vertex);
    auto point = BRep_Tool::Pnt(vertex);
    double u,v;
    GeomAPI_ProjectPointOnSurf(point, myHPlane).LowerDistanceParameters(u,v);
    const char *err = "FaceMakerBullseye::FaceDriller::hitTest: result unknown.";
    auto hit = HitTest::HitNone;
    if (!myFaceBound.IsNull()) {
        BRepClass_FaceClassifier cl(myFaceBound, gp_Pnt2d(u,v), tol);
        switch (cl.State()) {
        case TopAbs_OUT:
        case TopAbs_ON:
            return HitTest::HitNone;
        case TopAbs_IN:
            hit = HitTest::HitOuter;
            break;
        default:
            THROWM(Base::ValueError, err)
        }
    }
    BRepClass_FaceClassifier cl(myFace, gp_Pnt2d(u,v), tol);
    TopAbs_State ret = cl.State();
    switch(ret){
    case TopAbs_IN:
        return HitTest::Hit;
    case TopAbs_ON:
        if (hit == HitTest::HitOuter) {
            // the given point is within the outer wire, but on some other wire
            // of the face, which must be a hole wire, which means that two hole
            // wires have shared vertex (or edge). We can deal with this if
            // reuseInnerWire is on by merging these holes.
            return HitTest::Hit;
        }
        return HitTest::HitNone;
    case TopAbs_OUT:
        return hit;
    default:
        THROWM(Base::ValueError, err)
    }

}

void FaceMakerBullseye::FaceDriller::copyFaceBound(TopoDS_Face &face, TopoShape &topoFace, const TopoShape &source)
{
    face = BRepBuilderAPI_MakeFace(myHPlane, TopoDS::Wire(source.getSubShape(TopAbs_WIRE, 1)));
    topoFace = TopoShape(face);
}

void FaceMakerBullseye::FaceDriller::addHole(TopoDS_Wire w)
{
    //Ensure correct orientation of the wire.
    if (getWireDirection(myPlane, w) > 0) //if wire is CCW..
        w.Reverse();   //.. we want CW!

    if (this->myFaceBound.IsNull())
        copyFaceBound(this->myFaceBound, this->myTopoFaceBound, this->myTopoFace);

    BRep_Builder builder;
    builder.Add(this->myFace, w);
    for (TopExp_Explorer xp(w, TopAbs_EDGE); xp.More(); xp.Next())
        myEdges.Add(xp.Current());
}

void FaceMakerBullseye::FaceDriller::addHole(const WireInfo &wireInfo,
                                             std::vector<TopoShape> &sources)
{
    if (this->myFaceBound.IsNull())
        copyFaceBound(this->myFaceBound, this->myTopoFaceBound, this->myTopoFace);

    if (!myJoiner) {
        myJoiner.reset(new WireJoiner);
        myJoiner->setOutline(true);
    }
    myJoiner->addShape(wireInfo.wire);

    bool intersected = false;
    for (const auto &info : myHoles) {
        if (!info.bound.IsOut(wireInfo.bound)
                || !wireInfo.bound.IsOut(info.bound)) {
            intersected = true;
            break;
        }
    }

    myHoles.push_back(wireInfo);
    for (TopExp_Explorer xp(wireInfo.wire.getShape(), TopAbs_EDGE); xp.More(); xp.Next())
        myEdges.Add(xp.Current());
    TopoShape wire = wireInfo.wire;

    if (intersected) {
        TopoShape hole;
        // Join intersected wires and get their outline
        myJoiner->getResultWires(hole);
        // Check if the hole gets merged.
        if (!hole.findShape(wireInfo.wire.getShape())) {
            for (const auto &e : wireInfo.wire.getSubTopoShapes(TopAbs_EDGE)) {
                if (hole.findShape(e.getShape()) > 0)
                    continue;
                for (const auto &e : hole.searchSubShape(e.getShape()))
                    sources.push_back(e);
            }
            copyFaceBound(this->myFace, this->myTopoFace, this->myTopoFaceBound);
            wire = hole;
        }
    }

    BRep_Builder builder;
    for (const auto &w : wire.getSubShapes(TopAbs_WIRE)) {
        //Ensure correct orientation of the wire.
        if (getWireDirection(myPlane, TopoDS::Wire(w)) > 0) //if wire is CCW..
            builder.Add(this->myFace, TopoDS::Wire(w.Reversed())); //.. we want CW!
        else
            builder.Add(this->myFace, TopoDS::Wire(w));
    }
}

int FaceMakerBullseye::FaceDriller::getWireDirection(const gp_Pln& plane, const TopoDS_Wire& wire)
{
    //make a test face
    BRepBuilderAPI_MakeFace mkFace(wire, /*onlyplane=*/Standard_True);
    TopoDS_Face tmpFace = mkFace.Face();
    if (tmpFace.IsNull()) {
        throw Standard_Failure("getWireDirection: Failed to create face from wire");
    }

    //compare face surface normal with our plane's one
    BRepAdaptor_Surface surf(tmpFace);
    bool normal_co = surf.Plane().Axis().Direction().Dot(plane.Axis().Direction()) > 0;

    //unlikely, but just in case OCC decided to reverse our wire for the face...  take that into account!
    TopoDS_Iterator it(tmpFace, /*CumOri=*/Standard_False);
    normal_co ^= it.Value().Orientation() != wire.Orientation();

    return normal_co ? 1 : -1;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

TYPESYSTEM_SOURCE(Part::FaceMakerRing, Part::FaceMakerBullseye)

FaceMakerRing::FaceMakerRing()
{
    reuseInnerWire = true;
}

std::string FaceMakerRing::getUserFriendlyName() const
{
    return std::string(QT_TRANSLATE_NOOP("Part_FaceMaker","Ring facemaker"));
}

std::string FaceMakerRing::getBriefExplanation() const
{
    return std::string(QT_TRANSLATE_NOOP("Part_FaceMaker","Supports making planar faces with holes and holes as faces."));
}


/***************************************************************************
 *   Copyright (c) 2019 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <BRepAdaptor_Curve.hxx>
# include <BRepBuilderAPI_MakeEdge.hxx>
# include <BRepBuilderAPI_MakeWire.hxx>
# include <Geom_BSplineCurve.hxx>
# include <Precision.hxx>
# include <ShapeConstruct_Curve.hxx>
# include <Standard_ConstructionError.hxx>
# include <Standard_Version.hxx>
# include <TopExp_Explorer.hxx>
# include <TopLoc_Location.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Compound.hxx>
# include <TopoDS_Edge.hxx>
# include <TopoDS_Wire.hxx>
# include <TopTools_ListIteratorOfListOfShape.hxx>
# include <TopTools_ListOfShape.hxx>
# include <TopTools_MapOfShape.hxx>
# include <Geom_Circle.hxx>
#endif

#include "BRepOffsetAPI_MakeOffsetFix.h"
#include "Geometry.h"
#include "PartFeature.h"


using namespace Part;

BRepOffsetAPI_MakeOffsetFix::BRepOffsetAPI_MakeOffsetFix() = default;

BRepOffsetAPI_MakeOffsetFix::BRepOffsetAPI_MakeOffsetFix(const GeomAbs_JoinType Join, const Standard_Boolean IsOpenResult)
    :myJoin(Join)
    ,myIsOpenResult(IsOpenResult)
{
}

BRepOffsetAPI_MakeOffsetFix::~BRepOffsetAPI_MakeOffsetFix() = default;

void BRepOffsetAPI_MakeOffsetFix::AddWire(const TopoDS_Wire& Spine)
{
    myWires.push_back(Spine);
    myResult.Nullify();
}

void BRepOffsetAPI_MakeOffsetFix::Perform (const Standard_Real Offset, const Standard_Real Alt)
{
    myMap.clear();
    myOffsetCircles.clear();
    mkOffset.reset();
    for (auto s : myWires) {
        TopExp_Explorer xp(s, TopAbs_EDGE);
        if (xp.More()) {
            TopoDS_Edge E = TopoDS::Edge(xp.Current());
            xp.Next();
            if (!xp.More()) {
                if (Offset < 0.) {
                    // Work around on OCCT crash when shrink a circle with
                    // radius equals to one, by manually create a new circle
                    // with reduced radius.
                    //
                    // See a possible fix of this OCCT bug at
                    // https://github.com/realthunder/OCCT/commit/e05d9f709f36e1f34c5bcac491be63c0a89b2136
                    Standard_Real f,l;
                    TopLoc_Location L;
                    Handle(Geom_Curve) C =  BRep_Tool::Curve(E,L,f,l);
                    if (C->IsKind(STANDARD_TYPE(Geom_Circle)) && BRep_Tool::IsClosed(E)) {
                        Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(C);
                        if (circle->Radius() <= -Offset)
                            continue;
                        if (circle->Radius() <= 1.) {
                            GeomCircle gCircle;
                            gp_Ax1 axis = circle->Axis();
                            const gp_Pnt& loc = axis.Location();
                            gCircle.setLocation(Base::Vector3d(loc.X(), loc.Y(), loc.Z()));
                            gCircle.setRadius(circle->Radius() + Offset);
                            myOffsetCircles.push_back(TopoDS::Edge(gCircle.toShape().Located(L)));
                            myMap[E].Append(myOffsetCircles.back());
                            continue;
                        }
                    }
                }

                // The trick is to reset the placement of an edge before
                // passing it to BRepOffsetAPI_MakeOffset because then it
                // will create the expected result.
                // Afterwards apply the placement again on the result shape.
                // See the method MakeWire()
                auto e = TopoDS::Edge(E.Located(TopLoc_Location()));
                myLocations.emplace_back(e, E.Location());
                s = BRepBuilderAPI_MakeWire(e).Wire();
            }
        }
        if (!mkOffset) {
            mkOffset.reset(new BRepOffsetAPI_MakeOffset);
#if OCC_VERSION_HEX >= 0x060900
            mkOffset->Init(myJoin, myIsOpenResult);
#else
            mkOffset->Init(myJoin);
#endif
        }
        mkOffset->AddWire(s);
    }
    if (mkOffset)
        mkOffset->Perform(Offset, Alt);
}

#if OCC_VERSION_HEX >= 0x070600
void BRepOffsetAPI_MakeOffsetFix::Build(const Message_ProgressRange&)
#else
void BRepOffsetAPI_MakeOffsetFix::Build()
#endif
{
    if (mkOffset)
        mkOffset->Build();
}

void BRepOffsetAPI_MakeOffsetFix::Init(const TopoDS_Face& Spine, const GeomAbs_JoinType Join,
                                       const Standard_Boolean IsOpenResult)
{
    if (!mkOffset) {
        mkOffset.reset(new BRepOffsetAPI_MakeOffset);
    }
    mkOffset->Init(Spine, Join, IsOpenResult);
}

void BRepOffsetAPI_MakeOffsetFix::Init(const GeomAbs_JoinType Join, const Standard_Boolean IsOpenResult)
{
    if (!mkOffset) {
        mkOffset.reset(new BRepOffsetAPI_MakeOffset);
    }
    mkOffset->Init(Join, IsOpenResult);
}

Standard_Boolean BRepOffsetAPI_MakeOffsetFix::IsDone() const
{
    if (mkOffset)
        return mkOffset->IsDone();
    return !myOffsetCircles.empty();
}

void BRepOffsetAPI_MakeOffsetFix::MakeWire(TopoDS_Shape& wire)
{
    if (!mkOffset)
        return;

    // get the edges of the wire and check which of the stored edges
    // serve as input of the wire
    TopTools_MapOfShape aMap;
    TopExp_Explorer xp(wire, TopAbs_EDGE);
    while (xp.More()) {
        aMap.Add(xp.Current());
        xp.Next();
    }

    myMap.clear();
    std::list<TopoDS_Edge> edgeList;
    for (const auto& itLoc : myLocations) {
        TopTools_ListOfShape newShapes = mkOffset->Generated(itLoc.first);
        auto &entry = myMap[itLoc.first.Located(itLoc.second)];
        // Check generated shapes for the vertexes, too
        TopExp_Explorer xpv(itLoc.first, TopAbs_VERTEX);
        while (xpv.More()) {
            TopTools_ListOfShape newEdge = mkOffset->Generated(xpv.Current());
            if (!newEdge.IsEmpty()) {
                newShapes.Append(newEdge);
            }
            xpv.Next();
        }
        for (TopTools_ListIteratorOfListOfShape it(newShapes); it.More(); it.Next()) {
            TopoDS_Shape newShape = it.Value();
            if (aMap.Contains(newShape)) {
                TopoShape::move(newShape,itLoc.second);
                edgeList.push_back(TopoDS::Edge(newShape));
                entry.Append(newShape);
            }
        }
    }

    if (!edgeList.empty()) {
        BRepBuilderAPI_MakeWire mkWire;
        mkWire.Add(edgeList.front());
        edgeList.pop_front();
        wire = mkWire.Wire();

        bool found = false;
        do {
            found = false;
            for (std::list<TopoDS_Edge>::iterator pE = edgeList.begin(); pE != edgeList.end(); ++pE) {
                mkWire.Add(*pE);
                if (mkWire.Error() != BRepBuilderAPI_DisconnectedWire) {
                    // edge added ==> remove it from list
                    found = true;
                    edgeList.erase(pE);
                    wire = mkWire.Wire();
                    break;
                }
            }
        }
        while (found);
    }
}

const TopoDS_Shape& BRepOffsetAPI_MakeOffsetFix::Shape()
{
    if (myResult.IsNull()) {
        BRep_Builder builder;
        TopoDS_Compound comp;
        builder.MakeCompound(comp);
        int count = 0;
        for (const auto &e : myOffsetCircles) {
            builder.Add(comp, BRepBuilderAPI_MakeWire(e).Wire());
            ++count;
        }
        if (!mkOffset) {
            if (count)
                myResult = comp;
        } else {
            TopoDS_Shape result = mkOffset->Shape();
            if (!result.IsNull()) {
                if (result.ShapeType() == TopAbs_WIRE) {
                    MakeWire(result);
                    if (count) {
                        builder.Add(comp, result);
                        ++count;
                    }
                }
                else if (result.ShapeType() == TopAbs_COMPOUND) {
                    TopExp_Explorer xp(result, TopAbs_WIRE);
                    while (xp.More()) {
                        TopoDS_Wire wire = TopoDS::Wire(xp.Current());
                        MakeWire(wire);
                        builder.Add(comp, wire);
                        ++count;
                        xp.Next();
                    }
                }
            }
            myResult = count ? comp : result;
        }
    }
    return myResult;
}

const TopTools_ListOfShape& BRepOffsetAPI_MakeOffsetFix::Generated(const TopoDS_Shape& S)
{
    static TopTools_ListOfShape dummy;
    auto it = myMap.find(S);
    if (it == myMap.end()) {
        if (mkOffset)
            return mkOffset->Generated(S);
        return dummy;
    }
    return it->second;
}

const TopTools_ListOfShape& BRepOffsetAPI_MakeOffsetFix::Modified (const TopoDS_Shape& S)
{
    static TopTools_ListOfShape dummy;
    if (mkOffset)
        return mkOffset->Modified(S);
    return dummy;
}

Standard_Boolean BRepOffsetAPI_MakeOffsetFix::IsDeleted (const TopoDS_Shape& S)
{
    if (mkOffset)
        return mkOffset->IsDeleted(S);
    return Standard_False;
}

TopoDS_Shape BRepOffsetAPI_MakeOffsetFix::Replace(GeomAbs_CurveType type, const TopoDS_Shape& S) const
{
    if (S.IsNull())
        throw Standard_ConstructionError("Input shape is null");

    // Nothing to do
    if (type == GeomAbs_BSplineCurve)
        return S;

    if (S.ShapeType() == TopAbs_COMPOUND) {
        BRep_Builder builder;
        TopoDS_Compound comp;
        builder.MakeCompound(comp);
        comp.Location(S.Location());

        TopExp_Explorer xp(S, TopAbs_WIRE);
        while (xp.More()) {
            TopoDS_Wire wire = TopoDS::Wire(xp.Current());
            wire = ReplaceEdges(type, wire);
            builder.Add(comp, wire);
            xp.Next();
        }

        return TopoDS_Compound(std::move(comp));
    }
    else if (S.ShapeType() == TopAbs_WIRE) {
        return ReplaceEdges(type, TopoDS::Wire(S));
    }
    else {
        throw Standard_ConstructionError("Wrong shape type");
    }
}

TopoDS_Wire BRepOffsetAPI_MakeOffsetFix::ReplaceEdges(GeomAbs_CurveType type, const TopoDS_Wire& wire) const
{
    BRepBuilderAPI_MakeWire mkWire;
    for (TopExp_Explorer xp(wire, TopAbs_EDGE); xp.More(); xp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(xp.Current());
        TopLoc_Location edgeLocation = edge.Location();

        BRepAdaptor_Curve curve(edge);

        if (curve.GetType() == type) {
            ShapeConstruct_Curve scc;
            double u = curve.FirstParameter();
            double v = curve.LastParameter();
            Handle(Geom_BSplineCurve) spline = scc.ConvertToBSpline(curve.Curve().Curve(), u, v, Precision::Confusion());
            if (!spline.IsNull()) {
                BRepBuilderAPI_MakeEdge mkEdge(spline, u, v);
                edge = mkEdge.Edge();
                edge.Location(edgeLocation);
            }
        }

        mkWire.Add(edge);
    }

    return mkWire.Wire();
}

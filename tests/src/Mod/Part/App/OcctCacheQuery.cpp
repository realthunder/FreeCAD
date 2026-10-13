// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <BRep_CurveRepresentation.hxx>
#include <BRep_TEdge.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_Plane.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pln.hxx>

#include <Mod/Part/App/PartPyCXX.h>

// Part asks the OCCT fork whether a representation is a cache through a C
// function it looks up by name at run time (Part::isOCCTCacheRepresentation),
// so that Part still loads against upstream OCCT, where there is no such
// function and nothing is a cache. A frozen edge given a pcurve on a surface
// it has none on yet holds that pcurve as a cache; its own pcurve is not one.
TEST(OcctCacheQuery, aFrozenEdgeTakesAPCurveAsACache)
{
    if (!Part::lookUpTKBRep("BRep_Builder_IsCache")) {
        GTEST_SKIP() << "no BRep_Builder_IsCache: not the OCCT fork, or one from before it";
    }
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(1, 0, 0));
    BRep_Builder builder;
    Handle(Geom_Plane) own = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
    Handle(Geom_Plane) other = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)));
    const TopLoc_Location none;
    builder.UpdateEdge(edge, new Geom2d_Line(gp_Pnt2d(0, 0), gp_Dir2d(1, 0)), own, none, 1e-7);
    edge.TShape()->Immutable(true);
    builder.UpdateEdge(edge, new Geom2d_Line(gp_Pnt2d(0, 0), gp_Dir2d(1, 0)), other, none, 1e-7);

    int ownCaches = 0;
    int otherCaches = 0;
    int pcurves = 0;
    for (const auto& rep : Handle(BRep_TEdge)::DownCast(edge.TShape())->Curves()) {
        if (!rep->IsCurveOnSurface()) {
            continue;
        }
        ++pcurves;
        const bool isCache = Part::isOCCTCacheRepresentation(rep.get());
        (rep->Surface() == own ? ownCaches : otherCaches) += isCache ? 1 : 0;
    }
    EXPECT_EQ(pcurves, 2);
    EXPECT_EQ(ownCaches, 0);
    EXPECT_EQ(otherCaches, 1);
    EXPECT_FALSE(Part::isOCCTCacheRepresentation(nullptr));
}

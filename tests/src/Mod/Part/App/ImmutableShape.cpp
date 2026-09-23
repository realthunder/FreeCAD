// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_LockedShape.hxx>
#include <TopoDS_FrozenShape.hxx>

// The OCCT fork's Immutable flag (docs/TransactionLog.md sec 23.6): a
// shape held as a property value refuses every change to its geometry and
// topology but still takes the caches a mesher writes.
namespace {

void setImmutable(const TopoDS_Shape& shape)
{
    shape.TShape()->Immutable(true);
    for (TopoDS_Iterator it(shape); it.More(); it.Next())
        setImmutable(it.Value());
}

} // namespace

TEST(ImmutableShapeTest, meshesButRefusesEdits)
{
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    setImmutable(box);
    TopExp_Explorer faces(box, TopAbs_FACE);
    ASSERT_TRUE(faces.More());
    const TopoDS_Face face = TopoDS::Face(faces.Current());
    EXPECT_TRUE(face.Immutable());

    // The cache path: BRepMesh writes triangulation into every face and
    // polygons into every edge through the six carved-out setters.
    BRepMesh_IncrementalMesh mesh(box, 0.5);
    TopLoc_Location loc;
    EXPECT_FALSE(BRep_Tool::Triangulation(face, loc).IsNull());

    // The value path: geometry, tolerance and topology throw.
    BRep_Builder builder;
    EXPECT_THROW(builder.UpdateFace(face, 0.1), TopoDS_LockedShape);
    TopExp_Explorer edges(box, TopAbs_EDGE);
    const TopoDS_Edge edge = TopoDS::Edge(edges.Current());
    EXPECT_THROW(builder.UpdateEdge(edge, 0.1), TopoDS_LockedShape);
    EXPECT_THROW(builder.Range(edge, 0.0, 1.0), TopoDS_LockedShape);
    TopoDS_Shape copy = box;
    EXPECT_THROW(builder.Add(copy, BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(1, 1, 1)).Edge()),
                 TopoDS_FrozenShape);
    EXPECT_THROW(builder.Remove(copy, face), TopoDS_FrozenShape);

    // Locked is untouched: never set by this, and a plain shape is neither.
    EXPECT_FALSE(face.Locked());
    TopoDS_Shape plain = BRepPrimAPI_MakeBox(1, 1, 1).Shape();
    EXPECT_FALSE(plain.Immutable());
    const TopoDS_Face plainFace = TopoDS::Face(TopExp_Explorer(plain, TopAbs_FACE).Current());
    EXPECT_NO_THROW(builder.UpdateFace(plainFace, 0.1));
}

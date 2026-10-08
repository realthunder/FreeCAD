// SPDX-License-Identifier: LGPL-2.1-or-later

// The private twin a worker meshes in place of a document's shape
// (docs/DocumentLoad.md sec 18.9): what it copies and what it shares, that
// the original is not touched until the landing, and that the landing leaves
// on the original what a mesh made in place would have.
//
// No document and no application: shapes and OCCT's mesher.

#include <gtest/gtest.h>

#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_CurveRepresentation.hxx>
#include <BRep_TEdge.hxx>
#include <BRep_TFace.hxx>
#include <BRep_TVertex.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeCircle.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <Mod/Part/App/MeshTwin.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{

TopoDS_Shape solid()
{
    // Planes, a cylinder and a sphere with their seams, edges shared
    // between faces
    TopoDS_Shape box = BRepPrimAPI_MakeBox(40, 30, 20).Shape();
    TopoDS_Shape cyl =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20, 15, 10), gp_Dir(0, 0, 1)), 8, 30).Shape();
    TopoDS_Shape sph = BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, 20), 9).Shape();
    return BRepAlgoAPI_Cut(BRepAlgoAPI_Fuse(box, cyl).Shape(), sph).Shape();
}

TopoDS_Shape torus()
{
    return BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(80, 0, 0), gp_Dir(0, 0, 1)), 20, 5).Shape();
}

/// The solid twice, at two places, and a torus: \a parts gets the three.
TopoDS_Shape model(std::vector<TopoDS_Shape>* parts = nullptr)
{
    TopoDS_Shape one = solid();
    gp_Trsf move;
    move.SetTranslation(gp_Vec(0, 100, 0));
    TopoDS_Shape two = one.Moved(TopLoc_Location(move));
    TopoDS_Shape three = torus();
    TopoDS_Compound all;
    BRep_Builder builder;
    builder.MakeCompound(all);
    builder.Add(all, one);
    builder.Add(all, two);
    builder.Add(all, three);
    if (parts) {
        *parts = {one, two, three};
    }
    return all;
}

/// A line and a circle, on no face.
TopoDS_Shape freeEdges()
{
    TopoDS_Compound all;
    BRep_Builder builder;
    builder.MakeCompound(all);
    builder.Add(all, BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge());
    builder.Add(all,
                BRepBuilderAPI_MakeEdge(
                    GC_MakeCircle(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), 5.0).Value())
                    .Edge());
    return all;
}

IMeshTools_Parameters ask(double scale = 1.0)
{
    IMeshTools_Parameters params;
    params.Deflection = 0.05 * scale;
    params.Angle = 0.3 * scale;
    params.Relative = Standard_False;
    params.InParallel = Standard_False;
    params.AllowQualityDecrease = Standard_True;
    return params;
}

void mesh(const TopoDS_Shape& shape, double scale = 1.0)
{
    BRepMesh_IncrementalMesh mesher(shape, ask(scale));
}

void everyTShape(const TopoDS_Shape& shape, std::unordered_set<const void*>& out)
{
    if (!out.insert(shape.TShape().get()).second) {
        return;
    }
    for (TopoDS_Iterator it(shape, false, false); it.More(); it.Next()) {
        everyTShape(it.Value(), out);
    }
}

/// The distinct faces or edges of \a shape, in the order an explorer meets
/// them.
std::vector<TopoDS_Shape> distinct(const TopoDS_Shape& shape, TopAbs_ShapeEnum kind)
{
    std::vector<TopoDS_Shape> out;
    std::unordered_set<const void*> seen;
    for (TopExp_Explorer xp(shape, kind); xp.More(); xp.Next()) {
        if (seen.insert(xp.Current().TShape().get()).second) {
            out.push_back(xp.Current());
        }
    }
    return out;
}

Handle(Poly_Triangulation) active(const TopoDS_Shape& face)
{
    return Handle(BRep_TFace)::DownCast(face.TShape())->ActiveTriangulation();
}

std::vector<const void*> handles(const TopoDS_Shape& shape)
{
    std::vector<const void*> out;
    for (const TopoDS_Shape& face : distinct(shape, TopAbs_FACE)) {
        out.push_back(active(face).get());
    }
    return out;
}

struct Counts
{
    int faces = 0, meshed = 0, triangles = 0, nodes = 0;
    int edges = 0, onTriangulation = 0, own = 0;
    /// Polygons that index a triangulation no face of the shape holds.
    int stale = 0;
    /// Active triangulations without OCCT's "active" purpose bit.
    int unmarked = 0;

    bool operator==(const Counts& other) const
    {
        return faces == other.faces && meshed == other.meshed && triangles == other.triangles
            && nodes == other.nodes && edges == other.edges
            && onTriangulation == other.onTriangulation && own == other.own
            && stale == other.stale && unmarked == other.unmarked;
    }
};

std::ostream& operator<<(std::ostream& os, const Counts& c)
{
    return os << c.meshed << " of " << c.faces << " faces meshed, " << c.triangles
              << " triangles, " << c.nodes << " nodes; " << c.edges << " edges, "
              << c.onTriangulation << " polygons on triangulations (" << c.stale << " stale), "
              << c.own << " own polygons; " << c.unmarked << " active not marked";
}

Counts count(const TopoDS_Shape& shape)
{
    Counts c;
    std::unordered_set<const void*> held;
    for (const TopoDS_Shape& face : distinct(shape, TopAbs_FACE)) {
        ++c.faces;
        Handle(Poly_Triangulation) tri = active(face);
        if (tri.IsNull()) {
            continue;
        }
        held.insert(tri.get());
        ++c.meshed;
        c.triangles += tri->NbTriangles();
        c.nodes += tri->NbNodes();
        c.unmarked += (tri->MeshPurpose() & Poly_MeshPurpose_Active) ? 0 : 1;
    }
    for (const TopoDS_Shape& edge : distinct(shape, TopAbs_EDGE)) {
        ++c.edges;
        for (const auto& rep : Handle(BRep_TEdge)::DownCast(edge.TShape())->Curves()) {
            if (rep->IsPolygonOnTriangulation()) {
                ++c.onTriangulation;
                c.stale += held.count(rep->Triangulation().get()) ? 0 : 1;
            }
            else if (rep->IsPolygon3D()) {
                ++c.own;
            }
        }
    }
    return c;
}

}  // namespace

TEST(MeshTwin, isNewAtEveryLevel)
{
    const TopoDS_Shape original = model();
    Part::MeshTwin twin(original);
    ASSERT_FALSE(twin.isNull());

    std::unordered_set<const void*> mine;
    std::unordered_set<const void*> theirs;
    everyTShape(original, mine);
    everyTShape(twin.shape(), theirs);
    int shared = 0;
    for (const void* tshape : theirs) {
        shared += mine.count(tshape) ? 1 : 0;
    }
    EXPECT_EQ(shared, 0) << "TShapes the twin has in common with its original";
    // One for one: a solid the original holds at two places is one solid in
    // the twin too (BRepBuilderAPI_Copy makes two of it)
    EXPECT_EQ(theirs.size(), mine.size());
    EXPECT_EQ(twin.faceCount(), distinct(original, TopAbs_FACE).size());
    EXPECT_EQ(twin.edgeCount(), distinct(original, TopAbs_EDGE).size());
}

TEST(MeshTwin, keepsTheOrderAndLiesOnTheSameGeometry)
{
    gp_Trsf move;
    move.SetTranslation(gp_Vec(7, 0, 0));
    const TopoDS_Shape original = model().Moved(TopLoc_Location(move)).Reversed();
    Part::MeshTwin twin(original);
    EXPECT_TRUE(twin.shape().Location() == original.Location());
    EXPECT_EQ(twin.shape().Orientation(), original.Orientation());

    // Every face, the ones met twice too, in the order an explorer gives
    TopExp_Explorer of(original, TopAbs_FACE);
    TopExp_Explorer tf(twin.shape(), TopAbs_FACE);
    int faces = 0;
    for (; of.More() && tf.More(); of.Next(), tf.Next(), ++faces) {
        TopLoc_Location lo;
        TopLoc_Location lt;
        EXPECT_TRUE(BRep_Tool::Surface(TopoDS::Face(of.Current()), lo)
                    == BRep_Tool::Surface(TopoDS::Face(tf.Current()), lt));
        EXPECT_TRUE(lo == lt);
        EXPECT_EQ(of.Current().Orientation(), tf.Current().Orientation());
    }
    EXPECT_FALSE(of.More() || tf.More());
    // The solid is met twice
    EXPECT_GT(faces, int(twin.faceCount()));

    TopExp_Explorer oe(original, TopAbs_EDGE);
    TopExp_Explorer te(twin.shape(), TopAbs_EDGE);
    for (; oe.More() && te.More(); oe.Next(), te.Next()) {
        TopLoc_Location lo;
        TopLoc_Location lt;
        double f0 = 0;
        double l0 = 0;
        double f1 = 0;
        double l1 = 0;
        EXPECT_TRUE(BRep_Tool::Curve(TopoDS::Edge(oe.Current()), lo, f0, l0)
                    == BRep_Tool::Curve(TopoDS::Edge(te.Current()), lt, f1, l1));
        EXPECT_EQ(f0, f1);
        EXPECT_EQ(l0, l1);
        EXPECT_EQ(BRep_Tool::Tolerance(TopoDS::Edge(oe.Current())),
                  BRep_Tool::Tolerance(TopoDS::Edge(te.Current())));
        EXPECT_EQ(BRep_Tool::Degenerated(TopoDS::Edge(oe.Current())),
                  BRep_Tool::Degenerated(TopoDS::Edge(te.Current())));
    }
    EXPECT_FALSE(oe.More() || te.More());
}

TEST(MeshTwin, meshingTheTwinLeavesTheOriginalAlone)
{
    const TopoDS_Shape original = model();
    Part::MeshTwin twin(original);
    mesh(twin.shape());
    EXPECT_GT(count(twin.shape()).triangles, 0);
    const Counts c = count(original);
    EXPECT_EQ(c.meshed, 0);
    EXPECT_EQ(c.onTriangulation, 0);
    EXPECT_EQ(c.own, 0);
}

TEST(MeshTwin, landsWhatAMeshInPlaceGives)
{
    const TopoDS_Shape direct = model();
    mesh(direct);
    const Counts reference = count(direct);
    ASSERT_EQ(reference.meshed, reference.faces);

    const TopoDS_Shape original = model();
    Part::MeshTwin twin(original);
    mesh(twin.shape());
    const Part::MeshTwin::Landed landed = twin.land();
    EXPECT_EQ(landed.faces, reference.faces);
    EXPECT_EQ(landed.overtaken, 0);
    EXPECT_EQ(landed.polygonsAdded, reference.onTriangulation);
    EXPECT_EQ(landed.polygonsRemoved, 0);
    EXPECT_EQ(count(original), reference) << count(original) << "\n" << reference;

    // OCCT takes it for a complete mesh, and its own mesher, asked for the
    // same one, leaves every triangulation as it was landed
    EXPECT_TRUE(BRepTools::Triangulation(original, ask().Deflection, Standard_True));
    const std::vector<const void*> before = handles(original);
    mesh(original);
    EXPECT_EQ(handles(original), before);
    // ... which a finer ask does not, so the line above can fail
    mesh(original, 0.2);
    EXPECT_NE(handles(original), before);
}

TEST(MeshTwin, leavesAloneWhatTheOriginalAlreadyHolds)
{
    const TopoDS_Shape direct = model();
    mesh(direct);
    const Counts reference = count(direct);

    std::vector<TopoDS_Shape> parts;
    const TopoDS_Shape original = model(&parts);
    mesh(parts[2]);  // the torus only
    const std::vector<const void*> torusBefore = handles(parts[2]);
    const int torusFaces = int(torusBefore.size());

    Part::MeshTwin twin(original);
    mesh(twin.shape());
    const Part::MeshTwin::Landed landed = twin.land();
    EXPECT_EQ(landed.faces, reference.faces - torusFaces);
    EXPECT_EQ(landed.overtaken, 0);
    EXPECT_EQ(handles(parts[2]), torusBefore);
    EXPECT_EQ(count(original), reference) << count(original) << "\n" << reference;
}

TEST(MeshTwin, aFinerAskReplacesAndTakesTheOldPolygonsAway)
{
    const TopoDS_Shape direct = model();
    mesh(direct, 4.0);
    mesh(direct);
    const Counts reference = count(direct);
    ASSERT_EQ(reference.stale, 0);

    const TopoDS_Shape original = model();
    mesh(original, 4.0);
    const Counts coarse = count(original);
    ASSERT_LT(coarse.triangles, reference.triangles);
    const std::vector<const void*> before = handles(original);

    Part::MeshTwin twin(original);
    mesh(twin.shape());
    const Part::MeshTwin::Landed landed = twin.land();
    EXPECT_EQ(landed.overtaken, 0);
    EXPECT_GT(landed.faces, 0);
    EXPECT_GT(landed.polygonsRemoved, 0);
    EXPECT_NE(handles(original), before);
    EXPECT_EQ(count(original), reference) << count(original) << "\n" << reference;
}

TEST(MeshTwin, aFaceMeshedInTheMeantimeIsLeftAlone)
{
    const TopoDS_Shape original = model();
    Part::MeshTwin twin(original);
    mesh(twin.shape());
    // Somebody meshes the original before the twin lands
    mesh(original);
    const Counts meanwhile = count(original);
    const std::vector<const void*> before = handles(original);

    const Part::MeshTwin::Landed landed = twin.land();
    EXPECT_EQ(landed.faces, 0);
    EXPECT_EQ(landed.overtaken, meanwhile.faces);
    EXPECT_EQ(landed.polygonsAdded, 0);
    EXPECT_EQ(landed.polygonsRemoved, 0);
    EXPECT_EQ(handles(original), before);
    EXPECT_EQ(count(original), meanwhile) << count(original) << "\n" << meanwhile;
}

TEST(MeshTwin, abandonPutsBackWhatAMesherReachedThroughACarriedMesh)
{
    const TopoDS_Shape original = model();
    mesh(original, 4.0);
    const Counts coarse = count(original);
    ASSERT_EQ(coarse.unmarked, 0);

    Part::MeshTwin twin(original);
    mesh(twin.shape());
    // The twin's mesher replaced triangulations the original still holds as
    // its active ones, and OCCT cleared their "active" bit in doing so. If
    // this stops holding, abandon() has nothing left to do.
    EXPECT_GT(count(original).unmarked, 0);
    twin.abandon();
    EXPECT_EQ(count(original), coarse) << count(original) << "\n" << coarse;
}

TEST(MeshTwin, landsTheOwnPolygonOfAnEdgeOnNoFace)
{
    const TopoDS_Shape original = freeEdges();
    const TopoDS_Shape direct = freeEdges();
    mesh(direct);
    const Counts reference = count(direct);
    ASSERT_EQ(reference.own, 2);

    Part::MeshTwin twin(original);
    mesh(twin.shape());
    EXPECT_EQ(count(original).own, 0);
    const Part::MeshTwin::Landed landed = twin.land();
    EXPECT_EQ(landed.polygonsAdded, 2);
    EXPECT_EQ(count(original), reference) << count(original) << "\n" << reference;
    for (const TopoDS_Shape& edge : distinct(original, TopAbs_EDGE)) {
        TopLoc_Location loc;
        EXPECT_FALSE(BRep_Tool::Polygon3D(TopoDS::Edge(edge), loc).IsNull());
    }
}

TEST(MeshTwin, strippedHasNoMeshWhereThereIsGeometry)
{
    // A meshed solid, and a face that is a triangulation and no surface
    TopoDS_Shape meshed = solid();
    mesh(meshed);
    TopoDS_Shape source = torus();
    mesh(source);
    TopoDS_Face bare;
    BRep_Builder builder;
    builder.MakeFace(bare, active(distinct(source, TopAbs_FACE).front()));
    TopoDS_Compound original;
    builder.MakeCompound(original);
    builder.Add(original, meshed);
    builder.Add(original, bare);

    Part::MeshTwin twin(original, Part::MeshTwin::Resident::Strip);
    const Counts c = count(twin.shape());
    EXPECT_EQ(c.meshed, 1);
    EXPECT_EQ(c.onTriangulation, 0);
    const std::vector<TopoDS_Shape> faces = distinct(twin.shape(), TopAbs_FACE);
    EXPECT_TRUE(active(faces.back()) == active(bare));
    // And the original keeps all of it
    EXPECT_EQ(count(original).meshed, count(original).faces);
    EXPECT_GT(count(original).onTriangulation, 0);

    // Carried, by contrast: everything, by handle
    Part::MeshTwin carried(original);
    EXPECT_EQ(handles(carried.shape()), handles(original));
    EXPECT_EQ(count(carried.shape()), count(original));
}

TEST(MeshTwin, copiesAVertexWithItsParameters)
{
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    TopoDS_Vertex inner;
    BRep_Builder builder;
    builder.MakeVertex(inner, gp_Pnt(4, 0, 0), 1e-7);
    builder.Add(edge, inner.Oriented(TopAbs_INTERNAL));
    builder.UpdateVertex(inner, 4.0, edge, 1e-7);
    ASSERT_EQ(BRep_Tool::Parameter(inner, edge), 4.0);

    Part::MeshTwin twin(edge);
    const TopoDS_Edge copy = TopoDS::Edge(twin.shape());
    TopoDS_Vertex found;
    for (TopoDS_Iterator it(copy, false, false); it.More(); it.Next()) {
        if (it.Value().Orientation() == TopAbs_INTERNAL) {
            found = TopoDS::Vertex(it.Value());
        }
    }
    ASSERT_FALSE(found.IsNull());
    EXPECT_FALSE(found.TShape() == inner.TShape());
    EXPECT_EQ(BRep_Tool::Parameter(found, copy), 4.0);
    const auto& mine = Handle(BRep_TVertex)::DownCast(inner.TShape())->Points();
    const auto& theirs = Handle(BRep_TVertex)::DownCast(found.TShape())->Points();
    ASSERT_EQ(mine.Extent(), 1);
    ASSERT_EQ(theirs.Extent(), 1);
    // Its own entry: a boolean on the original rewrites the original's
    EXPECT_FALSE(mine.First() == theirs.First());
    // Moving the original's vertex along its edge does not move the twin's
    builder.UpdateVertex(inner, 6.0, edge, 1e-7);
    EXPECT_EQ(BRep_Tool::Parameter(found, copy), 4.0);
}

TEST(MeshTwin, isMeshedOnAnotherThreadWhileTheOriginalIsRead)
{
    const TopoDS_Shape direct = model();
    mesh(direct);
    const Counts reference = count(direct);

    const TopoDS_Shape original = model();
    Part::MeshTwin twin(original);
    std::thread worker([&twin]() { mesh(twin.shape()); });
    // What a save does to a shape, for as long as the worker runs it would
    // have walked the lists a mesher in place writes
    std::size_t bytes = 0;
    for (int i = 0; i < 20; ++i) {
        std::ostringstream out;
        BRepTools::Write(original, out);
        bytes += out.str().size();
        EXPECT_EQ(count(original).meshed, 0);
    }
    worker.join();
    EXPECT_GT(bytes, 0U);
    twin.land();
    EXPECT_EQ(count(original), reference) << count(original) << "\n" << reference;
}

TEST(MeshTwin, ofNothingIsNothing)
{
    Part::MeshTwin none;
    EXPECT_TRUE(none.isNull());
    EXPECT_EQ(none.land().faces, 0);
    Part::MeshTwin null {TopoDS_Shape()};
    EXPECT_TRUE(null.isNull());
    null.abandon();
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Matrix.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/PartParams.h>
#include <Mod/Part/App/PartPyCXX.h>
#include <Mod/Part/App/TopoShape.h>
#include <src/App/InitApplication.h>

#include <BOPAlgo_PaveFiller.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_TVertex.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Plane.hxx>
#include <gp_Pln.hxx>
#include <sstream>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
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

// Step 5 (docs/TransactionLog.md sec 23.7): a shape property's value is
// immutable from the moment it is set. The flag is per TShape, so every
// sub-shape carries it, and so does every other handle on those TShapes --
// the value is the TShapes, not the handle the caller passed in.
class PropertyShapeImmutableTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        // Stated, not inherited: the default follows the OCCT loaded at run
        // time (docs/TransactionLog.md sec 23.13).
        Part::PartParams::setImmutableShapeValues(true);
        _docName = App::GetApplication().getUniqueDocumentName("immutable");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _feature = static_cast<Part::Feature*>(_doc->addObject("Part::Feature", "Shape"));
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
        Part::PartParams::removeImmutableShapeValues();
    }

    static bool allImmutable(const TopoDS_Shape& shape)
    {
        if (!shape.Immutable())
            return false;
        for (TopoDS_Iterator it(shape); it.More(); it.Next())
            if (!allImmutable(it.Value()))
                return false;
        return true;
    }

    std::string _docName;
    App::Document* _doc = nullptr;
    Part::Feature* _feature = nullptr;
};

TEST_F(PropertyShapeImmutableTest, setValueFreezesEveryTShape)
{
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    ASSERT_FALSE(box.Immutable());
    _feature->Shape.setValue(box);

    EXPECT_TRUE(allImmutable(_feature->Shape.getValue()));
    // The caller's handle names the same TShapes.
    EXPECT_TRUE(allImmutable(box));

    // Displayable: the mesher's caches are not the value.
    const TopoDS_Face face = TopoDS::Face(TopExp_Explorer(box, TopAbs_FACE).Current());
    BRepMesh_IncrementalMesh mesh(_feature->Shape.getValue(), 0.5);
    TopLoc_Location loc;
    EXPECT_FALSE(BRep_Tool::Triangulation(face, loc).IsNull());

    BRep_Builder builder;
    EXPECT_THROW(builder.UpdateFace(face, 0.1), TopoDS_LockedShape);

    // The other overload, and a compound sharing a frozen solid with a new
    // face: the walk reaches the new parts under the shared ones.
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, box);
    TopoDS_Shape other = BRepPrimAPI_MakeBox(1, 1, 1).Shape();
    builder.Add(compound, other);
    _feature->Shape.setValue(Part::TopoShape(compound));
    EXPECT_TRUE(allImmutable(compound));
    EXPECT_TRUE(allImmutable(other));
}

TEST_F(PropertyShapeImmutableTest, offLeavesTheValueAlone)
{
    Part::PartParams::setImmutableShapeValues(false);
    TopoDS_Shape box = BRepPrimAPI_MakeBox(1, 1, 1).Shape();
    _feature->Shape.setValue(box);
    EXPECT_FALSE(box.Immutable());
}

TEST_F(PropertyShapeImmutableTest, transformGeometryIsANewValue)
{
    _feature->Shape.setValue(BRepPrimAPI_MakeBox(1, 1, 1).Shape());
    const TopoDS_Shape before = _feature->Shape.getValue();
    _feature->purgeTouched();

    Base::Matrix4D scale;
    scale.scale(2.0, 3.0, 4.0);
    _feature->Shape.transformGeometry(scale);

    const TopoDS_Shape after = _feature->Shape.getValue();
    EXPECT_FALSE(after.IsSame(before));
    EXPECT_TRUE(allImmutable(after));
    EXPECT_TRUE(_feature->isTouched());
    Base::BoundBox3d box = _feature->Shape.getBoundingBox();
    EXPECT_NEAR(box.LengthX(), 2.0, 1e-7);
    EXPECT_NEAR(box.LengthZ(), 4.0, 1e-7);
}

// A boolean on a property's shape must not tolerance-fix its arguments in
// place: the pave filler goes non-destructive for an Immutable argument as
// it does for a Locked one (the OCCT fork, BOPAlgo_PaveFiller_10.cxx).
TEST(ImmutableShapeTest, booleanGoesNonDestructive)
{
    TopoDS_Shape a = BRepPrimAPI_MakeBox(2, 2, 2).Shape();
    TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(1, 1, 1), 2, 2, 2).Shape();

    BOPAlgo_PaveFiller plain;
    NCollection_List<TopoDS_Shape> args;
    args.Append(a);
    args.Append(b);
    plain.SetArguments(args);
    plain.Perform();
    EXPECT_FALSE(plain.NonDestructive());

    setImmutable(a);
    BOPAlgo_PaveFiller frozen;
    frozen.SetArguments(args);
    frozen.Perform();
    EXPECT_FALSE(frozen.HasErrors());
    EXPECT_TRUE(frozen.NonDestructive());
}

// Pcurves as a cache (docs/TransactionLog.md sec 23.12): building on a frozen
// wire gives its edges pcurves for the new faces, which the fork lets through,
// and the storage writer leaves out, so the wire's bytes do not move.
TEST(ImmutableShapeTest, aFaceBuiltOnAFrozenWireLeavesItsBytes)
{
    tests::initApplication();  // the storage options read DocumentParams
    Handle(Geom_Circle) circle = new Geom_Circle(gp_Ax2(gp_Pnt(0, 2.5, 0), gp::DZ()), 2.5);
    BRepBuilderAPI_MakeWire mkWire;
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10, 0, 0), gp_Pnt(10, 5, 0)).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10, 5, 0), gp_Pnt(0, 5, 0)).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(circle, gp_Pnt(0, 5, 0), gp_Pnt(0, 0, 0)).Edge());
    const TopoDS_Wire wire = mkWire.Wire();
    setImmutable(wire);
    auto bytes = [&]() {
        std::ostringstream out;
        Part::TopoShape(wire).exportBrep(out, true);
        return out.str();
    };
    const std::string before = bytes();

    TopoDS_Shape prism;
    EXPECT_NO_THROW({
        const TopoDS_Face face = BRepBuilderAPI_MakeFace(wire, true).Face();
        prism = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 3)).Shape();
    });
    EXPECT_FALSE(prism.IsNull());
    // The arc did gain a pcurve on its cylindrical side face ...
    int pcurves = 0;
    for (TopExp_Explorer it(prism, TopAbs_FACE); it.More(); it.Next()) {
        for (TopExp_Explorer e(wire, TopAbs_EDGE); e.More(); e.Next()) {
            Standard_Real f, l;
            if (!BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Current()), TopoDS::Face(it.Current()), f, l)
                     .IsNull()
                && BRep_Tool::Surface(TopoDS::Face(it.Current()))->IsKind(
                    STANDARD_TYPE(Geom_CylindricalSurface))) {
                ++pcurves;
            }
        }
    }
    EXPECT_GT(pcurves, 0);
    // ... which the wire's stored bytes do not carry.
    EXPECT_EQ(bytes(), before);
}

TEST(ImmutableShapeTest, onlyACachePCurveIsReplaced)
{
    TopoDS_Shape box = BRepPrimAPI_MakeBox(1, 1, 1).Shape();
    setImmutable(box);
    const TopoDS_Face face = TopoDS::Face(TopExp_Explorer(box, TopAbs_FACE).Current());
    const TopoDS_Edge edge = TopoDS::Edge(TopExp_Explorer(face, TopAbs_EDGE).Current());
    const double tol = BRep_Tool::Tolerance(edge);
    BRep_Builder builder;

    // A pcurve on a surface the edge has none on: a cache, let through, and
    // being a cache it may be replaced or removed again.
    Handle(Geom_Surface) other = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 5), gp::DZ()));
    Handle(Geom2d_Curve) line = new Geom2d_Line(gp_Pnt2d(0, 0), gp_Dir2d(1, 0));
    EXPECT_NO_THROW(builder.UpdateEdge(edge, line, other, TopLoc_Location(), tol));
    EXPECT_NO_THROW(builder.UpdateEdge(edge, line, other, TopLoc_Location(), tol));
    Handle(Geom2d_Curve) line2 = new Geom2d_Line(gp_Pnt2d(0, 1), gp_Dir2d(1, 0));
    EXPECT_NO_THROW(builder.UpdateEdge(edge, line2, other, TopLoc_Location(), tol));
    EXPECT_NO_THROW(
        builder.UpdateEdge(edge, Handle(Geom2d_Curve)(), other, TopLoc_Location(), tol));
    // Not with a tolerance the edge would have to grow to.
    EXPECT_THROW(builder.UpdateEdge(edge, line, other, TopLoc_Location(), tol * 10),
                 TopoDS_LockedShape);

    // The value's own pcurve, on its face's surface, is neither replaced nor
    // removed.
    TopLoc_Location faceLoc;
    const Handle(Geom_Surface)& own = BRep_Tool::Surface(face, faceLoc);
    Standard_Real f, l;
    ASSERT_FALSE(BRep_Tool::CurveOnSurface(edge, face, f, l).IsNull());
    EXPECT_THROW(builder.UpdateEdge(edge, line2, own, faceLoc, tol), TopoDS_LockedShape);
    EXPECT_THROW(builder.UpdateEdge(edge, Handle(Geom2d_Curve)(), own, faceLoc, tol),
                 TopoDS_LockedShape);
    EXPECT_FALSE(BRep_Tool::CurveOnSurface(edge, face, f, l).IsNull());

    // A write that changes nothing passes; one that would change throws.
    EXPECT_NO_THROW(builder.UpdateEdge(edge, tol / 2));
    EXPECT_NO_THROW(builder.SameParameter(edge, BRep_Tool::SameParameter(edge)));
    EXPECT_THROW(builder.SameParameter(edge, !BRep_Tool::SameParameter(edge)), TopoDS_LockedShape);
    EXPECT_DOUBLE_EQ(BRep_Tool::Tolerance(edge), tol);

    // The TShape's own setter, which BRepLib uses for vertices, is guarded too.
    const TopoDS_Vertex vertex = TopoDS::Vertex(TopExp_Explorer(edge, TopAbs_VERTEX).Current());
    Handle(BRep_TVertex) tv = Handle(BRep_TVertex)::DownCast(vertex.TShape());
    EXPECT_NO_THROW(tv->UpdateTolerance(BRep_Tool::Tolerance(vertex) / 2));
    EXPECT_THROW(tv->UpdateTolerance(BRep_Tool::Tolerance(vertex) * 10), TopoDS_LockedShape);
}

// The switch defaults to what the loaded OCCT can honour.
TEST(ImmutableShapeTest, defaultFollowsTheLoadedKernel)
{
    EXPECT_GE(Part::initOCCTExtension(), 2);
    EXPECT_TRUE(Part::PartParams::defaultImmutableShapeValues());
}

// Copy-on-write where the kernel has to change a frozen part: a wire joining
// two frozen edges whose ends are close but not the same moves and widens the
// joining vertex. The wire gets a copy that is; the input keeps its own.
TEST(ImmutableShapeTest, aWireMergeCopiesAFrozenVertex)
{
    BRep_Builder builder;
    const TopoDS_Edge first = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    const TopoDS_Edge second =
        BRepBuilderAPI_MakeEdge(gp_Pnt(10, 1e-5, 0), gp_Pnt(10, 5, 0)).Edge();
    TopoDS_Vertex join, secondStart, secondEnd;
    TopExp::Vertices(first, secondStart, join);
    TopExp::Vertices(second, secondStart, secondEnd);
    builder.UpdateVertex(secondStart, 1e-4);  // covers the gap from its side only
    setImmutable(first);
    setImmutable(second);
    const double joinTol = BRep_Tool::Tolerance(join);
    const gp_Pnt joinPnt = BRep_Tool::Pnt(join);

    BRepBuilderAPI_MakeWire mkWire;
    EXPECT_NO_THROW(mkWire.Add(first));
    EXPECT_NO_THROW(mkWire.Add(second));
    ASSERT_TRUE(mkWire.IsDone());
    const TopoDS_Wire wire = mkWire.Wire();
    EXPECT_TRUE(BRepCheck_Analyzer(wire).IsValid());

    // Connected through one vertex that covers both ends ...
    TopTools_IndexedDataMapOfShapeListOfShape ancestors;
    TopExp::MapShapesAndAncestors(wire, TopAbs_VERTEX, TopAbs_EDGE, ancestors);
    int shared = 0;
    for (int i = 1; i <= ancestors.Extent(); ++i) {
        if (ancestors(i).Extent() == 2) {
            ++shared;
            EXPECT_GE(BRep_Tool::Tolerance(TopoDS::Vertex(ancestors.FindKey(i))), 1e-5);
        }
    }
    EXPECT_EQ(shared, 1);
    // ... which is not the input's: that one is where and what it was.
    EXPECT_EQ(BRep_Tool::Tolerance(join), joinTol);
    EXPECT_TRUE(BRep_Tool::Pnt(join).IsEqual(joinPnt, 0.));
}

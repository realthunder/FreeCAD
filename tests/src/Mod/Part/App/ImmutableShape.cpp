// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Matrix.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/PartParams.h>
#include <src/App/InitApplication.h>

#include <BOPAlgo_PaveFiller.hxx>
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
        // Off by default until the OCCT side copies instead of writing
        // (docs/TransactionLog.md sec 23.12).
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

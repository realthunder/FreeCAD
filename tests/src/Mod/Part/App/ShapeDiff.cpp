// SPDX-License-Identifier: LGPL-2.1-or-later

// What became of each element between two states of a shape
// (Part::ShapeDiff, docs/TransactionLog.md sec 32.6).

#include <gtest/gtest.h>

#include <set>
#include <string>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Base/Placement.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/FeaturePartCut.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/PrimitiveFeature.h>
#include <Mod/Part/App/ShapeDiff.h>
#include <Mod/Part/App/TopoShape.h>

#include "src/App/InitApplication.h"

using Part::ShapeDiff;

// A name read as steps, and the steps of some objects taken out of it. The
// names are of the forms the scanner model had (sec 32.5), cut short.

TEST(ShapeDiffNameTest, aStepThatOnlyPassedGoesOut)
{
    EXPECT_EQ(ShapeDiff::nameWithoutSteps("Top;:H4eb,F;:H1241,F;:H4ed,F", {0x1241}),
              "Top;:H4eb,F;:H4ed,F");
}

TEST(ShapeDiffNameTest, whatTheObjectDidGoesOutWithIt)
{
    // Top as it was in 4eb, cut by 1241, then as it was in 1241
    EXPECT_EQ(ShapeDiff::nameWithoutSteps("Top;:M;CUT;:H4eb:7,F;:H1241,F", {0x1241}),
              "Top;:H4eb,F");
}

TEST(ShapeDiffNameTest, whatWasDoneNextStays)
{
    // ... cut by 1241, and what 1241 gave cut again by the one after: that
    // second cut is the other side's too, done to what it had from 4eb
    EXPECT_EQ(
        ShapeDiff::nameWithoutSteps("Top;:M;CUT;:H4eb:7,F;:M2;CUT;:H1241:8,F;:H4ed,F", {0x1241}),
        "Top;:M2;CUT;:H4eb,F;:H4ed,F");
}

TEST(ShapeDiffNameTest, theLengthsAreDropped)
{
    EXPECT_EQ(ShapeDiff::nameWithoutSteps("Top;:M;CUT;:H4eb:7,F", {}), "Top;:M;CUT;:H4eb,F");
}

TEST(ShapeDiffNameTest, theNamesAnOperationListsAreNamesAndSorted)
{
    EXPECT_EQ(ShapeDiff::nameWithoutSteps(
                  "Top;:H1,F;:M(b;:H2,F;:H1241,F|a;:H2,F);RFI;:H3:d,F",
                  {0x1241}),
              "Top;:H1,F;:M(a;:H2,F|b;:H2,F);RFI;:H3,F");
}

TEST(ShapeDiffNameTest, anElementTheObjectMadeKeepsItsName)
{
    // Its root is in the object: there is nothing before to fall back on
    EXPECT_EQ(ShapeDiff::nameWithoutSteps("g1;SKT;:H1241,E;:G;XTR;:H1241:7,F", {0x1241}),
              "g1;SKT;:H1241,E;:G;XTR;:H1241,F");
    // A step of the object's own making, which a negative tag marks, is not
    // a shape the element was passed through
    EXPECT_EQ(ShapeDiff::nameWithoutSteps("Top;:H4eb,F;:H-1241,F", {0x1241}),
              "Top;:H4eb,F;:H-1241,F");
}

TEST(ShapeDiffNameTest, aNameWithNoStepsIsItself)
{
    EXPECT_EQ(ShapeDiff::nameWithoutSteps("Top", {0x1241}), "Top");
    EXPECT_EQ(ShapeDiff::nameWithoutSteps("", {0x1241}), "");
}

class ShapeDiffTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("shapediff");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
        _box->Length.setValue(40.0);
        _box->Width.setValue(20.0);
        _box->Height.setValue(10.0);
        _first = cutOf(_box, 10.0);
        _last = cutOf(_first, 30.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    Part::Cylinder* holeAt(double x)
    {
        auto* tool = static_cast<Part::Cylinder*>(_doc->addObject("Part::Cylinder", "Tool"));
        tool->Radius.setValue(2.0);
        tool->Height.setValue(100.0);
        tool->Placement.setValue(
            Base::Placement(Base::Vector3d(x, 10.0, -50.0), Base::Rotation()));
        return tool;
    }

    /// A hole through \a base at \a x
    Part::Cut* cutOf(App::DocumentObject* base, double x)
    {
        auto* cut = static_cast<Part::Cut*>(_doc->addObject("Part::Cut", "Cut"));
        cut->Base.setValue(base);
        cut->Tool.setValue(holeAt(x));
        return cut;
    }

    /// The last cut's shape as it is now, a value of its own
    Part::TopoShape now() const
    {
        return _last->Shape.getShape();
    }

    static int count(const ShapeDiff& diff, ShapeDiff::What what)
    {
        return diff.count(TopAbs_FACE, what);
    }
    static int count(const ShapeDiff& diff, ShapeDiff::By by)
    {
        return diff.count(TopAbs_FACE, by);
    }

    std::string _docName;
    App::Document* _doc = nullptr;
    Part::Box* _box = nullptr;
    Part::Cut* _first = nullptr;
    Part::Cut* _last = nullptr;
};

TEST_F(ShapeDiffTest, oneShapeIsTheSame)
{
    const Part::TopoShape shape = now();
    ASSERT_EQ(shape.countSubShapes(TopAbs_FACE), 8U);
    ShapeDiff diff(shape, shape);
    EXPECT_TRUE(diff.same());
    EXPECT_FALSE(diff.moved());
    EXPECT_EQ(diff.elements(TopAbs_FACE).size(), 8U);
    EXPECT_EQ(count(diff, ShapeDiff::What::Same), 8);
    EXPECT_EQ(count(diff, ShapeDiff::By::Shape), 8);
    // Not asked for, not given
    EXPECT_TRUE(diff.elements(TopAbs_EDGE).empty());
}

TEST_F(ShapeDiffTest, aShapeRecomputedAsItWasIsTheSame)
{
    const Part::TopoShape before = now();
    _box->touch();
    _doc->recompute();
    const Part::TopoShape after = now();
    ShapeDiff diff(before, after);
    EXPECT_TRUE(diff.same());
    EXPECT_EQ(count(diff, ShapeDiff::What::Same), 8);
}

TEST_F(ShapeDiffTest, aShapeMovedIsSaidToBeMoved)
{
    const Part::TopoShape before = now();
    _last->Placement.setValue(Base::Placement(Base::Vector3d(5.0, 0.0, 0.0), Base::Rotation()));
    _doc->recompute();
    ShapeDiff diff(before, now());
    EXPECT_FALSE(diff.same());
    ASSERT_TRUE(diff.moved());
    EXPECT_NEAR(diff.motion().TranslationPart().X(), 5.0, 1e-9);
    EXPECT_NEAR(diff.motion().TranslationPart().Y(), 0.0, 1e-9);
    EXPECT_EQ(count(diff, ShapeDiff::What::Moved), 8);
    EXPECT_EQ(count(diff, ShapeDiff::What::Changed), 0);
}

TEST_F(ShapeDiffTest, aParameterChangesFacesAndRenamesNone)
{
    const Part::TopoShape before = now();
    _box->Height.setValue(12.0);
    _doc->recompute();
    ShapeDiff diff(before, now());
    EXPECT_FALSE(diff.same());
    EXPECT_FALSE(diff.moved());
    EXPECT_EQ(count(diff, ShapeDiff::By::Name), 8);
    EXPECT_EQ(count(diff, ShapeDiff::What::Added), 0);
    EXPECT_EQ(count(diff, ShapeDiff::What::Removed), 0);
    // The bottom is as it was, the top is where it was not, and the four
    // sides and the two walls are taller
    EXPECT_EQ(count(diff, ShapeDiff::What::Same), 1);
    EXPECT_EQ(count(diff, ShapeDiff::What::Moved), 1);
    EXPECT_EQ(count(diff, ShapeDiff::What::Changed), 6);
}

TEST_F(ShapeDiffTest, aFeaturePutInEarlyIsSeenThrough)
{
    const Part::TopoShape before = now();
    // A hole more, cut before the two there are
    auto* early = cutOf(_box, 20.0);
    _first->Base.setValue(early);
    _doc->recompute();
    const Part::TopoShape after = now();
    ASSERT_EQ(after.countSubShapes(TopAbs_FACE), 9U);

    // By the names alone: every face but the new wall is paired, and most of
    // them only once the new cut's steps are out of their names
    ShapeDiff::Options namesOnly;
    namesOnly.geometry = false;
    namesOnly.around = false;
    ShapeDiff diff(before, after, namesOnly);
    EXPECT_EQ(count(diff, ShapeDiff::What::Removed), 0);
    EXPECT_EQ(count(diff, ShapeDiff::What::Added), 1);
    EXPECT_GT(count(diff, ShapeDiff::By::Steps), 0);
    EXPECT_EQ(count(diff, ShapeDiff::By::Name) + count(diff, ShapeDiff::By::Steps), 8);
    // The top and the bottom have a hole more; the rest are as they were
    EXPECT_EQ(count(diff, ShapeDiff::What::Changed), 2);
    EXPECT_EQ(count(diff, ShapeDiff::What::Same), 6);

    // And the other way about it is a feature taken out
    ShapeDiff back(after, before, namesOnly);
    EXPECT_EQ(count(back, ShapeDiff::What::Removed), 1);
    EXPECT_EQ(count(back, ShapeDiff::What::Added), 0);
    EXPECT_EQ(count(back, ShapeDiff::By::Name) + count(back, ShapeDiff::By::Steps), 8);
}

TEST_F(ShapeDiffTest, aPairIsOfOneFace)
{
    // What a feature put in early pairs is checked against where the faces
    // are: each pair that is the same has one centre
    const Part::TopoShape before = now();
    auto* early = cutOf(_box, 20.0);
    _first->Base.setValue(early);
    _doc->recompute();
    const Part::TopoShape after = now();
    ShapeDiff diff(before, after);
    int same = 0;
    for (const auto& element : diff.elements(TopAbs_FACE)) {
        if (element.what != ShapeDiff::What::Same) {
            continue;
        }
        ++same;
        const auto a = before.getSubTopoShape(TopAbs_FACE, element.a).getBoundBox();
        const auto b = after.getSubTopoShape(TopAbs_FACE, element.b).getBoundBox();
        EXPECT_NEAR((a.GetCenter() - b.GetCenter()).Length(), 0.0, 1e-9)
            << "Face" << element.a << " = Face" << element.b;
    }
    EXPECT_EQ(same, 6);
}

TEST_F(ShapeDiffTest, aFaceRenamedAndChangedIsFoundByTheFacesAround)
{
    // On one side the box is taller; on the other the last hole is cut with
    // another cylinder, in the same place. The hole's wall has another name
    // there and another height here.
    _box->Height.setValue(12.0);
    _doc->recompute();
    const Part::TopoShape taller = now();
    _box->Height.setValue(10.0);
    _last->Tool.setValue(holeAt(30.0));
    _doc->recompute();
    const Part::TopoShape recut = now();

    ShapeDiff::Options noneAround;
    noneAround.around = false;
    ShapeDiff without(taller, recut, noneAround);
    EXPECT_EQ(count(without, ShapeDiff::What::Removed), 1);
    EXPECT_EQ(count(without, ShapeDiff::What::Added), 1);

    ShapeDiff diff(taller, recut);
    EXPECT_EQ(count(diff, ShapeDiff::By::Around), 1);
    EXPECT_EQ(count(diff, ShapeDiff::What::Removed), 0);
    EXPECT_EQ(count(diff, ShapeDiff::What::Added), 0);
}

TEST_F(ShapeDiffTest, edgesAndVerticesWhereAskedFor)
{
    const Part::TopoShape before = now();
    _box->Height.setValue(12.0);
    _doc->recompute();
    const Part::TopoShape after = now();
    ShapeDiff::Options all;
    all.edges = true;
    all.vertices = true;
    ShapeDiff diff(before, after, all);
    EXPECT_EQ(diff.elements(TopAbs_EDGE).size(), before.countSubShapes(TopAbs_EDGE));
    EXPECT_EQ(diff.elements(TopAbs_VERTEX).size(), before.countSubShapes(TopAbs_VERTEX));
    EXPECT_EQ(diff.count(TopAbs_EDGE, ShapeDiff::What::Added), 0);
    EXPECT_EQ(diff.count(TopAbs_EDGE, ShapeDiff::What::Removed), 0);
    EXPECT_EQ(diff.count(TopAbs_VERTEX, ShapeDiff::What::Added), 0);
    EXPECT_EQ(diff.count(TopAbs_VERTEX, ShapeDiff::What::Removed), 0);
    // The bottom's edges and corners did not move, and something did
    EXPECT_GT(diff.count(TopAbs_EDGE, ShapeDiff::What::Same), 0);
    EXPECT_GT(diff.count(TopAbs_EDGE, ShapeDiff::What::Changed), 0);
    EXPECT_GT(diff.count(TopAbs_VERTEX, ShapeDiff::What::Same), 0);
    EXPECT_GT(diff.count(TopAbs_VERTEX, ShapeDiff::What::Moved), 0);
}

TEST_F(ShapeDiffTest, aCopyOfTheFileHasAnotherTableAndOneSpelling)
{
    // The file opened again beside the document, as a version is: its own
    // string table, the same objects, and a name is one once spelled out
    const std::string dir = Base::FileInfo::getTempPath() + _docName;
    Base::FileInfo(dir).createDirectories();
    const std::string path = dir + "/a.FCStd";
    const std::string copy = dir + "/b.FCStd";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    ASSERT_TRUE(Base::FileInfo(path).copyTo(copy.c_str()));
    App::Document* other = App::GetApplication().openDocument(copy.c_str());
    ASSERT_NE(other, nullptr);
    const std::string otherName = other->getName();
    auto* box = static_cast<Part::Box*>(other->getObject("Box"));
    auto* last = static_cast<Part::Cut*>(other->getObject(_last->getNameInDocument()));
    ASSERT_NE(box, nullptr);
    ASSERT_NE(last, nullptr);
    box->Height.setValue(12.0);
    other->recompute();

    const Part::TopoShape theirs = last->Shape.getShape();
    const Part::TopoShape ours = now();
    ASSERT_NE(ours.Hasher, theirs.Hasher);
    ShapeDiff::Options namesOnly;
    namesOnly.geometry = false;
    namesOnly.around = false;
    ShapeDiff diff(ours, theirs, namesOnly);
    EXPECT_EQ(count(diff, ShapeDiff::By::Name), 8);
    EXPECT_EQ(count(diff, ShapeDiff::What::Changed), 6);
    App::GetApplication().closeDocument(otherName.c_str());
    Base::FileInfo(dir).deleteDirectoryRecursive();
}

TEST_F(ShapeDiffTest, twoDocumentsMadeApartShareNoNames)
{
    // An object's tag is its id, and an id is the application's: the same
    // three objects made in another document have other tags, and no name
    // is one. The geometry is what is left.
    const std::string otherName = App::GetApplication().getUniqueDocumentName("shapediff");
    App::Document* other = App::GetApplication().newDocument(otherName.c_str(), "testUser");
    App::Document* mine = _doc;
    _doc = other;
    auto* box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    box->Length.setValue(40.0);
    box->Width.setValue(20.0);
    box->Height.setValue(10.0);
    auto* first = cutOf(box, 10.0);
    auto* last = cutOf(first, 35.0);
    other->recompute();
    _doc = mine;

    const Part::TopoShape theirs = last->Shape.getShape();
    ShapeDiff::Options namesOnly;
    namesOnly.geometry = false;
    namesOnly.around = false;
    ShapeDiff byNames(now(), theirs, namesOnly);
    EXPECT_EQ(count(byNames, ShapeDiff::What::Removed), 8);

    // The last hole is 5 further along: its wall is found by the faces
    // around it, the six with no hole in them by where they are
    ShapeDiff diff(now(), theirs);
    EXPECT_EQ(count(diff, ShapeDiff::By::Name), 0);
    EXPECT_EQ(count(diff, ShapeDiff::What::Removed), 0);
    EXPECT_EQ(count(diff, ShapeDiff::What::Added), 0);
    EXPECT_EQ(count(diff, ShapeDiff::By::Around), 1);
    App::GetApplication().closeDocument(otherName.c_str());
}

// Shapes with no names: the geometry and nothing else

TEST(ShapeDiffPlainTest, aHoleMoreInAShapeWithNoNames)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape();
    const TopoDS_Shape tool =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20.0, 10.0, -5.0), gp_Dir(0, 0, 1)), 2.0, 20.0)
            .Shape();
    const Part::TopoShape before(box);
    const Part::TopoShape after(BRepAlgoAPI_Cut(box, tool).Shape());
    ASSERT_EQ(before.getElementMapSize(), 0U);
    ASSERT_EQ(after.countSubShapes(TopAbs_FACE), 7U);

    ShapeDiff diff(before, after);
    EXPECT_FALSE(diff.same());
    EXPECT_EQ(diff.count(TopAbs_FACE, ShapeDiff::By::Geometry), 6);
    EXPECT_EQ(diff.count(TopAbs_FACE, ShapeDiff::What::Same), 4);
    // The top and the bottom are found by their outline, and have a hole more
    EXPECT_EQ(diff.count(TopAbs_FACE, ShapeDiff::What::Changed), 2);
    EXPECT_EQ(diff.count(TopAbs_FACE, ShapeDiff::What::Added), 1);
    EXPECT_EQ(diff.count(TopAbs_FACE, ShapeDiff::What::Removed), 0);
}

TEST(ShapeDiffPlainTest, nothingOfTheGeometryWhereItIsNotAskedFor)
{
    const Part::TopoShape before(BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape());
    const Part::TopoShape after(BRepPrimAPI_MakeBox(40.0, 20.0, 12.0).Shape());
    ShapeDiff::Options namesOnly;
    namesOnly.geometry = false;
    namesOnly.around = false;
    ShapeDiff diff(before, after, namesOnly);
    EXPECT_EQ(diff.count(TopAbs_FACE, ShapeDiff::What::Removed), 6);
    EXPECT_EQ(diff.count(TopAbs_FACE, ShapeDiff::What::Added), 6);
}

TEST(ShapeDiffPlainTest, aNullShape)
{
    const Part::TopoShape none;
    const Part::TopoShape box(BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape());
    EXPECT_TRUE(ShapeDiff(none, none).same());
    ShapeDiff made(none, box);
    EXPECT_FALSE(made.same());
    EXPECT_EQ(made.count(TopAbs_FACE, ShapeDiff::What::Added), 6);
    ShapeDiff gone(box, none);
    EXPECT_EQ(gone.count(TopAbs_FACE, ShapeDiff::What::Removed), 6);
}

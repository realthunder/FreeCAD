// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/PrimitiveFeature.h>
#include <Mod/Part/App/FeaturePartCut.h>
#include <Mod/Part/App/FeatureCompound.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Base/Writer.h>
#include <App/DocumentParams.h>
#include <Base/Interpreter.h>
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
#include <BRep_PointRepresentation.hxx>
#include <BRep_TEdge.hxx>
#include <BRep_TVertex.hxx>
#include <gp_Circ.hxx>
#include <gp.hxx>
#include <gp_Trsf.hxx>
#include <Geom_Curve.hxx>
#include <TopLoc_Location.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <Geom_Line.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopTools_ListOfShape.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Plane.hxx>
#include <gp_Pln.hxx>
#include <iostream>
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
#include <BRep_RepresentationLock.hxx>
#include <BRepTools.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

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

/// What a document save stores for \a shape.
std::string storedBytes(const TopoDS_Shape& shape)
{
    std::ostringstream out;
    Part::TopoShape(shape).exportBrep(out, true);
    return out.str();
}

/// The parameter representation \a vertex holds on \a curve, or none.
Handle(BRep_PointRepresentation) pointOn(const TopoDS_Vertex& vertex, const Handle(Geom_Curve)& curve)
{
    Handle(BRep_TVertex) tv = Handle(BRep_TVertex)::DownCast(vertex.TShape());
    for (const auto& point : tv->Points()) {
        if (point->IsPointOnCurve() && point->Curve() == curve)
            return point;
    }
    return {};
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
// The transaction log saves a frozen value's copy on its worker
// (canSaveOffThread). A copy shares its shape's cache with the live value,
// and an element map not made yet is made by Save() from that cache, which
// also clears the sub-shapes the cache holds -- on the worker, while this
// thread hands them out: a SIGSEGV in Cache::Info::_getTopoShape under
// Shape.Faces (docs/TransactionLog.md sec 31.27). Asked whether it may be
// saved off this thread, the copy makes its map here first.
TEST_F(PropertyShapeImmutableTest, aCopyForTheWorkerHasItsElementMap)
{
    const App::StringHasherRef hasher = _doc->getStringHasher();
    Part::TopoShape one(1, hasher, BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    Part::TopoShape two(2, hasher, BRepPrimAPI_MakeBox(1, 2, 3).Shape());
    // Made under the feature's own tag, as its recompute would: a value of
    // another tag is named again when it is set, and has its map by then.
    Part::TopoShape both(_feature->getID(), hasher);
    both.makECompound({one, two});
    // A part of it names its elements from the whole, when asked.
    const Part::TopoShape solid = both.getSubTopoShape(TopAbs_SOLID, 1);
    ASSERT_TRUE(solid.hasPendingElementMap());
    _feature->Shape.setValue(solid);
    ASSERT_TRUE(_feature->Shape.getValue().Immutable());

    std::unique_ptr<App::Property> copy(_feature->Shape.Copy());
    auto value = static_cast<Part::PropertyPartShape*>(copy.get());
    ASSERT_TRUE(value->getShape().hasPendingElementMap());
    EXPECT_TRUE(copy->canSaveOffThread());
    EXPECT_FALSE(value->getShape().hasPendingElementMap());
    EXPECT_GT(value->getShape().getElementMapSize(false), 0U);
}

// A copy given to the worker shares its element map with the live value and
// with every other copy, and a map is edited in place. So the map is held
// from the handover until the reader is done (Property::holdForOffThread),
// and the check (FC_ELEMENTMAP_CHECK) counts an edit of it meanwhile
// (docs/TransactionLog.md sec 31.28, 31.31).
namespace {

struct CheckingHeldMaps
{
    CheckingHeldMaps() { Data::ComplexGeoData::setElementMapCheck(true); }
    ~CheckingHeldMaps() { Data::ComplexGeoData::setElementMapCheck(false); }
    CheckingHeldMaps(const CheckingHeldMaps&) = delete;
    CheckingHeldMaps& operator=(const CheckingHeldMaps&) = delete;
};

} // namespace

class HeldElementMapTest: public PropertyShapeImmutableTest
{
protected:
    /// A compound of two boxes, named, as the feature's own value
    void SetUp() override
    {
        PropertyShapeImmutableTest::SetUp();
        _hasher = _doc->getStringHasher();
        Part::TopoShape one(1, _hasher, BRepPrimAPI_MakeBox(10, 20, 30).Shape());
        Part::TopoShape two(2, _hasher, BRepPrimAPI_MakeBox(1, 2, 3).Shape());
        Part::TopoShape both(_feature->getID(), _hasher);
        both.makECompound({one, two});
        _feature->Shape.setValue(both);
        _names = _feature->Shape.getShape().getElementMapSize();
        ASSERT_GT(_names, 0U);
    }

    std::size_t names() const
    {
        return _feature->Shape.getShape().getElementMapSize();
    }

    std::pair<std::string, std::string> nameOf(const char* element) const
    {
        return _feature->getElementName(element, App::GeoFeature::Export);
    }

    App::StringHasherRef _hasher;
    std::size_t _names = 0;
    CheckingHeldMaps _checking;
};

TEST_F(HeldElementMapTest, anEditOfAHeldMapIsCounted)
{
    const unsigned long before = Data::ComplexGeoData::elementMapEditsWhileHeld();
    // Not held: a name given to a copy of the value is no finding.
    Part::TopoShape early = _feature->Shape.getShape();
    early.setElementName(Data::IndexedName::fromConst("Face", 1), Data::MappedName("early"));
    EXPECT_EQ(Data::ComplexGeoData::elementMapEditsWhileHeld(), before);

    std::unique_ptr<App::Property> copy(_feature->Shape.Copy());
    ASSERT_TRUE(copy->canSaveOffThread());
    std::shared_ptr<void> hold = copy->holdForOffThread();
    ASSERT_TRUE(hold);
    EXPECT_TRUE(_feature->Shape.getShape().isElementMapHeld());
    // What a later shape is made of is its own map: no finding.
    Part::TopoShape made(_feature->getID(), _hasher);
    made.makECompound({_feature->Shape.getShape()});
    EXPECT_EQ(Data::ComplexGeoData::elementMapEditsWhileHeld(), before);
    // A name set in place on a copy of the value is the reader's map
    // changed under it.
    Part::TopoShape late = _feature->Shape.getShape();
    late.setElementName(Data::IndexedName::fromConst("Face", 2), Data::MappedName("late"));
    EXPECT_EQ(Data::ComplexGeoData::elementMapEditsWhileHeld(), before + 1);
    // The reader done, it is nobody's but the main thread's again.
    hold.reset();
    EXPECT_FALSE(_feature->Shape.getShape().isElementMapHeld());
    late.setElementName(Data::IndexedName::fromConst("Face", 3), Data::MappedName("later"));
    EXPECT_EQ(Data::ComplexGeoData::elementMapEditsWhileHeld(), before + 1);
}

// A solid has no name of its own: asked for one, a feature makes it of its
// faces' names and keeps it in the shape's element map -- the property's own
// map, written through a copy of the shape. That was the one edit of a held
// map the check found (sec 31.28). While the map is held the name waits
// beside it, found there by whoever asks, and goes in once the reader is
// done.
TEST_F(HeldElementMapTest, aSolidNamedWhileTheMapIsHeldWaits)
{
    const unsigned long edits = Data::ComplexGeoData::elementMapEditsWhileHeld();
    std::unique_ptr<App::Property> copy(_feature->Shape.Copy());
    std::shared_ptr<void> hold = copy->holdForOffThread();

    const auto made = nameOf("Solid1");
    EXPECT_NE(made.first.find(".Solid1"), std::string::npos);
    EXPECT_EQ(made.second, "Solid1");
    EXPECT_EQ(names(), _names);   // not in the map
    EXPECT_EQ(Data::ComplexGeoData::elementMapEditsWhileHeld(), edits);
    // Asked again it is the same name, and the name finds the solid.
    EXPECT_EQ(nameOf("Solid1"), made);
    EXPECT_EQ(nameOf(made.first.c_str()).second, "Solid1");
    EXPECT_EQ(names(), _names);

    hold.reset();
    // Still waiting: nothing takes it in but a hold, a save, or being asked to.
    EXPECT_EQ(names(), _names);
    Part::TopoShape value = _feature->Shape.getShape();
    value.mergeDeferredElementNames();
    EXPECT_EQ(names(), _names + 1);
    EXPECT_EQ(nameOf("Solid1"), made);
    EXPECT_EQ(nameOf(made.first.c_str()).second, "Solid1");
}

// The next handover takes the waiting names in first, so the value handed
// over has them.
TEST_F(HeldElementMapTest, aHandoverTakesInWhatWaited)
{
    std::unique_ptr<App::Property> copy(_feature->Shape.Copy());
    std::shared_ptr<void> hold = copy->holdForOffThread();
    const auto made = nameOf("Solid1");
    EXPECT_EQ(names(), _names);
    hold.reset();

    hold = copy->holdForOffThread();
    EXPECT_EQ(names(), _names + 1);
    EXPECT_EQ(nameOf("Solid1"), made);
}

// Off the main thread nothing writes the map at all: the name waits, held or
// not, and it is the name the main thread would have made -- the string
// table has a lock (sec 31.30).
TEST_F(HeldElementMapTest, aSolidNamedOffTheMainThreadWaits)
{
    ASSERT_TRUE(App::Application::isMainThread());
    bool there = true;
    std::pair<std::string, std::string> made;
    std::thread other([&]() {
        there = App::Application::isMainThread();
        made = nameOf("Solid1");
    });
    other.join();
    EXPECT_FALSE(there);
    EXPECT_NE(made.first.find(".Solid1"), std::string::npos);
    EXPECT_EQ(names(), _names);
    // The main thread knows it, and makes no other.
    EXPECT_EQ(nameOf(made.first.c_str()).second, "Solid1");
    EXPECT_EQ(nameOf("Solid1"), made);

    Part::TopoShape value = _feature->Shape.getShape();
    value.mergeDeferredElementNames();
    EXPECT_EQ(names(), _names + 1);
    EXPECT_EQ(nameOf("Solid1"), made);
}

// A waiting name is not taken in where the map moved on under it.
TEST_F(HeldElementMapTest, aStaleNameIsDropped)
{
    Part::TopoShape value = _feature->Shape.getShape();
    const Data::IndexedName solid = Data::IndexedName::fromConst("Solid", 1);
    const Data::IndexedName face1 = Data::IndexedName::fromConst("Face", 1);
    const Data::IndexedName face2 = Data::IndexedName::fromConst("Face", 2);
    // Names of this map's own, which can be taken away again: a face of a
    // compound is named through the map of the box it came from.
    const Data::MappedName low1 =
        value.setElementName(face1, Data::MappedName("lowone;:H1:1,F"));
    const Data::MappedName low2 =
        value.setElementName(face2, Data::MappedName("lowtwo;:H1:1,F"));
    ASSERT_TRUE(low1 && low2);

    // One of the names it was made of is gone.
    value.deferElementName(solid, Data::MappedName("waiting;:H1:1,S"), {}, {low1, low2});
    EXPECT_EQ(value.getIndexedName(Data::MappedName("waiting;:H1:1,S")), solid);
    ASSERT_TRUE(value.eraseElementName(low2));
    value.mergeDeferredElementNames();
    EXPECT_FALSE(value.getIndexedName(Data::MappedName("waiting;:H1:1,S")));

    // The element was named meanwhile.
    value.deferElementName(solid, Data::MappedName("second;:H1:1,S"), {}, {low1});
    value.setElementName(solid, Data::MappedName("first;:H1:1,S"));
    value.mergeDeferredElementNames();
    EXPECT_FALSE(value.getIndexedName(Data::MappedName("second;:H1:1,S")));
    EXPECT_EQ(value.getIndexedName(Data::MappedName("first;:H1:1,S")), solid);

    // The name is another element's by then.
    const Data::IndexedName shell = Data::IndexedName::fromConst("Shell", 1);
    value.deferElementName(shell, Data::MappedName("taken;:H1:1,S"), {}, {low1});
    value.setElementName(face2, Data::MappedName("taken;:H1:1,S"));
    value.mergeDeferredElementNames();
    EXPECT_EQ(value.getIndexedName(Data::MappedName("taken;:H1:1,S")), face2);

    // And a map that is replaced takes its waiting names with it.
    value.deferElementName(shell, Data::MappedName("gone;:H1:1,S"), {}, {low1});
    Part::TopoShape other(_feature->getID(), _hasher, BRepPrimAPI_MakeBox(4, 5, 6).Shape());
    other.setElementName(face1, Data::MappedName("another;:H1:1,F"));
    _feature->Shape.setValue(other);
    Part::TopoShape now = _feature->Shape.getShape();
    now.mergeDeferredElementNames();
    EXPECT_FALSE(now.getIndexedName(Data::MappedName("gone;:H1:1,S")));
}

// Many givers, one taker, no lock. Each thread has the map held for it and
// a shape of its own over it -- its own cache, which asking a shape for its
// parts writes (sec 31.27) -- and asks for the same names; the main thread
// takes them in afterwards, each element named once.
TEST_F(HeldElementMapTest, namesGivenFromManyThreadsAreTakenInOnce)
{
    struct Naming: Part::Feature
    {
        using Part::Feature::getExportElementName;
    };
    const auto* naming = static_cast<const Naming*>(_feature);
    constexpr int threads = 8;
    constexpr int rounds = 100;
    struct Mapped: Part::TopoShape
    {
        using Data::ComplexGeoData::elementMap;
    };
    const Part::TopoShape value = _feature->Shape.getShape();
    const Data::ElementMapPtr map = static_cast<const Mapped&>(value).elementMap();
    ASSERT_TRUE(map);
    std::unique_ptr<App::Property> copy(_feature->Shape.Copy());
    std::shared_ptr<void> hold = copy->holdForOffThread();
    std::vector<std::thread> givers;
    std::vector<std::pair<std::string, std::string>> first(threads);
    std::vector<std::pair<std::string, std::string>> second(threads);
    for (int t = 0; t < threads; ++t) {
        givers.emplace_back([&, t]() {
            for (int i = 0; i < rounds; ++i) {
                Part::TopoShape own(value.Tag, _hasher, value.getShape());
                own.resetElementMap(map);
                first[t] = naming->getExportElementName(own, "Solid1");
                second[t] = naming->getExportElementName(own, "Solid2");
            }
        });
    }
    for (auto& giver : givers)
        giver.join();
    for (int t = 1; t < threads; ++t) {
        EXPECT_EQ(first[t], first[0]);
        EXPECT_EQ(second[t], second[0]);
    }
    EXPECT_NE(first[0].first, second[0].first);
    EXPECT_NE(first[0].first.find(".Solid1"), std::string::npos);
    EXPECT_EQ(names(), _names);
    hold.reset();
    Part::TopoShape now = _feature->Shape.getShape();
    now.mergeDeferredElementNames();
    EXPECT_EQ(names(), _names + 2);
    EXPECT_EQ(nameOf("Solid1"), first[0]);
    EXPECT_EQ(nameOf("Solid2"), second[0]);
}

// Not a check: what a recompute costs beside a thread that saves element
// maps without a pause, as the transaction log's worker would with a queue
// that never empties (docs/TransactionLog.md sec 31.32, which has what it
// printed for the scheme that was not kept). Run with
// --gtest_also_run_disabled_tests --gtest_filter='*aRecomputeBesideAReaderOfMaps'.
TEST_F(PropertyShapeImmutableTest, DISABLED_aRecomputeBesideAReaderOfMaps)
{
    using Clock = std::chrono::steady_clock;
    constexpr int cuts = 40;
    constexpr int rounds = 4;
    const long logWas = App::DocumentParams::getTransactionLog();
    App::DocumentParams::setTransactionLog(0);   // the reader below is the only one

    auto* box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    box->Length.setValue(4.0 * cuts + 4.0);
    box->Width.setValue(10.0);
    box->Height.setValue(10.0);
    App::DocumentObject* base = box;
    std::vector<Part::Feature*> made;
    for (int i = 0; i < cuts; ++i) {
        auto* tool = static_cast<Part::Cylinder*>(_doc->addObject("Part::Cylinder", "Tool"));
        tool->Radius.setValue(1.0);
        tool->Height.setValue(20.0);
        tool->Placement.setValue(
            Base::Placement(Base::Vector3d(4.0 * i + 4.0, 5.0, -5.0), Base::Rotation()));
        auto* cut = static_cast<Part::Cut*>(_doc->addObject("Part::Cut", "Cut"));
        cut->Base.setValue(base);
        cut->Tool.setValue(tool);
        base = cut;
        made.push_back(cut);
    }
    _doc->recompute();
    ASSERT_FALSE(made.back()->Shape.getValue().IsNull());

    auto recompute = [&]() {
        double best = 1e30;
        double sum = 0.0;
        for (int round = 0; round < rounds; ++round) {
            for (auto* obj : _doc->getObjects())
                obj->touch();
            const auto start = Clock::now();
            _doc->recompute();
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            best = std::min(best, ms);
            sum += ms;
        }
        return std::make_pair(best, sum / rounds);
    };
    const auto alone = recompute();

    // The values a worker would have been handed: the last cuts' shapes,
    // their maps made and held, here on the main thread.
    std::vector<Part::TopoShape> values;
    std::vector<std::shared_ptr<void>> holds;
    for (int i = cuts - 10; i < cuts; ++i) {
        values.push_back(made[i]->Shape.getShape());
        (void)values.back().getElementMapSize();
        holds.push_back(made[i]->Shape.holdForOffThread());
    }
    std::atomic<bool> stop {false};
    std::atomic<long> saves {0};
    std::atomic<long> bytes {0};
    std::thread reader([&]() {
        while (!stop) {
            for (const auto& value : values) {
                Base::StringWriter writer;
                value.Save(writer);
                bytes += static_cast<long>(writer.getString().size());
                ++saves;
            }
        }
    });
    const auto startBeside = Clock::now();
    const auto beside = recompute();
    const double span = std::chrono::duration<double, std::milli>(Clock::now() - startBeside).count();
    stop = true;
    reader.join();
    holds.clear();
    App::DocumentParams::setTransactionLog(logWas);

    std::cout << "BESIDE: alone " << alone.first
              << " ms (mean " << alone.second << "), beside the reader " << beside.first
              << " ms (mean " << beside.second << "), reader " << saves.load() << " saves in "
              << span << " ms, " << (saves.load() ? span / saves.load() : 0.0) << " ms each, "
              << (saves.load() ? bytes.load() / saves.load() : 0) << " bytes each\n";
}

// Not a check: what holding a shape's element map for another thread costs
// the main thread at a handover, by how many features the shape was made
// of, beside what the save that follows costs the worker and what the
// recompute that made the shapes cost (docs/TransactionLog.md sec 31.33).
// Run with --gtest_also_run_disabled_tests --gtest_filter='*aHoldOfAMapByItsDepth'.
TEST_F(PropertyShapeImmutableTest, DISABLED_aHoldOfAMapByItsDepth)
{
    using Clock = std::chrono::steady_clock;
    constexpr int cuts = 80;
    const long logWas = App::DocumentParams::getTransactionLog();
    App::DocumentParams::setTransactionLog(0);

    auto* box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    box->Length.setValue(4.0 * cuts + 4.0);
    box->Width.setValue(10.0);
    box->Height.setValue(10.0);
    App::DocumentObject* base = box;
    std::vector<Part::Feature*> made;
    std::vector<App::DocumentObject*> leaves;
    for (int i = 0; i < cuts; ++i) {
        auto* tool = static_cast<Part::Cylinder*>(_doc->addObject("Part::Cylinder", "Tool"));
        tool->Radius.setValue(1.0);
        tool->Height.setValue(20.0);
        tool->Placement.setValue(
            Base::Placement(Base::Vector3d(4.0 * i + 4.0, 5.0, -5.0), Base::Rotation()));
        auto* cut = static_cast<Part::Cut*>(_doc->addObject("Part::Cut", "Cut"));
        cut->Base.setValue(base);
        cut->Tool.setValue(tool);
        base = cut;
        made.push_back(cut);
        leaves.push_back(cut);
        leaves.push_back(tool);
    }
    // A cut's map has every name itself; a compound's has its children's
    // maps, and a hold walks those. Of 10, 40 and 160 shapes, and one of
    // four compounds of 40.
    auto compoundOf = [&](std::size_t from, std::size_t count) {
        auto* compound = static_cast<Part::Compound*>(_doc->addObject("Part::Compound", "Group"));
        compound->Links.setValues(std::vector<App::DocumentObject*>(
            leaves.begin() + from, leaves.begin() + from + count));
        return compound;
    };
    std::vector<std::pair<std::string, Part::Feature*>> groups;
    groups.emplace_back("compound of 10", compoundOf(0, 10));
    groups.emplace_back("compound of 40", compoundOf(0, 40));
    groups.emplace_back("compound of 160", compoundOf(0, 160));
    {
        std::vector<App::DocumentObject*> inner;
        for (int i = 0; i < 4; ++i)
            inner.push_back(compoundOf(40 * i, 40));
        auto* outer = static_cast<Part::Compound*>(_doc->addObject("Part::Compound", "Outer"));
        outer->Links.setValues(inner);
        groups.emplace_back("compound of 4 compounds of 40", outer);
    }
    const auto startAll = Clock::now();
    _doc->recompute();
    const double all = std::chrono::duration<double, std::milli>(Clock::now() - startAll).count();
    ASSERT_FALSE(made.back()->Shape.getValue().IsNull());

    auto micros = [](Clock::time_point from) {
        return std::chrono::duration<double, std::micro>(Clock::now() - from).count();
    };
    std::vector<double> holds;
    std::vector<double> saves;
    std::vector<double> names;
    std::vector<double> bytes;
    auto measure = [&](Part::Feature* feature) {
        Part::TopoShape value = feature->Shape.getShape();
        names.push_back(static_cast<double>(value.getElementMapSize()));
        double hold = 1e30;
        for (int round = 0; round < 50; ++round) {
            const auto start = Clock::now();
            auto held = feature->Shape.holdForOffThread();
            hold = std::min(hold, micros(start));
            EXPECT_TRUE(held);
            EXPECT_TRUE(value.isElementMapHeld());
        }
        EXPECT_FALSE(value.isElementMapHeld());
        double save = 1e30;
        for (int round = 0; round < 3; ++round) {
            Base::StringWriter writer;
            const auto start = Clock::now();
            value.Save(writer);
            save = std::min(save, micros(start));
            if (round == 0)
                bytes.push_back(static_cast<double>(writer.getString().size()));
        }
        holds.push_back(hold);
        saves.push_back(save);
    };
    for (auto* cut : made)
        measure(cut);
    for (auto& group : groups)
        measure(group.second);
    App::DocumentParams::setTransactionLog(logWas);

    std::cout << "HOLD recompute of " << cuts << " cuts " << all << " ms\n";
    double holdSum = 0.0;
    double saveSum = 0.0;
    for (int i = 0; i < cuts; ++i) {
        holdSum += holds[i];
        saveSum += saves[i];
        const int depth = i + 1;
        if (depth == 1 || depth == 10 || depth == 40 || depth == 80) {
            std::cout << "HOLD cut " << depth << ": " << names[i] << " names, hold " << holds[i]
                      << " us, save " << saves[i] << " us of " << bytes[i]
                      << " bytes; up to here holds " << holdSum / 1000.0 << " ms, saves "
                      << saveSum / 1000.0 << " ms\n";
        }
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const std::size_t at = cuts + i;
        std::cout << "HOLD " << groups[i].first << ": " << names[at] << " names, hold "
                  << holds[at] << " us, save " << saves[at] << " us of " << bytes[at]
                  << " bytes\n";
    }
}

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

// A frozen edge takes a pcurve cache for each new face on it; a face that is
// gone leaves its cache to the next one added (docs/TransactionLog.md sec
// 27.81-27.82). Extruded over and over, a sketch's frozen edge carried a
// cache for every extrusion ever made, and every walk of its list slowed.
TEST(ImmutableShapeTest, aFrozenEdgeKeepsNoCacheOfAFaceThatIsGone)
{
    gp_Circ circle(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), 5);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(circle).Edge()).Wire();
    setImmutable(wire);
    const TopoDS_Edge edge = TopoDS::Edge(TopExp_Explorer(wire, TopAbs_EDGE).Current());
    auto edgeCurves = [&edge]() {
        return Handle(BRep_TEdge)::DownCast(edge.TShape())->Curves().Size();
    };
    auto extrude = [&wire]() {
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire, true).Face();
        TopoDS_Shape prism = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 3)).Shape();
        ASSERT_TRUE(BRepCheck_Analyzer(prism).IsValid());
    };
    extrude();
    const int first = edgeCurves();
    for (int i = 0; i < 50; ++i)
        extrude();
    // The caches of the live faces at most: the last extrusion's, gone too
    // but not yet replaced.
    EXPECT_LE(edgeCurves(), first + 2);
    // A face still alive keeps its pcurve.
    TopoDS_Face kept = BRepBuilderAPI_MakeFace(wire, true).Face();
    for (int i = 0; i < 5; ++i)
        extrude();
    Standard_Real f, l;
    EXPECT_FALSE(BRep_Tool::CurveOnSurface(edge, kept, f, l).IsNull());
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
    // The copies are thawed copies of what they stand for, the joining vertex
    // and the first edge, which uses it (docs/TransactionLog.md sec 23.15).
    for (int i = 1; i <= ancestors.Extent(); ++i) {
        if (ancestors(i).Extent() == 2) {
            const TopoDS_Shape& vertex = ancestors.FindKey(i);
            EXPECT_TRUE(vertex.TShape()->Thawed());
            EXPECT_EQ(TopoDS_TShape::ThawedFrom(vertex.TShape().get()), join.TShape());
        }
    }
    int firstCopies = 0;
    for (TopExp_Explorer it(wire, TopAbs_EDGE); it.More(); it.Next()) {
        if (TopoDS_TShape::ThawedFrom(it.Current().TShape().get()) == first.TShape())
            ++firstCopies;
    }
    EXPECT_EQ(firstCopies, 1);
}

// A vertex parameter is derived data like a pcurve (docs/TransactionLog.md sec
// 23.14): a frozen vertex put on a new edge -- as the offset algorithm puts an
// input edge's ends, INTERNAL, on the edge it extends -- takes a parameter on
// that edge's curve, marked a cache; its own parameters may only be restated.
TEST(ImmutableShapeTest, aFrozenVertexTakesAParameterOnANewCurve)
{
    tests::initApplication();
    BRep_Builder builder;
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    TopoDS_Vertex start, end;
    TopExp::Vertices(edge, start, end);
    // An internal vertex whose parameter on the edge is the edge's own.
    TopoDS_Vertex inner;
    builder.MakeVertex(inner, gp_Pnt(5, 0, 0), BRep_Tool::Tolerance(edge));
    builder.UpdateVertex(TopoDS::Vertex(inner.Oriented(TopAbs_INTERNAL)), 5., edge, 0.);
    builder.Add(edge, inner.Oriented(TopAbs_INTERNAL));
    setImmutable(edge);
    const std::string before = storedBytes(edge);

    Handle(Geom_Curve) line = new Geom_Line(gp_Pnt(10, 0, 0), gp::DY());
    TopoDS_Edge extended = BRepBuilderAPI_MakeEdge(line, -5., 5.).Edge();
    const double tol = BRep_Tool::Tolerance(end);
    const TopoDS_Vertex onExtended = TopoDS::Vertex(end.Oriented(TopAbs_INTERNAL));
    EXPECT_NO_THROW(builder.UpdateVertex(onExtended, 0., extended, tol));
    ASSERT_FALSE(pointOn(end, line).IsNull());
    EXPECT_TRUE(pointOn(end, line)->IsCache());
    // Being a cache it may be rewritten; not with a tolerance to grow to.
    EXPECT_NO_THROW(builder.UpdateVertex(onExtended, 1e-3, extended, tol));
    EXPECT_THROW(builder.UpdateVertex(onExtended, 0., extended, tol * 10), TopoDS_LockedShape);
    // The inner vertex's parameter is the value's: restated, not moved.
    Standard_Real first, last;
    Handle(Geom_Curve) own = BRep_Tool::Curve(edge, first, last);
    ASSERT_FALSE(pointOn(inner, own).IsNull());
    EXPECT_FALSE(pointOn(inner, own)->IsCache());
    const TopoDS_Vertex innerOn = TopoDS::Vertex(inner.Oriented(TopAbs_INTERNAL));
    EXPECT_NO_THROW(builder.UpdateVertex(innerOn, 5., edge, 0.));
    EXPECT_THROW(builder.UpdateVertex(innerOn, 6., edge, 0.), TopoDS_LockedShape);
    // And the edge stores what it did: a parameter on a curve none of its
    // edges carries is left out.
    EXPECT_EQ(storedBytes(edge), before);
}

// The same on a frozen face's own surface: a new edge lying on it carries a
// pcurve there, and the frozen vertex's parameter on that pcurve names the
// face's surface -- so the surface alone cannot tell it from the face's own.
TEST(ImmutableShapeTest, aParameterOnANewPCurveLeavesTheFaceBytes)
{
    tests::initApplication();
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
    const TopoDS_Face face = TopoDS::Face(TopExp_Explorer(box, TopAbs_FACE).Current());
    setImmutable(box);
    const std::string before = storedBytes(face);

    TopLoc_Location loc;
    const Handle(Geom_Surface)& surface = BRep_Tool::Surface(face, loc);
    const TopoDS_Vertex vertex = TopoDS::Vertex(TopExp_Explorer(face, TopAbs_VERTEX).Current());
    const gp_Pnt at = BRep_Tool::Pnt(vertex);
    GeomAPI_ProjectPointOnSurf project(at, surface);
    ASSERT_TRUE(project.NbPoints() > 0);
    Standard_Real u, v;
    project.LowerDistanceParameters(u, v);
    // A new edge on the face's plane through the vertex, with its pcurve.
    Handle(Geom2d_Curve) pcurve = new Geom2d_Line(gp_Pnt2d(u, v), gp_Dir2d(1, 1));
    TopoDS_Edge onFace = BRepBuilderAPI_MakeEdge(pcurve, surface, -1., 1.).Edge();
    BRep_Builder builder;
    EXPECT_NO_THROW(builder.UpdateVertex(TopoDS::Vertex(vertex.Oriented(TopAbs_INTERNAL)),
                                         0.,
                                         onFace,
                                         BRep_Tool::Tolerance(vertex)));
    EXPECT_EQ(storedBytes(face), before);
}

// A face on a frozen wire whose edges' tolerances differ: FindSurface gives the
// face the largest, and UpdateTolerances raises every other edge to it. Those
// are frozen, so the face holds copies that are raised -- thawed copies, which
// name their originals -- and the wire keeps its own (docs/TransactionLog.md
// sec 23.15).
namespace {

TopoDS_Wire wireOfUnequalTolerances()
{
    BRep_Builder builder;
    BRepBuilderAPI_MakeWire mkWire;
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10, 0, 0), gp_Pnt(10, 5, 0)).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10, 5, 0), gp_Pnt(0, 5, 0)).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 5, 0), gp_Pnt(0, 0, 0)).Edge());
    const TopoDS_Wire wire = mkWire.Wire();
    const TopoDS_Edge wide = TopoDS::Edge(TopExp_Explorer(wire, TopAbs_EDGE).Current());
    builder.UpdateEdge(wide, 2e-7);
    for (TopExp_Explorer it(wide, TopAbs_VERTEX); it.More(); it.Next())
        builder.UpdateVertex(TopoDS::Vertex(it.Current()), 2e-7);
    return wire;
}

std::vector<double> tolerancesOf(const TopoDS_Shape& shape)
{
    std::vector<double> tolerances;
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next())
        tolerances.push_back(BRep_Tool::Tolerance(TopoDS::Edge(it.Current())));
    for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next())
        tolerances.push_back(BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())));
    return tolerances;
}

} // namespace

TEST(ImmutableShapeTest, aFaceOnAFrozenWireThawsWhatMustGrow)
{
    const TopoDS_Wire wire = wireOfUnequalTolerances();
    setImmutable(wire);
    const std::vector<double> tolerances = tolerancesOf(wire);

    TopoDS_Face face;
    EXPECT_NO_THROW(face = BRepBuilderAPI_MakeFace(wire, true).Face());
    ASSERT_FALSE(face.IsNull());
    EXPECT_TRUE(BRepCheck_Analyzer(face).IsValid());
    EXPECT_EQ(tolerancesOf(wire), tolerances);
    // The face is what it is without the freeze: the largest tolerance ...
    EXPECT_DOUBLE_EQ(BRep_Tool::Tolerance(face), 2e-7);
    // ... on a thawed copy of the wire, holding thawed copies of the three
    // edges that grew, and the wide one itself.
    const TopoDS_Shape faceWire = TopExp_Explorer(face, TopAbs_WIRE).Current();
    EXPECT_TRUE(faceWire.TShape()->Thawed());
    EXPECT_EQ(TopoDS_TShape::ThawedFrom(faceWire.TShape().get()), wire.TShape());
    int thawed = 0;
    int same = 0;
    for (TopExp_Explorer it(faceWire, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Shape& edge = it.Current();
        if (edge.TShape()->Thawed()) {
            ++thawed;
            EXPECT_DOUBLE_EQ(BRep_Tool::Tolerance(TopoDS::Edge(edge)), 2e-7);
            const Handle(TopoDS_TShape) from = TopoDS_TShape::ThawedFrom(edge.TShape().get());
            bool inWire = false;
            for (TopExp_Explorer w(wire, TopAbs_EDGE); w.More(); w.Next())
                inWire = inWire || w.Current().TShape() == from;
            EXPECT_TRUE(inWire);
        }
        else {
            ++same;
            EXPECT_TRUE(edge.Immutable());
        }
    }
    EXPECT_EQ(thawed, 3);
    EXPECT_EQ(same, 1);
}

// The same on a wire that is placed and holds an edge reversed, as a pattern
// instance does: a thawed copy of an edge keeps its vertices at its curve's
// ends. The copy took the placed edge's location and orientation, and the
// vertices it was given were moved back by it and turned round -- a
// PolarPattern refined with vertices of tolerance 6.8 (docs/TransactionLog.md
// sec 27.84).
TEST(ImmutableShapeTest, aThawedCopyOfAPlacedEdgeKeepsItsVertices)
{
    BRep_Builder builder;
    BRepBuilderAPI_MakeWire mkWire;
    const TopoDS_Edge wide = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    builder.UpdateEdge(wide, 2e-7);
    for (TopExp_Explorer it(wide, TopAbs_VERTEX); it.More(); it.Next())
        builder.UpdateVertex(TopoDS::Vertex(it.Current()), 2e-7);
    mkWire.Add(wide);
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10, 0, 0), gp_Pnt(10, 5, 0)).Edge());
    // Made the other way round, so the wire holds it reversed.
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 5, 0), gp_Pnt(10, 5, 0)).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 5, 0), gp_Pnt(0, 0, 0)).Edge());
    gp_Trsf place;
    place.SetRotation(gp::OZ(), M_PI / 2);
    gp_Trsf lift;
    lift.SetTranslation(gp_Vec(0, 0, 2));
    const TopoDS_Shape wire = mkWire.Wire().Moved(TopLoc_Location(lift * place));
    int reversed = 0;
    for (TopExp_Explorer it(wire, TopAbs_EDGE); it.More(); it.Next())
        reversed += it.Current().Orientation() == TopAbs_REVERSED ? 1 : 0;
    ASSERT_EQ(reversed, 1);
    setImmutable(wire);

    TopoDS_Face face;
    EXPECT_NO_THROW(face = BRepBuilderAPI_MakeFace(TopoDS::Wire(wire), true).Face());
    ASSERT_FALSE(face.IsNull());
    int thawed = 0;
    for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        thawed += edge.TShape()->Thawed() ? 1 : 0;
        double first = 0;
        double last = 0;
        const Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        TopoDS_Vertex start;
        TopoDS_Vertex end;
        TopExp::Vertices(TopoDS::Edge(edge.Oriented(TopAbs_FORWARD)), start, end);
        EXPECT_LT(BRep_Tool::Pnt(start).Distance(curve->Value(first)), 1e-6);
        EXPECT_LT(BRep_Tool::Pnt(end).Distance(curve->Value(last)), 1e-6);
    }
    EXPECT_EQ(thawed, 3);  // the path under test was taken
    EXPECT_TRUE(BRepCheck_Analyzer(face).IsValid());
}

// The names a face gives the edges of a frozen wire are the ones it gives them
// on the same wire unfrozen: a thawed copy is named as its original.
TEST(ImmutableShapeTest, aThawedCopyKeepsItsName)
{
    tests::initApplication();
    Base::Interpreter().runString("import sys; sys.path[:0] = ['" FC_BUILD_LIB_DIR
                                  "', '" FC_BUILD_MOD_PART_DIR "']");
    Base::Interpreter().runString("import Part");
    // FaceMakerSimple is BRepBuilderAPI_MakeFace(wire), which raises the
    // narrow edges to the wide one.
    auto faceOn = [](bool frozen) {
        const Part::TopoShape wire(wireOfUnequalTolerances(), 10L);
        if (frozen)
            setImmutable(wire.getShape());
        Part::TopoShape face(20L);
        face.makEFace(wire, nullptr, "Part::FaceMakerSimple");
        return face;
    };
    const Part::TopoShape plain = faceOn(false);
    const Part::TopoShape frozen = faceOn(true);
    ASSERT_EQ(frozen.countSubShapes(TopAbs_EDGE), 4);
    int thawed = 0;
    for (TopExp_Explorer it(frozen.getShape(), TopAbs_EDGE); it.More(); it.Next())
        thawed += it.Current().TShape()->Thawed() ? 1 : 0;
    EXPECT_GT(thawed, 0);  // the path under test was taken
    for (const char* type : {"Edge", "Vertex", "Face"}) {
        const int count = frozen.countSubShapes(type);
        ASSERT_EQ(plain.countSubShapes(type), count);
        for (int i = 1; i <= count; ++i) {
            const auto element = Data::IndexedName::fromConst(type, i);
            const Data::MappedName name = frozen.getMappedName(element);
            EXPECT_TRUE(name) << type << i;
            EXPECT_EQ(name.toString(), plain.getMappedName(element).toString()) << type << i;
        }
    }
}

// A thick solid from a frozen hemisphere, its spherical face removed: the
// offset puts the removed face's vertices on the edges it extends (the first
// test above), and leaves the input's bytes as they were. A whole sphere
// with its one face removed, which this used, leaves no face to thicken and
// is refused (docs/TransactionLog.md sec 27.105): not done, and no throw.
TEST(ImmutableShapeTest, aThickSolidLeavesAFrozenInput)
{
    tests::initApplication();
    TopoDS_Shape hemisphere = BRepPrimAPI_MakeSphere(5, 0., M_PI / 2).Shape();
    setImmutable(hemisphere);
    const std::string before = storedBytes(hemisphere);
    TopTools_ListOfShape removed;
    removed.Append(TopExp_Explorer(hemisphere, TopAbs_FACE).Current());
    ASSERT_EQ(BRepAdaptor_Surface(TopoDS::Face(removed.First())).GetType(), GeomAbs_Sphere);
    BRepOffsetAPI_MakeThickSolid thick;
    EXPECT_NO_THROW(thick.MakeThickSolidByJoin(hemisphere, removed, 0.5, 1e-3));
    EXPECT_TRUE(thick.IsDone());
    EXPECT_EQ(storedBytes(hemisphere), before);

    TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(5).Shape();
    setImmutable(sphere);
    const std::string sphereBefore = storedBytes(sphere);
    TopTools_ListOfShape all;
    all.Append(TopExp_Explorer(sphere, TopAbs_FACE).Current());
    BRepOffsetAPI_MakeThickSolid none;
    EXPECT_NO_THROW(none.MakeThickSolidByJoin(sphere, all, 0.5, 1e-3));
    EXPECT_FALSE(none.IsDone());
    EXPECT_EQ(storedBytes(sphere), sphereBefore);
}

// TopoShape::fix() repairs a copy, then the shape itself in place to keep its
// sharing. A revolve's cap is its base face, so a revolved frozen face would be
// repaired inside the value: the fixed copy stands instead.
TEST(ImmutableShapeTest, fixLeavesAFrozenPartAlone)
{
    tests::initApplication();
    TopoDS_Shape torus = BRepPrimAPI_MakeTorus(6, 2).Shape();
    const TopoDS_Face face = TopoDS::Face(TopExp_Explorer(torus, TopAbs_FACE).Current());
    setImmutable(face);
    const std::string before = storedBytes(face);
    Part::TopoShape revolved;
    EXPECT_NO_THROW(revolved.makERevolve(Part::TopoShape(face),
                                         gp_Ax1(gp_Pnt(-20, 0, 0), gp::DY()),
                                         M_PI / 2));
    EXPECT_FALSE(revolved.isNull());
    EXPECT_EQ(storedBytes(face), before);
}

// A frozen shape is read on another thread -- the transaction log writes the
// values it is given on a worker -- while it still takes caches here: a
// mesher's polygons, a pcurve for a new face. Both edit the edge's list of
// representations, and the writer walked a node the remesh had freed
// (docs/TransactionLog.md sec 27.98). In the OCCT fork every edit of an
// Immutable TShape's representations, and every walk of them by a writer,
// holds BRep_RepresentationLock.
namespace {

TopoDS_Edge firstEdge(const TopoDS_Shape& shape)
{
    return TopoDS::Edge(TopExp_Explorer(shape, TopAbs_EDGE).Current());
}

} // namespace

TEST(ImmutableShapeTest, aRemeshWaitsForAWriterOfAFrozenEdge)
{
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    setImmutable(box);
    const TopoDS_Edge edge = firstEdge(box);
    std::atomic<bool> released {false};
    std::promise<void> holding;
    std::thread writer([&]() {
        BRep_RepresentationLock lock(edge.TShape().get());
        holding.set_value();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        released = true;
    });
    holding.get_future().wait();
    // Writes a polygon into every edge, the held one among them.
    BRepMesh_IncrementalMesh mesh(box, 0.5);
    EXPECT_TRUE(released.load());
    writer.join();
}

TEST(ImmutableShapeTest, aWriterWaitsForAnEditOfAFrozenEdge)
{
    tests::initApplication();  // the storage options read DocumentParams
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    setImmutable(box);
    BRepMesh_IncrementalMesh mesh(box, 0.5);
    const std::string expected = storedBytes(box);
    std::future<std::string> written;
    {
        BRep_RepresentationLock lock(firstEdge(box).TShape().get());
        written = std::async(std::launch::async, [&box]() { return storedBytes(box); });
        EXPECT_EQ(written.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);
    }
    EXPECT_EQ(written.get(), expected);
}

TEST(ImmutableShapeTest, aShapeThatIsNotFrozenTakesNoLock)
{
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    BRep_RepresentationLock lock(firstEdge(box).TShape().get());
    auto meshed = std::async(std::launch::async, [&box]() { BRepMesh_IncrementalMesh(box, 0.5); });
    EXPECT_EQ(meshed.wait_for(std::chrono::seconds(10)), std::future_status::ready);
}

TEST(ImmutableShapeTest, aFrozenShapeIsWrittenWhileItIsRemeshed)
{
    tests::initApplication();  // the storage options read DocumentParams
    // The race itself, unforced: one thread writes the value over and over
    // while this one remeshes it at changing deflections, replacing every
    // edge's polygons. Before the lock this could crash; it must give the
    // same bytes every time, since polygons are not stored.
    TopoDS_Shape shape = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            gp_Ax2 axis(gp_Pnt(1.5 + i * 2.3, 1.5 + j * 2.3, 0), gp::DZ());
            shape = BRepAlgoAPI_Cut(shape, BRepPrimAPI_MakeCylinder(axis, 0.4, 10).Shape()).Shape();
        }
    }
    setImmutable(shape);
    BRepMesh_IncrementalMesh(shape, 0.5);
    const std::string expected = storedBytes(shape);
    std::atomic<bool> stop {false};
    std::atomic<int> differ {0};
    std::thread writer([&]() {
        while (!stop) {
            if (storedBytes(shape) != expected)
                ++differ;
        }
    });
    // Cleaned first, or a coarser request keeps the finer mesh and edits
    // nothing; the clean removes every polygon node too.
    for (int k = 0; k < 200; ++k) {
        BRepTools::Clean(shape);
        BRepMesh_IncrementalMesh(shape, 0.2 + 0.1 * (k % 3));
    }
    stop = true;
    writer.join();
    EXPECT_EQ(differ.load(), 0);
}

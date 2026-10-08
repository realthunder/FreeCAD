// SPDX-License-Identifier: LGPL-2.1-or-later

// The claims of the load's parallel pre-mesh (docs/DocumentLoad.md sec 18):
// a shape a worker is still meshing answers "in flight" until that worker
// publishes it, whatever is cleared in the meantime.
//
// clearPreMeshClaims used to free every claim, the ones in flight too. The
// worker then published into freed memory -- one byte, the flag, into a
// block somebody else had been given: the heap corruption of sec 18.7 --
// and from the clear on the shape answered "not claimed", so the GUI
// thread was free to build a shape a worker was still writing.
//
// No document and no application: the claims are keyed on TShape addresses
// and the workers are OCCT's.

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

#include <Mod/Part/Gui/PreMesh.h>

namespace
{

// A sphere asked for finely enough that its mesh is still being made when
// the questions below are asked: some ten thousand triangles, a few tenths
// of a second, where a thread start and a question are microseconds. Every
// test asserts that it found the shape in flight before it concludes
// anything from it.
PartGui::PreMeshItem slowItem()
{
    PartGui::PreMeshItem item;
    item.shape = BRepPrimAPI_MakeSphere(100.0).Shape();
    BRepBndLib::AddOptimal(item.shape, item.geomBox, Standard_False);
    item.deflection = 0.03;
    item.angle = 0.5;
    return item;
}

std::vector<PartGui::PreMeshItem> batchOf(const PartGui::PreMeshItem& item)
{
    std::vector<PartGui::PreMeshItem> items;
    items.push_back(item);
    return items;
}

const void* keyOf(const PartGui::PreMeshItem& item)
{
    return item.shape.TShape().get();
}

bool meshed(const TopoDS_Shape& shape)
{
    for (TopExp_Explorer xp(shape, TopAbs_FACE); xp.More(); xp.Next()) {
        TopLoc_Location loc;
        if (BRep_Tool::Triangulation(TopoDS::Face(xp.Current()), loc).IsNull()) {
            return false;
        }
    }
    return true;
}

struct Stats
{
    std::size_t claimed = 0, meshed = 0, failed = 0;
    double wall = 0.0;
};

Stats stats()
{
    Stats s;
    PartGui::preMeshStats(s.claimed, s.meshed, s.failed, s.wall);
    return s;
}

// The batch's own end, which comes a moment after its last claim is
// published: its counts are added then.
bool batchCounted(std::size_t meshedCount, double seconds)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (stats().meshed + stats().failed >= meshedCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

}  // namespace

class PreMeshTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        PartGui::clearPreMeshClaims();
    }
};

TEST_F(PreMeshTest, aClaimIsInFlightUntilItsWorkerPublishesIt)
{
    PartGui::PreMeshItem item = slowItem();
    const void* key = keyOf(item);
    Bnd_Box box;
    EXPECT_FALSE(PartGui::preMeshInFlight(key));

    PartGui::submitPreMesh(batchOf(item));
    ASSERT_TRUE(PartGui::preMeshInFlight(key)) << "meshed before it could be asked about";
    EXPECT_TRUE(PartGui::preMeshBox(key, box));
    EXPECT_FALSE(box.IsVoid());

    ASSERT_TRUE(PartGui::waitPreMesh(key, 120.0));
    EXPECT_FALSE(PartGui::preMeshInFlight(key));
    EXPECT_TRUE(meshed(item.shape));
    ASSERT_TRUE(batchCounted(1, 30.0));
    EXPECT_EQ(stats().claimed, 1U);
    EXPECT_EQ(stats().meshed, 1U);

    // Nothing in flight: a clear forgets all of it
    PartGui::clearPreMeshClaims();
    EXPECT_EQ(stats().claimed, 0U);
    EXPECT_FALSE(PartGui::preMeshInFlight(key));
}

TEST_F(PreMeshTest, aShapeStillBeingMeshedIsInFlightAfterAClear)
{
    PartGui::PreMeshItem item = slowItem();
    const void* key = keyOf(item);

    PartGui::submitPreMesh(batchOf(item));
    ASSERT_TRUE(PartGui::preMeshInFlight(key)) << "meshed before it could be asked about";

    // What the drain does when its queues are empty, and what a closing
    // document does -- with the worker still writing the shape
    PartGui::clearPreMeshClaims();

    EXPECT_TRUE(PartGui::preMeshInFlight(key))
        << "a shape a worker is still meshing answered as free to build";
    Bnd_Box box;
    EXPECT_TRUE(PartGui::preMeshBox(key, box))
        << "its box has to come from the claim while the shape may not be read";

    // The wait ends when the worker publishes, not when the claim is cleared
    ASSERT_TRUE(PartGui::waitPreMesh(key, 120.0));
    EXPECT_TRUE(meshed(item.shape)) << "the wait returned with the worker still meshing";
    EXPECT_FALSE(PartGui::preMeshInFlight(key));
    // A result nobody waited for is counted for nobody
    EXPECT_EQ(stats().claimed, 0U);
    EXPECT_EQ(stats().meshed, 0U);
}

TEST_F(PreMeshTest, aShapeInFlightIsNotHandedToASecondWorker)
{
    PartGui::PreMeshItem item = slowItem();
    const void* key = keyOf(item);

    PartGui::submitPreMesh(batchOf(item));
    ASSERT_TRUE(PartGui::preMeshInFlight(key)) << "meshed before it could be asked about";

    // The same shape again while its worker runs: a load that parks it a
    // second time, a second document holding the same TShape
    PartGui::submitPreMesh(batchOf(item));
    EXPECT_EQ(stats().claimed, 1U) << "two workers were given one triangulation to write";

    ASSERT_TRUE(PartGui::waitPreMesh(key, 120.0));
    EXPECT_TRUE(meshed(item.shape)) << "published by one worker with the other still meshing";
    ASSERT_TRUE(batchCounted(1, 30.0));
    EXPECT_EQ(stats().meshed, 1U);

    // Published, it can be claimed again
    PartGui::submitPreMesh(batchOf(item));
    EXPECT_EQ(stats().claimed, 2U);
    ASSERT_TRUE(PartGui::waitPreMesh(key, 120.0));
    ASSERT_TRUE(batchCounted(2, 30.0));
}

TEST_F(PreMeshTest, aStopWaitsForTheWorkersAndStartsNoOtherShape)
{
    // Far more shapes than the workers can have started when the stop
    // comes: a batch of them would run for seconds
    std::vector<PartGui::PreMeshItem> items;
    std::vector<TopoDS_Shape> shapes;
    for (int i = 0; i < 96; ++i) {
        items.push_back(slowItem());
        shapes.push_back(items.back().shape);
    }
    const void* last = keyOf(items.back());
    PartGui::submitPreMesh(std::move(items));
    ASSERT_TRUE(PartGui::preMeshInFlight(last)) << "meshed before it could be asked about";

    const auto start = std::chrono::steady_clock::now();
    PartGui::stopPreMesh();
    const double waited =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    // Returned with the batch ended: its counts are in, and they are all of it
    EXPECT_EQ(stats().claimed, 96U);
    EXPECT_EQ(stats().meshed + stats().failed, 96U) << "the stop returned with the batch running";
    EXPECT_LT(stats().meshed, 96U) << "every shape was meshed: nothing was stopped";
    EXPECT_GT(stats().failed, 0U);
    // No claim left in flight, the shapes not started among them
    std::size_t flying = 0, unmeshed = 0;
    for (const TopoDS_Shape& shape : shapes) {
        if (PartGui::preMeshInFlight(shape.TShape().get())) {
            ++flying;
        }
        if (!meshed(shape)) {
            ++unmeshed;
        }
    }
    EXPECT_EQ(flying, 0U);
    EXPECT_EQ(unmeshed, stats().failed);
    // A stop is the wait for the shapes in hand, not for the batch
    EXPECT_LT(waited, 3.0) << "96 shapes of a few tenths of a second each";

    // And the pre-mesh works again after it
    PartGui::clearPreMeshClaims();
    PartGui::PreMeshItem item = slowItem();
    const void* key = keyOf(item);
    PartGui::submitPreMesh(batchOf(item));
    ASSERT_TRUE(PartGui::waitPreMesh(key, 120.0));
    EXPECT_TRUE(meshed(item.shape));
    ASSERT_TRUE(batchCounted(1, 30.0));
    EXPECT_EQ(stats().meshed, 1U);
}

TEST_F(PreMeshTest, aShapeMadeOfAClaimedOnesFacesIsInFlightToo)
{
    PartGui::PreMeshItem item = slowItem();
    // What a compound feature over that object holds, and a boolean's
    // result where it keeps the object's faces: another shape, never in
    // the batch, made of faces that are
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, item.shape);
    builder.Add(compound, BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape());
    TopExp_Explorer xp(item.shape, TopAbs_FACE);
    const TopoDS_Shape face = xp.Current();
    const TopoDS_Shape apart = BRepPrimAPI_MakeBox(5.0, 5.0, 5.0).Shape();
    EXPECT_FALSE(PartGui::preMeshInFlight(compound));

    PartGui::submitPreMesh(batchOf(item));
    ASSERT_TRUE(PartGui::preMeshInFlight(keyOf(item))) << "meshed before it could be asked about";

    // Not claimed itself...
    EXPECT_FALSE(PartGui::preMeshInFlight(compound.TShape().get()));
    // ...and not to be read or meshed all the same
    EXPECT_TRUE(PartGui::preMeshInFlight(compound))
        << "a compound over a shape a worker is meshing answered as free to build";
    EXPECT_TRUE(PartGui::preMeshInFlight(face));
    EXPECT_TRUE(PartGui::preMeshInFlight(item.shape));
    EXPECT_FALSE(PartGui::preMeshInFlight(apart));

    // Claimed itself, it is a claim of its own -- behind the one that has
    // its faces, which are in one twin at a time (sec 18.9; it used to be
    // refused, and left to the GUI thread)
    PartGui::PreMeshItem second;
    second.shape = compound;
    second.geomBox = item.geomBox;
    second.deflection = item.deflection;
    second.angle = item.angle;
    PartGui::submitPreMesh(batchOf(second));
    EXPECT_EQ(stats().claimed, 2U);
    EXPECT_TRUE(PartGui::preMeshInFlight(compound.TShape().get()));

    // The wait for it ends when both are on their shapes
    ASSERT_TRUE(PartGui::waitPreMesh(compound, 120.0));
    EXPECT_TRUE(meshed(item.shape)) << "the wait returned with the worker still meshing";
    EXPECT_TRUE(meshed(compound));
    EXPECT_FALSE(PartGui::preMeshInFlight(compound));
    EXPECT_FALSE(PartGui::preMeshInFlight(face));
    ASSERT_TRUE(batchCounted(2, 30.0));

    // Done, the compound can be claimed again, and has nothing left to do
    PartGui::submitPreMesh(batchOf(second));
    EXPECT_EQ(stats().claimed, 3U);
    ASSERT_TRUE(PartGui::waitPreMesh(compound, 120.0));
    ASSERT_TRUE(batchCounted(3, 30.0));
    EXPECT_FALSE(PartGui::preMeshInFlight(item.shape));
}

// From here: the private twin (docs/DocumentLoad.md sec 18.9). A worker is
// handed a copy, and what it made there reaches the shape on the shape's own
// thread, when that thread asks.

namespace
{

std::vector<const void*> triangulationsOf(const TopoDS_Shape& shape)
{
    std::vector<const void*> out;
    for (TopExp_Explorer xp(shape, TopAbs_FACE); xp.More(); xp.Next()) {
        TopLoc_Location loc;
        out.push_back(BRep_Tool::Triangulation(TopoDS::Face(xp.Current()), loc).get());
    }
    return out;
}

}  // namespace

TEST_F(PreMeshTest, aWorkerDoesNotWriteTheShape)
{
    PartGui::PreMeshItem item = slowItem();
    const void* key = keyOf(item);
    PartGui::submitPreMesh(batchOf(item));

    // Long past what the mesh takes -- a few tenths of a second -- with
    // nothing asked of the pre-mesh in the meantime: whatever a worker was
    // going to do to this shape, it has done
    std::this_thread::sleep_for(std::chrono::seconds(3));
    EXPECT_FALSE(meshed(item.shape)) << "the shape was meshed by another thread than its own";

    // Its own thread asks, and takes the mesh
    ASSERT_TRUE(PartGui::waitPreMesh(key, 120.0));
    EXPECT_TRUE(meshed(item.shape));
    EXPECT_FALSE(PartGui::preMeshInFlight(key));
}

TEST_F(PreMeshTest, shapesSharingFacesAreBothMeshedAndEachFaceOnce)
{
    // An object and a compound over it, as two objects of one document hold
    // them: one batch, the same ask
    PartGui::PreMeshItem item = slowItem();
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, item.shape);
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    builder.Add(compound, box);
    PartGui::PreMeshItem second;
    second.shape = compound;
    second.geomBox = item.geomBox;
    second.deflection = item.deflection;
    second.angle = item.angle;

    std::vector<PartGui::PreMeshItem> items;
    items.push_back(item);
    items.push_back(second);
    PartGui::submitPreMesh(std::move(items));
    EXPECT_EQ(stats().claimed, 2U) << "a shape sharing faces with another was left to the GUI thread";
    EXPECT_TRUE(PartGui::preMeshInFlight(compound));

    ASSERT_TRUE(PartGui::waitPreMesh(keyOf(item), 120.0));
    ASSERT_TRUE(meshed(item.shape));
    const std::vector<const void*> first = triangulationsOf(item.shape);

    ASSERT_TRUE(PartGui::waitPreMesh(compound, 120.0));
    EXPECT_FALSE(PartGui::preMeshInFlight(compound));
    EXPECT_TRUE(meshed(box)) << "the compound's own faces were not meshed";
    EXPECT_TRUE(meshed(compound));
    // The faces the two share were meshed for the first, and stand
    EXPECT_EQ(triangulationsOf(item.shape), first) << "a shared face was meshed a second time";
}

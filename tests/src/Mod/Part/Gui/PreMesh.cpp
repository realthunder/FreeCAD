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
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
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

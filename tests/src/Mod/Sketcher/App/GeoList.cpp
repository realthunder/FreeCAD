// SPDX-License-Identifier: LGPL-2.1-or-later

#include <FCConfig.h>

#include <memory>
#include <type_traits>
#include <vector>

#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/GeoList.h>
#include <Mod/Sketcher/App/GeometryFacade.h>

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

using namespace Sketcher;

namespace
{

Part::GeomLineSegment* makeLine(double x1, double y1, double x2, double y2)
{
    auto* line = new Part::GeomLineSegment();
    line->setPoints(Base::Vector3d(x1, y1, 0.0), Base::Vector3d(x2, y2, 0.0));
    return line;
}

class GeoListTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
};

}  // namespace

// A GeoList holds naked geometry pointers, so a facade query has to build the
// facade and the caller owns it. The owning form used to be returned as a bare
// pointer -- released out of its unique_ptr -- with only a comment asking the
// caller to delete it, so every call leaked one facade. Pinning the type is
// what stops that coming back: a unique_ptr cannot be dropped on the floor.
TEST_F(GeoListTest, ownsTheFacadeItBuilds)
{
    static_assert(std::is_same_v<GeoList::GeometryFacadeRef,
                                 std::unique_ptr<const Sketcher::GeometryFacade>>,
                  "a GeoList builds the facade, so it must hand back ownership");
}

// A GeoListFacade already owns its facades, so a query only borrows one and
// the caller must not free it. That half was always right and must stay a
// plain pointer: returning ownership here would double free.
TEST_F(GeoListTest, lendsTheFacadeItHolds)
{
    static_assert(
        std::is_same_v<GeoListFacade::GeometryFacadeRef, const Sketcher::GeometryFacade*>,
        "a GeoListFacade owns its facades, so it must hand back a borrowed pointer");
}

TEST_F(GeoListTest, builtFacadeIsUsableAndFreesItself)
{
    std::vector<Part::Geometry*> geometry {makeLine(0.0, 0.0, 1.0, 0.0),
                                           makeLine(1.0, 0.0, 1.0, 1.0)};

    // ownerT = true, so the list deletes the geometry it was handed
    auto geolist = GeoList::getGeoListModel(std::move(geometry), 2, true);

    auto facade = geolist.getGeometryFacadeFromGeoId(0);

    ASSERT_TRUE(facade);
    EXPECT_TRUE(facade->getGeometry()->is<Part::GeomLineSegment>());

    // The facade is ours: it goes away with this scope, and the geometry it
    // borrows outlives it because the list owns that.
    facade.reset();
    EXPECT_EQ(geolist.getInternalCount(), 2);
}

TEST_F(GeoListTest, borrowedFacadeMatchesTheModel)
{
    std::vector<Part::Geometry*> geometry {makeLine(0.0, 0.0, 1.0, 0.0)};

    auto geolist = GeoList::getGeoListModel(std::move(geometry), 1, true);
    auto geolistfacade = getGeoListFacade(geolist);

    const Sketcher::GeometryFacade* borrowed = geolistfacade.getGeometryFacadeFromGeoId(0);

    ASSERT_NE(borrowed, nullptr);
    // Same object the model holds, not a copy built for the call.
    EXPECT_EQ(borrowed, geolistfacade.geomlist[0].get());
}

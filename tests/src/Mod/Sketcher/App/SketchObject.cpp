// SPDX-License-Identifier: LGPL-2.1-or-later

#include <FCConfig.h>

#include <algorithm>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <Mod/Sketcher/App/GeoEnum.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include "SketcherTestHelpers.h"

using namespace SketcherTestHelpers;


TEST_F(SketchObjectTest, createSketchObject)  // NOLINT
{
    // Arrange

    // Act

    // Assert
}

TEST_F(SketchObjectTest, testGeoIdFromShapeTypeEdge)
{
    // Arrange
    // TODO: Do we need to separate existing vs non-existing?
    // It would need to be implemented in code as well.
    Data::IndexedName name("Edge", 1);
    int geoId;
    Sketcher::PointPos posId;

    // Act
    getObject()->geoIdFromShapeType(name, geoId, posId);

    // Assert
    EXPECT_EQ(geoId, 0);
    EXPECT_EQ(posId, Sketcher::PointPos::none);
}

TEST_F(SketchObjectTest, testGeoIdFromShapeTypeVertex)
{
    // Arrange
    // For operating on vertices, there is newName a check if the vertex exists.
    Base::Vector3d p1(0.0, 0.0, 0.0), p2(1.0, 0.0, 0.0);
    std::unique_ptr<Part::Geometry> geoline(new Part::GeomLineSegment());
    static_cast<Part::GeomLineSegment*>(geoline.get())->setPoints(p1, p2);
    getObject()->addGeometry(geoline.get());
    // TODO: Do we need to separate existing vs non-existing?
    // It would need to be implemented in code as well.
    Data::IndexedName name("Vertex", 1);
    int geoId;
    Sketcher::PointPos posId;

    // Act
    getObject()->geoIdFromShapeType(name, geoId, posId);

    // Assert
    EXPECT_EQ(geoId, 0);
    EXPECT_EQ(posId, Sketcher::PointPos::start);
}

TEST_F(SketchObjectTest, testGeoIdFromShapeTypeExternalEdge)
{
    // Arrange
    // TODO: Do we need to separate existing vs non-existing?
    // It would need to be implemented in code as well.
    Data::IndexedName name("ExternalEdge", 1);
    int geoId;
    Sketcher::PointPos posId;

    // Act
    getObject()->geoIdFromShapeType(name, geoId, posId);

    // Assert
    EXPECT_EQ(geoId, Sketcher::GeoEnum::RefExt);
    EXPECT_EQ(posId, Sketcher::PointPos::none);
}

TEST_F(SketchObjectTest, testGeoIdFromShapeTypeHAxis)
{
    // Arrange
    Data::IndexedName name("H_Axis");
    int geoId;
    Sketcher::PointPos posId;

    // Act
    getObject()->geoIdFromShapeType(name, geoId, posId);

    // Assert
    EXPECT_EQ(geoId, Sketcher::GeoEnum::HAxis);
    EXPECT_EQ(posId, Sketcher::PointPos::none);
}

TEST_F(SketchObjectTest, testGeoIdFromShapeTypeVAxis)
{
    // Arrange
    Data::IndexedName name("V_Axis");
    int geoId;
    Sketcher::PointPos posId;

    // Act
    getObject()->geoIdFromShapeType(name, geoId, posId);

    // Assert
    EXPECT_EQ(geoId, Sketcher::GeoEnum::VAxis);
    EXPECT_EQ(posId, Sketcher::PointPos::none);
}

TEST_F(SketchObjectTest, testGeoIdFromShapeTypeRootPoint)
{
    // Arrange
    Data::IndexedName name("RootPoint");
    int geoId;
    Sketcher::PointPos posId;

    // Act
    getObject()->geoIdFromShapeType(name, geoId, posId);

    // Assert
    EXPECT_EQ(geoId, Sketcher::GeoEnum::RtPnt);
    EXPECT_EQ(posId, Sketcher::PointPos::start);
}

TEST_F(SketchObjectTest, testGetPointFromGeomPoint)
{
    // Arrange
    Base::Vector3d coords(1.0, 2.0, 0.0);
    Part::GeomPoint point(coords);

    // Act
    auto ptStart = Sketcher::SketchObject::getPoint(&point, Sketcher::PointPos::start);
    auto ptMid = Sketcher::SketchObject::getPoint(&point, Sketcher::PointPos::mid);
    auto ptEnd = Sketcher::SketchObject::getPoint(&point, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone = Sketcher::SketchObject::getPoint(&point, Sketcher::PointPos::none);

    // Assert
    EXPECT_DOUBLE_EQ(ptStart[0], 1.0);
    EXPECT_DOUBLE_EQ(ptStart[1], 2.0);
    EXPECT_DOUBLE_EQ(ptMid[0], 1.0);
    EXPECT_DOUBLE_EQ(ptMid[1], 2.0);
    EXPECT_DOUBLE_EQ(ptEnd[0], 1.0);
    EXPECT_DOUBLE_EQ(ptEnd[1], 2.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomLineSegment)
{
    // Arrange
    Base::Vector3d coords1(1.0, 2.0, 0.0);
    Base::Vector3d coords2(3.0, 4.0, 0.0);
    Part::GeomLineSegment lineSeg;
    lineSeg.setPoints(coords1, coords2);

    // Act
    auto ptStart = Sketcher::SketchObject::getPoint(&lineSeg, Sketcher::PointPos::start);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptMid = Sketcher::SketchObject::getPoint(&lineSeg, Sketcher::PointPos::mid);
    auto ptEnd = Sketcher::SketchObject::getPoint(&lineSeg, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone = Sketcher::SketchObject::getPoint(&lineSeg, Sketcher::PointPos::none);

    // Assert
    EXPECT_DOUBLE_EQ(ptStart[0], 1.0);
    EXPECT_DOUBLE_EQ(ptStart[1], 2.0);
    EXPECT_DOUBLE_EQ(ptEnd[0], 3.0);
    EXPECT_DOUBLE_EQ(ptEnd[1], 4.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomCircle)
{
    // Arrange
    Base::Vector3d coordsCenter(1.0, 2.0, 0.0);
    double radius = 3.0;
    Part::GeomCircle circle;
    circle.setCenter(coordsCenter);
    circle.setRadius(radius);

    // Act
    // TODO: Maybe we want this to give an error instead of some default value
    auto ptStart = Sketcher::SketchObject::getPoint(&circle, Sketcher::PointPos::start);
    auto ptMid = Sketcher::SketchObject::getPoint(&circle, Sketcher::PointPos::mid);
    // TODO: Maybe we want this to give an error instead of some default value
    auto ptEnd = Sketcher::SketchObject::getPoint(&circle, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone = Sketcher::SketchObject::getPoint(&circle, Sketcher::PointPos::none);

    // Assert
    // NOTE: Presently, start/end points of a circle are defined as the point on circle right of the
    // the center
    EXPECT_DOUBLE_EQ(ptStart[0], 1.0 + radius);
    EXPECT_DOUBLE_EQ(ptStart[1], 2.0);
    EXPECT_DOUBLE_EQ(ptEnd[0], 1.0 + radius);
    EXPECT_DOUBLE_EQ(ptEnd[1], 2.0);
    EXPECT_DOUBLE_EQ(ptMid[0], 1.0);
    EXPECT_DOUBLE_EQ(ptMid[1], 2.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomEllipse)
{
    // Arrange
    Base::Vector3d coordsCenter(1.0, 2.0, 0.0);
    double majorRadius = 4.0;
    double minorRadius = 3.0;
    Part::GeomEllipse ellipse;
    ellipse.setCenter(coordsCenter);
    ellipse.setMajorRadius(majorRadius);
    ellipse.setMinorRadius(minorRadius);

    // Act
    // TODO: Maybe we want this to give an error instead of some default value
    auto ptStart = Sketcher::SketchObject::getPoint(&ellipse, Sketcher::PointPos::start);
    auto ptMid = Sketcher::SketchObject::getPoint(&ellipse, Sketcher::PointPos::mid);
    // TODO: Maybe we want this to give an error instead of some default value
    auto ptEnd = Sketcher::SketchObject::getPoint(&ellipse, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone = Sketcher::SketchObject::getPoint(&ellipse, Sketcher::PointPos::none);

    // Assert
    // NOTE: Presently, start/end points of an ellipse are defined as the point on the major axis in
    // it's "positive" direction
    EXPECT_DOUBLE_EQ(ptStart[0], 1.0 + majorRadius);
    EXPECT_DOUBLE_EQ(ptStart[1], 2.0);
    EXPECT_DOUBLE_EQ(ptEnd[0], 1.0 + majorRadius);
    EXPECT_DOUBLE_EQ(ptEnd[1], 2.0);
    EXPECT_DOUBLE_EQ(ptMid[0], 1.0);
    EXPECT_DOUBLE_EQ(ptMid[1], 2.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomArcOfCircle)
{
    // Arrange
    Base::Vector3d coordsCenter(1.0, 2.0, 0.0);
    double radius = 3.0, startParam = std::numbers::pi / 3, endParam = std::numbers::pi * 1.5;
    Part::GeomArcOfCircle arcOfCircle;
    arcOfCircle.setCenter(coordsCenter);
    arcOfCircle.setRadius(radius);
    arcOfCircle.setRange(startParam, endParam, true);

    // Act
    auto ptStart = Sketcher::SketchObject::getPoint(&arcOfCircle, Sketcher::PointPos::start);
    auto ptMid = Sketcher::SketchObject::getPoint(&arcOfCircle, Sketcher::PointPos::mid);
    auto ptEnd = Sketcher::SketchObject::getPoint(&arcOfCircle, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone
        = Sketcher::SketchObject::getPoint(&arcOfCircle, Sketcher::PointPos::none);

    // Assert
    // NOTE: parameters for arc of circle are CCW angles from positive x-axis
    EXPECT_DOUBLE_EQ(ptStart[0], 1.0 + cos(startParam) * radius);
    EXPECT_DOUBLE_EQ(ptStart[1], 2.0 + sin(startParam) * radius);
    EXPECT_DOUBLE_EQ(ptEnd[0], 1.0 + cos(endParam) * radius);
    EXPECT_DOUBLE_EQ(ptEnd[1], 2.0 + sin(endParam) * radius);
    EXPECT_DOUBLE_EQ(ptMid[0], 1.0);
    EXPECT_DOUBLE_EQ(ptMid[1], 2.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomArcOfEllipse)
{
    // Arrange
    Base::Vector3d coordsCenter(1.0, 2.0, 0.0);
    double majorRadius = 4.0;
    double minorRadius = 3.0;
    double startParam = std::numbers::pi / 3, endParam = std::numbers::pi * 1.5;
    Part::GeomArcOfEllipse arcOfEllipse;
    arcOfEllipse.setCenter(coordsCenter);
    arcOfEllipse.setMajorRadius(majorRadius);
    arcOfEllipse.setMinorRadius(minorRadius);
    arcOfEllipse.setRange(startParam, endParam, true);

    // Act
    auto ptStart = Sketcher::SketchObject::getPoint(&arcOfEllipse, Sketcher::PointPos::start);
    auto ptMid = Sketcher::SketchObject::getPoint(&arcOfEllipse, Sketcher::PointPos::mid);
    auto ptEnd = Sketcher::SketchObject::getPoint(&arcOfEllipse, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone
        = Sketcher::SketchObject::getPoint(&arcOfEllipse, Sketcher::PointPos::none);

    // Assert
    // NOTE: parameters for arc of ellipse are CCW angles from positive x-axis
    EXPECT_DOUBLE_EQ(ptStart[0], 1.0 + cos(startParam) * majorRadius);
    EXPECT_DOUBLE_EQ(ptStart[1], 2.0 + sin(startParam) * minorRadius);
    EXPECT_DOUBLE_EQ(ptEnd[0], 1.0 + cos(endParam) * majorRadius);
    EXPECT_DOUBLE_EQ(ptEnd[1], 2.0 + sin(endParam) * minorRadius);
    EXPECT_DOUBLE_EQ(ptMid[0], 1.0);
    EXPECT_DOUBLE_EQ(ptMid[1], 2.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomArcOfHyperbola)
{
    // Arrange
    Base::Vector3d coordsCenter(1.0, 2.0, 0.0);
    double majorRadius = 4.0;
    double minorRadius = 3.0;
    double startParam = std::numbers::pi / 3, endParam = std::numbers::pi * 1.5;
    Part::GeomArcOfHyperbola arcOfHyperbola;
    arcOfHyperbola.setCenter(coordsCenter);
    arcOfHyperbola.setMajorRadius(majorRadius);
    arcOfHyperbola.setMinorRadius(minorRadius);
    arcOfHyperbola.setRange(startParam, endParam, true);

    // Act
    [[maybe_unused]] auto ptStart
        = Sketcher::SketchObject::getPoint(&arcOfHyperbola, Sketcher::PointPos::start);
    auto ptMid = Sketcher::SketchObject::getPoint(&arcOfHyperbola, Sketcher::PointPos::mid);
    [[maybe_unused]] auto ptEnd
        = Sketcher::SketchObject::getPoint(&arcOfHyperbola, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone
        = Sketcher::SketchObject::getPoint(&arcOfHyperbola, Sketcher::PointPos::none);

    // Assert
    // FIXME: Figure out how this is defined
    // EXPECT_DOUBLE_EQ(ptStart[0], 1.0);
    // EXPECT_DOUBLE_EQ(ptStart[1], 2.0);
    // EXPECT_DOUBLE_EQ(ptEnd[0], 1.0);
    // EXPECT_DOUBLE_EQ(ptEnd[1], 2.0);
    EXPECT_DOUBLE_EQ(ptMid[0], 1.0);
    EXPECT_DOUBLE_EQ(ptMid[1], 2.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomArcOfParabola)
{
    // Arrange
    Base::Vector3d coordsCenter(1.0, 2.0, 0.0);
    double focal = 3.0;
    double startParam = std::numbers::pi / 3, endParam = std::numbers::pi * 1.5;
    Part::GeomArcOfParabola arcOfParabola;
    arcOfParabola.setCenter(coordsCenter);
    arcOfParabola.setFocal(focal);
    arcOfParabola.setRange(startParam, endParam, true);

    // Act
    [[maybe_unused]] auto ptStart
        = Sketcher::SketchObject::getPoint(&arcOfParabola, Sketcher::PointPos::start);
    auto ptMid = Sketcher::SketchObject::getPoint(&arcOfParabola, Sketcher::PointPos::mid);
    [[maybe_unused]] auto ptEnd
        = Sketcher::SketchObject::getPoint(&arcOfParabola, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone
        = Sketcher::SketchObject::getPoint(&arcOfParabola, Sketcher::PointPos::none);

    // Assert
    // FIXME: Figure out how this is defined
    // EXPECT_DOUBLE_EQ(ptStart[0], 1.0);
    // EXPECT_DOUBLE_EQ(ptStart[1], 2.0);
    // EXPECT_DOUBLE_EQ(ptEnd[0], 1.0);
    // EXPECT_DOUBLE_EQ(ptEnd[1], 2.0);
    EXPECT_DOUBLE_EQ(ptMid[0], 1.0);
    EXPECT_DOUBLE_EQ(ptMid[1], 2.0);
}

TEST_F(SketchObjectTest, testGetPointFromGeomBSplineCurveNonPeriodic)
{
    // Arrange
    int degree = 3;
    std::vector<Base::Vector3d> poles;
    poles.emplace_back(1, 0, 0);
    poles.emplace_back(1, 1, 0);
    poles.emplace_back(1, 0.5, 0);
    poles.emplace_back(0, 1, 0);
    poles.emplace_back(0, 0, 0);
    std::vector<double> weights(5, 1.0);
    std::vector<double> knotsNonPeriodic = {0.0, 1.0, 2.0};
    std::vector<int> multiplicitiesNonPeriodic = {degree + 1, 1, degree + 1};
    Part::GeomBSplineCurve
        nonPeriodicBSpline(poles, weights, knotsNonPeriodic, multiplicitiesNonPeriodic, degree, false);

    // Act
    auto ptStart = Sketcher::SketchObject::getPoint(&nonPeriodicBSpline, Sketcher::PointPos::start);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptMid
        = Sketcher::SketchObject::getPoint(&nonPeriodicBSpline, Sketcher::PointPos::mid);
    auto ptEnd = Sketcher::SketchObject::getPoint(&nonPeriodicBSpline, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone
        = Sketcher::SketchObject::getPoint(&nonPeriodicBSpline, Sketcher::PointPos::none);

    // Assert
    EXPECT_DOUBLE_EQ(ptStart[0], poles.front()[0]);
    EXPECT_DOUBLE_EQ(ptStart[1], poles.front()[1]);
    EXPECT_DOUBLE_EQ(ptEnd[0], poles.back()[0]);
    EXPECT_DOUBLE_EQ(ptEnd[1], poles.back()[1]);
}

TEST_F(SketchObjectTest, testGetPointFromGeomBSplineCurvePeriodic)
{
    // Arrange
    int degree = 3;
    std::vector<Base::Vector3d> poles;
    poles.emplace_back(1, 0, 0);
    poles.emplace_back(1, 1, 0);
    poles.emplace_back(1, 0.5, 0);
    poles.emplace_back(0, 1, 0);
    poles.emplace_back(0, 0, 0);
    std::vector<double> weights(5, 1.0);
    std::vector<double> knotsPeriodic = {0.0, 0.3, 1.0, 1.5, 1.8, 2.0};
    std::vector<int> multiplicitiesPeriodic(6, 1);
    Part::GeomBSplineCurve
        periodicBSpline(poles, weights, knotsPeriodic, multiplicitiesPeriodic, degree, true);

    // Act
    // TODO: Maybe we want this to give an error instead of some default value
    auto ptStart = Sketcher::SketchObject::getPoint(&periodicBSpline, Sketcher::PointPos::start);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptMid
        = Sketcher::SketchObject::getPoint(&periodicBSpline, Sketcher::PointPos::mid);
    // TODO: Maybe we want this to give an error instead of some default value
    auto ptEnd = Sketcher::SketchObject::getPoint(&periodicBSpline, Sketcher::PointPos::end);
    // TODO: Maybe we want this to give an error instead of some default value
    [[maybe_unused]] auto ptNone
        = Sketcher::SketchObject::getPoint(&periodicBSpline, Sketcher::PointPos::none);

    // Assert
    // With non-trivial values for weights, knots, mults, etc, getting the coordinates is
    // non-trivial as well. This is the best we can do.
    EXPECT_DOUBLE_EQ(ptStart[0], ptEnd[0]);
    EXPECT_DOUBLE_EQ(ptStart[1], ptEnd[1]);
}

TEST_F(SketchObjectTest, testConstraintAfterDeletingGeo)
{
    // Arrange
    int geoId1 = 42, geoId2 = 10, geoId3 = 0, geoId4 = -8;

    Sketcher::Constraint* nullConstr = nullptr;

    Sketcher::Constraint constr1;
    constr1.Type = Sketcher::ConstraintType::Coincident;
    constr1.First = geoId1;
    constr1.FirstPos = Sketcher::PointPos::start;
    constr1.Second = geoId2;
    constr1.SecondPos = Sketcher::PointPos::end;

    Sketcher::Constraint constr2;
    constr2.Type = Sketcher::ConstraintType::Tangent;
    constr2.First = geoId4;
    constr2.FirstPos = Sketcher::PointPos::none;
    constr2.Second = geoId3;
    constr2.SecondPos = Sketcher::PointPos::none;
    constr2.Third = geoId1;
    constr2.ThirdPos = Sketcher::PointPos::start;

    // Act
    auto nullConstrAfter = getObject()->getConstraintAfterDeletingGeo(nullConstr, 5);

    // Assert
    EXPECT_EQ(nullConstrAfter, nullptr);

    // Act
    getObject()->changeConstraintAfterDeletingGeo(nullConstr, 5);

    // Assert
    EXPECT_EQ(nullConstr, nullptr);

    // Act
    // delete typical in-sketch geo
    auto constr1PtrAfter1 = getObject()->getConstraintAfterDeletingGeo(&constr1, 5);
    // delete external geo (negative id)
    auto constr1PtrAfter2 = getObject()->getConstraintAfterDeletingGeo(&constr1, -5);
    // Delete a geo involved in the constraint
    auto constr1PtrAfter3 = getObject()->getConstraintAfterDeletingGeo(&constr1, 10);

    // Assert
    EXPECT_EQ(constr1.Type, Sketcher::ConstraintType::Coincident);
    EXPECT_EQ(constr1.First, geoId1);
    EXPECT_EQ(constr1.Second, geoId2);
    EXPECT_EQ(constr1PtrAfter1->First, geoId1 - 1);
    EXPECT_EQ(constr1PtrAfter1->Second, geoId2 - 1);
    EXPECT_EQ(constr1PtrAfter2->Third, Sketcher::GeoEnum::GeoUndef);
    EXPECT_EQ(constr1PtrAfter3.get(), nullptr);

    // Act
    getObject()->changeConstraintAfterDeletingGeo(&constr2, -3);

    // Assert
    EXPECT_EQ(constr2.Type, Sketcher::ConstraintType::Tangent);
    EXPECT_EQ(constr2.First, geoId4 + 1);
    EXPECT_EQ(constr2.Second, geoId3);
    EXPECT_EQ(constr2.Third, geoId1);

    // Act
    // Delete a geo involved in the constraint
    getObject()->changeConstraintAfterDeletingGeo(&constr2, 0);

    // Assert
    EXPECT_EQ(constr2.Type, Sketcher::ConstraintType::None);
}

TEST_F(SketchObjectTest, testDeleteExposeInternalGeometryOfEllipse)
{
    // Arrange
    Part::GeomEllipse ellipse;
    setupEllipse(ellipse);
    int geoId = getObject()->addGeometry(&ellipse);

    // Act
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);

    // Act
    // "Expose" internal geometry
    getObject()->exposeInternalGeometry(geoId);

    // Assert
    // Ensure all internal geometry is satisfied
    // TODO: Also try to ensure types of geometries that have this type
    const auto constraints = getObject()->Constraints.getValues();
    for (auto alignmentType :
         {Sketcher::InternalAlignmentType::EllipseMajorDiameter,
          Sketcher::InternalAlignmentType::EllipseMinorDiameter,
          Sketcher::InternalAlignmentType::EllipseFocus1,
          Sketcher::InternalAlignmentType::EllipseFocus2}) {
        // TODO: Ensure there exists one and only one curve with this type
        int numConstraintsOfThisType = std::count_if(
            constraints.begin(),
            constraints.end(),
            [&geoId, &alignmentType](const auto* constr) {
                return constr->Type == Sketcher::ConstraintType::InternalAlignment
                    && constr->AlignmentType == alignmentType && constr->Second == geoId;
            }
        );
        EXPECT_EQ(numConstraintsOfThisType, 1);
    }

    // Act
    // Delete internal geometry (again)
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);
}

TEST_F(SketchObjectTest, testDeleteExposeInternalGeometryOfHyperbola)
{
    // Arrange
    Part::GeomArcOfHyperbola aoh;
    setupArcOfHyperbola(aoh);
    int geoId = getObject()->addGeometry(&aoh);

    // Act
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);

    // Act
    // "Expose" internal geometry
    getObject()->exposeInternalGeometry(geoId);

    // Assert
    // Ensure all internal geometry is satisfied
    // TODO: Also try to ensure types of geometries that have this type
    const auto constraints = getObject()->Constraints.getValues();
    for (auto alignmentType :
         {Sketcher::InternalAlignmentType::HyperbolaMajor,
          Sketcher::InternalAlignmentType::HyperbolaMinor,
          Sketcher::InternalAlignmentType::HyperbolaFocus}) {
        // TODO: Ensure there exists one and only one curve with this type
        int numConstraintsOfThisType = std::count_if(
            constraints.begin(),
            constraints.end(),
            [&geoId, &alignmentType](const auto* constr) {
                return constr->Type == Sketcher::ConstraintType::InternalAlignment
                    && constr->AlignmentType == alignmentType && constr->Second == geoId;
            }
        );
        EXPECT_EQ(numConstraintsOfThisType, 1);
    }

    // Act
    // Delete internal geometry (again)
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);
}

TEST_F(SketchObjectTest, testDeleteExposeInternalGeometryOfParabola)
{
    // Arrange
    Part::GeomArcOfParabola aoh;
    setupArcOfParabola(aoh);
    int geoId = getObject()->addGeometry(&aoh);

    // Act
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);

    // Act
    // "Expose" internal geometry
    getObject()->exposeInternalGeometry(geoId);

    // Assert
    // Ensure all internal geometry is satisfied
    // TODO: Also try to ensure types of geometries that have this type
    const auto constraints = getObject()->Constraints.getValues();
    for (auto alignmentType :
         {Sketcher::InternalAlignmentType::ParabolaFocalAxis,
          Sketcher::InternalAlignmentType::ParabolaFocus}) {
        // TODO: Ensure there exists one and only one curve with this type
        int numConstraintsOfThisType = std::count_if(
            constraints.begin(),
            constraints.end(),
            [&geoId, &alignmentType](const auto* constr) {
                return constr->Type == Sketcher::ConstraintType::InternalAlignment
                    && constr->AlignmentType == alignmentType && constr->Second == geoId;
            }
        );
        EXPECT_EQ(numConstraintsOfThisType, 1);
    }

    // Act
    // Delete internal geometry (again)
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);
}

TEST_F(SketchObjectTest, testDeleteExposeInternalGeometryOfBSpline)
{
    // NOTE: We test only non-periodic B-spline here. Periodic B-spline should behave exactly the
    // same.

    // Arrange
    auto nonPeriodicBSpline = createTypicalNonPeriodicBSpline();
    int geoId = getObject()->addGeometry(nonPeriodicBSpline.get());

    // Act
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);

    // Act
    // "Expose" internal geometry
    getObject()->exposeInternalGeometry(geoId);

    // Assert
    // Ensure all internal geometry is satisfied
    // TODO: Also try to ensure types of geometries that have this type
    const auto constraints = getObject()->Constraints.getValues();
    std::map<Sketcher::InternalAlignmentType, int> numConstraintsOfThisType;
    for (auto alignmentType :
         {Sketcher::InternalAlignmentType::BSplineControlPoint,
          Sketcher::InternalAlignmentType::BSplineKnotPoint}) {
        // TODO: Ensure there exists one and only one curve with this type
        numConstraintsOfThisType[alignmentType] = std::count_if(
            constraints.begin(),
            constraints.end(),
            [&geoId, &alignmentType](const auto* constr) {
                return constr->Type == Sketcher::ConstraintType::InternalAlignment
                    && constr->AlignmentType == alignmentType && constr->Second == geoId;
            }
        );
    }
    EXPECT_EQ(
        numConstraintsOfThisType[Sketcher::InternalAlignmentType::BSplineControlPoint],
        nonPeriodicBSpline->countPoles()
    );
    EXPECT_EQ(
        numConstraintsOfThisType[Sketcher::InternalAlignmentType::BSplineKnotPoint],
        nonPeriodicBSpline->countKnots()
    );

    // Act
    // Delete internal geometry (again)
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoId);

    // Assert
    // Ensure there's only one curve
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 0);
}

// TODO: Needs to be done for other curves too but currently they are working as intended
TEST_F(SketchObjectTest, testDeleteOnlyUnusedInternalGeometryOfBSpline)
{
    // NOTE: We test only non-periodic B-spline here. Periodic B-spline should behave exactly the
    // same.

    // Arrange
    auto nonPeriodicBSpline = createTypicalNonPeriodicBSpline();
    int geoIdBsp = getObject()->addGeometry(nonPeriodicBSpline.get());
    // Ensure "exposed" internal geometry
    getObject()->exposeInternalGeometry(geoIdBsp);
    Base::Vector3d coords(1.0, 1.0, 0.0);
    Part::GeomPoint point(coords);
    int geoIdPnt = getObject()->addGeometry(&point);
    const auto constraints = getObject()->Constraints.getValues();
    auto it = std::find_if(constraints.begin(), constraints.end(), [&geoIdBsp](const auto* constr) {
        return constr->Type == Sketcher::ConstraintType::InternalAlignment
            && constr->AlignmentType == Sketcher::InternalAlignmentType::BSplineControlPoint
            && constr->Second == geoIdBsp && constr->InternalAlignmentIndex == 1;
    });
    // One Assert to avoid
    EXPECT_NE(it, constraints.end());
    auto constraint = new Sketcher::Constraint();  // Ownership will be transferred to the sketch
    constraint->Type = Sketcher::ConstraintType::Coincident;
    constraint->First = geoIdPnt;
    constraint->FirstPos = Sketcher::PointPos::start;
    constraint->Second = (*it)->First;
    constraint->SecondPos = Sketcher::PointPos::mid;
    getObject()->addConstraint(constraint);

    // Act
    getObject()->deleteUnusedInternalGeometryAndUpdateGeoId(geoIdBsp);

    // Assert
    // Ensure there are 3 curves: the B-spline, its pole, and the point coincident on the pole
    EXPECT_EQ(getObject()->getHighestCurveIndex(), 2);
}

auto setupAngleConstraint(Sketcher::SketchObject* obj, const std::string& expr)
{
    auto constraint = std::make_unique<Sketcher::Constraint>();
    constraint->Type = Sketcher::ConstraintType::Angle;
    auto id = obj->addConstraint(constraint.get());
    obj->setExpression(obj->Constraints.createPath(id), App::Expression::parse(obj, expr));
    return std::tuple {std::move(constraint), id};
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionNoUnits1)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "180 - 60");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("60"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionNoUnits2)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "60");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("180 - 60"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionWithUnits1)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "180 ° - 60 °");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("60 °"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionWithUnits2)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "60 °");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("180 ° - 60 °"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionWithUnits3)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "60 deg");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("180 ° - 60 deg"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionWithUnits4)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "1rad");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("180 ° - 1 rad"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionApplyAndReverse1)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "180");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("180"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionApplyAndReverse2)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "(30 + 15) * 2 / 3");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    auto supExpr = getObject()->getConstraintExpression(id);
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("180 - (30 + 15) * 2 / 3"), supExpr);
    EXPECT_EQ(std::string("(30 + 15) * 2 / 3"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionSimple)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "180 - (60)");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("60"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testReverseAngleConstraintToSupplementaryExpressionApplyAndReverse)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "32 °");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("32 °"), getObject()->getConstraintExpression(id));
}

// Pending upstream 8b06bca68a: supplementary angle expression built as an AST, keeping the unit.
TEST_F(SketchObjectTest, DISABLED_testReverseAngleConstraintToSupplementaryExpressionFunction)
{
    auto [constraint, id] = setupAngleConstraint(getObject(), "atan(0.03)");
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    auto supExpr = getObject()->getConstraintExpression(id);
    getObject()->reverseAngleConstraintToSupplementary(constraint.get(), id);
    EXPECT_EQ(std::string("180 ° - atan(0.03)"), supExpr);
    EXPECT_EQ(std::string("atan(0.03)"), getObject()->getConstraintExpression(id));
}

TEST_F(SketchObjectTest, testGetElementName)
{
    // Arrange
    Base::Vector3d p1(0.0, 0.0, 0.0), p2(1.0, 0.0, 0.0);
    std::unique_ptr<Part::Geometry> geoline(new Part::GeomLineSegment());
    static_cast<Part::GeomLineSegment*>(geoline.get())->setPoints(p1, p2);
    auto id = getObject()->addGeometry(geoline.get());
    long tag;
    getObject()->getGeometryId(id, tag);  // We need to look up the tag that got assigned
    std::ostringstream oss;
    oss << "g" << tag;
    auto tagName = oss.str();
    getObject()->recomputeFeature();  // or ->execute()
    // Act
    // unless it's Export, we are really just testing the superclass App::GeoFeature::getElementName
    // call.
    auto forward_normal_name = getObject()->getElementName(
        (tagName + ";SKT").c_str(),
        App::GeoFeature::ElementNameType::Normal
    );
    auto reverse_normal_name
        = getObject()->getElementName("Vertex2", App::GeoFeature::ElementNameType::Normal);
    auto reverse_export_name
        = getObject()->getElementName("Vertex1", App::GeoFeature::ElementNameType::Export);
    auto map = getObject()->Shape.getShape().getElementMap();
    ASSERT_EQ(map.size(), 3);
    EXPECT_STREQ(map[0].name.toString().c_str(), (tagName + ";SKT").c_str());
    EXPECT_EQ(map[0].index.toString(), "Edge1");
    EXPECT_STREQ(map[1].name.toString().c_str(), (tagName + "v1;SKT").c_str());
    EXPECT_EQ(map[1].index.toString(), "Vertex1");
    EXPECT_STREQ(map[2].name.toString().c_str(), (tagName + "v2;SKT").c_str());
    EXPECT_EQ(map[2].index.toString(), "Vertex2");
    // Assert
    EXPECT_STREQ(forward_normal_name.first.c_str(), (";" + tagName + ";SKT.Edge1").c_str());
    EXPECT_STREQ(forward_normal_name.second.c_str(), "Edge1");
    EXPECT_STREQ(reverse_normal_name.first.c_str(), (";" + tagName + "v2;SKT.Vertex2").c_str());
    EXPECT_STREQ(reverse_normal_name.second.c_str(), "Vertex2");
    EXPECT_STREQ(reverse_export_name.first.c_str(), (";" + tagName + "v1;SKT.Vertex1").c_str());
    EXPECT_STREQ(reverse_export_name.second.c_str(), "Vertex1");
}

// A Group constraint binds a set of geometries to a construction line handle, the first
// element of the constraint. Text constraints are groups too.
TEST_F(SketchObjectTest, testGroupQueries)
{
    // Arrange
    auto addLine = [this](Base::Vector3d p1, Base::Vector3d p2, bool construction) {
        Part::GeomLineSegment line;
        line.setPoints(p1, p2);
        return getObject()->addGeometry(&line, construction);
    };
    int handle = addLine(Base::Vector3d(0.0, 0.0, 0.0), Base::Vector3d(0.0, 1.0, 0.0), true);
    int member1 = addLine(Base::Vector3d(0.0, 0.0, 0.0), Base::Vector3d(1.0, 0.0, 0.0), false);
    int member2 = addLine(Base::Vector3d(1.0, 0.0, 0.0), Base::Vector3d(1.0, 1.0, 0.0), false);
    int loner = addLine(Base::Vector3d(5.0, 5.0, 0.0), Base::Vector3d(6.0, 5.0, 0.0), false);

    auto constraint = std::make_unique<Sketcher::Constraint>();
    constraint->Type = Sketcher::Group;
    constraint->setElement(0, Sketcher::GeoElementId(handle));
    constraint->setElement(1, Sketcher::GeoElementId(member1));
    constraint->setElement(2, Sketcher::GeoElementId(member2));
    getObject()->addConstraint(std::move(constraint));

    // Act & Assert
    EXPECT_TRUE(getObject()->isInGroup(handle));
    EXPECT_FALSE(getObject()->isInGroup(handle, /*includeHandle*/ false));
    EXPECT_TRUE(getObject()->isInGroup(member1));
    EXPECT_TRUE(getObject()->isInGroup(member2, /*includeHandle*/ false));
    EXPECT_FALSE(getObject()->isInGroup(loner));

    EXPECT_TRUE(getObject()->isGroupHandle(handle));
    EXPECT_FALSE(getObject()->isGroupHandle(member1));
    EXPECT_FALSE(getObject()->isGroupHandle(loner));

    EXPECT_EQ(getObject()->getGroupHandleIfInGroup(member1), handle);
    EXPECT_EQ(getObject()->getGroupHandleIfInGroup(member2), handle);
    EXPECT_EQ(getObject()->getGroupHandleIfInGroup(handle), handle);
    EXPECT_EQ(getObject()->getGroupHandleIfInGroup(loner), loner);

    std::set<int> expected {member1, member2};
    EXPECT_EQ(getObject()->getGroupGeometries(handle), expected);
    EXPECT_TRUE(getObject()->getGroupGeometries(loner).empty());
}

// Several geometries move together in one solver pass.
TEST_F(SketchObjectTest, testMoveGeometries)
{
    // Arrange
    Part::GeomLineSegment line;
    line.setPoints(Base::Vector3d(0.0, 0.0, 0.0), Base::Vector3d(10.0, 0.0, 0.0));
    int geoId = getObject()->addGeometry(&line, false);
    getObject()->solve();

    // Act
    std::vector<Sketcher::GeoElementId> moved {
        Sketcher::GeoElementId(geoId, Sketcher::PointPos::start),
        Sketcher::GeoElementId(geoId, Sketcher::PointPos::end)
    };
    auto status = getObject()->moveGeometries(moved, Base::Vector3d(3.0, 4.0, 0.0), true);

    // Assert
    EXPECT_EQ(status, Sketcher::SketchSolveStatus::Success);
    EXPECT_EQ(getObject()->getPoint(geoId, Sketcher::PointPos::start),
              Base::Vector3d(3.0, 4.0, 0.0));
    EXPECT_EQ(getObject()->getPoint(geoId, Sketcher::PointPos::end),
              Base::Vector3d(13.0, 4.0, 0.0));
}

// The solver ignores constraints whose geometry is inside a group, so the UI shows them
// as inactive even when the user has not deactivated them.
TEST_F(SketchObjectTest, testConstraintActiveInSketch)
{
    // Arrange
    auto addLine = [this](Base::Vector3d p1, Base::Vector3d p2, bool construction) {
        Part::GeomLineSegment line;
        line.setPoints(p1, p2);
        return getObject()->addGeometry(&line, construction);
    };
    int handle = addLine(Base::Vector3d(0.0, 0.0, 0.0), Base::Vector3d(0.0, 1.0, 0.0), true);
    int member = addLine(Base::Vector3d(0.0, 0.0, 0.0), Base::Vector3d(1.0, 0.0, 0.0), false);
    int member2 = addLine(Base::Vector3d(1.0, 0.0, 0.0), Base::Vector3d(1.0, 1.0, 0.0), false);
    int loner = addLine(Base::Vector3d(5.0, 5.0, 0.0), Base::Vector3d(6.0, 5.0, 0.0), false);

    auto onMember = std::make_unique<Sketcher::Constraint>();
    onMember->Type = Sketcher::Horizontal;
    onMember->First = member;
    auto onLoner = std::make_unique<Sketcher::Constraint>();
    onLoner->Type = Sketcher::Horizontal;
    onLoner->First = loner;
    auto deactivated = std::make_unique<Sketcher::Constraint>();
    deactivated->Type = Sketcher::Horizontal;
    deactivated->First = loner;
    deactivated->isActive = false;

    auto group = std::make_unique<Sketcher::Constraint>();
    group->Type = Sketcher::Group;
    group->setElement(0, Sketcher::GeoElementId(handle));
    group->setElement(1, Sketcher::GeoElementId(member));
    group->setElement(2, Sketcher::GeoElementId(member2));

    const Sketcher::Constraint* onMemberPtr = onMember.get();
    const Sketcher::Constraint* onLonerPtr = onLoner.get();
    const Sketcher::Constraint* deactivatedPtr = deactivated.get();
    const Sketcher::Constraint* groupPtr = group.get();

    getObject()->addConstraint(std::move(onMember));
    getObject()->addConstraint(std::move(onLoner));
    getObject()->addConstraint(std::move(deactivated));

    // Act & Assert: before the group, every active constraint counts
    EXPECT_TRUE(getObject()->isConstraintActiveInSketch(onMemberPtr));
    EXPECT_TRUE(getObject()->isConstraintActiveInSketch(onLonerPtr));
    EXPECT_FALSE(getObject()->isConstraintActiveInSketch(deactivatedPtr));

    getObject()->addConstraint(std::move(group));

    EXPECT_FALSE(getObject()->isConstraintActiveInSketch(onMemberPtr));
    EXPECT_TRUE(getObject()->isConstraintActiveInSketch(onLonerPtr));
    EXPECT_TRUE(getObject()->isConstraintActiveInSketch(groupPtr));
    EXPECT_FALSE(getObject()->isConstraintActiveInSketch(nullptr));
}

// A solve that leaves the geometry exactly where it was still has to publish its
// diagnosis. SketchObject only copies the solved elements back when they differ, and
// the diagnosis rides on those elements, so it used to be dropped in that case --
// leaving whatever the previous solve had concluded on the geometry that
// ViewProviderSketch colours and that getGeometryWithDependentParameters() reads.
TEST_F(SketchObjectTest, testSolverDiagnosisSurvivesAnUnchangedSolve)
{
    // Arrange: a line nothing constrains. The solver has nothing to move, so the
    // copy-back is skipped and only the hand-over under test can deliver a diagnosis.
    Base::Vector3d start(0.0, 0.0, 0.0), end(1.0, 0.0, 0.0);
    std::unique_ptr<Part::GeomLineSegment> line(new Part::GeomLineSegment());
    line->setPoints(start, end);
    int geoId = getObject()->addGeometry(line.get());

    // Act
    getObject()->solve();

    // Assert: four free parameters, and the element says so.
    EXPECT_EQ(getObject()->getLastDoF(), 4);

    std::vector<std::pair<int, Sketcher::PointPos>> dependent;
    getObject()->getGeometryWithDependentParameters(dependent);

    EXPECT_FALSE(dependent.empty());
    EXPECT_TRUE(std::any_of(dependent.begin(), dependent.end(), [geoId](const auto& element) {
        return element.first == geoId;
    }));

    // Act: pin it down where it already is, so again nothing moves -- but now the
    // diagnosis is the opposite one and must replace what is on the element.
    auto* toOrigin = new Sketcher::Constraint();
    toOrigin->Type = Sketcher::ConstraintType::Coincident;
    toOrigin->First = geoId;
    toOrigin->FirstPos = Sketcher::PointPos::start;
    toOrigin->Second = Sketcher::GeoEnum::RtPnt;
    toOrigin->SecondPos = Sketcher::PointPos::start;
    getObject()->addConstraint(toOrigin);

    auto* alongX = new Sketcher::Constraint();
    alongX->Type = Sketcher::ConstraintType::DistanceX;
    alongX->First = geoId;
    alongX->FirstPos = Sketcher::PointPos::end;
    alongX->setValue(end.x);
    getObject()->addConstraint(alongX);

    auto* alongY = new Sketcher::Constraint();
    alongY->Type = Sketcher::ConstraintType::DistanceY;
    alongY->First = geoId;
    alongY->FirstPos = Sketcher::PointPos::end;
    alongY->setValue(end.y);
    getObject()->addConstraint(alongY);

    getObject()->solve();

    // Assert: fully constrained, and no element left claiming a free parameter.
    EXPECT_EQ(getObject()->getLastDoF(), 0);

    dependent.clear();
    getObject()->getGeometryWithDependentParameters(dependent);

    EXPECT_TRUE(dependent.empty());
}

TEST_F(SketchObjectTest, groupQueriesFollowConstraintChanges)  // NOLINT
{
    // Arrange: a handle line and two members in a group, a third line outside
    // it, and a Horizontal on a member and on the outsider.
    std::vector<int> ids;
    for (int i = 0; i < 4; ++i) {
        Part::GeomLineSegment line;
        line.setPoints(Base::Vector3d(0, i, 0), Base::Vector3d(10, i, 0));
        ids.push_back(getObject()->addGeometry(&line));
    }
    auto* group = new Sketcher::Constraint();
    group->Type = Sketcher::ConstraintType::Group;
    for (int i = 0; i < 3; ++i) {
        // as Sketcher.Constraint('Group', ...) does: a fresh constraint
        // already holds element slots, which addElement() would append after
        group->setElement(i, Sketcher::GeoElementId(ids[i]));
    }
    int groupId = getObject()->addConstraint(group);
    auto* onMember = new Sketcher::Constraint();
    onMember->Type = Sketcher::ConstraintType::Horizontal;
    onMember->First = ids[1];
    getObject()->addConstraint(onMember);
    auto* onOutsider = new Sketcher::Constraint();
    onOutsider->Type = Sketcher::ConstraintType::Horizontal;
    onOutsider->First = ids[3];
    getObject()->addConstraint(onOutsider);

    // Assert: the handle is in the group only when asked to count it.
    EXPECT_TRUE(getObject()->isGroupHandle(ids[0]));
    EXPECT_TRUE(getObject()->isInGroup(ids[0], true));
    EXPECT_FALSE(getObject()->isInGroup(ids[0], false));
    EXPECT_TRUE(getObject()->isInGroup(ids[1], false));
    EXPECT_FALSE(getObject()->isGroupHandle(ids[1]));
    EXPECT_FALSE(getObject()->isInGroup(ids[3], true));
    const auto& constraints = getObject()->Constraints.getValues();
    EXPECT_FALSE(getObject()->isConstraintActiveInSketch(constraints[1]));
    EXPECT_TRUE(getObject()->isConstraintActiveInSketch(constraints[2]));

    // Act: the group goes. The answers were cached on the first question and
    // must follow the change of Constraints.
    getObject()->delConstraint(groupId);

    // Assert
    EXPECT_FALSE(getObject()->isGroupHandle(ids[0]));
    EXPECT_FALSE(getObject()->isInGroup(ids[1], true));
    for (const auto* constr : getObject()->Constraints.getValues()) {
        EXPECT_TRUE(getObject()->isConstraintActiveInSketch(constr));
    }
}

// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <limits>
#include <mutex>
#endif

#include <Base/Exception.h>
#include <Base/Matrix.h>
#include <Base/Rotation.h>
#include <Base/Tools.h>

#include "Datums.h"
#include "DocumentObject.h"
#include "Pattern.h"
#include "PropertyLinks.h"
#include "PropertyGeo.h"
#include "PropertyStandard.h"
#include "PropertyUnits.h"

using namespace App;

// As OCCT's Precision::Confusion() and Precision::Angular(), which App cannot use
static constexpr double Confusion = 1e-7;
static constexpr double Angular = 1e-12;

const char* Pattern::TypeEnums[] = {"Linear", "Polar", "Circular", "Path", "Point", nullptr};

namespace
{

const char* ModeEnums[] = {"Extent", "Spacing", nullptr};
const char* SpacingModeEnums[] = {"Fixed count", "Fixed spacing", "Fixed count and spacing", nullptr};

const PropertyIntegerConstraint::Constraints OccurrencesRange = {1, INT_MAX, 1};
const PropertyIntegerConstraint::Constraints NumberCirclesRange = {2, INT_MAX, 1};
const PropertyIntegerConstraint::Constraints SymmetryRange = {1, INT_MAX, 1};
const PropertyQuantityConstraint::Constraints AngleRange = {-360.0, 360.0, 1.0};

enum class Mode
{
    Extent,
    Spacing,
};

enum class SpacingMode
{
    FixedCount,
    FixedSpacing,
    FixedCountAndSpacing,
};

// ----------------------------------------------------------------------------
// The property tables

const std::vector<Pattern::PropertySpec> LinearSpecs = {
    {"Direction", "App::PropertyLinkSub", "Direction 1",
     "The first direction of the pattern: a straight edge, a datum line, a sketch axis,\n"
     "or the normal of a planar face"},
    {"Reversed", "App::PropertyBool", "Direction 1", "Reverse the first direction"},
    {"Mode", "App::PropertyEnumeration", "Direction 1",
     "How the first direction is dimensioned.\n"
     "'Extent': Length from the first to the last occurrence.\n"
     "'Spacing': Offset between consecutive occurrences."},
    {"Length", "App::PropertyLength", "Direction 1",
     "Distance from the first to the last occurrence, in 'Extent' mode"},
    {"Offset", "App::PropertyLength", "Direction 1",
     "Distance between consecutive occurrences, in 'Spacing' mode"},
    {"Occurrences", "App::PropertyIntegerConstraint", "Direction 1",
     "Number of occurrences in the first direction, the original included"},
    {"Spacings", "App::PropertyFloatList", "Direction 1",
     "Individual gaps in 'Spacing' mode, one per gap: item i is the gap before\n"
     "occurrence i + 2. -1 uses SpacingPattern, or Offset."},
    {"SpacingPattern", "App::PropertyFloatList", "Direction 1",
     "Gaps repeated along the first direction in 'Spacing' mode, e.g. [10, 20]\n"
     "alternates 10 and 20. Used when it has more than one value."},
    {"Direction2", "App::PropertyLinkSub", "Direction 2",
     "The second direction of the pattern, used when Occurrences2 is more than one"},
    {"Reversed2", "App::PropertyBool", "Direction 2", "Reverse the second direction"},
    {"Mode2", "App::PropertyEnumeration", "Direction 2",
     "How the second direction is dimensioned.\n"
     "'Extent': Length2 from the first to the last occurrence.\n"
     "'Spacing': Offset2 between consecutive occurrences."},
    {"Length2", "App::PropertyLength", "Direction 2",
     "Distance from the first to the last occurrence, in 'Extent' mode"},
    {"Offset2", "App::PropertyLength", "Direction 2",
     "Distance between consecutive occurrences, in 'Spacing' mode"},
    {"Occurrences2", "App::PropertyIntegerConstraint", "Direction 2",
     "Number of occurrences in the second direction, the original included.\n"
     "One leaves the second direction off."},
    {"Spacings2", "App::PropertyFloatList", "Direction 2",
     "Individual gaps of the second direction, as Spacings"},
    {"SpacingPattern2", "App::PropertyFloatList", "Direction 2",
     "Gaps repeated along the second direction, as SpacingPattern"},
    {"SuppressedPositions", "App::PropertyIntPairList", "Pattern",
     "Suppressed occurrences as zero-based (direction 1, direction 2) indices.\n"
     "Positions outside the current pattern are kept."},
};

const std::vector<Pattern::PropertySpec> PolarSpecs = {
    {"Axis", "App::PropertyLinkSub", "PolarPattern",
     "The axis of rotation: a datum axis, a sketch axis, a straight or circular edge"},
    {"Reversed", "App::PropertyBool", "PolarPattern",
     "Turn clockwise instead of counter-clockwise"},
    {"Mode", "App::PropertyEnumeration", "PolarPattern",
     "How the pattern is dimensioned.\n"
     "'Extent': Angle holding all occurrences.\n"
     "'Spacing': Offset between consecutive occurrences."},
    {"Angle", "App::PropertyAngle", "PolarPattern",
     "Angle from the first to the last occurrence, in 'Extent' mode. A full turn\n"
     "spaces the occurrences evenly around it."},
    {"Offset", "App::PropertyAngle", "PolarPattern",
     "Angle between consecutive occurrences, in 'Spacing' mode"},
    {"Occurrences", "App::PropertyIntegerConstraint", "PolarPattern",
     "Number of occurrences, the original included"},
    {"Spacings", "App::PropertyFloatList", "PolarPattern",
     "Individual angles in 'Spacing' mode, one per gap: item i is the angle before\n"
     "occurrence i + 2. -1 uses SpacingPattern, or Offset."},
    {"SpacingPattern", "App::PropertyFloatList", "PolarPattern",
     "Angles repeated around the axis in 'Spacing' mode, e.g. [10, 20] alternates\n"
     "10 and 20 degrees. Used when it has more than one value."},
};

const std::vector<Pattern::PropertySpec> CircularSpecs = {
    {"Axis", "App::PropertyLinkSub", "CircularPattern",
     "The axis and center of the concentric circles"},
    {"RadialDistance", "App::PropertyLength", "CircularPattern",
     "Distance between consecutive circles"},
    {"TangentialDistance", "App::PropertyLength", "CircularPattern",
     "Approximate distance between consecutive occurrences on a circle"},
    {"NumberCircles", "App::PropertyIntegerConstraint", "CircularPattern",
     "Number of concentric circles, the original's included"},
    {"Symmetry", "App::PropertyIntegerConstraint", "CircularPattern",
     "The occurrences of each circle are rounded down to a multiple of this"},
};

const std::vector<Pattern::PropertySpec> PathSpecs = {
    {"Path", "App::PropertyLinkSub", "PathPattern",
     "The path: edges of a shape, or its first wire when no edge is given"},
    {"Count", "App::PropertyIntegerConstraint", "PathPattern", "Number of occurrences"},
    {"SpacingMode", "App::PropertyEnumeration", "PathPattern",
     "Whether the occurrences follow Count, Spacing, or both"},
    {"Spacing", "App::PropertyLength", "PathPattern",
     "Distance along the path between consecutive occurrences"},
    {"StartOffset", "App::PropertyLength", "PathPattern",
     "Length left free at the start of the path"},
    {"EndOffset", "App::PropertyLength", "PathPattern", "Length left free at the end of the path"},
    {"ReversePath", "App::PropertyBool", "PathPattern", "Walk the path from its end"},
    {"Align", "App::PropertyBool", "PathPattern",
     "Turn each occurrence to have its X axis along the path"},
    {"VerticalVector", "App::PropertyVector", "PathPattern",
     "The direction the Z axis of an aligned occurrence keeps to"},
};

const std::vector<Pattern::PropertySpec> PointSpecs = {
    {"PointObject", "App::PropertyLinkSub", "PointPattern",
     "An object whose vertices, or the ones given, place the occurrences"},
};

// ----------------------------------------------------------------------------
// Reading the inputs

template<class T>
T* getProp(const PropertyContainer& obj, const char* name)
{
    auto prop = dynamic_cast<T*>(Pattern::getProperty(obj, name));
    if (!prop) {
        throw Base::RuntimeError(std::string("Pattern property missing: ") + name);
    }
    return prop;
}

template<class T>
T* findProp(const PropertyContainer& obj, const char* name)
{
    return dynamic_cast<T*>(Pattern::getProperty(obj, name));
}

std::string withSuffix(const char* name, const char* suffix)
{
    return std::string(name) + suffix;
}

DocumentObject* getLink(const PropertyContainer& obj, const char* name, std::vector<std::string>& subs)
{
    auto prop = Pattern::getProperty(obj, name);
    if (auto link = dynamic_cast<PropertyLinkSub*>(prop)) {
        subs = link->getSubValues();
        return link->getValue();
    }
    if (auto link = dynamic_cast<PropertyXLink*>(prop)) {
        subs = link->getSubValues();
        return link->getValue();
    }
    if (auto link = dynamic_cast<PropertyLink*>(prop)) {
        subs.clear();
        return link->getValue();
    }
    throw Base::RuntimeError(std::string("Pattern property missing: ") + name);
}

// ----------------------------------------------------------------------------
// App's own datums

/// The datum or coordinate system \a sub of \a obj points to, and its transformation
DocumentObject* getDatum(DocumentObject* obj, const std::string& sub, Base::Matrix4D& mat)
{
    DocumentObject* datum = nullptr;
    try {
        datum = obj->getSubObject(sub.c_str(), nullptr, &mat, true);
    }
    catch (const Base::Exception&) {
        return nullptr;
    }
    if (datum && (datum->isDerivedFrom<DatumElement>()
                  || datum->isDerivedFrom<LocalCoordinateSystem>())) {
        return datum;
    }
    return nullptr;
}

Base::Vector3d transformDirection(const Base::Matrix4D& mat, const Base::Vector3d& dir)
{
    Base::Vector3d res = mat * dir - mat * Base::Vector3d();
    if (res.Length() < Confusion) {
        throw Base::ValueError("Direction reference has no direction");
    }
    return res.Normalize();
}

class DatumResolver: public Pattern::Resolver
{
public:
    bool getDirection(DocumentObject* obj, const std::string& sub, Base::Vector3d& dir) const override
    {
        Base::Matrix4D mat;
        auto datum = getDatum(obj, sub, mat);
        if (!datum) {
            return false;
        }
        if (datum->isDerivedFrom<App::Point>()) {
            throw Base::TypeError("A datum point gives no direction");
        }
        if (auto element = freecad_cast<DatumElement*>(datum)) {
            // A line along it, a plane along its normal
            dir = transformDirection(mat, element->getBaseDirection());
        }
        else {
            // A coordinate system along its Z axis
            dir = transformDirection(mat, Base::Vector3d(0, 0, 1));
        }
        return true;
    }

    bool getAxis(DocumentObject* obj, const std::string& sub, Pattern::Axis& axis) const override
    {
        Base::Matrix4D mat;
        auto datum = getDatum(obj, sub, mat);
        if (!datum) {
            return false;
        }
        if (datum->isDerivedFrom<App::Point>() || datum->isDerivedFrom<App::Plane>()) {
            throw Base::TypeError("Axis reference must be a line");
        }
        auto element = freecad_cast<DatumElement*>(datum);
        axis.base = mat * Base::Vector3d();
        axis.direction =
            transformDirection(mat, element ? element->getBaseDirection() : Base::Vector3d(0, 0, 1));
        return true;
    }

    bool getPoints(DocumentObject* obj,
                   const std::vector<std::string>& subs,
                   std::vector<Base::Vector3d>& points) const override
    {
        std::vector<std::pair<DocumentObject*, Base::Matrix4D>> datums;
        for (const auto& sub : subs.empty() ? std::vector<std::string>(1) : subs) {
            Base::Matrix4D mat;
            auto datum = getDatum(obj, sub, mat);
            if (!datum || (!datum->isDerivedFrom<App::Point>()
                           && !datum->isDerivedFrom<LocalCoordinateSystem>())) {
                return false;
            }
            datums.emplace_back(datum, mat);
        }
        for (auto& [datum, mat] : datums) {
            points.push_back(mat * Base::Vector3d());
        }
        return true;
    }
};

std::mutex ResolverMutex;

std::vector<std::shared_ptr<Pattern::Resolver>>& getResolvers()
{
    static std::vector<std::shared_ptr<Pattern::Resolver>> resolvers {
        std::make_shared<DatumResolver>()};
    return resolvers;
}

/// App's datums first, then the resolvers added, the last one first
std::vector<std::shared_ptr<Pattern::Resolver>> resolverList()
{
    std::lock_guard<std::mutex> lock(ResolverMutex);
    auto& resolvers = getResolvers();
    std::vector<std::shared_ptr<Pattern::Resolver>> res;
    res.push_back(resolvers.front());
    res.insert(res.end(), resolvers.rbegin(), resolvers.rend() - 1);
    return res;
}

const std::string& firstSub(const std::vector<std::string>& subs)
{
    static const std::string empty;
    return subs.empty() ? empty : subs.front();
}

// ----------------------------------------------------------------------------
// The patterns

Base::Vector3d toLocal(const Pattern::Context& context, const Base::Vector3d& dir)
{
    Base::Vector3d res;
    context.placement.getRotation().inverse().multVec(dir, res);
    return res;
}

Base::Placement rotationAbout(const Base::Vector3d& base, const Base::Vector3d& dir, double angle)
{
    return {Base::Vector3d(), Base::Rotation(dir, angle), base};
}

std::vector<Base::Vector3d> linearSteps(const PropertyContainer& obj,
                                        const Pattern::Context& context,
                                        bool second)
{
    const char* suffix = second ? "2" : "";
    auto occurrencesProp =
        findProp<PropertyInteger>(obj, withSuffix("Occurrences", suffix).c_str());
    if (!occurrencesProp && second) {
        return {Base::Vector3d()};
    }
    if (!occurrencesProp) {
        throw Base::RuntimeError("Pattern property missing: Occurrences");
    }
    long occurrences = occurrencesProp->getValue();
    std::vector<Base::Vector3d> steps {Base::Vector3d()};
    if (occurrences <= 1) {
        return steps;
    }

    bool extent = getProp<PropertyEnumeration>(obj, withSuffix("Mode", suffix).c_str())->getValue()
        == static_cast<long>(Mode::Extent);
    double length = getProp<PropertyFloat>(obj, withSuffix("Length", suffix).c_str())->getValue();
    if (extent && length < Confusion) {
        throw Base::ValueError("Pattern length too small");
    }

    Base::Vector3d unit;
    std::vector<std::string> subs;
    auto linked = getLink(obj, withSuffix("Direction", suffix).c_str(), subs);
    if (linked) {
        unit = toLocal(context, Pattern::resolveDirection(linked, subs));
    }
    else if (context.defaultReferences) {
        unit = second ? Base::Vector3d(0, 1, 0) : Base::Vector3d(1, 0, 0);
    }
    else {
        throw Base::ValueError(second ? "No reference for the second direction"
                                      : "No direction reference specified");
    }
    if (getProp<PropertyBool>(obj, withSuffix("Reversed", suffix).c_str())->getValue()) {
        unit = -unit;
    }

    const auto& spacings =
        getProp<PropertyFloatList>(obj, withSuffix("Spacings", suffix).c_str())->getValues();
    const auto& pattern =
        getProp<PropertyFloatList>(obj, withSuffix("SpacingPattern", suffix).c_str())->getValues();
    double offset = getProp<PropertyFloat>(obj, withSuffix("Offset", suffix).c_str())->getValue();

    steps.reserve(occurrences);
    double distance = 0.0;
    for (long i = 1; i < occurrences; ++i) {
        if (extent) {
            distance = length * i / (occurrences - 1);
        }
        else {
            distance += Pattern::getSpacing(spacings, pattern, offset, static_cast<int>(i - 1));
        }
        steps.push_back(unit * distance);
    }
    return steps;
}

std::vector<Base::Placement> linearPlacements(const PropertyContainer& obj,
                                              const Pattern::Context& context)
{
    long occurrences = getProp<PropertyInteger>(obj, "Occurrences")->getValue();
    auto occurrences2Prop = findProp<PropertyInteger>(obj, "Occurrences2");
    long occurrences2 = occurrences2Prop ? occurrences2Prop->getValue() : 1;
    if (occurrences < 1 || occurrences2 < 1) {
        throw Base::ValueError("At least one occurrence required");
    }

    auto steps1 = linearSteps(obj, context, false);
    auto steps2 = linearSteps(obj, context, true);

    // Row by row along the first direction, the original first
    std::vector<Base::Placement> res;
    res.reserve(steps1.size() * steps2.size());
    for (const auto& step1 : steps1) {
        for (const auto& step2 : steps2) {
            res.emplace_back(step1 + step2, Base::Rotation());
        }
    }
    return res;
}

/// The axis of a polar or circular pattern, in the local frame
Pattern::Axis localAxis(const PropertyContainer& obj, const Pattern::Context& context)
{
    std::vector<std::string> subs;
    auto linked = getLink(obj, "Axis", subs);
    if (!linked) {
        if (!context.defaultReferences) {
            throw Base::ValueError("No axis reference specified");
        }
        return {Base::Vector3d(), Base::Vector3d(0, 0, 1)};
    }
    auto axis = Pattern::resolveAxis(linked, subs);
    Base::Vector3d base;
    context.placement.inverse().multVec(axis.base, base);
    return {base, toLocal(context, axis.direction)};
}

std::vector<Base::Placement> polarPlacements(const PropertyContainer& obj,
                                             const Pattern::Context& context)
{
    long occurrences = getProp<PropertyInteger>(obj, "Occurrences")->getValue();
    if (occurrences < 1) {
        throw Base::ValueError("At least one occurrence required");
    }
    if (occurrences == 1) {
        return {Base::Placement()};
    }

    auto axis = localAxis(obj, context);
    if (getProp<PropertyBool>(obj, "Reversed")->getValue()) {
        axis.direction = -axis.direction;
    }

    bool extent =
        getProp<PropertyEnumeration>(obj, "Mode")->getValue() == static_cast<long>(Mode::Extent);
    double step = 0.0;
    if (extent) {
        double angle = getProp<PropertyFloat>(obj, "Angle")->getValue();
        // Two occurrences in a full turn are half a turn apart
        if (std::fabs(std::fabs(angle) - 360.0) < Confusion) {
            angle /= occurrences;
        }
        else {
            angle /= occurrences - 1;
        }
        step = Base::toRadians(angle);
        if (std::fabs(step) < Angular) {
            throw Base::ValueError("Pattern angle too small");
        }
    }

    const auto& spacings = getProp<PropertyFloatList>(obj, "Spacings")->getValues();
    const auto& pattern = getProp<PropertyFloatList>(obj, "SpacingPattern")->getValues();
    double offset = getProp<PropertyFloat>(obj, "Offset")->getValue();

    std::vector<Base::Placement> res {Base::Placement()};
    res.reserve(occurrences);
    double cumulative = 0.0;
    for (long i = 1; i < occurrences; ++i) {
        if (extent) {
            cumulative = i * step;
        }
        else {
            cumulative += Base::toRadians(
                Pattern::getSpacing(spacings, pattern, offset, static_cast<int>(i - 1)));
        }
        res.push_back(rotationAbout(axis.base, axis.direction, cumulative));
    }
    return res;
}

std::vector<Base::Placement> circularPlacements(const PropertyContainer& obj,
                                                const Pattern::Context& context)
{
    const double radialDistance = getProp<PropertyFloat>(obj, "RadialDistance")->getValue();
    const double tangentialDistance = getProp<PropertyFloat>(obj, "TangentialDistance")->getValue();
    const long circleCount = getProp<PropertyInteger>(obj, "NumberCircles")->getValue();
    const long symmetry = std::max(1L, getProp<PropertyInteger>(obj, "Symmetry")->getValue());

    if (!std::isfinite(radialDistance) || radialDistance <= Confusion) {
        throw Base::ValueError("Radial distance must be greater than zero");
    }
    if (!std::isfinite(tangentialDistance) || tangentialDistance <= Confusion) {
        throw Base::ValueError("Tangential distance must be greater than zero");
    }
    if (circleCount < 2) {
        throw Base::ValueError("At least two concentric circles are required");
    }
    if (circleCount > Pattern::MaxGeneratedOccurrences) {
        throw Base::ValueError("Circular pattern would create more than "
                               + std::to_string(Pattern::MaxGeneratedOccurrences) + " circles");
    }

    const auto axis = localAxis(obj, context);
    const Base::Vector3d& dir = axis.direction;

    // Upstream's choice of the first radial direction, for the same layout
    Base::Vector3d lead(0.0, 1.0, 0.0);
    if (std::abs(dir.x) <= Confusion && std::abs(dir.z) <= Confusion) {
        lead = Base::Vector3d(1.0, 0.0, 0.0);
    }
    Base::Vector3d radial = dir.Cross(lead);
    radial.Normalize();

    std::vector<Base::Placement> res {Base::Placement()};
    const double fullCircle = 2.0 * std::acos(-1.0);
    for (long circle = 1; circle < circleCount; ++circle) {
        const double radius = circle * radialDistance;
        const double requested = std::floor(fullCircle * radius / tangentialDistance);
        // Bound the population before narrowing it or making any occurrence
        if (!std::isfinite(requested) || requested > Pattern::MaxGeneratedOccurrences
            || res.size() + static_cast<std::size_t>(requested)
                > static_cast<std::size_t>(Pattern::MaxGeneratedOccurrences)) {
            throw Base::ValueError("Circular pattern would create more than "
                                   + std::to_string(Pattern::MaxGeneratedOccurrences)
                                   + " occurrences");
        }
        long count = static_cast<long>(requested);
        count = count / symmetry * symmetry;
        if (count == 0) {
            continue;
        }
        const double step = fullCircle / count;
        for (long i = 0; i < count; ++i) {
            res.push_back(rotationAbout(axis.base, dir, i * step)
                          * Base::Placement(radial * radius, Base::Rotation()));
        }
    }
    return res;
}

std::vector<double> pathDistances(double pathLength,
                                  bool closed,
                                  long count,
                                  SpacingMode mode,
                                  double spacing,
                                  double startOffset,
                                  double endOffset)
{
    if (startOffset < 0.0 || endOffset < 0.0) {
        throw Base::ValueError("Path offsets cannot be negative");
    }
    if (startOffset + endOffset > pathLength + Confusion) {
        throw Base::ValueError("Path offsets exceed the path length");
    }

    const double available = std::max(0.0, pathLength - startOffset - endOffset);
    std::vector<double> distances;

    if (mode == SpacingMode::FixedCount) {
        if (count > Pattern::MaxGeneratedOccurrences) {
            throw Base::ValueError("Path pattern would create more than "
                                   + std::to_string(Pattern::MaxGeneratedOccurrences)
                                   + " occurrences");
        }
        // A closed path without offsets would put the last one on the first
        const long segments =
            std::max(1L, closed && startOffset == 0.0 && endOffset == 0.0 ? count : count - 1);
        const double step = available / segments;
        distances.reserve(count);
        for (long i = 0; i < count; ++i) {
            distances.push_back(startOffset + i * step);
        }
        return distances;
    }

    if (spacing <= Confusion) {
        throw Base::ValueError("Path spacing must be greater than zero");
    }

    const long maximum = mode == SpacingMode::FixedCountAndSpacing ? count : LONG_MAX;
    for (long i = 0; i < maximum; ++i) {
        const double distance = startOffset + i * spacing;
        if (distance > startOffset + available + Confusion) {
            break;
        }
        if (distances.size() >= static_cast<std::size_t>(Pattern::MaxGeneratedOccurrences)) {
            throw Base::ValueError("Path pattern would create more than "
                                   + std::to_string(Pattern::MaxGeneratedOccurrences)
                                   + " occurrences");
        }
        distances.push_back(distance);
    }
    return distances;
}

/** The frame of an occurrence: X along the tangent when aligned, Z the
 * vertical vector made perpendicular to it (a world axis when they are
 * parallel), Y completing a right-handed frame
 */
Base::Placement pathFrame(const Base::Vector3d& position,
                          const Base::Vector3d& tangent,
                          bool align,
                          const Base::Vector3d& vertical)
{
    if (!align) {
        return {position, Base::Rotation()};
    }
    if (tangent.Length() <= Confusion) {
        throw Base::ValueError("Path tangent is null");
    }
    Base::Vector3d x = tangent;
    x.Normalize();
    Base::Vector3d z = vertical - x * (vertical * x);
    if (z.Length() <= Confusion) {
        z = std::abs(x.z) < 0.9 ? Base::Vector3d(0, 0, 1) : Base::Vector3d(0, 1, 0);
        z = z - x * (z * x);
    }
    z.Normalize();
    Base::Vector3d y = z.Cross(x);
    Base::Matrix4D mat;
    mat.setCol(0, x);
    mat.setCol(1, y);
    mat.setCol(2, z);
    return {position, Base::Rotation(mat)};
}

/// The placements relative to the first: rotations and translations each
/// taken from the first's, as upstream does
void makeRelativeToFirst(std::vector<Base::Placement>& placements)
{
    if (placements.empty()) {
        return;
    }
    const Base::Rotation inverseFirst = placements.front().getRotation().inverse();
    const Base::Vector3d firstPosition = placements.front().getPosition();
    for (auto& placement : placements) {
        placement = Base::Placement(placement.getPosition() - firstPosition,
                                    placement.getRotation() * inverseFirst);
    }
}

std::vector<Base::Placement> pathPlacements(const PropertyContainer& obj,
                                            const Pattern::Context& context)
{
    std::vector<std::string> subs;
    auto linked = getLink(obj, "Path", subs);
    if (!linked) {
        return {};
    }
    auto path = Pattern::resolvePath(linked, subs);
    const double length = path->length();
    if (length <= Confusion) {
        throw Base::ValueError("Path length must be greater than zero");
    }

    const auto distances = pathDistances(
        length,
        path->isClosed(),
        getProp<PropertyInteger>(obj, "Count")->getValue(),
        static_cast<SpacingMode>(getProp<PropertyEnumeration>(obj, "SpacingMode")->getValue()),
        getProp<PropertyFloat>(obj, "Spacing")->getValue(),
        getProp<PropertyFloat>(obj, "StartOffset")->getValue(),
        getProp<PropertyFloat>(obj, "EndOffset")->getValue());
    if (distances.empty()) {
        throw Base::ValueError("Path pattern produced no occurrences");
    }

    bool reverse = getProp<PropertyBool>(obj, "ReversePath")->getValue();
    bool align = getProp<PropertyBool>(obj, "Align")->getValue();
    const auto& vertical = getProp<PropertyVector>(obj, "VerticalVector")->getValue();

    std::vector<Base::Placement> res;
    res.reserve(distances.size());
    for (double distance : distances) {
        Base::Vector3d position, tangent;
        if (reverse) {
            path->evaluate(length - distance, position, tangent);
            tangent = -tangent;
        }
        else {
            path->evaluate(distance, position, tangent);
        }
        res.push_back(pathFrame(position, tangent, align, vertical));
    }
    if (context.relativeToFirst) {
        makeRelativeToFirst(res);
    }
    return res;
}

std::vector<Base::Placement> pointPlacements(const PropertyContainer& obj,
                                             const Pattern::Context& context)
{
    std::vector<std::string> subs;
    auto linked = getLink(obj, "PointObject", subs);
    if (!linked) {
        return {};
    }
    std::vector<Base::Vector3d> points;
    for (const auto& point : Pattern::resolvePoints(linked, subs)) {
        bool duplicate = std::any_of(points.begin(), points.end(), [&](const Base::Vector3d& p) {
            return Base::Distance(p, point) <= Confusion;
        });
        if (!duplicate) {
            points.push_back(point);
        }
    }
    if (points.empty()) {
        throw Base::ValueError("Point object does not contain vertices");
    }

    Base::Vector3d origin = context.relativeToFirst ? points.front() : Base::Vector3d();
    std::vector<Base::Placement> res;
    res.reserve(points.size());
    for (const auto& point : points) {
        res.emplace_back(point - origin, Base::Rotation());
    }
    return res;
}

// ----------------------------------------------------------------------------
// Keeping the inputs in step

void setEnums(PropertyEnumeration* prop, const char** enums)
{
    long value = prop->getValue();
    prop->setEnums(enums);
    long count = 0;
    while (enums[count]) {
        ++count;
    }
    if (value >= 0 && value < count && prop->getValue() != value) {
        prop->setValue(value);
    }
}

void setupProperty(Pattern::Type type, Property* prop, const char* name = nullptr)
{
    if (!name) {
        name = prop->getName();
    }
    if (!name) {
        return;
    }
    if (auto enumProp = dynamic_cast<PropertyEnumeration*>(prop)) {
        setEnums(enumProp, std::strcmp(name, "SpacingMode") == 0 ? SpacingModeEnums : ModeEnums);
    }
    else if (auto intProp = dynamic_cast<PropertyIntegerConstraint*>(prop)) {
        if (std::strcmp(name, "NumberCircles") == 0) {
            intProp->setConstraints(&NumberCirclesRange);
        }
        else if (std::strcmp(name, "Symmetry") == 0) {
            intProp->setConstraints(&SymmetryRange);
        }
        else {
            intProp->setConstraints(&OccurrencesRange);
        }
    }
    else if (auto angleProp = dynamic_cast<PropertyAngle*>(prop)) {
        if (type == Pattern::Type::Polar) {
            angleProp->setConstraints(&AngleRange);
        }
    }
}

void setModeStatus(PropertyContainer& obj, const char* suffix, const char* lengthName)
{
    auto mode = findProp<PropertyEnumeration>(obj, withSuffix("Mode", suffix).c_str());
    auto length = findProp<Property>(obj, withSuffix(lengthName, suffix).c_str());
    auto offset = findProp<Property>(obj, withSuffix("Offset", suffix).c_str());
    if (!mode || !length || !offset) {
        return;
    }
    bool extent = mode->getValue() == static_cast<long>(Mode::Extent);
    length->setReadOnly(!extent);
    offset->setReadOnly(extent);
}

void setPathStatus(PropertyContainer& obj)
{
    auto mode = findProp<PropertyEnumeration>(obj, "SpacingMode");
    auto align = findProp<PropertyBool>(obj, "Align");
    if (!mode || !align) {
        return;
    }
    auto spacingMode = static_cast<SpacingMode>(mode->getValue());
    if (auto count = findProp<Property>(obj, "Count")) {
        count->setStatus(Property::Hidden, spacingMode == SpacingMode::FixedSpacing);
    }
    if (auto spacing = findProp<Property>(obj, "Spacing")) {
        spacing->setStatus(Property::Hidden, spacingMode == SpacingMode::FixedCount);
    }
    if (auto vertical = findProp<Property>(obj, "VerticalVector")) {
        vertical->setStatus(Property::Hidden, !align->getValue());
    }
}

/// One item per gap, so that the property editor shows them all, up to
/// MaxListedSpacings. Not on recompute, which would touch the object there.
void resizeSpacings(PropertyContainer& obj, const char* suffix)
{
    auto occurrences = findProp<PropertyInteger>(obj, withSuffix("Occurrences", suffix).c_str());
    auto spacings = findProp<PropertyFloatList>(obj, withSuffix("Spacings", suffix).c_str());
    if (!occurrences || !spacings) {
        return;
    }
    int gaps = static_cast<int>(std::max(0L, occurrences->getValue() - 1));
    int size = spacings->getSize();
    int target = size > gaps ? gaps : std::min(gaps, std::max(size, Pattern::MaxListedSpacings));
    if (size == target) {
        return;
    }
    std::vector<double> values = spacings->getValues();
    values.resize(target, -1.0);
    spacings->setValues(values);
}

/// Keep Length in step with Offset and the number of gaps, which a change of
/// Occurrences changes too. One occurrence counts as one gap.
void syncLengthAndOffset(PropertyContainer& obj, const char* suffix)
{
    auto mode = findProp<PropertyEnumeration>(obj, withSuffix("Mode", suffix).c_str());
    auto length = findProp<PropertyFloat>(obj, withSuffix("Length", suffix).c_str());
    auto offset = findProp<PropertyFloat>(obj, withSuffix("Offset", suffix).c_str());
    auto occurrences = findProp<PropertyInteger>(obj, withSuffix("Occurrences", suffix).c_str());
    if (!mode || !length || !offset || !occurrences) {
        return;
    }
    long gaps = occurrences->getValue() > 1 ? occurrences->getValue() - 1 : 1;
    if (mode->getValue() == static_cast<long>(Mode::Spacing)) {
        if (!length->testStatus(Property::Immutable)) {
            length->setValue(offset->getValue() * gaps);
        }
    }
    else if (!offset->testStatus(Property::Immutable)) {
        offset->setValue(length->getValue() / gaps);
    }
}

bool isNamed(const Property* prop, const char* name, const char* suffix = "")
{
    const char* propName = prop->getName();
    return propName && withSuffix(name, suffix) == propName;
}

}  // namespace

// ----------------------------------------------------------------------------

bool Pattern::Resolver::getDirection(DocumentObject*, const std::string&, Base::Vector3d&) const
{
    return false;
}

bool Pattern::Resolver::getAxis(DocumentObject*, const std::string&, Axis&) const
{
    return false;
}

std::unique_ptr<Pattern::Path> Pattern::Resolver::getPath(DocumentObject*,
                                                          const std::vector<std::string>&) const
{
    return {};
}

bool Pattern::Resolver::getPoints(DocumentObject*,
                                  const std::vector<std::string>&,
                                  std::vector<Base::Vector3d>&) const
{
    return false;
}

void Pattern::addResolver(std::shared_ptr<Resolver> resolver)
{
    if (!resolver) {
        return;
    }
    std::lock_guard<std::mutex> lock(ResolverMutex);
    getResolvers().push_back(std::move(resolver));
}

void Pattern::removeResolver(const Resolver* resolver)
{
    std::lock_guard<std::mutex> lock(ResolverMutex);
    auto& resolvers = getResolvers();
    // Never App's own, the first
    resolvers.erase(std::remove_if(resolvers.begin() + 1,
                                   resolvers.end(),
                                   [resolver](const auto& r) {
                                       return r.get() == resolver;
                                   }),
                    resolvers.end());
}

Base::Vector3d Pattern::resolveDirection(DocumentObject* obj, const std::vector<std::string>& subs)
{
    if (!obj) {
        throw Base::ValueError("No direction reference specified");
    }
    Base::Vector3d dir;
    for (const auto& resolver : resolverList()) {
        if (resolver->getDirection(obj, firstSub(subs), dir)) {
            if (dir.Length() < Confusion) {
                throw Base::ValueError("Direction reference has no direction");
            }
            return dir.Normalize();
        }
    }
    throw Base::TypeError(
        "Direction reference must be a straight edge, a planar face, or a datum line or plane");
}

Pattern::Axis Pattern::resolveAxis(DocumentObject* obj, const std::vector<std::string>& subs)
{
    if (!obj) {
        throw Base::ValueError("No axis reference specified");
    }
    Axis axis;
    for (const auto& resolver : resolverList()) {
        if (resolver->getAxis(obj, firstSub(subs), axis)) {
            if (axis.direction.Length() < Confusion) {
                throw Base::ValueError("Axis reference has no direction");
            }
            axis.direction.Normalize();
            return axis;
        }
    }
    throw Base::TypeError(
        "Axis reference must be a straight or circular edge, a sketch axis, or a datum line");
}

std::unique_ptr<Pattern::Path> Pattern::resolvePath(DocumentObject* obj,
                                                    const std::vector<std::string>& subs)
{
    if (!obj) {
        throw Base::ValueError("No path specified");
    }
    for (const auto& resolver : resolverList()) {
        if (auto path = resolver->getPath(obj, subs)) {
            return path;
        }
    }
    throw Base::TypeError("Path must reference an object with a shape");
}

std::vector<Base::Vector3d> Pattern::resolvePoints(DocumentObject* obj,
                                                   const std::vector<std::string>& subs)
{
    if (!obj) {
        throw Base::ValueError("No point object specified");
    }
    std::vector<Base::Vector3d> points;
    for (const auto& resolver : resolverList()) {
        if (resolver->getPoints(obj, subs, points)) {
            return points;
        }
    }
    throw Base::TypeError("Point object must reference an object with a shape or a datum point");
}

double Pattern::getSpacing(const std::vector<double>& spacings,
                           const std::vector<double>& pattern,
                           double offset,
                           int index)
{
    if (index >= 0 && index < static_cast<int>(spacings.size()) && spacings[index] != -1.0) {
        return spacings[index];
    }
    if (pattern.size() > 1) {
        return pattern[index % pattern.size()];
    }
    return offset;
}

std::vector<Base::Placement> Pattern::getPlacements(Type type,
                                                    const PropertyContainer& obj,
                                                    const Context& context)
{
    switch (type) {
        case Type::Linear:
            return linearPlacements(obj, context);
        case Type::Polar:
            return polarPlacements(obj, context);
        case Type::Circular:
            return circularPlacements(obj, context);
        case Type::Path:
            return pathPlacements(obj, context);
        case Type::Point:
            return pointPlacements(obj, context);
    }
    throw Base::ValueError("Unknown pattern type");
}

const std::vector<Pattern::PropertySpec>& Pattern::getPropertySpecs(Type type)
{
    switch (type) {
        case Type::Linear:
            return LinearSpecs;
        case Type::Polar:
            return PolarSpecs;
        case Type::Circular:
            return CircularSpecs;
        case Type::Path:
            return PathSpecs;
        case Type::Point:
            return PointSpecs;
    }
    static const std::vector<PropertySpec> none;
    return none;
}

const Pattern::PropertySpec* Pattern::getPropertySpec(Type type, const char* name)
{
    for (const auto& spec : getPropertySpecs(type)) {
        if (std::strcmp(spec.name, name) == 0) {
            return &spec;
        }
    }
    return nullptr;
}

void Pattern::initProperty(Type type, Property* prop, const char* propName)
{
    if (!propName && prop) {
        propName = prop->getName();
    }
    if (!prop || !propName) {
        return;
    }
    setupProperty(type, prop, propName);

    std::string name = propName;
    auto setFloat = [prop](double value) {
        if (auto p = dynamic_cast<PropertyFloat*>(prop)) {
            p->setValue(value);
        }
    };
    auto setInt = [prop](long value) {
        if (auto p = dynamic_cast<PropertyInteger*>(prop)) {
            p->setValue(value);
        }
    };
    switch (type) {
        case Type::Linear:
            if (name == "Length" || name == "Length2") {
                setFloat(100.0);
            }
            else if (name == "Offset" || name == "Offset2") {
                setFloat(10.0);
            }
            else if (name == "Occurrences") {
                setInt(2);
            }
            else if (name == "Occurrences2") {
                setInt(1);
            }
            else if (name == "Spacings") {
                static_cast<PropertyFloatList*>(prop)->setValues({-1.0});
            }
            break;
        case Type::Polar:
            if (name == "Angle") {
                setFloat(360.0);
            }
            else if (name == "Offset") {
                setFloat(120.0);
            }
            else if (name == "Occurrences") {
                setInt(3);
            }
            else if (name == "Spacings") {
                static_cast<PropertyFloatList*>(prop)->setValues({-1.0, -1.0});
            }
            break;
        case Type::Circular:
            if (name == "RadialDistance") {
                setFloat(50.0);
            }
            else if (name == "TangentialDistance") {
                setFloat(25.0);
            }
            else if (name == "NumberCircles") {
                setInt(3);
            }
            else if (name == "Symmetry") {
                setInt(1);
            }
            break;
        case Type::Path:
            if (name == "Count") {
                setInt(4);
            }
            else if (name == "Spacing") {
                setFloat(20.0);
            }
            else if (name == "VerticalVector") {
                static_cast<PropertyVector*>(prop)->setValue(Base::Vector3d(0, 0, 1));
            }
            break;
        case Type::Point:
            break;
    }
}

void Pattern::setupProperties(Type type, PropertyContainer& obj)
{
    for (const auto& spec : getPropertySpecs(type)) {
        if (auto prop = getProperty(obj, spec.name)) {
            setupProperty(type, prop);
        }
    }
    switch (type) {
        case Type::Linear:
            setModeStatus(obj, "", "Length");
            setModeStatus(obj, "2", "Length");
            break;
        case Type::Polar:
            setModeStatus(obj, "", "Angle");
            break;
        case Type::Path:
            setPathStatus(obj);
            break;
        default:
            break;
    }
}

void Pattern::onChanged(Type type, PropertyContainer& obj, const Property* prop)
{
    if (!prop) {
        return;
    }
    switch (type) {
        case Type::Linear:
            for (const char* suffix : {"", "2"}) {
                auto mode = findProp<PropertyEnumeration>(obj, withSuffix("Mode", suffix).c_str());
                bool spacing = mode && mode->getValue() == static_cast<long>(Mode::Spacing);
                if (isNamed(prop, "Mode", suffix)) {
                    setModeStatus(obj, suffix, "Length");
                }
                else if (isNamed(prop, "Occurrences", suffix)) {
                    resizeSpacings(obj, suffix);
                    syncLengthAndOffset(obj, suffix);
                }
                else if ((spacing && isNamed(prop, "Offset", suffix))
                         || (!spacing && isNamed(prop, "Length", suffix))) {
                    syncLengthAndOffset(obj, suffix);
                }
            }
            break;
        case Type::Polar:
            if (isNamed(prop, "Mode")) {
                setModeStatus(obj, "", "Angle");
            }
            else if (isNamed(prop, "Occurrences")) {
                resizeSpacings(obj, "");
            }
            break;
        case Type::Path:
            if (isNamed(prop, "SpacingMode") || isNamed(prop, "Align")) {
                setPathStatus(obj);
            }
            break;
        default:
            break;
    }
}

bool Pattern::isTouched(Type type, const PropertyContainer& obj)
{
    for (const auto& spec : getPropertySpecs(type)) {
        auto prop = getProperty(obj, spec.name);
        if (prop && prop->isTouched()) {
            return true;
        }
    }
    return false;
}

Property* Pattern::getProperty(const PropertyContainer& obj, const char* name)
{
    auto prop = obj.getPropertyByName(name);
    // Not a property a link finds on the object it links to
    return prop && prop->getContainer() == &obj ? prop : nullptr;
}

bool Pattern::isPatternProperty(Type type, const Property* prop)
{
    return prop && prop->getName() && getPropertySpec(type, prop->getName());
}

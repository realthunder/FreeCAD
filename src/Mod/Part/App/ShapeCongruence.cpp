/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <BRepAdaptor_Curve.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Surface.hxx>
#include <TopoDS_Face.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <algorithm>
#include <chrono>
#include <cmath>
#endif

#include <Base/Console.h>

#include "ShapeCongruence.h"

FC_LOG_LEVEL_INIT("Congruence", true, true);

namespace Part
{

namespace
{

/// Adds its lifetime to a running total. What this measures is what a save
/// waits for, so it is wall clock and not CPU.
class Stopwatch
{
public:
    explicit Stopwatch(double& into)
        : _into(into)
        , _start(std::chrono::steady_clock::now())
    {}
    ~Stopwatch()
    {
        _into += std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count();
    }

private:
    double& _into;
    std::chrono::steady_clock::time_point _start;
};

/// What invariantsOf() spent, which is a static and so cannot reach the
/// index's own counters. Folded into them by report().
struct GPropCost
{
    std::size_t computed = 0;
    std::size_t lengths = 0;
    std::size_t volumes = 0;
    double lengthSeconds = 0.;
    double areaSeconds = 0.;
    double volumeSeconds = 0.;
};
GPropCost theCost;

//! Every vertex of a shape, in the order TopExp gives them -- which is the
//! order the correspondence relies on.
void vertexPoints(const TopoDS_Shape& shape, std::vector<gp_Pnt>& points)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_VERTEX, map);
    points.clear();
    points.reserve(map.Extent());
    for (int i = 1; i <= map.Extent(); ++i)
        points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(map(i))));
}

//! The midpoint of every edge, again in index order. A vertex cloud alone
//! cannot tell two shapes apart that share their corners and differ in what
//! runs between them.
void edgeMidPoints(const TopoDS_Shape& shape, std::vector<gp_Pnt>& points)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_EDGE, map);
    points.clear();
    points.reserve(map.Extent());
    for (int i = 1; i <= map.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(map(i));
        try {
            BRepAdaptor_Curve curve(edge);
            const double first = curve.FirstParameter();
            const double last = curve.LastParameter();
            points.push_back(curve.Value(0.5 * (first + last)));
        }
        catch (const Standard_Failure&) {
            // A degenerate edge has no curve to sample. Its vertices are
            // already in the other list, so a placeholder keeps the indices
            // lined up without claiming anything.
            points.emplace_back(0., 0., 0.);
        }
    }
}

//! Three points spread as widely as the cloud allows: the first, the one
//! furthest from it, and the one furthest from the line through those two.
//! Returns false when the cloud is a point or a line, which leaves a rotation
//! about that line undetermined and is not worth resolving.
bool spreadTriple(const std::vector<gp_Pnt>& points, int& i1, int& i2, int& i3, double& extent)
{
    if (points.size() < 3)
        return false;
    i1 = 0;
    double best = 0;
    i2 = -1;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double d = points[i].SquareDistance(points[0]);
        if (d > best) {
            best = d;
            i2 = static_cast<int>(i);
        }
    }
    if (i2 < 0 || best <= 0)
        return false;
    extent = std::sqrt(best);

    const gp_Vec axis(points[i1], points[i2]);
    double bestArea = 0;
    i3 = -1;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (static_cast<int>(i) == i2)
            continue;
        const double area = axis.Crossed(gp_Vec(points[i1], points[i])).Magnitude();
        if (area > bestArea) {
            bestArea = area;
            i3 = static_cast<int>(i);
        }
    }
    // The perpendicular spread, not the cross product itself, so the threshold
    // is a length and can be compared with a tolerance.
    return i3 >= 0 && bestArea / extent > Precision::Confusion() * 100.;
}

//! A right-handed frame on three points.
bool frameOf(const gp_Pnt& origin, const gp_Pnt& onX, const gp_Pnt& inXY, gp_Ax3& frame)
{
    const gp_Vec vx(origin, onX);
    const gp_Vec vxy(origin, inXY);
    const gp_Vec normal = vx.Crossed(vxy);
    if (vx.Magnitude() <= gp::Resolution() || normal.Magnitude() <= gp::Resolution())
        return false;
    frame = gp_Ax3(origin, gp_Dir(normal), gp_Dir(vx));
    return true;
}

bool agrees(const std::vector<gp_Pnt>& from,
            const std::vector<gp_Pnt>& to,
            const gp_Trsf& motion,
            double tolerance)
{
    if (from.size() != to.size())
        return false;
    for (std::size_t i = 0; i < from.size(); ++i) {
        gp_Pnt moved = from[i];
        moved.Transform(motion);
        if (moved.Distance(to[i]) > tolerance)
            return false;
    }
    return true;
}

/** Is this motion the identity, to within what the match was accepted at?
 *
 * It matters that this is asked at all. A motion recovered from three points
 * of two shapes that are simply equal comes out as the identity give or take
 * 1e-17, and TopLoc_Location::IsIdentity is exact -- so without this, two
 * equal parts would have a hair of a motion baked into one of them, which
 * makes a new TShape and costs precisely the sharing that content addressing
 * was already getting right.
 */
bool isNearlyIdentity(const gp_Trsf& motion, double tolerance)
{
    if (motion.TranslationPart().Modulus() > tolerance)
        return false;
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 3; ++col) {
            const double expected = (row == col) ? 1. : 0.;
            if (std::abs(motion.Value(row, col) - expected) > tolerance)
                return false;
        }
    }
    return true;
}

int countOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, type, map);
    return map.Extent();
}

/** Face by face, in index order: the same kind of surface, and the same
 * surface.
 *
 * The kind is nearly free and catches an exact cylinder standing in for a
 * spline approximating it. The sampling behind it is what catches the case
 * that defeated everything cheaper: two patches can share every vertex, every
 * edge midpoint, their area and their volume, and still bulge differently --
 * measured on a real assembly, a pair whose boxes differ by half their depth.
 * A boundary does not determine an interior, and neither do the invariants.
 *
 * Each face is sampled in its own parameter space, so a parametrization that
 * is merely offset between two instances still lines up; one that genuinely
 * differs makes the shapes fail to match, which costs a sharing opportunity
 * and nothing else.
 */
gp_Pnt sampleSurface(const TopoDS_Face& face, double u, double v)
{
    TopLoc_Location loc;
    const occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face, loc);
    if (surface.IsNull())
        return gp_Pnt(0., 0., 0.);
    gp_Pnt point = surface->Value(u, v);
    if (!loc.IsIdentity())
        point.Transform(loc.Transformation());
    return point;
}

bool surfacesAgree(const TopoDS_Shape& from, const TopoDS_Shape& to, const gp_Trsf& motion)
{
    TopTools_IndexedMapOfShape fromFaces;
    TopTools_IndexedMapOfShape toFaces;
    TopExp::MapShapes(from, TopAbs_FACE, fromFaces);
    TopExp::MapShapes(to, TopAbs_FACE, toFaces);
    if (fromFaces.Extent() != toFaces.Extent())
        return false;

    // Corners and centre of each face's own parameter rectangle.
    static const double kSamples[5][2] = {
        {0.5, 0.5}, {0.25, 0.25}, {0.75, 0.25}, {0.25, 0.75}, {0.75, 0.75}};

    for (int i = 1; i <= fromFaces.Extent(); ++i) {
        const TopoDS_Face& a = TopoDS::Face(fromFaces(i));
        const TopoDS_Face& b = TopoDS::Face(toFaces(i));
        TopLoc_Location aLoc;
        TopLoc_Location bLoc;
        const occ::handle<Geom_Surface> aSurface = BRep_Tool::Surface(a, aLoc);
        const occ::handle<Geom_Surface> bSurface = BRep_Tool::Surface(b, bLoc);
        if (aSurface.IsNull() != bSurface.IsNull())
            return false;
        if (aSurface.IsNull())
            continue;
        if (aSurface->DynamicType() != bSurface->DynamicType())
            return false;

        double au1 = 0., au2 = 0., av1 = 0., av2 = 0.;
        double bu1 = 0., bu2 = 0., bv1 = 0., bv2 = 0.;
        try {
            BRepTools::UVBounds(a, au1, au2, av1, av2);
            BRepTools::UVBounds(b, bu1, bu2, bv1, bv2);
        }
        catch (const Standard_Failure&) {
            return false;
        }
        // A face whose parameter rectangle is a different size is a different
        // face, whatever its boundary does.
        if (std::abs((au2 - au1) - (bu2 - bu1)) > Precision::PConfusion()
            || std::abs((av2 - av1) - (bv2 - bv1)) > Precision::PConfusion())
            return false;

        for (const auto& sample : kSamples) {
            const double au = au1 + (au2 - au1) * sample[0];
            const double av = av1 + (av2 - av1) * sample[1];
            const double bu = bu1 + (bu2 - bu1) * sample[0];
            const double bv = bv1 + (bv2 - bv1) * sample[1];
            gp_Pnt moved;
            try {
                moved = sampleSurface(a, au, av);
                moved.Transform(motion);
                if (moved.Distance(sampleSurface(b, bu, bv)) > Precision::Confusion() * 0.01)
                    return false;
            }
            catch (const Standard_Failure&) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

bool recoverShapeMotion(const TopoDS_Shape& from, const TopoDS_Shape& to, gp_Trsf& motion)
{
    if (from.IsNull() || to.IsNull())
        return false;
    if (from.ShapeType() != to.ShapeType())
        return false;

    std::vector<gp_Pnt> fromPoints;
    std::vector<gp_Pnt> toPoints;
    vertexPoints(from, fromPoints);
    vertexPoints(to, toPoints);
    if (fromPoints.size() != toPoints.size() || fromPoints.empty())
        return false;

    int i1 = 0;
    int i2 = 0;
    int i3 = 0;
    double extent = 0;
    if (!spreadTriple(fromPoints, i1, i2, i3, extent))
        return false;

    // The same three, by index. If the two shapes are congruent with this
    // correspondence, the triangles are the same triangle; if they are not,
    // this is where it shows, before any frame is built.
    //
    // A hundredth of Confusion, not Confusion. Two instances of one part agree
    // to about 1e-12 (docs/SharedShapeStorage.md sec 12.11), so nothing real is
    // turned away by asking for 1e-9 -- while accepting the full 1e-7 lets a
    // near-miss through and moves a centre of mass by 6e-8, measured. What is
    // being decided is not whether two shapes are close enough to touch; it is
    // whether one may be stored in place of the other.
    const double tolerance = Precision::Confusion() * 0.01;
    const std::pair<int, int> sides[3] = {{i1, i2}, {i1, i3}, {i2, i3}};
    for (const auto& side : sides) {
        const double a = fromPoints[side.first].Distance(fromPoints[side.second]);
        const double b = toPoints[side.first].Distance(toPoints[side.second]);
        if (std::abs(a - b) > tolerance)
            return false;
    }

    gp_Ax3 fromFrame;
    gp_Ax3 toFrame;
    if (!frameOf(fromPoints[i1], fromPoints[i2], fromPoints[i3], fromFrame)
        || !frameOf(toPoints[i1], toPoints[i2], toPoints[i3], toFrame))
        return false;

    // Built both ways round and settled by trying it, rather than by reasoning
    // about which direction SetTransformation means: the answer has to survive
    // every vertex either way, so the check is the same check.
    gp_Trsf fromTrsf;
    fromTrsf.SetTransformation(fromFrame);
    gp_Trsf toTrsf;
    toTrsf.SetTransformation(toFrame);

    const gp_Trsf candidates[2] = {toTrsf.Inverted() * fromTrsf, fromTrsf.Inverted() * toTrsf};
    for (const gp_Trsf& candidate : candidates) {
        // A mirrored instance shares every distance with its original and is
        // not a rigid motion. Refused here rather than counted as sharing.
        if (candidate.IsNegative() || candidate.ScaleFactor() < 0.)
            continue;
        if (!agrees(fromPoints, toPoints, candidate, tolerance))
            continue;

        std::vector<gp_Pnt> fromEdges;
        std::vector<gp_Pnt> toEdges;
        edgeMidPoints(from, fromEdges);
        edgeMidPoints(to, toEdges);
        if (!agrees(fromEdges, toEdges, candidate, tolerance))
            continue;

        if (countOf(from, TopAbs_FACE) != countOf(to, TopAbs_FACE))
            continue;
        if (!surfacesAgree(from, to, candidate))
            continue;

        motion = candidate;
        return true;
    }
    return false;
}

std::size_t shapeCongruenceKey(const TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return 0;

    std::vector<gp_Pnt> points;
    vertexPoints(shape, points);
    // Below three vertices there is no frame to recover, so there is nothing
    // to propose either.
    if (points.size() < 3)
        return 0;

    gp_XYZ centroid(0., 0., 0.);
    for (const gp_Pnt& point : points)
        centroid += point.XYZ();
    centroid.Divide(static_cast<double>(points.size()));

    std::vector<double> radii;
    radii.reserve(points.size());
    double extent = 0;
    for (const gp_Pnt& point : points) {
        const double r = (point.XYZ() - centroid).Modulus();
        radii.push_back(r);
        extent = std::max(extent, r);
    }
    if (extent <= Precision::Confusion())
        return 0;
    std::sort(radii.begin(), radii.end());

    // Quantized relative to the shape's own size, and coarsely: a bucket that
    // splits costs one sharing opportunity, where a bucket that is too clever
    // costs nothing it can gain. Everything it proposes is verified anyway.
    const double grid = extent * 1e-4;
    std::size_t key = std::hash<std::size_t>{}(points.size());
    const auto mix = [&key](std::size_t value) {
        key ^= value + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2);
    };
    mix(std::hash<int>{}(countOf(shape, TopAbs_EDGE)));
    mix(std::hash<int>{}(countOf(shape, TopAbs_FACE)));
    for (const double r : radii)
        mix(std::hash<long long>{}(static_cast<long long>(std::llround(r / grid))));
    return key ? key : 1;
}

double CongruenceIndex::lengthOf(const TopoDS_Shape& shape, Invariants& cache)
{
    if (!cache.lengthKnown) {
        cache.lengthKnown = true;
        ++theCost.lengths;
        Stopwatch watch(theCost.lengthSeconds);
        try {
            GProp_GProps linear;
            BRepGProp::LinearProperties(shape, linear);
            cache.length = linear.Mass();
        }
        catch (const Standard_Failure&) {
            cache.length = 0.;
        }
    }
    return cache.length;
}

double CongruenceIndex::areaOf(const TopoDS_Shape& shape, Invariants& cache)
{
    if (!cache.areaKnown) {
        cache.areaKnown = true;
        ++theCost.computed;
        Stopwatch watch(theCost.areaSeconds);
        try {
            GProp_GProps surface;
            BRepGProp::SurfaceProperties(shape, surface);
            cache.area = surface.Mass();
        }
        catch (const Standard_Failure&) {
            cache.area = 0.;
        }
    }
    return cache.area;
}

double CongruenceIndex::volumeOf(const TopoDS_Shape& shape, Invariants& cache)
{
    if (!cache.volumeKnown) {
        cache.volumeKnown = true;
        ++theCost.volumes;
        Stopwatch watch(theCost.volumeSeconds);
        try {
            GProp_GProps volume;
            BRepGProp::VolumeProperties(shape, volume);
            cache.volume = volume.Mass();
        }
        catch (const Standard_Failure&) {
            cache.volume = 0.;
        }
    }
    return cache.volume;
}

bool CongruenceIndex::find(const TopoDS_Shape& shape, Match& match) const
{
    ++_cost.finds;
    std::size_t key = 0;
    {
        Stopwatch watch(_cost.keySeconds);
        ++_cost.keys;
        key = shapeCongruenceKey(shape);
    }
    if (!key)
        return false;
    const auto it = _buckets.find(key);
    if (it == _buckets.end())
        return false;

    // Only now, and only when a bucket actually proposes something: the area
    // below is the expensive part of this, and a bucket that proposes nothing
    // must not pay for it.
    Invariants own;

    for (const Entry& entry : it->second) {
        ++_cost.candidates;
        Stopwatch theirWatch(_cost.invariantSeconds);
        // Relative, because these are areas and volumes of parts whose size is
        // not known in advance. Two instances of one part agree here to about
        // 1e-12; a part and an approximation of it disagree by a part in a
        // thousand, which is the case this is here for.
        // Length first: a rigid motion preserves it exactly as it preserves
        // the other two, and integrating along the edges is an order cheaper
        // than integrating over the faces.
        const double ownLength = lengthOf(shape, own);
        const double theirLength = lengthOf(entry.shape, entry.invariants);
        const double lengthScale = std::max(std::abs(ownLength), std::abs(theirLength));
        if (std::abs(ownLength - theirLength) > 1e-7 * std::max(lengthScale, 1.))
            continue;
        const double ownArea = areaOf(shape, own);
        const double theirArea = areaOf(entry.shape, entry.invariants);
        const double areaScale = std::max(std::abs(ownArea), std::abs(theirArea));
        if (std::abs(ownArea - theirArea) > 1e-7 * std::max(areaScale, 1.))
            continue;
        // Reached only once the areas already agree, which is what makes
        // computing it lazily worth the second flag.
        const double ownVolume = volumeOf(shape, own);
        const double theirVolume = volumeOf(entry.shape, entry.invariants);
        const double volumeScale = std::max(std::abs(ownVolume), std::abs(theirVolume));
        if (std::abs(ownVolume - theirVolume) > 1e-7 * std::max(volumeScale, 1.))
            continue;

        gp_Trsf motion;
        ++_cost.motions;
        Stopwatch motionWatch(_cost.motionSeconds);
        if (recoverShapeMotion(entry.shape, shape, motion)) {
            ++_cost.matches;
            match.slot = entry.slot;
            // Snapped, so that two equal parts share a file *and* the one
            // TShape that reading it once gives, as they always have.
            match.motion =
                isNearlyIdentity(motion, Precision::Confusion() * 0.01) ? gp_Trsf() : motion;
            return true;
        }
    }
    return false;
}

void CongruenceIndex::add(const TopoDS_Shape& shape, int slot)
{
    std::size_t key = 0;
    {
        Stopwatch watch(_cost.keySeconds);
        ++_cost.keys;
        key = shapeCongruenceKey(shape);
    }
    if (!key)
        return;
    _buckets[key].push_back(Entry {shape, slot});
    ++_count;
}

void CongruenceIndex::report() const
{
    if (!_cost.finds && !_cost.keys)
        return;
    FC_LOG("congruence: " << _cost.finds << " asked, " << _count << " stored, "
           << _cost.candidates << " candidates, " << _cost.motions << " motions, "
           << _cost.matches << " matched; keys " << _cost.keySeconds << "s, invariants "
           << _cost.invariantSeconds << "s over " << theCost.lengths << " lengths ("
           << theCost.lengthSeconds << "s), " << theCost.computed << " areas ("
           << theCost.areaSeconds << "s) and " << theCost.volumes << " volumes ("
           << theCost.volumeSeconds << "s), motions "
           << _cost.motionSeconds << "s");
    theCost = GPropCost();
}

void CongruenceIndex::clear()
{
    // Before the counts go with it: an index is cleared when the save it was
    // built for is over, which is exactly when what it cost is worth saying.
    report();
    _buckets.clear();
    _count = 0;
    _cost = Cost();
}

CongruenceIndex* CongruenceIndex::forSave(std::uint64_t generation)
{
    static std::uint64_t built = 0;
    static CongruenceIndex index;
    if (built != generation) {
        built = generation;
        index.clear();
    }
    return &index;
}

}  // namespace Part

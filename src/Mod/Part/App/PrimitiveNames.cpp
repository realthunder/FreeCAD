// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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
# include <algorithm>
# include <cctype>
# include <cmath>
# include <map>
# include <set>
# include <string>
# include <vector>

# include <BRep_Tool.hxx>
# include <BRepAdaptor_Curve.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <GeomLib_IsPlanarSurface.hxx>
# include <Standard_Failure.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopoDS_Face.hxx>
# include <TopoDS_Vertex.hxx>
# include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
# include <TopTools_ListOfShape.hxx>
# include <gp_Pnt.hxx>
# include <gp_Vec.hxx>
#endif

#include <App/IndexedName.h>
#include <App/MappedName.h>

#include "PrimitiveNames.h"

using namespace Part;

namespace
{

/// Lengths are a primitive's own, of the size somebody typed; angles and
/// the components of a unit vector the same number serves.
const double Tol = 1e-6;

bool near(double a, double b)
{
    return std::fabs(a - b) < Tol;
}

/// Which way a face faces, and whether it is flat.
struct Facing
{
    bool flat {false};
    gp_Vec normal;  ///< outward, of length one; null where it could not be had
    gp_Pnt at;  ///< a point of the surface, the middle of its bounds
};

Facing facingOf(const TopoDS_Face& face)
{
    Facing res;
    try {
        BRepAdaptor_Surface surface(face);
        const double u = 0.5 * (surface.FirstUParameter() + surface.LastUParameter());
        const double v = 0.5 * (surface.FirstVParameter() + surface.LastVParameter());
        gp_Vec du, dv;
        surface.D1(u, v, res.at, du, dv);
        gp_Vec normal = du.Crossed(dv);
        if (normal.Magnitude() > gp::Resolution()) {
            normal.Normalize();
            if (face.Orientation() == TopAbs_REVERSED)
                normal.Reverse();
            res.normal = normal;
        }
        // An ellipsoid is a ball stretched, and what was a plane of the ball
        // is a spline surface of it that is still flat
        res.flat = surface.GetType() == GeomAbs_Plane
            || GeomLib_IsPlanarSurface(BRep_Tool::Surface(face), Tol).IsPlanar();
    }
    catch (const Standard_Failure&) {
    }
    return res;
}

gp_Pnt middleOf(const TopoDS_Edge& edge)
{
    try {
        if (!BRep_Tool::Degenerated(edge)) {
            BRepAdaptor_Curve curve(edge);
            return curve.Value(0.5 * (curve.FirstParameter() + curve.LastParameter()));
        }
    }
    catch (const Standard_Failure&) {
    }
    TopExp_Explorer it(edge, TopAbs_VERTEX);
    return it.More() ? BRep_Tool::Pnt(TopoDS::Vertex(it.Current())) : gp_Pnt();
}

/// The angle about Z, from +X, in [0, 2 pi)
double angleOf(const gp_Pnt& p)
{
    double a = std::atan2(p.Y(), p.X());
    if (a < -Tol)
        a += 2.0 * M_PI;
    return a < 0.0 ? 0.0 : a;
}

std::string joined(std::vector<std::string> names)
{
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::string res;
    for (const auto& name : names)
        res += (res.empty() ? "" : "_") + name;
    return res;
}

class Namer
{
public:
    explicit Namer(const TopoDS_Shape& shape)
        : shape(shape)
    {
        TopExp::MapShapes(shape, TopAbs_FACE, faces);
        TopExp::MapShapes(shape, TopAbs_EDGE, edges);
        TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, facesOfEdge);
        TopExp::MapShapesAndAncestors(shape, TopAbs_VERTEX, TopAbs_FACE, facesOfVertex);
        TopExp::MapShapesAndAncestors(shape, TopAbs_VERTEX, TopAbs_EDGE, edgesOfVertex);
        faceNames.resize(static_cast<std::size_t>(faces.Extent()));
        edgeNames.resize(static_cast<std::size_t>(edges.Extent()));
        vertexNames.resize(static_cast<std::size_t>(vertices.Extent()));
        facings.reserve(faceNames.size());
        for (int i = 1; i <= faces.Extent(); ++i)
            facings.push_back(facingOf(TopoDS::Face(faces(i))));
        for (int i = 1; i <= vertices.Extent(); ++i) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vertices(i)));
            if (i == 1) {
                low = high = p;
                continue;
            }
            low.SetCoord(std::min(low.X(), p.X()), std::min(low.Y(), p.Y()),
                         std::min(low.Z(), p.Z()));
            high.SetCoord(std::max(high.X(), p.X()), std::max(high.Y(), p.Y()),
                          std::max(high.Z(), p.Z()));
        }
    }

    /// Six flat faces, or fewer, each facing one way out of six
    void hexahedron()
    {
        for (std::size_t i = 0; i < faceNames.size(); ++i) {
            const Facing& f = facings[i];
            const double x = f.normal.X(), y = f.normal.Y(), z = f.normal.Z();
            if (!f.flat || f.normal.Magnitude() < 0.5)
                continue;
            // A wedge's sides lean, each about one axis: what faces along X
            // has nothing of Z in it, and the other way round
            if (std::fabs(x) < Tol && std::fabs(z) < Tol)
                faceNames[i] = y < 0 ? "Front" : "Rear";
            else if (std::fabs(z) < Tol)
                faceNames[i] = x < 0 ? "Left" : "Right";
            else if (std::fabs(x) < Tol)
                faceNames[i] = z < 0 ? "Bottom" : "Top";
        }
    }

    /// What is made about the Z axis, or pushed along it
    void axial(PrimitiveNames::Kind kind, const PrimitiveNames::Tube& tube)
    {
        using Kind = PrimitiveNames::Kind;
        const bool pushed = kind == Kind::Cylinder || kind == Kind::Prism;
        std::vector<std::size_t> sides;
        for (std::size_t i = 0; i < faceNames.size(); ++i) {
            const Facing& f = facings[i];
            const TopoDS_Face face = TopoDS::Face(faces(static_cast<int>(i) + 1));
            if (kind == Kind::Torus) {
                // What closes the tube may be flat and face up or down, so
                // it is known by where it is: at the middle of the tube
                if (f.flat && std::fabs(f.normal.Z()) < Tol)
                    faceNames[i] = startsAtNought(face, false) ? "Start" : "End";
                else if (double at = 0.0; closesTube(face, tube, at))
                    faceNames[i] = nearerAngle(at, tube.from, tube.to) ? "TubeStart" : "TubeEnd";
                else
                    faceNames[i] = "Lateral";
                continue;
            }
            if (!f.flat)
                faceNames[i] = "Lateral";
            else if (f.normal.Z() < -1.0 + Tol)
                faceNames[i] = "Bottom";
            else if (f.normal.Z() > 1.0 - Tol)
                faceNames[i] = "Top";
            else if (kind == Kind::Prism)
                sides.push_back(i);
            else
                faceNames[i] = startsAtNought(face, pushed) ? "Start" : "End";
        }
        // Where a cone, a ball or an ellipsoid closes to a point on the axis
        // and has no cap, the point is the pole of that end. By the end and
        // not by a number: a cap at the other end leaves it the pole it was.
        if (kind == Kind::Cone || kind == Kind::Sphere || kind == Kind::Ellipsoid) {
            for (std::size_t i = 0; i < vertexNames.size(); ++i) {
                const TopoDS_Shape& vertex = vertices(static_cast<int>(i) + 1);
                const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vertex));
                if (std::hypot(p.X(), p.Y()) > Tol)
                    continue;
                const auto around = namesAround(facesOfVertex, vertex, faces, faceNames);
                const char* cap = below(p) ? "Bottom" : "Top";
                if (std::find(around.begin(), around.end(), cap) == around.end())
                    vertexNames[i] = std::string(cap) + "Pole";
            }
        }
        // A prism's sides, the first from the corner on +X
        const double step = sides.empty() ? 0.0 : 2.0 * M_PI / static_cast<double>(sides.size());
        for (std::size_t i : sides) {
            TopoDS_Edge base;
            if (!edgeAtBottom(TopoDS::Face(faces(static_cast<int>(i) + 1)), base))
                continue;
            int n = static_cast<int>(std::floor(angleOf(middleOf(base)) / step)) + 1;
            n = std::max(1, std::min(n, static_cast<int>(sides.size())));
            faceNames[i] = "Side" + std::to_string(n);
        }
    }

    void plane()
    {
        for (auto& name : faceNames)
            name = "Plane";
        for (std::size_t i = 0; i < edgeNames.size(); ++i) {
            const gp_Pnt m = middleOf(TopoDS::Edge(edges(static_cast<int>(i) + 1)));
            if (near(m.Y(), low.Y()))
                edgeNames[i] = "Front";
            else if (near(m.Y(), high.Y()))
                edgeNames[i] = "Rear";
            else if (near(m.X(), low.X()))
                edgeNames[i] = "Left";
            else if (near(m.X(), high.X()))
                edgeNames[i] = "Right";
        }
        // A corner by the two edges: the one face says nothing of it
        for (std::size_t i = 0; i < vertexNames.size(); ++i)
            vertexNames[i] = joined(namesAround(edgesOfVertex, vertices(static_cast<int>(i) + 1),
                                                edges, edgeNames));
    }

    /// One edge, by what it is, and its two ends
    void curve(const char* name)
    {
        for (std::size_t i = 0; i < edgeNames.size(); ++i) {
            edgeNames[i] = name;
            TopoDS_Vertex first, last;
            TopExp::Vertices(TopoDS::Edge(edges(static_cast<int>(i) + 1)), first, last, true);
            give(first, "Start");
            if (!last.IsNull() && !last.IsSame(first))
                give(last, "End");
        }
    }

    /// Edges one after the other, a helix as it is cut into lengths
    void chain()
    {
        // In the order they were made, which is the order they are laid in
        TopoDS_Vertex end;
        for (std::size_t i = 0; i < edgeNames.size(); ++i) {
            edgeNames[i] = "Segment" + std::to_string(i + 1);
            TopoDS_Vertex first, last;
            TopExp::Vertices(TopoDS::Edge(edges(static_cast<int>(i) + 1)), first, last, true);
            give(first, i == 0 ? std::string("Start") : "Joint" + std::to_string(i));
            end = last;
        }
        if (!end.IsNull()) {
            const int at = vertices.FindIndex(end);
            if (at > 0)
                vertexNames[static_cast<std::size_t>(at) - 1] = "End";
        }
    }

    /// A closed run of straight edges about Z, the first from the corner on +X
    void polygon()
    {
        const std::size_t count = edgeNames.size();
        if (count == 0)
            return;
        const double step = 2.0 * M_PI / static_cast<double>(count);
        for (std::size_t i = 0; i < count; ++i) {
            const gp_Pnt m = middleOf(TopoDS::Edge(edges(static_cast<int>(i) + 1)));
            int n = static_cast<int>(std::floor(angleOf(m) / step)) + 1;
            n = std::max(1, std::min(n, static_cast<int>(count)));
            edgeNames[i] = "Side" + std::to_string(n);
        }
        for (std::size_t i = 0; i < vertexNames.size(); ++i) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vertices(static_cast<int>(i) + 1)));
            const int n = static_cast<int>(std::lround(angleOf(p) / step)) % static_cast<int>(count);
            vertexNames[i] = "Corner" + std::to_string(n + 1);
        }
    }

    void point()
    {
        for (auto& name : vertexNames)
            name = "Point";
    }

    /// What has no name yet is named by what it lies in, and no two alike
    void finish(TopoShape& target)
    {
        for (auto& name : faceNames) {
            if (name.empty())
                name = "Other";
        }
        // The faces told apart before anything is named by them
        apart();
        for (std::size_t i = 0; i < edgeNames.size(); ++i) {
            if (!edgeNames[i].empty())
                continue;
            const TopoDS_Edge edge = TopoDS::Edge(edges(static_cast<int>(i) + 1));
            const auto around = namesAround(facesOfEdge, edge, faces, faceNames);
            std::string name = joined(around);
            if (name.empty())
                name = "Other";
            else if (name.find('_') != std::string::npos)
                ;   // between two faces
            else if (BRep_Tool::Degenerated(edge))
                name += below(middleOf(edge)) ? "_BottomPole" : "_TopPole";
            else
                name += "_Seam";
            edgeNames[i] = name;
        }
        apart();
        for (std::size_t i = 0; i < vertexNames.size(); ++i) {
            if (!vertexNames[i].empty())
                continue;
            const TopoDS_Shape& vertex = vertices(static_cast<int>(i) + 1);
            std::string name = joined(namesAround(facesOfVertex, vertex, faces, faceNames));
            if (!name.empty())
                name += "_Corner";
            else
                name = joined(namesAround(edgesOfVertex, vertex, edges, edgeNames));
            vertexNames[i] = name.empty() ? "Other" : name;
        }
        apart();

        target.resetElementMap();
        write(target, "Face", faceNames);
        write(target, "Edge", edgeNames);
        write(target, "Vertex", vertexNames);
    }

private:
    void give(const TopoDS_Vertex& vertex, const std::string& name)
    {
        const int at = vertex.IsNull() ? 0 : vertices.FindIndex(vertex);
        if (at > 0 && vertexNames[static_cast<std::size_t>(at) - 1].empty())
            vertexNames[static_cast<std::size_t>(at) - 1] = name;
    }

    static std::vector<std::string>
    namesAround(const TopTools_IndexedDataMapOfShapeListOfShape& around, const TopoDS_Shape& of,
                const TopTools_IndexedMapOfShape& in, const std::vector<std::string>& names)
    {
        std::vector<std::string> res;
        const int at = around.FindIndex(of);
        if (at <= 0)
            return res;
        for (TopTools_ListOfShape::Iterator it(around.FindFromIndex(at)); it.More(); it.Next()) {
            const int index = in.FindIndex(it.Value());
            if (index > 0 && !names[static_cast<std::size_t>(index) - 1].empty())
                res.push_back(names[static_cast<std::size_t>(index) - 1]);
        }
        return res;
    }

    /// Whether a point is in the lower half of the shape
    bool below(const gp_Pnt& p) const
    {
        return p.Z() < 0.5 * (low.Z() + high.Z());
    }

    /// An edge of the face that lies on the floor of the shape
    bool edgeAtBottom(const TopoDS_Face& face, TopoDS_Edge& edge) const
    {
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            bool all = true;
            for (TopExp_Explorer vt(it.Current(), TopAbs_VERTEX); all && vt.More(); vt.Next())
                all = near(BRep_Tool::Pnt(TopoDS::Vertex(vt.Current())).Z(), low.Z());
            if (all) {
                edge = TopoDS::Edge(it.Current());
                return true;
            }
        }
        return false;
    }

    /** Whether a flat side is the one at the angle nought
     *
     * It has an edge whose middle is on the +X side of the XZ plane. Of
     * what was pushed along Z, and may have been pushed askew, only the
     * edge on the floor says so.
     */
    bool startsAtNought(const TopoDS_Face& face, bool pushed) const
    {
        if (pushed) {
            TopoDS_Edge base;
            if (!edgeAtBottom(face, base))
                return false;
            const gp_Pnt m = middleOf(base);
            return std::fabs(m.Y()) < Tol && m.X() > Tol;
        }
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            const gp_Pnt m = middleOf(TopoDS::Edge(it.Current()));
            if (std::fabs(m.Y()) < Tol && m.X() > Tol)
                return true;
        }
        return false;
    }

    /// Whether the face runs from the middle of a torus' tube outward, and
    /// at which angle of the tube, in degrees. The tube is a circle about
    /// +Y as TopoShape::makeTorus() lays it: from +X, turning toward -Z.
    static bool closesTube(const TopoDS_Face& face, const PrimitiveNames::Tube& tube, double& at)
    {
        if (tube.radius <= 0.0)
            return false;
        bool middle = false, found = false;
        for (TopExp_Explorer it(face, TopAbs_VERTEX); it.More(); it.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
            const double r = std::hypot(p.X(), p.Y()) - tube.radius;
            if (std::fabs(r) < Tol && std::fabs(p.Z()) < Tol) {
                middle = true;
            }
            else if (!found) {
                at = std::atan2(-p.Z(), r) * 180.0 / M_PI;
                found = true;
            }
        }
        return middle && found;
    }

    /// Whether the angle is nearer the first of two than the second, all in
    /// degrees and each the same a turn on
    static bool nearerAngle(double angle, double first, double second)
    {
        auto away = [angle](double other) {
            return std::fabs(std::remainder(angle - other, 360.0));
        };
        return away(first) <= away(second);
    }

    struct Member
    {
        std::string* name;
        double z, angle, radius;
        bool operator<(const Member& other) const
        {
            if (!near(z, other.z))
                return z < other.z;
            if (!near(angle, other.angle))
                return angle < other.angle;
            if (!near(radius, other.radius))
                return radius < other.radius;
            return false;
        }
    };

    static Member member(std::string& name, const gp_Pnt& p)
    {
        const double radius = std::hypot(p.X(), p.Y());
        return {&name, p.Z(), radius < Tol ? 0.0 : angleOf(p), radius};
    }

    /// Those with one name told apart: the second on numbered, by height,
    /// then by the angle about Z, then by the distance from it. Asked after
    /// the faces, the edges and the vertices are named, each in turn, so
    /// that what is named by two faces is named by what they ended as.
    void apart()
    {
        std::map<std::string, std::vector<Member>> byName;
        std::set<std::string> taken;
        for (std::size_t i = 0; i < faceNames.size(); ++i)
            byName[faceNames[i]].push_back(member(faceNames[i], facings[i].at));
        for (std::size_t i = 0; i < edgeNames.size(); ++i)
            byName[edgeNames[i]].push_back(
                member(edgeNames[i], middleOf(TopoDS::Edge(edges(static_cast<int>(i) + 1)))));
        for (std::size_t i = 0; i < vertexNames.size(); ++i)
            byName[vertexNames[i]].push_back(member(
                vertexNames[i], BRep_Tool::Pnt(TopoDS::Vertex(vertices(static_cast<int>(i) + 1)))));
        // What is still to be named is not a name two have
        byName.erase(std::string());
        for (const auto& v : byName)
            taken.insert(v.first);
        for (auto& v : byName) {
            if (v.second.size() < 2)
                continue;
            std::stable_sort(v.second.begin(), v.second.end());
            int n = 1;
            for (std::size_t i = 1; i < v.second.size(); ++i) {
                std::string name;
                // "Lateral_Lateral2" is two faces, and its second is not "...22"
                const char* sep = std::isdigit(static_cast<unsigned char>(v.first.back())) ? "_" : "";
                do {
                    name = v.first + sep + std::to_string(++n);
                } while (!taken.insert(name).second);
                *v.second[i].name = name;
            }
        }
    }

    static void write(TopoShape& target, const char* type, const std::vector<std::string>& names)
    {
        for (std::size_t i = 0; i < names.size(); ++i)
            target.setElementName(Data::IndexedName::fromConst(type, static_cast<int>(i) + 1),
                                  Data::MappedName(names[i]));
    }

    const TopoDS_Shape& shape;
    TopTools_IndexedMapOfShape faces, edges, vertices;
    TopTools_IndexedDataMapOfShapeListOfShape facesOfEdge, facesOfVertex, edgesOfVertex;
    std::vector<std::string> faceNames, edgeNames, vertexNames;
    std::vector<Facing> facings;
    gp_Pnt low, high;  ///< the corners of the box about the vertices
};

}  // namespace

void PrimitiveNames::apply(TopoShape& shape, Kind kind)
{
    apply(shape, kind, Tube());
}

TopoShape PrimitiveNames::named(const TopoDS_Shape& shape, Kind kind)
{
    return named(shape, kind, Tube());
}

void PrimitiveNames::apply(TopoShape& shape, Kind kind, const Tube& tube)
{
    if (shape.isNull())
        return;
    Namer namer(shape.getShape());
    switch (kind) {
    case Kind::Box:
    case Kind::Wedge:
        namer.hexahedron();
        break;
    case Kind::Cylinder:
    case Kind::Prism:
    case Kind::Cone:
    case Kind::Sphere:
    case Kind::Ellipsoid:
    case Kind::Torus:
        namer.axial(kind, tube);
        break;
    case Kind::Plane:
        namer.plane();
        break;
    case Kind::Line:
        namer.curve("Line");
        break;
    case Kind::Circle:
        namer.curve("Circle");
        break;
    case Kind::Ellipse:
        namer.curve("Ellipse");
        break;
    case Kind::Helix:
    case Kind::Spiral:
        namer.chain();
        break;
    case Kind::RegularPolygon:
        namer.polygon();
        break;
    case Kind::Vertex:
        namer.point();
        break;
    }
    namer.finish(shape);
}

TopoShape PrimitiveNames::named(const TopoDS_Shape& shape, Kind kind, const Tube& tube)
{
    TopoShape res(0);
    res.setShape(shape);
    apply(res, kind, tube);
    return res;
}

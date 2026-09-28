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
#include <cstdlib>
#include <list>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#endif

#include <App/DocumentObject.h>
#include <App/ElementNamingUtils.h>
#include <Base/Axis.h>
#include <Base/Exception.h>

#include "Part2DObject.h"
#include "PartFeature.h"
#include "PatternResolver.h"
#include "Tools.h"

using namespace Part;

namespace
{

Base::Vector3d toVector(const gp_XYZ& xyz)
{
    return Base::Vector3d(xyz.X(), xyz.Y(), xyz.Z());
}

/// An axis of a sketch -- H_Axis, V_Axis, N_Axis, AxisN, or the sketch as a
/// whole for its normal -- in the frame of the referenced object's parent
bool getSketchAxis(App::DocumentObject* obj, const std::string& sub, App::Pattern::Axis& res)
{
    const char* element = Data::findElementName(sub.c_str());
    std::string path(sub.c_str(), element);
    Base::Matrix4D mat;
    auto sketch = freecad_cast<Part2DObject*>(obj->getSubObject(path.c_str(), nullptr, &mat, true));
    if (!sketch) {
        return false;
    }

    std::string name(element);
    Base::Axis axis;
    if (name.empty() || name == "N_Axis") {
        axis = sketch->getAxis(Part2DObject::N_Axis);
    }
    else if (name == "H_Axis") {
        axis = sketch->getAxis(Part2DObject::H_Axis);
    }
    else if (name == "V_Axis") {
        axis = sketch->getAxis(Part2DObject::V_Axis);
    }
    else if (name.compare(0, 4, "Axis") == 0) {
        int index = std::atoi(name.c_str() + 4);
        if (index < 0 || index >= sketch->getAxisCount()) {
            throw Base::ValueError("Sketch axis not found: " + name);
        }
        axis = sketch->getAxis(index);
    }
    else {
        return false;
    }
    res.base = mat * axis.getBase();
    res.direction = mat * axis.getDirection() - mat * Base::Vector3d();
    return true;
}

TopoShape getShape(App::DocumentObject* obj, const std::string& sub)
{
    return Feature::getTopoShape(obj, sub.c_str(), true);
}

/// The one face, or else the one edge, a reference names
TopoDS_Shape getElement(const TopoShape& shape, bool faces)
{
    const TopoDS_Shape& s = shape.getShape();
    if (s.ShapeType() == TopAbs_EDGE || (faces && s.ShapeType() == TopAbs_FACE)) {
        return s;
    }
    if (faces && shape.countSubShapes(TopAbs_FACE) == 1) {
        return shape.getSubShape(TopAbs_FACE, 1);
    }
    if (shape.countSubShapes(TopAbs_FACE) == 0 && shape.countSubShapes(TopAbs_EDGE) == 1) {
        return shape.getSubShape(TopAbs_EDGE, 1);
    }
    return {};
}

bool verticesCoincide(const TopoDS_Vertex& first, const TopoDS_Vertex& second)
{
    return BRep_Tool::Pnt(first).Distance(BRep_Tool::Pnt(second)) <= Precision::Confusion();
}

double edgeLength(const TopoDS_Edge& edge)
{
    BRepAdaptor_Curve curve(edge);
    return GCPnts_AbscissaPoint::Length(curve,
                                        curve.FirstParameter(),
                                        curve.LastParameter(),
                                        Precision::Confusion());
}

/// The edges of a path chained end to end, as upstream's path pattern does
std::vector<TopoDS_Edge> chainEdges(std::list<TopoDS_Edge> edges)
{
    std::vector<TopoDS_Edge> result;
    result.reserve(edges.size());
    result.push_back(edges.front());
    edges.pop_front();

    while (!edges.empty()) {
        TopoDS_Vertex currentFirst, currentLast, ignored;
        TopExp::Vertices(result.front(), currentFirst, ignored, true);
        TopExp::Vertices(result.back(), ignored, currentLast, true);

        bool connected = false;
        for (auto it = edges.begin(); it != edges.end(); ++it) {
            TopoDS_Vertex candidateFirst, candidateLast;
            TopExp::Vertices(*it, candidateFirst, candidateLast, true);
            if (verticesCoincide(currentLast, candidateFirst)) {
                result.push_back(*it);
            }
            else if (verticesCoincide(currentLast, candidateLast)) {
                result.push_back(TopoDS::Edge(it->Reversed()));
            }
            else if (verticesCoincide(currentFirst, candidateLast)) {
                result.insert(result.begin(), *it);
            }
            else if (verticesCoincide(currentFirst, candidateFirst)) {
                result.insert(result.begin(), TopoDS::Edge(it->Reversed()));
            }
            else {
                continue;
            }
            edges.erase(it);
            connected = true;
            break;
        }
        if (!connected) {
            throw Base::ValueError("Path edges must form one connected path");
        }
    }
    return result;
}

class ShapePath: public App::Pattern::Path
{
public:
    explicit ShapePath(std::vector<TopoDS_Edge> edges)
        : edges(std::move(edges))
    {
        ends.reserve(this->edges.size());
        double total = 0.0;
        for (const auto& edge : this->edges) {
            total += edgeLength(edge);
            ends.push_back(total);
        }
        TopoDS_Vertex first, last, ignored;
        TopExp::Vertices(this->edges.front(), first, ignored, true);
        TopExp::Vertices(this->edges.back(), ignored, last, true);
        closed = !first.IsNull() && !last.IsNull() && verticesCoincide(first, last);
    }

    double length() const override
    {
        return ends.back();
    }

    bool isClosed() const override
    {
        return closed;
    }

    void evaluate(double distance, Base::Vector3d& position, Base::Vector3d& tangent) const override
    {
        auto end = std::lower_bound(ends.begin(), ends.end(), distance - Precision::Confusion());
        const std::size_t index = end == ends.end() ? edges.size() - 1
                                                    : static_cast<std::size_t>(end - ends.begin());
        const double edgeStart = index == 0 ? 0.0 : ends[index - 1];
        const double localDistance =
            std::clamp(distance - edgeStart, 0.0, ends[index] - edgeStart);

        // From the oriented start of the edge
        BRepAdaptor_Curve curve(edges[index]);
        const bool reversed = edges[index].Orientation() == TopAbs_REVERSED;
        const double startParameter = reversed ? curve.LastParameter() : curve.FirstParameter();
        double parameter = startParameter;
        if (localDistance > Precision::Confusion()) {
            GCPnts_AbscissaPoint point(curve,
                                       reversed ? -localDistance : localDistance,
                                       startParameter);
            if (!point.IsDone()) {
                throw Base::CADKernelError("Failed to evaluate point on path");
            }
            parameter = point.Parameter();
        }

        gp_Pnt pnt;
        gp_Vec vec;
        curve.D1(parameter, pnt, vec);
        if (reversed) {
            vec.Reverse();
        }
        position = toVector(pnt.XYZ());
        tangent = toVector(vec.XYZ());
    }

private:
    std::vector<TopoDS_Edge> edges;
    std::vector<double> ends;
    bool closed = false;
};

}  // namespace

bool PatternResolver::getDirection(App::DocumentObject* obj,
                                   const std::string& sub,
                                   Base::Vector3d& dir) const
{
    App::Pattern::Axis axis;
    if (getSketchAxis(obj, sub, axis)) {
        dir = axis.direction;
        return true;
    }

    TopoShape shape = getShape(obj, sub);
    if (shape.isNull()) {
        return false;
    }
    TopoDS_Shape element = getElement(shape, true);
    if (element.IsNull()) {
        throw Base::TypeError("Direction reference must be edge or face");
    }
    if (element.ShapeType() == TopAbs_FACE) {
        BRepAdaptor_Surface adapt(TopoDS::Face(element));
        if (adapt.GetType() != GeomAbs_Plane) {
            throw Base::TypeError("Direction face must be planar");
        }
        dir = toVector(adapt.Plane().Axis().Direction().XYZ());
        return true;
    }
    BRepAdaptor_Curve adapt(TopoDS::Edge(element));
    if (adapt.GetType() != GeomAbs_Line) {
        throw Base::TypeError("Direction edge must be a straight line");
    }
    dir = toVector(adapt.Line().Direction().XYZ());
    return true;
}

bool PatternResolver::getAxis(App::DocumentObject* obj,
                              const std::string& sub,
                              App::Pattern::Axis& axis) const
{
    if (getSketchAxis(obj, sub, axis)) {
        return true;
    }

    TopoShape shape = getShape(obj, sub);
    if (shape.isNull()) {
        return false;
    }
    TopoDS_Shape element = getElement(shape, false);
    if (element.IsNull()) {
        throw Base::TypeError("Axis reference must be an edge");
    }
    BRepAdaptor_Curve adapt(TopoDS::Edge(element));
    if (adapt.GetType() == GeomAbs_Line) {
        axis.base = toVector(adapt.Line().Location().XYZ());
        axis.direction = toVector(adapt.Line().Direction().XYZ());
    }
    else if (adapt.GetType() == GeomAbs_Circle) {
        axis.base = toVector(adapt.Circle().Location().XYZ());
        axis.direction = toVector(adapt.Circle().Axis().Direction().XYZ());
    }
    else {
        throw Base::TypeError("Rotation edge must be a straight line, circle or arc of circle");
    }
    return true;
}

std::unique_ptr<App::Pattern::Path>
PatternResolver::getPath(App::DocumentObject* obj, const std::vector<std::string>& subs) const
{
    std::list<TopoDS_Edge> edges;
    bool hasElement = false;
    for (const auto& sub : subs) {
        if (sub.empty()) {
            continue;
        }
        hasElement = true;
        TopoShape shape = getShape(obj, sub);
        if (shape.isNull()) {
            return {};
        }
        if (shape.getShape().ShapeType() != TopAbs_EDGE) {
            throw Base::TypeError("Path subelements must be edges");
        }
        edges.push_back(TopoDS::Edge(shape.getShape()));
    }

    if (!hasElement) {
        TopoShape shape = getShape(obj, std::string());
        if (shape.isNull()) {
            return {};
        }
        // The first wire, in its order, else all the edges
        TopExp_Explorer wires(shape.getShape(), TopAbs_WIRE);
        if (wires.More()) {
            for (BRepTools_WireExplorer it(TopoDS::Wire(wires.Current())); it.More(); it.Next()) {
                edges.push_back(it.Current());
            }
        }
        else {
            for (TopExp_Explorer it(shape.getShape(), TopAbs_EDGE); it.More(); it.Next()) {
                edges.push_back(TopoDS::Edge(it.Current()));
            }
        }
    }

    if (edges.empty()) {
        throw Base::ValueError("Path does not contain edges");
    }
    return std::make_unique<ShapePath>(chainEdges(std::move(edges)));
}

bool PatternResolver::getPoints(App::DocumentObject* obj,
                                const std::vector<std::string>& subs,
                                std::vector<Base::Vector3d>& points) const
{
    std::vector<TopoShape> shapes;
    for (const auto& sub : subs.empty() ? std::vector<std::string>(1) : subs) {
        TopoShape shape = getShape(obj, sub);
        if (shape.isNull()) {
            return false;
        }
        shapes.push_back(shape);
    }
    for (const auto& shape : shapes) {
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(shape.getShape(), TopAbs_VERTEX, vertices);
        for (int i = 1; i <= vertices.Extent(); ++i) {
            points.push_back(toVector(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))).XYZ()));
        }
    }
    return true;
}

void PatternResolver::init()
{
    static bool done = false;
    if (!done) {
        done = true;
        App::Pattern::addResolver(std::make_shared<PatternResolver>());
    }
}

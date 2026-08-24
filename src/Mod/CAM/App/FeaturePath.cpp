// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2014 Yorik van Havre <yorik@uncreated.net>              *
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


#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <App/Document.h>
#include <App/DocumentObjectPy.h>
#include <Base/Console.h>
#include <Base/Placement.h>

#include "FeaturePath.h"
#include "PathSegmentWalker.h"

FC_LOG_LEVEL_INIT("Path", true, true)

using namespace Path;

PROPERTY_SOURCE(Path::Feature, Part::Feature)


Feature::Feature()
{
    ADD_PROPERTY_TYPE(Path, (Path::Toolpath()), "Base", App::Prop_None, "The path data of this feature");
    ADD_PROPERTY_TYPE(BuildShape,
                      (false),
                      "Base",
                      App::Prop_None,
                      "Whether to build shape from tool path");
    ADD_PROPERTY_TYPE(CommandFilter,
                      (),
                      "Base",
                      App::Prop_None,
                      "Exclude paths from a list of commands when building shape");
}

Feature::~Feature()
{}

short Feature::mustExecute() const
{
    return inherited::mustExecute();
}

PyObject* Feature::getPyObject()
{
    // Part::Feature already hands out the right Python type for a shape-owning
    // feature; the plain DocumentObjectPy this used to make would hide Shape.
    return inherited::getPyObject();
}

void Feature::onChanged(const App::Property* prop)
{
    // Rebuilding the shape while the document is restoring, or replaying a
    // transaction, would fight whatever is being restored into these same
    // properties. User1 marks a change this function made itself.
    if (getDocument() && !getDocument()->testStatus(App::Document::Restoring)
        && !getDocument()->isPerformingTransaction()) {
        if (!prop->testStatus(App::Property::User1)) {
            if (prop == &Path || prop == &BuildShape || prop == &CommandFilter) {
                if (BuildShape.getValue()) {
                    auto pla = Placement.getValue();
                    Part::TopoShape shape;
                    std::set<int> filter;
                    for (int idx : CommandFilter.getValues()) {
                        filter.insert(idx);
                    }
                    try {
                        shape = shapeFromPath(Path.getValue(), filter);
                    }
                    catch (Standard_Failure& e) {
                        FC_ERR("Failed to obtain shape: " << e.GetMessageString());
                    }
                    catch (Base::Exception& e) {
                        FC_ERR("Failed to obtain shape: " << e.what());
                    }
                    shape.setPlacement(pla);
                    Shape.setValue(shape);
                }
                else {
                    Shape.setValue(Part::TopoShape());
                }
            }
        }
    }
    inherited::onChanged(prop);
}

namespace Path
{

/// Collects the points a toolpath walk visits, remembering which command each
/// run of points came from and whether it was a cutting move.
class ShapePathSegmentVisitor: public PathSegmentVisitor
{
public:
    ShapePathSegmentVisitor() = default;

    void setup(const Base::Vector3d& last) override
    {
        points.push_back(last);
    }

    void g0(int id,
            const Base::Vector3d& last,
            const Base::Vector3d& next,
            const std::deque<Base::Vector3d>& pts) override
    {
        (void)last;
        gx(id, &next, pts, 0);
    }

    void g1(int id,
            const Base::Vector3d& last,
            const Base::Vector3d& next,
            const std::deque<Base::Vector3d>& pts) override
    {
        (void)last;
        gx(id, &next, pts, 1);
    }

    void g23(int id,
             const Base::Vector3d& last,
             const Base::Vector3d& next,
             const std::deque<Base::Vector3d>& pts,
             const Base::Vector3d& center) override
    {
        (void)last;
        (void)center;
        gx(id, &next, pts, 1);
    }

    void g8x(int id,
             const Base::Vector3d& last,
             const Base::Vector3d& next,
             const std::deque<Base::Vector3d>& pts,
             const std::deque<Base::Vector3d>& p,
             const std::deque<Base::Vector3d>& q) override
    {
        (void)last;
        (void)q;

        gx(id, nullptr, pts, 0);

        points.push_back(p[0]);
        colorindex.push_back(0);

        points.push_back(p[1]);
        colorindex.push_back(0);

        points.push_back(next);
        colorindex.push_back(1);

        points.push_back(p[2]);
        colorindex.push_back(0);

        pushCommand(id);
    }

    void g38(int id, const Base::Vector3d& last, const Base::Vector3d& next) override
    {
        Base::Vector3d p1(next.x, next.y, last.z);
        points.push_back(p1);
        colorindex.push_back(0);

        points.push_back(next);
        colorindex.push_back(2);

        Base::Vector3d p3(next.x, next.y, last.z);
        points.push_back(p3);
        colorindex.push_back(0);

        pushCommand(id);
    }

    std::vector<int> edge2Command;
    std::vector<int> edgeIndices;

    std::vector<int> colorindex;
    std::vector<Base::Vector3d> points;

    virtual void gx(int id, const Base::Vector3d* next, const std::deque<Base::Vector3d>& pts, int color)
    {
        for (const auto& pt : pts) {
            points.push_back(pt);
            colorindex.push_back(color);
        }

        if (next) {
            points.push_back(*next);
            colorindex.push_back(color);

            pushCommand(id);
        }
    }

    void pushCommand(int id)
    {
        edgeIndices.push_back(points.size());
        edge2Command.push_back(id);
    }
};

Part::TopoShape shapeFromPath(const Toolpath& toolPath, const std::set<int>& filter)
{
    ShapePathSegmentVisitor visitor;
    PathSegmentWalker segments(toolPath);
    segments.walk(visitor, Base::Vector3d());
    int last = 0;

    const auto& points = visitor.points;

    BRep_Builder builder;
    TopoDS_Compound comp;
    builder.MakeCompound(comp);

    std::unique_ptr<BRepBuilderAPI_MakeWire> mkWire;
    auto flush = [&]() {
        if (mkWire && mkWire->IsDone()) {
            builder.Add(comp, mkWire->Wire());
        }
        mkWire.reset();
    };

    int k = -1;
    for (int idx : visitor.edgeIndices) {
        ++k;
        // Skip the filtered commands, and every non-cutting move: a wire is
        // only wanted where the tool is actually in the material.
        if (filter.count(visitor.edge2Command[k] + 1)
            || visitor.colorindex[last ? last - 1 : last] != 1) {
            flush();
            last = idx;
            continue;
        }
        if (!mkWire) {
            mkWire = std::make_unique<BRepBuilderAPI_MakeWire>();
        }
        for (int i = last ? last : 1; i < idx; ++i) {
            gp_Pnt p1(points[i - 1].x, points[i - 1].y, points[i - 1].z);
            gp_Pnt p2(points[i].x, points[i].y, points[i].z);
            if (p1.SquareDistance(p2) > Precision::SquareConfusion()) {
                mkWire->Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
            }
        }
        last = idx;
    }
    flush();
    return comp;
}

}  // namespace Path

// Python Path feature ---------------------------------------------------------

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Path::FeaturePython, Path::Feature)
template<>
const char* Path::FeaturePython::getViewProviderName() const
{
    return "PathGui::ViewProviderPathPython";
}
/// @endcond

// explicit template instantiation
template class PathExport FeaturePythonT<Path::Feature>;
}  // namespace App

/***************************************************************************
 *   Copyright (c) 2017 Zheng, Lei <realthunder.dev@gmail.com>             *
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

// Turning a set of wires into G-code. This used to be Area::toPath, sitting in
// the middle of the 2D area engine, which is why that engine could not be used
// without the CAM module behind it. The geometry now lives in Mod/Area and
// knows nothing about toolpaths; emitting them is this file's job, and it is
// the only part of the old Area.cpp that needs Path::Toolpath and
// Path::Command.

#include <cmath>

#include <BRepAdaptor_Curve.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <Base/Console.h>
#include <Base/Vector3D.h>
#include <libarea/Area.h>

#include <Base/BoundBox.h>
#include <Mod/Area/App/Area.h>

#include "AreaToolpath.h"
#include "Command.h"
#include "Path.h"
#include "PathSegmentWalker.h"

// The area engine's logging macros, which the moved code still uses.
#define AREA_LOG FC_LOG
#define AREA_WARN FC_WARN
#define AREA_ERR FC_ERR
#define AREA_TRACE FC_TRACE
#define AREA_XYZ FC_XYZ

using namespace Path;

FC_LOG_LEVEL_INIT("Path.AreaToolpath", true, true)

// Lifted with the code below: the area engine spells a gp_Pnt accessor this way.
typedef Standard_Real (gp_Pnt::*AxisGetter)() const;
typedef void (gp_Pnt::*AxisSetter)(Standard_Real);

// The parameter enums are declared inside AreaLib::Area; name the ones used here.
static const auto ArcPlaneNone = AreaLib::Area::ArcPlaneNone;
static const auto ArcPlaneVariable = AreaLib::Area::ArcPlaneVariable;
static const auto ArcPlaneXY = AreaLib::Area::ArcPlaneXY;
static const auto ArcPlaneYZ = AreaLib::Area::ArcPlaneYZ;
static const auto ArcPlaneZX = AreaLib::Area::ArcPlaneZX;
static const auto RetractAxisX = AreaLib::Area::RetractAxisX;
static const auto RetractAxisY = AreaLib::Area::RetractAxisY;

using AreaLib::discretize;

static inline void addParameter(bool verbose, Command& cmd, const char* name,
    double last, double next, bool relative = false)
{
    double d = next - last;
    if (verbose || fabs(d) > Precision::Confusion())
        cmd.Parameters[name] = relative ? d : next;
}

static inline void addGCode(bool verbose, Toolpath& path, const gp_Pnt& last,
    const gp_Pnt& next, const char* name)
{
    Command cmd;
    cmd.Name = name;
    addParameter(verbose, cmd, "X", last.X(), next.X());
    addParameter(verbose, cmd, "Y", last.Y(), next.Y());
    addParameter(verbose, cmd, "Z", last.Z(), next.Z());
    path.addCommand(cmd);
    return;
}

static inline void addG1(bool verbose, Toolpath& path, const gp_Pnt& last,
    const gp_Pnt& next, double f, double& last_f)
{
    addGCode(verbose, path, last, next, "G1");
    if (f > Precision::Confusion()) {
        Command& cmd = path.getCommand(path.getSize() - 1);
        addParameter(verbose, cmd, "F", last_f, f);
        last_f = f;
    }
    return;
}

static void addG0(bool verbose, Toolpath& path,
    gp_Pnt last, const gp_Pnt& next,
    AxisSetter setter, double height)
{
    gp_Pnt pt(next);
    (pt.*setter)(height);
    if (!last.IsEqual(pt, Precision::Confusion())) {
        addGCode(verbose, path, last, pt, "G0");
    }
}

static void addGArc(bool verbose, bool abs_center, Toolpath& path,
    const gp_Pnt& pstart, const gp_Pnt& pend, const gp_Pnt& center,
    bool clockwise, double f, double& last_f)
{
    Command cmd;
    cmd.Name = clockwise ? "G2" : "G3";
    if (abs_center) {
        addParameter(verbose, cmd, "I", 0.0, center.X());
        addParameter(verbose, cmd, "J", 0.0, center.Y());
        addParameter(verbose, cmd, "K", 0.0, center.Z());
    }
    else {
        addParameter(verbose, cmd, "I", pstart.X(), center.X(), true);
        addParameter(verbose, cmd, "J", pstart.Y(), center.Y(), true);
        addParameter(verbose, cmd, "K", pstart.Z(), center.Z(), true);
    }
    addParameter(verbose, cmd, "X", pstart.X(), pend.X());
    addParameter(verbose, cmd, "Y", pstart.Y(), pend.Y());
    addParameter(verbose, cmd, "Z", pstart.Z(), pend.Z());
    if (f > Precision::Confusion()) {
        addParameter(verbose, cmd, "F", last_f, f);
        last_f = f;
    }
    path.addCommand(cmd);
}

static inline void addGCode(Toolpath& path, const char* name) {
    Command cmd;
    cmd.Name = name;
    path.addCommand(cmd);
}

void Path::areaToPath(Toolpath& path, const std::list<TopoDS_Shape>& shapes,
    const gp_Pnt* _pstart, gp_Pnt* pend, PARAM_ARGS(PARAM_FARG, AREA_PARAMS_PATH))
{
    std::list<TopoDS_Shape> wires;

    gp_Pnt pstart;
    if (_pstart) pstart = *_pstart;

    double stepdown_hint = 1.0;
    wires = Area::sortWires(shapes, _pstart != nullptr, &pstart, pend, &stepdown_hint,
        PARAM_REF(PARAM_FARG, AREA_PARAMS_ARC_PLANE),
        PARAM_FIELDS(PARAM_FARG, AREA_PARAMS_SORT));

    if (wires.empty())
        return;

    short currentArcPlane = arc_plane;
    if (preamble) {
        // absolute mode
        addGCode(path, "G90");
        if (abs_center)
            addGCode(path, "G90.1"); // absolute center for arc move

        if (arc_plane == ArcPlaneZX)
            addGCode(path, "G18");
        else if (arc_plane == ArcPlaneYZ)
            addGCode(path, "G19");
        else {
            currentArcPlane = ArcPlaneXY;
            addGCode(path, "G17");
        }
    }

    AxisGetter getter;
    AxisSetter setter;
    switch (retract_axis) {
    case RetractAxisX:
        getter = &gp_Pnt::X;
        setter = &gp_Pnt::SetX;
        break;
    case RetractAxisY:
        getter = &gp_Pnt::Y;
        setter = &gp_Pnt::SetY;
        break;
    default:
        getter = &gp_Pnt::Z;
        setter = &gp_Pnt::SetZ;
    }

    threshold = fabs(threshold);
    if (threshold < Precision::Confusion())
        threshold = Precision::Confusion();
    threshold *= threshold;

    // in case the user didn't specify feed start, sortWire() will choose one
    // based on the bound. We'll further adjust that according to resume height
    if (!_pstart || pstart.SquareDistance(*_pstart) > Precision::SquareConfusion())
        (pstart.*setter)(resume_height);

    gp_Pnt plast, p;
    // initial vertical rapid pull up to retraction (or start Z height if higher)
    (p.*setter)(std::max(retraction, (pstart.*getter)()));
    addGCode(false, path, plast, p, "G0");
    plast = p;
    p = pstart;

    // rapid horizontal move to start point
    gp_Pnt tmpPlast = plast;
    (tmpPlast.*setter)((p.*getter)());
    if (_pstart && p.IsEqual(tmpPlast, Precision::Confusion())) {
        plast.SetCoord(10.0, 10.0, 10.0);
        (plast.*setter)(retraction);
    }
    (p.*setter)(retraction);
    addGCode(false, path, plast, p, "G0");


    plast = p;
    bool first = true;
    bool arcWarned = false;
    double cur_f = 0.0; // current feed rate
    double nf = fabs(feedrate); // user specified normal move feed rate
    double vf = fabs(feedrate_v); // user specified vertical move feed rate
    if (vf < Precision::Confusion()) vf = nf;

    for (const TopoDS_Shape& wire : wires) {

        BRepTools_WireExplorer xp(TopoDS::Wire(wire));
        p = BRep_Tool::Pnt(xp.CurrentVertex());

        gp_Pnt pTmp(p), plastTmp(plast);
        // Assuming the stepdown direction is the same as retraction direction.
        // We don't want to count step down distance in stepdown direction,
        // because it is always safe to go in that direction in feed move
        // without getting bumped.
        (pTmp.*setter)(0.0);
        (plastTmp.*setter)(0.0);

        if (first) {
            // G0 to initial at retraction to handle if start point was set
            addG0(false, path, plast, p, setter, retraction);
            // rapid to plunge height
            addG0(false, path, plast, p, setter, resume_height);
        }
        else if (pTmp.SquareDistance(plastTmp) > threshold) {
            // raise to retraction height
            addG0(false, path, plast, plast, setter, retraction);
            // move to new location
            addG0(false, path, plast, p, setter, retraction);
            // lower to plunge height
            addG0(false, path, plast, p, setter, resume_height);
        }
        addG1(verbose, path, plast, p, vf, cur_f);
        plast = p;
        first = false;
        for (; xp.More(); xp.Next(), plast = p) {
            const auto& edge = xp.Current();
            BRepAdaptor_Curve curve(edge);
            bool reversed = (edge.Orientation() == TopAbs_REVERSED);
            p = curve.Value(reversed ? curve.FirstParameter() : curve.LastParameter());

            switch (curve.GetType()) {
            case GeomAbs_Line: {
                if (segmentation > Precision::Confusion()) {
                    GCPnts_UniformAbscissa discretizer(curve, segmentation,
                        curve.FirstParameter(), curve.LastParameter());
                    if (discretizer.IsDone() && discretizer.NbPoints() > 2) {
                        int nbPoints = discretizer.NbPoints();
                        if (reversed) {
                            for (int i = nbPoints - 1; i >= 1; --i) {
                                gp_Pnt pt = curve.Value(discretizer.Parameter(i));
                                addG1(verbose, path, plast, pt, nf, cur_f);
                                plast = pt;
                            }
                        }
                        else {
                            for (int i = 2; i <= nbPoints; i++) {
                                gp_Pnt pt = curve.Value(discretizer.Parameter(i));
                                addG1(verbose, path, plast, pt, nf, cur_f);
                                plast = pt;
                            }
                        }
                        break;
                    }
                }
                addG1(verbose, path, plast, p, nf, cur_f);
                break;
            } case GeomAbs_Circle: {
                const gp_Circ& circle = curve.Circle();
                const gp_Dir& dir = circle.Axis().Direction();
                short arcPlane = ArcPlaneNone;
                bool clockwise;
                const char* cmd;
                if (fabs(dir.X()) < Precision::Confusion() &&
                    fabs(dir.Y()) < Precision::Confusion()) {
                    clockwise = dir.Z() < 0;
                    arcPlane = ArcPlaneXY;
                    cmd = "G17";
                }
                else if (fabs(dir.Z()) < Precision::Confusion() &&
                    fabs(dir.X()) < Precision::Confusion()) {
                    clockwise = dir.Y() < 0;
                    arcPlane = ArcPlaneZX;
                    cmd = "G18";
                }
                else if (fabs(dir.Y()) < Precision::Confusion() &&
                    fabs(dir.Z()) < Precision::Confusion()) {
                    clockwise = dir.X() < 0;
                    arcPlane = ArcPlaneYZ;
                    cmd = "G19";
                }

                if (arcPlane != ArcPlaneNone &&
                    (arcPlane == currentArcPlane || arc_plane == ArcPlaneVariable))
                {
                    if (arcPlane != currentArcPlane)
                        addGCode(path, cmd);

                    if (reversed) clockwise = !clockwise;
                    gp_Pnt center = circle.Location();

                    double first = curve.FirstParameter();
                    double last = curve.LastParameter();
                    if (segmentation > Precision::Confusion()) {
                        GCPnts_UniformAbscissa discretizer(curve, segmentation, first, last);
                        if (discretizer.IsDone() && discretizer.NbPoints() > 2) {
                            int nbPoints = discretizer.NbPoints();
                            if (reversed) {
                                for (int i = nbPoints - 1; i >= 1; --i) {
                                    gp_Pnt pt = curve.Value(discretizer.Parameter(i));
                                    addGArc(verbose, abs_center, path, plast, pt, center, clockwise, nf, cur_f);
                                    plast = pt;
                                }
                            }
                            else {
                                for (int i = 2; i <= nbPoints; i++) {
                                    gp_Pnt pt = curve.Value(discretizer.Parameter(i));
                                    addGArc(verbose, abs_center, path, plast, pt, center, clockwise, nf, cur_f);
                                    plast = pt;
                                }
                            }
                            break;
                        }
                    }

                    if (fabs(first - last) > M_PI) {
                        // Split arc(circle) larger than half circle.
                        gp_Pnt mid = curve.Value((last - first) * 0.5 + first);
                        addGArc(verbose, abs_center, path, plast, mid, center, clockwise, nf, cur_f);
                        plast = mid;
                    }
                    addGArc(verbose, abs_center, path, plast, p, center, clockwise, nf, cur_f);
                    break;
                }

                if (!arcWarned) {
                    arcWarned = true;
                    AREA_WARN("arc plane not aligned, force discretization");
                }
                AREA_TRACE("arc discretize " << AREA_XYZ(dir));
            }
                             /* FALLTHRU */
            default: {
                const auto& pts = discretize(edge, deflection);
                for (size_t i = 1; i < pts.size(); ++i) {
                    auto& pt = pts[i];
                    addG1(verbose, path, plast, pt, nf, cur_f);
                    plast = pt;
                }
            }
            }
        }
    }
}

namespace
{

/// Walks a toolpath and accumulates the region the tool has swept, as the
/// union of a thickened polyline along every cutting move and a disc at every
/// drilled point. Only libarea and Base types are involved: the area engine
/// itself never learns about toolpaths.
class ClearedAreaSegmentVisitor: public Path::PathSegmentVisitor
{
private:
    CArea pathSegments;
    CArea holes;
    double maxZ;
    double radius;
    Base::BoundBox3d bbox;

    void point(const Base::Vector3d& p)
    {
        if (p.z <= maxZ) {
            if (bbox.MinX <= p.x && p.x <= bbox.MaxX && bbox.MinY <= p.y && p.y <= bbox.MaxY) {
                CCurve curve;
                curve.append(CVertex {{p.x + radius, p.y}});
                curve.append(CVertex {1, {p.x - radius, p.y}, {p.x, p.y}});
                curve.append(CVertex {1, {p.x + radius, p.y}, {p.x, p.y}});
                holes.append(curve);
            }
        }
    }

    void line(const Base::Vector3d& last, const Base::Vector3d& next)
    {
        if (last.z <= maxZ && next.z <= maxZ) {
            Base::BoundBox2d segBox = {};
            segBox.Add({last.x, last.y});
            segBox.Add({next.x, next.y});
            if (bbox.Intersect(segBox)) {
                CCurve curve;
                curve.append(CVertex {{last.x, last.y}});
                curve.append(CVertex {{next.x, next.y}});
                pathSegments.append(curve);
            }
        }
    }

public:
    ClearedAreaSegmentVisitor(double maxZ, double radius, Base::BoundBox3d bbox)
        : maxZ(maxZ)
        , radius(radius)
        , bbox(bbox)
    {
        bbox.Enlarge(radius);
    }

    CArea getClearedArea()
    {
        CArea result {pathSegments};
        result.Thicken(radius);
        result.Union(holes);
        return result;
    }

    void g0(int id,
            const Base::Vector3d& last,
            const Base::Vector3d& next,
            const std::deque<Base::Vector3d>& pts) override
    {
        (void)id;
        (void)pts;
        line(last, next);
    }

    void g1(int id,
            const Base::Vector3d& last,
            const Base::Vector3d& next,
            const std::deque<Base::Vector3d>& pts) override
    {
        (void)id;
        (void)pts;
        line(last, next);
    }

    void g23(int id,
             const Base::Vector3d& last,
             const Base::Vector3d& next,
             const std::deque<Base::Vector3d>& pts,
             const Base::Vector3d& center) override
    {
        (void)id;

        // Compute cw vs ccw
        const Base::Vector3d vdirect = next - last;
        const Base::Vector3d vstep = pts[0] - last;
        const bool ccw = vstep.x * vdirect.y - vstep.y * vdirect.x > 0;

        // Add an arc
        CCurve curve;
        curve.append(CVertex {{last.x, last.y}});
        curve.append(CVertex {ccw ? 1 : -1, {next.x, next.y}, {center.x, center.y}});
        pathSegments.append(curve);
    }

    void g8x(int id,
             const Base::Vector3d& last,
             const Base::Vector3d& next,
             const std::deque<Base::Vector3d>& pts,
             const std::deque<Base::Vector3d>& plist,
             const std::deque<Base::Vector3d>& qlist) override
    {
        // (peck) drilling
        (void)id;
        (void)qlist;  // pecks are always within the bounds of plist

        point(last);
        for (const auto& p : pts) {
            point(p);
        }
        for (const auto& p : plist) {
            point(p);
        }
        point(next);
    }

    void g38(int id, const Base::Vector3d& last, const Base::Vector3d& next) override
    {
        // probe operation; clears nothing
        (void)id;
        (void)last;
        (void)next;
    }
};

/// Area keeps build() and its CArea members protected, and the assembly below
/// needs them. Deriving is enough -- a member function of the derived class
/// may reach the protected members of its own type.
class ClearedArea: public AreaLib::Area
{
public:
    explicit ClearedArea(const AreaParams* params)
        : AreaLib::Area(params)
    {}

    static std::shared_ptr<AreaLib::Area> assemble(const CArea& area, const AreaParams& params)
    {
        auto res = std::make_shared<ClearedArea>(&params);
        res->myTrsf = {};
        if (!area.m_curves.empty()) {
            res->add(AreaLib::Area::toShape(area, false), OperationCompound);
            res->build();
        }
        else {
            res->myArea = std::make_unique<CArea>();
            res->myAreaOpen = std::make_unique<CArea>();
        }
        return res;
    }
};

}  // namespace

namespace Path
{

std::shared_ptr<AreaLib::Area> clearedAreaFromPath(const Toolpath& path,
    double diameter, double zmax, const Base::BoundBox3d& bbox)
{
    // Precision losses in arc/segment conversions (multiples of Accuracy):
    // 2.3 in generation of gcode (see CCurve::CheckForArc in libarea/Curve.cpp),
    // 1 in gcode arc to segment, 1 in Thicken() cleared area, 2 in getRestArea
    // target area offset in and back out. Oversize cleared areas by buffer to
    // smooth out imprecision in arc/segment conversion; getRestArea()
    // compensates for it.
    AreaParams params = {};
    const double buffer = params.Accuracy * 3;
    params.Accuracy = params.Accuracy * .7 / 4;  // 2.3 already in gcode; 4 * .7/4 = 3 total

    // Do not fit arcs after these offsets: it adds approximation error, and all
    // of those arcs are converted back to segments for the clipper difference
    // in getRestArea anyway.
    CAreaConfig conf(params, /*noFitArcs*/ true);

    ClearedAreaSegmentVisitor visitor(zmax, diameter / 2 + buffer, bbox);
    PathSegmentWalker walker(path);
    walker.walk(visitor, Base::Vector3d(0, 0, zmax + 1));

    return ClearedArea::assemble(visitor.getClearedArea(), params);
}

}  // namespace Path

/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                  *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ***************************************************************************/

// The cell draft of docs/NewDraft.md, ported from its Python prototype.
//
// For one drafted face F of solid S: P is F's plane, P' its new plane. The
// space in a box around F's swept band is split by S, P', and F's
// neighbours' surfaces extended (BOPAlgo_Builder, non-destructive). The
// swept region K is a set of cells found by a flood fill from the cells
// beside F's pieces, blocked by P', F and the neighbours' surfaces. The
// result is the cells of S not in K plus the cells of K on P's inner side,
// the faces put back together by origin (UnifySameDomain restricted to
// pieces of one input face). Several faces are drafted one after the other.

#include "PreCompiled.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

#include <BOPAlgo_Alerts.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BOPAlgo_Builder.hxx>
#include <BOPAlgo_CheckerSI.hxx>
#include <BOPAlgo_ShellSplitter.hxx>
#include <BOPDS_DS.hxx>
#include <BOPDS_IteratorSI.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass3d_SolidExplorer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <ElSLib.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomConvert.hxx>
#include <GeomLib.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_Surface.hxx>
#include <IntAna_QuadQuadGeo.hxx>
#include <IntTools_Context.hxx>
#include <IntTools_FaceFace.hxx>
#include <Precision.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_ErrorHandler.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>

#include <Base/Console.h>

#include "CellDraft.h"

FC_LOG_LEVEL_INIT("CellDraft", true, true)

using namespace Part;

namespace
{

// A neighbour coplanar with the drafted face (a face split in pieces).
constexpr double CoplanarTol = 1e-7;
// Normals this close to parallel make a tangent neighbour.
constexpr double TangentTol = 1e-6;
// A face turned by this much or more turns over.
constexpr double TurnLimit = M_PI / 2 - 1e-9;

// The plane of a face and its outward normal, as the face lies in its solid.
bool facePlane(const TopoDS_Face& face, gp_Pln& pln, gp_Dir& outward)
{
    BRepAdaptor_Surface surf(face, false);
    if (surf.GetType() != GeomAbs_Plane) {
        return false;
    }
    pln = surf.Plane();
    // The surface normal is D1U x D1V: the plane's axis for a direct
    // position, its reverse otherwise.
    gp_Dir n = pln.Axis().Direction();
    if (!pln.Position().Direct()) {
        n.Reverse();
    }
    if (face.Orientation() == TopAbs_REVERSED) {
        n.Reverse();
    }
    outward = n;
    return true;
}

// The outward normal of a face at a point near it.
bool faceNormal(const TopoDS_Face& face, const gp_Pnt& p, gp_Dir& n)
{
    BRepAdaptor_Surface surf(face);
    double u, v;
    switch (surf.GetType()) {
        case GeomAbs_Plane:
            ElSLib::Parameters(surf.Plane(), p, u, v);
            break;
        case GeomAbs_Cylinder:
            ElSLib::Parameters(surf.Cylinder(), p, u, v);
            break;
        case GeomAbs_Cone:
            ElSLib::Parameters(surf.Cone(), p, u, v);
            break;
        case GeomAbs_Sphere:
            ElSLib::Parameters(surf.Sphere(), p, u, v);
            break;
        case GeomAbs_Torus:
            ElSLib::Parameters(surf.Torus(), p, u, v);
            break;
        default: {
            Handle(Geom_Surface) s = BRep_Tool::Surface(face);
            GeomAPI_ProjectPointOnSurf proj(p, s);
            if (proj.NbPoints() == 0) {
                return false;
            }
            proj.LowerDistanceParameters(u, v);
        }
    }
    gp_Pnt q;
    gp_Vec du, dv;
    surf.D1(u, v, q, du, dv);
    gp_Vec nv = du.Crossed(dv);
    if (nv.Magnitude() < gp::Resolution()) {
        return false;
    }
    n = gp_Dir(nv);
    if (face.Orientation() == TopAbs_REVERSED) {
        n.Reverse();
    }
    return true;
}

// Draft_Modification's FindRotation (TKOffset, Draft_Modification_1.cxx),
// which is private there: the line about which the plane turns and the
// angle, or why there is none.
CellDraft::ErrorType findRotation(const gp_Pln& pl,
                                  TopAbs_Orientation oris,
                                  const gp_Dir& direction,
                                  double angle,
                                  const gp_Pln& neutralPlane,
                                  gp_Ax1& axe,
                                  double& theta)
{
    IntAna_QuadQuadGeo i2pl(pl, neutralPlane, Precision::Angular(), Precision::Confusion());
    if (!i2pl.IsDone() || i2pl.TypeInter() != IntAna_Line) {
        return CellDraft::ParallelToNeutral;
    }
    gp_Lin li = i2pl.Line(1);
    gp_Dir nx = li.Direction();
    gp_Dir ny = pl.Axis().Direction().Crossed(nx);
    double a = direction.Dot(nx);
    if (std::abs(a) > 1 - Precision::Angular()) {
        return CellDraft::AngleTooSteep;
    }
    double b = direction.Dot(ny);
    double c = direction.Dot(pl.Axis().Direction());
    bool direct(pl.Position().Direct());
    if ((direct && oris == TopAbs_REVERSED) || (!direct && oris == TopAbs_FORWARD)) {
        b = -b;
        c = -c;
    }
    double denom = std::sqrt(1 - a * a);
    double sina = std::sin(angle);
    if (denom <= std::abs(sina)) {
        return CellDraft::AngleTooSteep;
    }
    double phi = std::atan2(b / denom, c / denom);
    double theta0 = std::acos(sina / denom);
    theta = theta0 - phi;
    if (std::cos(theta) < 0.) {
        theta = -theta0 - phi;
    }
    while (std::abs(theta) > M_PI) {
        theta = theta + M_PI * (theta < 0 ? 1 : -1);
    }
    axe = li.Position();
    return CellDraft::NoError;
}

// The position of a surface of revolution made direct, turning the same way
// with its seam in the same place: its axis reversed if it is not direct.
// Measured: a tool solid in a frame of its own (gp_Ax2 from the axis alone)
// loses cells or leaves a self-intersection (#876's cones, #334), turning
// the seam away (0.9 rad) does worse, and a tool on #523's corner post's
// own indirect surface fuses into cells that are neither inside nor outside.
gp_Ax3 directFrame(const gp_Ax3& pos)
{
    if (pos.Direct()) {
        return pos;
    }
    gp_Dir main = pos.Direction();
    main.Reverse();
    return gp_Ax3(pos.Location(), main, pos.XDirection());
}

// A solid of revolution on a surface (a cone's), between two parameters
// along it, closed by planes square to its axis. Made on the neighbour's own
// surface, the pieces of its extension are pieces of that surface, and the
// merge puts them back together with the neighbour's own; made from the
// cone's apex instead (another location and reference radius, so another
// v), the merged cones of #876 came out with self-intersecting wires and the
// draft fell back to merging planar pieces only (66 to 78 faces where 44 to
// 48 do).
TopoDS_Shape revolutionTool(const Handle(Geom_Surface) & surface, double v0, double v1)
{
    TopoDS_Face lateral =
        BRepBuilderAPI_MakeFace(surface, 0.0, 2 * M_PI, v0, v1, Precision::Confusion()).Face();
    BRep_Builder builder;
    TopoDS_Shell shell;
    builder.MakeShell(shell);
    builder.Add(shell, lateral);
    for (TopExp_Explorer exp(lateral, TopAbs_EDGE); exp.More(); exp.Next()) {
        const TopoDS_Edge& e = TopoDS::Edge(exp.Current());
        if (BRep_Tool::Degenerated(e) || BRep_Tool::IsClosed(e, lateral)) {
            continue;  // the apex, the seam
        }
        // the cap uses the circle the other way round from the lateral face
        TopoDS_Face cap = BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(e).Wire(), true).Face();
        for (TopExp_Explorer ce(cap, TopAbs_EDGE); ce.More(); ce.Next()) {
            if (ce.Current().IsSame(e) && ce.Current().Orientation() == e.Orientation()) {
                cap.Reverse();
            }
        }
        builder.Add(shell, cap);
    }
    shell.Closed(true);
    TopoDS_Solid solid;
    builder.MakeSolid(solid);
    builder.Add(solid, shell);
    BRepLib::OrientClosedSolid(solid);
    return solid;
}

// The range of v on a surface of revolution (v along the axis by
// vPerHeight) that covers the given points, padded by a tenth.
std::pair<double, double>
axialRange(const gp_Ax3& pos, double vPerHeight, const std::vector<gp_Pnt>& pts)
{
    double v0 = Precision::Infinite(), v1 = -Precision::Infinite();
    for (const auto& p : pts) {
        double v = gp_Vec(pos.Location(), p).Dot(gp_Vec(pos.Direction())) * vPerHeight;
        v0 = std::min(v0, v);
        v1 = std::max(v1, v);
    }
    double pad = 0.1 * (v1 - v0);
    return {v0 - pad, v1 + pad};
}

// A face on a plane covering the given points, each side extended.
TopoDS_Face planeRect(const gp_Pln& pl, const std::vector<gp_Pnt>& pts, double ext)
{
    double u0 = Precision::Infinite(), u1 = -Precision::Infinite();
    double v0 = u0, v1 = u1;
    for (const auto& p : pts) {
        double u, v;
        ElSLib::Parameters(pl, p, u, v);
        u0 = std::min(u0, u);
        u1 = std::max(u1, u);
        v0 = std::min(v0, v);
        v1 = std::max(v1, v);
    }
    return BRepBuilderAPI_MakeFace(pl, u0 - ext, u1 + ext, v0 - ext, v1 + ext).Face();
}

std::vector<gp_Pnt> boxCorners(const Bnd_Box& box)
{
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    std::vector<gp_Pnt> pts;
    for (double x : {x0, x1}) {
        for (double y : {y0, y1}) {
            for (double z : {z0, z1}) {
                pts.emplace_back(x, y, z);
            }
        }
    }
    return pts;
}

// The part of a tool inside the box, or the tool itself if the common fails.
TopoDS_Shape clipToBox(const TopoDS_Shape& tool, const TopoDS_Shape& box)
{
    try {
        BRepAlgoAPI_Common common(tool, box);
        if (common.IsDone() && !common.HasErrors()) {
            return common.Shape();
        }
    }
    catch (Standard_Failure&) {
    }
    return tool;
}

// Whether a face lies on the boundary of the box.
bool onBox(const TopoDS_Face& face, const Bnd_Box& box)
{
    BRepAdaptor_Surface surf(face, false);
    if (surf.GetType() != GeomAbs_Plane) {
        return false;
    }
    Bnd_Box fb;
    BRepBndLib::Add(face, fb, false);
    double x0, y0, z0, x1, y1, z1;
    fb.Get(x0, y0, z0, x1, y1, z1);
    double bx0, by0, bz0, bx1, by1, bz1;
    box.Get(bx0, by0, bz0, bx1, by1, bz1);
    // Bnd_Box adds the face's tolerance on every side
    double tol = 1e-6 + 2 * BRep_Tool::Tolerance(face) + 2 * fb.GetGap();
    auto flatAt = [tol](double lo, double hi, double at) {
        return std::abs(lo - at) < tol && std::abs(hi - at) < tol;
    };
    return flatAt(x0, x1, bx0) || flatAt(x0, x1, bx1) || flatAt(y0, y1, by0)
        || flatAt(y0, y1, by1) || flatAt(z0, z1, bz0) || flatAt(z0, z1, bz1);
}

double boxDiagonal(const Bnd_Box& box)
{
    if (box.IsVoid()) {
        return 0.0;
    }
    return std::sqrt(box.SquareExtent());
}

// A point inside a cell.
bool interiorPoint(const TopoDS_Shape& cell, gp_Pnt& p)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(cell, props);
    p = props.CentreOfMass();
    BRepClass3d_SolidClassifier classifier(cell);
    const double tol = 1e-7;
    classifier.Perform(p, tol);
    if (classifier.State() == TopAbs_IN) {
        return true;
    }
    Bnd_Box cb;
    BRepBndLib::Add(cell, cb, false);
    double size = boxDiagonal(cb);
    for (TopExp_Explorer exp(cell, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(exp.Current());
        if (face.Orientation() != TopAbs_FORWARD && face.Orientation() != TopAbs_REVERSED) {
            continue;
        }
        gp_Pnt q;
        double u, v;
        if (!BRepClass3d_SolidExplorer::FindAPointInTheFace(face, q, u, v)) {
            continue;
        }
        BRepAdaptor_Surface surf(face);
        gp_Pnt qq;
        gp_Vec du, dv;
        surf.D1(u, v, qq, du, dv);
        gp_Vec nv = du.Crossed(dv);
        if (nv.Magnitude() < gp::Resolution()) {
            continue;
        }
        nv.Normalize();
        if (face.Orientation() == TopAbs_REVERSED) {
            nv.Reverse();
        }
        for (double d : {1e-4, 1e-3, 1e-2}) {
            gp_Pnt r = q.Translated(-d * size * nv);
            classifier.Perform(r, tol);
            if (classifier.State() == TopAbs_IN) {
                p = r;
                return true;
            }
        }
    }
    return false;
}

// The self-intersection test of the boolean check, on the pairs of shapes
// that involve a set of faces (the faces a draft made, with their edges and
// vertices) only. The rest of the shape is the draft's input, clean already:
// intersecting its faces with each other again is most of the full check's
// time (and testing its B-spline faces for intersecting themselves).
class RegionIteratorSI: public BOPDS_IteratorSI
{
public:
    explicit RegionIteratorSI(const Handle(NCollection_BaseAllocator) & allocator)
        : BOPDS_IteratorSI(allocator)
    {}

    void restrict(const std::vector<char>& keep)
    {
        std::vector<BOPDS_Pair> kept;
        for (int t = 0; t < myLists.Length(); ++t) {
            auto& pairs = myLists(t);
            kept.clear();
            for (int i = 0; i < pairs.Length(); ++i) {
                int n1, n2;
                pairs(i).Indices(n1, n2);
                if (keep[n1] || keep[n2]) {
                    kept.push_back(pairs(i));
                }
            }
            pairs.Clear();
            for (const auto& pair : kept) {
                pairs.Append(pair);
            }
        }
    }
};

class RegionCheckerSI: public BOPAlgo_CheckerSI
{
public:
    explicit RegionCheckerSI(const TopTools_MapOfShape& region)
        : region(region)
    {}

    void Perform(const Message_ProgressRange& range = Message_ProgressRange()) override
    {
        try {
            OCC_CATCH_SIGNALS
            BOPAlgo_PaveFiller::Perform(range);
            checkFaces();
            if (!HasErrors()) {
                PerformVZ(Message_ProgressRange());
            }
            if (!HasErrors()) {
                PerformEZ(Message_ProgressRange());
            }
            if (!HasErrors()) {
                PerformFZ(Message_ProgressRange());
            }
            if (!HasErrors()) {
                PerformZZ(Message_ProgressRange());
            }
            if (!HasErrors()) {
                PostTreat();
            }
        }
        catch (Standard_Failure&) {
            AddError(new BOPAlgo_AlertIntersectionFailed);
        }
    }

protected:
    void Init(const Message_ProgressRange& /*range*/) override
    {
        Clear();
        myDS = new BOPDS_DS(myAllocator);
        myDS->SetArguments(myArguments);
        myDS->Init(myFuzzyValue);
        myContext = new IntTools_Context;
        auto iterator = new RegionIteratorSI(myAllocator);
        iterator->SetDS(myDS);
        iterator->Prepare(myContext, myUseOBB, myFuzzyValue);
        iterator->UpdateByLevelOfCheck(myLevelOfCheck);
        std::vector<char> keep(myDS->NbSourceShapes(), 0);
        for (int i = 0; i < myDS->NbSourceShapes(); ++i) {
            keep[i] = region.Contains(myDS->Shape(i)) ? 1 : 0;
        }
        iterator->restrict(keep);
        myIterator = iterator;
    }

    // BOPAlgo_CheckerSI::CheckFaceSelfIntersection on the region's faces
    void checkFaces()
    {
        auto& interferences = const_cast<NCollection_Map<BOPDS_Pair>&>(myDS->Interferences());
        interferences.Clear();
        for (int i = 0; i < myDS->NbSourceShapes(); ++i) {
            const BOPDS_ShapeInfo& info = myDS->ShapeInfo(i);
            if (info.ShapeType() != TopAbs_FACE || !region.Contains(info.Shape())) {
                continue;
            }
            const TopoDS_Face& face = TopoDS::Face(info.Shape());
            BRepAdaptor_Surface surface(face, false);
            switch (surface.GetType()) {
                case GeomAbs_Plane:
                case GeomAbs_Cylinder:
                case GeomAbs_Cone:
                case GeomAbs_Sphere:
                    continue;
                case GeomAbs_Torus:
                    if (surface.Torus().MajorRadius()
                        > surface.Torus().MinorRadius() + Precision::Confusion()) {
                        continue;
                    }
                    break;
                default:
                    break;
            }
            IntTools_FaceFace self;
            self.Perform(face, face);
            if (self.IsDone() && (self.Lines().Length() || self.Points().Length())) {
                interferences.Add(BOPDS_Pair(i, i));
            }
        }
    }

private:
    const TopTools_MapOfShape& region;
};

// The boolean argument check's self-intersection and curve-on-surface tests
// (BOPAlgo_ArgumentAnalyzer) of the given faces against a region of a shape,
// without checking the rest of the region against itself.
std::string regionCheck(const TopoDS_Shape& shape, const std::vector<TopoDS_Shape>& faces)
{
    BRepBuilderAPI_Copy copy(shape, true, false);
    TopTools_MapOfShape region;
    BRep_Builder builder;
    TopoDS_Compound made;
    builder.MakeCompound(made);
    for (const auto& f : faces) {
        TopoDS_Shape face = copy.ModifiedShape(f);
        builder.Add(made, face);
        TopTools_IndexedMapOfShape sub;
        TopExp::MapShapes(face, sub);
        for (int i = 1; i <= sub.Extent(); ++i) {
            region.Add(sub(i));
        }
    }
    RegionCheckerSI checker(region);
    NCollection_List<TopoDS_Shape> args;
    args.Append(copy.Shape());
    checker.SetArguments(args);
    checker.SetNonDestructive(true);
    checker.SetRunParallel(true);
    checker.Perform();
    bool selfInter = false;
    if (checker.HasErrors()) {
        return "the result fails the boolean check (other)";
    }
    const BOPDS_DS& ds = *checker.PDS();
    for (NCollection_Map<BOPDS_Pair>::Iterator it(ds.Interferences()); it.More(); it.Next()) {
        int n1, n2;
        it.Value().Indices(n1, n2);
        if (!ds.IsNewShape(n1) && !ds.IsNewShape(n2)) {
            selfInter = true;
            if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
                for (int n : {n1, n2}) {
                    Bnd_Box fb;
                    BRepBndLib::Add(ds.Shape(n), fb, false);
                    double a0, b0, c0, a1, b1, c1;
                    fb.Get(a0, b0, c0, a1, b1, c1);
                    FC_LOG("self-intersection: shape type "
                           << int(ds.Shape(n).ShapeType()) << " bb (" << a0 << ", " << b0
                           << ", " << c0 << ")-(" << a1 << ", " << b1 << ", " << c1 << ")");
                }
            }
        }
    }
    BOPAlgo_ArgumentAnalyzer check;
    check.SetShape1(made);
    check.CurveOnSurfaceMode() = true;
    check.Perform();
    bool curveOnSurface = check.HasFaulty();
    if (!selfInter && !curveOnSurface) {
        return std::string();
    }
    std::string what = selfInter && curveOnSurface
        ? "self-intersection, an edge off its face"
        : (selfInter ? "self-intersection" : "an edge off its face");
    return "the result fails the boolean check (" + what + ")";
}

// The faces of a draft's result that are not (pieces of) faces of its input
// -- the ones the draft made -- and the region around them: those faces and
// every face whose box meets one of theirs.
TopoDS_Compound draftRegion(const TopoDS_Shape& result,
                            const TopTools_MapOfShape& inputFaces,
                            std::vector<TopoDS_Shape>& made)
{
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(result, TopAbs_FACE, faces);
    std::vector<Bnd_Box> boxes(faces.Extent());
    std::vector<int> madeIdx;
    for (int i = 1; i <= faces.Extent(); ++i) {
        BRepBndLib::Add(faces(i), boxes[i - 1], false);
        boxes[i - 1].Enlarge(Precision::Confusion());
        if (!inputFaces.Contains(faces(i))) {
            madeIdx.push_back(i);
            made.push_back(faces(i));
        }
    }
    BRep_Builder builder;
    TopoDS_Compound region;
    builder.MakeCompound(region);
    for (int i = 1; i <= faces.Extent(); ++i) {
        if (std::any_of(madeIdx.begin(), madeIdx.end(), [&](int m) {
                return m == i || !boxes[m - 1].IsOut(boxes[i - 1]);
            })) {
            builder.Add(region, faces(i));
        }
    }
    return region;
}

// What is wrong with a draft's result, or nothing: BRepCheck, and the
// boolean check, which also sees faces crossing each other (section 2.2 of
// docs/NewDraft.md), of the faces the draft made against the rest. The rest
// is the input's faces or pieces of them, which meet each other only where
// the input's faces did: the input is taken as clean.
std::string checkResult(const TopoDS_Shape& shape, const TopTools_MapOfShape& inputFaces)
{
    if (!BRepCheck_Analyzer(shape).IsValid()) {
        return "the result is not a valid solid";
    }
    std::vector<TopoDS_Shape> made;
    TopoDS_Compound region = draftRegion(shape, inputFaces, made);
    FC_LOG("check: " << made.size() << " faces made");
    if (made.empty()) {
        return std::string();
    }
    return regionCheck(region, made);
}

// Sample points of a shape: its vertices and points along its edges.
std::vector<gp_Pnt> samplePoints(const TopoDS_Shape& shape)
{
    std::vector<gp_Pnt> samples;
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    for (int i = 1; i <= vertices.Extent(); ++i) {
        samples.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))));
    }
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge& e = TopoDS::Edge(edges(i));
        if (BRep_Tool::Degenerated(e)) {
            continue;
        }
        TopLoc_Location loc;
        double first, last;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(e, loc, first, last);
        if (curve.IsNull()) {
            continue;
        }
        for (double t : {0.25, 0.5, 0.75}) {
            samples.push_back(
                curve->Value(first + t * (last - first)).Transformed(loc.Transformation()));
        }
    }
    return samples;
}

// A plane of the second ring that caps the added side (section 4.9).
struct Cap
{
    TopoDS_Face face;
    int group;
    gp_Pln plane;
    gp_Dir outward;
    double tol;
};

// The caps of a drafted face's set: the planes of the faces beside its
// neighbours (not the set, not a neighbour, not on the set's plane or a
// planar neighbour's) that have the whole solid on their inner side.
std::vector<Cap> findCaps(const TopoDS_Shape& solid,
                          const TopTools_IndexedMapOfShape& solidFaces,
                          const TopTools_IndexedDataMapOfShapeListOfShape& edgeFaces,
                          const TopTools_MapOfShape& fsetMap,
                          const std::vector<TopoDS_Face>& neighbours,
                          const gp_Pln& plane,
                          const gp_Dir& normal)
{
    std::vector<Cap> caps;
    std::vector<gp_Pnt> samples = samplePoints(solid);
    TopTools_MapOfShape neighbourMap, ring;
    std::vector<std::pair<gp_Pln, gp_Dir>> neighbourPlanes;
    for (const auto& nb : neighbours) {
        neighbourMap.Add(nb);
        gp_Pln pn;
        gp_Dir nn;
        if (facePlane(nb, pn, nn)) {
            neighbourPlanes.emplace_back(pn, nn);
        }
    }
    for (const auto& nb : neighbours) {
        for (TopExp_Explorer exp(nb, TopAbs_EDGE); exp.More(); exp.Next()) {
            const TopoDS_Edge& e = TopoDS::Edge(exp.Current());
            if (BRep_Tool::Degenerated(e)) {
                continue;
            }
            for (const auto& s : edgeFaces.FindFromKey(e)) {
                int idx = solidFaces.FindIndex(s);
                TopoDS_Face h = TopoDS::Face(solidFaces.FindKey(idx));
                if (fsetMap.Contains(h) || neighbourMap.Contains(h) || !ring.Add(h)) {
                    continue;
                }
                gp_Pln ph;
                gp_Dir nh;
                if (!facePlane(h, ph, nh)) {
                    continue;  // curved faces do not cap (yet)
                }
                if (std::abs(std::abs(nh.Dot(normal)) - 1) < 1e-9
                    && plane.Distance(ph.Location()) < CoplanarTol) {
                    continue;
                }
                // a plane that bounds the region already
                auto samePlane = [&ph, &nh](const gp_Pln& other, const gp_Dir& on) {
                    return on.Dot(nh) > 1 - 1e-9 && other.Distance(ph.Location()) < CoplanarTol;
                };
                if (std::any_of(neighbourPlanes.begin(), neighbourPlanes.end(), [&](const auto& n) {
                        return samePlane(n.first, n.second);
                    })) {
                    continue;
                }
                if (std::any_of(caps.begin(), caps.end(), [&](const Cap& c) {
                        return samePlane(c.plane, c.outward);
                    })) {
                    continue;
                }
                double tol = std::max(1e-6, BRep_Tool::Tolerance(h));
                bool bounds = std::none_of(samples.begin(), samples.end(), [&](const gp_Pnt& p) {
                    return gp_Vec(ph.Location(), p).Dot(gp_Vec(nh)) > tol;
                });
                if (bounds) {
                    caps.push_back({h, idx, ph, nh, tol});
                }
            }
        }
    }
    return caps;
}

bool isBoundaryFace(const TopoDS_Shape& face)
{
    return face.Orientation() == TopAbs_FORWARD || face.Orientation() == TopAbs_REVERSED;
}

using ShapeIntMap = NCollection_DataMap<TopoDS_Shape, int, TopTools_ShapeMapHasher>;

}  // namespace

namespace Part
{

// One drafted face (with the faces coplanar to it) on one solid.
class CellDraftOne
{
public:
    CellDraftOne(const TopoDS_Shape& solid,
                 const std::vector<TopoDS_Face>& faces,
                 const CellDraft::FaceDraft& draft,
                 bool stopAtBody)
        : solid(solid)
        , faces(faces)
        , draft(draft)
        , stopAtBody(stopAtBody)
    {}

    bool run();

    TopoDS_Shape result;
    Handle(BRepTools_History) history;

    CellDraft::ErrorType error = CellDraft::NoError;
    TopoDS_Face errorFace;
    TopoDS_Face errorNeighbour;
    std::string message;

private:
    bool fail(CellDraft::ErrorType err,
              const std::string& msg,
              const TopoDS_Face& face = TopoDS_Face(),
              const TopoDS_Face& neighbour = TopoDS_Face())
    {
        error = err;
        message = msg;
        errorFace = face.IsNull() ? faces.front() : face;
        errorNeighbour = neighbour;
        return false;
    }

    bool prepare();
    // false with error == NoClosure when the swept region leaks to the box
    bool attempt(double scale);

    struct Neighbour
    {
        TopoDS_Face face;
        int group;
        bool planar = false;
        gp_Pln plane;
        gp_Dir outward;
        // how far the face's plane must reach past its own face
        double reach = 0.0;
        bool grazing = false;
    };

    const TopoDS_Shape& solid;
    std::vector<TopoDS_Face> faces;
    const CellDraft::FaceDraft& draft;
    bool stopAtBody;
    // the general fuse's fuzzy value: none, then a little on a second try
    double fuzzy = 0.0;

    TopTools_IndexedMapOfShape solidFaces;
    std::vector<TopoDS_Face> fset;
    TopTools_MapOfShape fsetMap;
    std::vector<Neighbour> neighbours;
    std::vector<Cap> caps;
    gp_Pln plane, newPlane;
    gp_Dir normal, newNormal;
    gp_Ax1 hinge;
    double turn = 0.0;
    double disp = 0.0;
    double reachLimit = 0.0;
    Bnd_Box band;
};

bool CellDraftOne::prepare()
{
    TopExp::MapShapes(solid, TopAbs_FACE, solidFaces);
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndUniqueAncestors(solid, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

    // the faces as they lie in the solid (orientation)
    for (auto& f : faces) {
        int idx = solidFaces.FindIndex(f);
        if (idx == 0) {
            return fail(CellDraft::FaceVanishes, "the face is not a face of the shape", f);
        }
        f = TopoDS::Face(solidFaces.FindKey(idx));
    }
    const TopoDS_Face& face = faces.front();
    if (!facePlane(face, plane, normal)) {
        return fail(CellDraft::UnsupportedSurface, "the face is not planar");
    }

    double theta = 0.0;
    auto err = findRotation(plane,
                            face.Orientation(),
                            draft.direction,
                            draft.angle,
                            draft.neutralPlane,
                            hinge,
                            theta);
    if (err == CellDraft::ParallelToNeutral) {
        return fail(err, "the face is parallel to the neutral plane");
    }
    if (err == CellDraft::AngleTooSteep) {
        return fail(err,
                    "no plane through the face's line with the neutral plane "
                    "makes the angle with the pull direction");
    }
    if (std::abs(theta) <= Precision::Angular()) {
        // the face makes the angle already
        result = solid;
        return true;
    }
    newPlane = plane.Rotated(hinge, theta);
    newNormal = normal.Rotated(hinge, theta);
    turn = normal.Angle(newNormal);
    if (turn >= TurnLimit) {
        std::ostringstream ss;
        ss << "the face would turn over (by " << turn * 180 / M_PI << " deg)";
        return fail(CellDraft::TurnsOver, ss.str());
    }

    // The drafted set: the faces and every neighbour coplanar with them,
    // same side out (a face split in pieces), drafted as one face.
    std::vector<TopoDS_Face> todo(faces.begin(), faces.end());
    while (!todo.empty()) {
        TopoDS_Face f = todo.back();
        todo.pop_back();
        if (!fsetMap.Add(f)) {
            continue;
        }
        fset.push_back(f);
        for (TopExp_Explorer exp(f, TopAbs_EDGE); exp.More(); exp.Next()) {
            const TopoDS_Edge& e = TopoDS::Edge(exp.Current());
            if (BRep_Tool::Degenerated(e)) {
                continue;
            }
            for (const auto& s : edgeFaces.FindFromKey(e)) {
                TopoDS_Face g = TopoDS::Face(solidFaces.FindKey(solidFaces.FindIndex(s)));
                if (fsetMap.Contains(g)) {
                    continue;
                }
                gp_Pln pg;
                gp_Dir ng;
                if (facePlane(g, pg, ng) && ng.Dot(normal) > 1 - 1e-9
                    && plane.Distance(pg.Location()) < CoplanarTol) {
                    todo.push_back(g);
                }
            }
        }
    }

    // How far a neighbour is extended at most: across the whole solid. A
    // neighbour nearly parallel to the new plane meets it far away (#474's
    // ledge: 1668 away on a part 30 across, and a fuse of 52 s in a box that
    // size); past the solid, the stop at the body cuts the growth off, and
    // without the stop a larger box is tried on a leak.
    Bnd_Box solidBox;
    BRepBndLib::Add(solid, solidBox, false);
    reachLimit = boxDiagonal(solidBox);

    // The neighbours: faces sharing an edge with the drafted set.
    TopTools_MapOfShape seen;
    for (const auto& f : fset) {
        for (TopExp_Explorer exp(f, TopAbs_EDGE); exp.More(); exp.Next()) {
            const TopoDS_Edge& e = TopoDS::Edge(exp.Current());
            if (BRep_Tool::Degenerated(e)) {
                continue;
            }
            for (const auto& s : edgeFaces.FindFromKey(e)) {
                int idx = solidFaces.FindIndex(s);
                TopoDS_Face g = TopoDS::Face(solidFaces.FindKey(idx));
                if (fsetMap.Contains(g)) {
                    continue;
                }
                bool known = !seen.Add(g);
                Neighbour* nb = nullptr;
                if (!known) {
                    neighbours.emplace_back();
                    nb = &neighbours.back();
                    nb->face = g;
                    nb->group = idx;
                    nb->planar = facePlane(g, nb->plane, nb->outward);
                }
                else {
                    for (auto& n : neighbours) {
                        if (n.face.IsSame(g)) {
                            nb = &n;
                        }
                    }
                }
                double first, last;
                Handle(Geom_Curve) curve = BRep_Tool::Curve(e, first, last);
                if (curve.IsNull()) {
                    continue;
                }
                // a curve with a location
                TopLoc_Location loc;
                curve = BRep_Tool::Curve(e, loc, first, last);
                gp_Trsf trsf = loc.Transformation();
                if (!nb->planar) {
                    // tangent to the drafted face anywhere along the edge
                    for (double t : {0.25, 0.5, 0.75}) {
                        gp_Pnt p = curve->Value(first + t * (last - first)).Transformed(trsf);
                        gp_Dir ng;
                        if (faceNormal(g, p, ng) && gp_Vec(ng).Crossed(gp_Vec(normal)).Magnitude() < TangentTol) {
                            return fail(CellDraft::TangentNeighbour,
                                        "a neighbour is tangent to the face",
                                        face,
                                        g);
                        }
                    }
                    continue;
                }
                // How far the neighbour's plane must reach across the edge to
                // meet the new plane: along the neighbour's plane, square to
                // the edge, from each end of the edge.
                gp_Pnt p0 = curve->Value(first).Transformed(trsf);
                gp_Pnt p1 = curve->Value(last).Transformed(trsf);
                gp_Vec along(p0, p1);
                if (along.Magnitude() < Precision::Confusion()) {
                    continue;
                }
                gp_Vec w = gp_Vec(nb->outward).Crossed(along);
                if (w.Magnitude() < gp::Resolution()) {
                    continue;
                }
                w.Normalize();
                double slope = w.Dot(gp_Vec(newNormal));
                if (std::abs(slope) < 1e-3) {
                    nb->grazing = true;
                    continue;
                }
                for (const auto& p : {p0, p1}) {
                    double d = gp_Vec(newPlane.Location(), p).Dot(gp_Vec(newNormal));
                    double t = -d / slope;
                    if (std::abs(t) > reachLimit) {
                        t = t > 0 ? reachLimit : -reachLimit;
                    }
                    nb->reach = std::max(nb->reach, std::abs(t));
                    band.Add(p.Translated(t * w));
                }
            }
        }
    }

    // The swept band: the drafted faces, where they turn to, and where the
    // neighbours meet the new plane.
    gp_Lin hingeLine(hinge);
    double far = 0.0;
    for (const auto& f : fset) {
        BRepBndLib::Add(f, band, false);
        for (TopExp_Explorer exp(f, TopAbs_VERTEX); exp.More(); exp.Next()) {
            gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
            far = std::max(far, hingeLine.Distance(p));
            band.Add(p.Rotated(hinge, theta));
        }
    }
    disp = far * std::tan(turn);
    FC_LOG("faces " << fset.size() << ", neighbours " << neighbours.size() << ", theta "
                    << theta * 180 / M_PI << ", turn " << turn * 180 / M_PI << ", disp " << disp);
    for (const auto& nb : neighbours) {
        FC_LOG("neighbour " << nb.group << " planar " << nb.planar << " reach " << nb.reach
                            << " grazing " << nb.grazing);
    }

    // The second ring caps the added side: a drafted face stops at a plane
    // of the body that has the whole body on its inner side (section 4.9).
    if (stopAtBody) {
        std::vector<TopoDS_Face> nbFaces;
        for (const auto& nb : neighbours) {
            nbFaces.push_back(nb.face);
        }
        caps = findCaps(solid, solidFaces, edgeFaces, fsetMap, nbFaces, plane, normal);
    }
    return true;
}

bool CellDraftOne::attempt(double scale)
{
    const int NoGroup = -1;
    const int FGroup = 0;
    FC_TIME_INIT(t);

    double diag = boxDiagonal(band);
    // The move of the face's far edge bounds the region, but no further
    // than across the solid: an 80 deg turn moved #474's ledge 832 on a part
    // 30 across, and the fuse in a box that size took 50 s and gave cells
    // of negative volume. A leak tries a larger box.
    double move = std::min(std::max(2 * disp, 0.25 * diag), reachLimit);
    double margin = scale * move + Precision::Confusion() * 100;
    Bnd_Box local = band;
    local.Enlarge(margin);
    double x0, y0, z0, x1, y1, z1;
    local.Get(x0, y0, z0, x1, y1, z1);
    TopoDS_Shape box =
        BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), gp_Pnt(x1, y1, z1)).Shape();
    double boxDiag = boxDiagonal(local);
    std::vector<gp_Pnt> corners = boxCorners(local);

    // The splitting tools, each with the group its faces belong to.
    ShapeIntMap groups;      // argument face -> group
    TopTools_MapOfShape newPlaneFaces, blocking, capFaces, boxFaces;
    for (TopExp_Explorer exp(box, TopAbs_FACE); exp.More(); exp.Next()) {
        boxFaces.Add(exp.Current());
        groups.Bind(exp.Current(), NoGroup);
    }
    for (int i = 1; i <= solidFaces.Extent(); ++i) {
        groups.Bind(solidFaces(i), fsetMap.Contains(solidFaces(i)) ? FGroup : i);
    }
    for (const auto& f : fset) {
        blocking.Add(f);
    }
    std::vector<std::pair<TopoDS_Shape, const TopoDS_Face*>> tools;  // tool, owner

    // the new plane
    TopoDS_Shape pn = clipToBox(planeRect(newPlane, corners, 0.0), box);
    for (TopExp_Explorer exp(pn, TopAbs_FACE); exp.More(); exp.Next()) {
        newPlaneFaces.Add(exp.Current());
        blocking.Add(exp.Current());
        groups.Bind(exp.Current(), FGroup);
    }
    tools.emplace_back(pn, &fset.front());

    // the neighbours, extended
    for (const auto& nb : neighbours) {
        blocking.Add(nb.face);
        TopoDS_Shape tool;
        bool solidTool = false;
        if (nb.planar) {
            std::vector<gp_Pnt> pts;
            double ext;
            if (nb.grazing) {
                pts = corners;
                ext = 0.0;
            }
            else {
                Bnd_Box fb;
                BRepBndLib::Add(nb.face, fb, false);
                pts = boxCorners(fb);
                ext = scale * (1.5 * nb.reach + std::min(disp, reachLimit)) + 0.1 * diag;
            }
            tool = planeRect(nb.plane, pts, ext);
        }
        else {
            BRepAdaptor_Surface surf(nb.face);
            switch (surf.GetType()) {
                case GeomAbs_Cylinder: {
                    // A surface of revolution goes in as a solid: its full
                    // turn as a face loses cells when its seam lies in the
                    // plane of another tool (#523's corner post). The solid
                    // turns the way the face's surface does, so that the
                    // neighbour's own piece and the pieces of its extension
                    // merge back as one surface. Unlike a cone's (below), it
                    // starts where the box does: on the face's own surface,
                    // #876's bottom face drafted about face 16 at 5 deg came
                    // out 0.34 over the classic draft's volume.
                    gp_Cylinder cyl = surf.Cylinder();
                    gp_Ax3 pos = directFrame(cyl.Position());
                    gp_Ax2 frame(pos.Location(), pos.Direction(), pos.XDirection());
                    gp_Pnt loc = frame.Location();
                    gp_Vec dir(frame.Direction());
                    double t0 = Precision::Infinite(), t1 = -Precision::Infinite();
                    for (const auto& p : corners) {
                        double t = gp_Vec(loc, p).Dot(dir);
                        t0 = std::min(t0, t);
                        t1 = std::max(t1, t);
                    }
                    double pad = 0.1 * (t1 - t0);
                    frame.SetLocation(loc.Translated((t0 - pad) * dir));
                    tool =
                        BRepPrimAPI_MakeCylinder(frame, cyl.Radius(), t1 - t0 + 2 * pad).Shape();
                    solidTool = true;
                    break;
                }
                case GeomAbs_Cone: {
                    // the nappe the face is on, on the face's own surface
                    // (revolutionTool()), from past the box to past the box
                    // or to the apex
                    gp_Cone cone = surf.Cone();
                    double u0, u1, fv0, fv1;
                    BRepTools::UVBounds(nb.face, u0, u1, fv0, fv1);
                    double faceV = (fv0 + fv1) / 2;
                    if (!cone.Position().Direct()) {
                        // the axis reversed: v changes sign
                        cone = gp_Cone(directFrame(cone.Position()), -cone.SemiAngle(),
                                       cone.RefRadius());
                        faceV = -faceV;
                    }
                    double semi = cone.SemiAngle();
                    auto range = axialRange(cone.Position(), 1.0 / std::cos(semi), corners);
                    double vApex = -cone.RefRadius() / std::sin(semi);
                    if (faceV > vApex) {
                        range.first = std::max(range.first, vApex);
                    }
                    else {
                        range.second = std::min(range.second, vApex);
                    }
                    if (range.second - range.first < Precision::Confusion()) {
                        continue;  // the box is past the apex: nothing to extend
                    }
                    tool = revolutionTool(new Geom_ConicalSurface(cone), range.first,
                                          range.second);
                    solidTool = true;
                    break;
                }
                case GeomAbs_Sphere: {
                    gp_Sphere sphere = surf.Sphere();
                    tool = BRepPrimAPI_MakeSphere(sphere.Location(), sphere.Radius()).Shape();
                    solidTool = true;
                    break;
                }
                case GeomAbs_Torus: {
                    gp_Torus torus = surf.Torus();
                    tool = BRepPrimAPI_MakeTorus(gp_Ax2(torus.Location(), torus.Axis().Direction()),
                                                 torus.MajorRadius(),
                                                 torus.MinorRadius())
                               .Shape();
                    solidTool = true;
                    break;
                }
                default: {
                    // extended by the largest move of the face's boundary
                    double u0, u1, v0, v1;
                    BRepTools::UVBounds(nb.face, u0, u1, v0, v1);
                    TopLoc_Location loc;
                    Handle(Geom_Surface) s = BRep_Tool::Surface(nb.face, loc);
                    if (!loc.IsIdentity()) {
                        s = Handle(Geom_Surface)::DownCast(s->Transformed(loc.Transformation()));
                    }
                    Handle(Geom_BoundedSurface) bs = GeomConvert::SurfaceToBSplineSurface(
                        new Geom_RectangularTrimmedSurface(s, u0, u1, v0, v1));
                    double length = scale * move;
                    bool closedU = s->IsUPeriodic() && u1 - u0 >= s->UPeriod() - 1e-9;
                    bool closedV = s->IsVPeriodic() && v1 - v0 >= s->VPeriod() - 1e-9;
                    for (bool inU : {true, false}) {
                        if ((inU && closedU) || (!inU && closedV)) {
                            continue;
                        }
                        for (bool after : {true, false}) {
                            GeomLib::ExtendSurfByLength(bs, length, 1, inU, after);
                        }
                    }
                    tool = BRepBuilderAPI_MakeFace(bs, Precision::Confusion()).Face();
                }
            }
        }
        tool = clipToBox(tool, box);
        for (TopExp_Explorer exp(tool, TopAbs_FACE); exp.More(); exp.Next()) {
            const TopoDS_Face& tf = TopoDS::Face(exp.Current());
            if (solidTool && onBox(tf, local)) {
                boxFaces.Add(tf);
                groups.Bind(tf, NoGroup);
                continue;
            }
            if (solidTool && BRepAdaptor_Surface(tf, false).GetType() == GeomAbs_Plane) {
                // not on the box, not the neighbour's surface: a plain face
                groups.Bind(tf, NoGroup);
                continue;
            }
            blocking.Add(tf);
            groups.Bind(tf, nb.group);
        }
        tools.emplace_back(tool, &nb.face);
    }

    // the caps
    for (const auto& cap : caps) {
        TopoDS_Shape tool = clipToBox(planeRect(cap.plane, corners, 0.0), box);
        for (TopExp_Explorer exp(tool, TopAbs_FACE); exp.More(); exp.Next()) {
            capFaces.Add(exp.Current());
            groups.Bind(exp.Current(), cap.group);
        }
        tools.emplace_back(tool, &cap.face);
    }

    // The general fuse.
    BOPAlgo_Builder gf;
    TopTools_ListOfShape args;
    args.Append(solid);
    args.Append(box);
    for (const auto& t : tools) {
        args.Append(t.first);
    }
    FC_TIME_LOG(t, "tools");
    gf.SetArguments(args);
    gf.SetNonDestructive(true);
    if (fuzzy > 0) {
        gf.SetFuzzyValue(fuzzy);
    }
    gf.SetRunParallel(true);
    gf.Perform();
    if (gf.HasErrors()) {
        return fail(CellDraft::Boolean, "the general fuse failed");
    }
    const TopoDS_Shape& fused = gf.Shape();
    FC_TIME_LOG(t, "general fuse");

    auto images = [&gf](const TopoDS_Shape& s) {
        TopTools_MapOfShape res;
        if (const auto* lst = gf.Images().Seek(s)) {
            for (const auto& x : *lst) {
                res.Add(x);
            }
        }
        else {
            res.Add(s);
        }
        return res;
    };
    auto origins = [&gf](const TopoDS_Shape& s) {
        TopTools_ListOfShape res;
        if (const auto* lst = gf.Origins().Seek(s)) {
            res = *lst;
        }
        else {
            res.Append(s);
        }
        return res;
    };

    // The cells: pieces of the box. Pieces of the solid outside the box
    // are kept as they are.
    TopTools_MapOfShape boxCells = images(box);
    TopTools_MapOfShape solidPieces = images(solid);
    std::vector<TopoDS_Shape> cells;
    TopTools_IndexedMapOfShape cellIndex;
    for (TopExp_Explorer exp(fused, TopAbs_SOLID); exp.More(); exp.Next()) {
        if (cellIndex.Add(exp.Current()) > static_cast<int>(cells.size())) {
            cells.push_back(exp.Current());
        }
    }
    const int ncells = static_cast<int>(cells.size());
    std::vector<char> inBox(ncells), inS(ncells), between(ncells), inner(ncells);
    std::vector<gp_Pnt> points(ncells);
    std::vector<char> hasPoint(ncells, 0);
    for (int i = 0; i < ncells; ++i) {
        inBox[i] = boxCells.Contains(cells[i]);
        inS[i] = solidPieces.Contains(cells[i]);
        if (!inBox[i]) {
            continue;
        }
        gp_Pnt p;
        hasPoint[i] = interiorPoint(cells[i], p);
        if (!hasPoint[i]) {
            FC_LOG("no point inside cell " << i);
        }
        points[i] = p;
        double sp = gp_Vec(plane.Location(), p).Dot(gp_Vec(normal));
        double sn = gp_Vec(newPlane.Location(), p).Dot(gp_Vec(newNormal));
        between[i] = (sp < 0) != (sn < 0);
        inner[i] = sn < 0;
    }

    FC_TIME_LOG(t, "cell points");
    // What each face of the fuse is a piece of.
    struct FaceInfo
    {
        std::set<int> groups;
        bool fromF = false;
        bool blocks = false;
        bool cap = false;
        bool box = false;
        int neighbour = -1;
    };
    NCollection_DataMap<TopoDS_Shape, FaceInfo, TopTools_ShapeMapHasher> infos;
    std::set<int> neighbourGroups;
    for (const auto& nb : neighbours) {
        neighbourGroups.insert(nb.group);
    }
    auto info = [&](const TopoDS_Shape& f) -> const FaceInfo& {
        if (const auto* fi = infos.Seek(f)) {
            return *fi;
        }
        FaceInfo fi;
        // A piece on a plane shared by a face of the solid and a tool (a
        // neighbour's extension over a coplanar face, a cap) belongs to the
        // solid's face; one shared by a neighbour's extension and a cap, to
        // the neighbour.
        std::set<int> solidGroups, toolGroups, capGroups;
        for (const auto& o : origins(f)) {
            int g = NoGroup;
            if (const int* pg = groups.Seek(o)) {
                g = *pg;
            }
            if (g != NoGroup) {
                if (solidFaces.Contains(o)) {
                    solidGroups.insert(g);
                }
                else if (capFaces.Contains(o)) {
                    capGroups.insert(g);
                }
                else {
                    toolGroups.insert(g);
                }
            }
            if (fsetMap.Contains(o)) {
                fi.fromF = true;
            }
            if (blocking.Contains(o)) {
                fi.blocks = true;
                if (neighbourGroups.count(g)) {
                    fi.neighbour = g;
                }
            }
            if (capFaces.Contains(o)) {
                fi.cap = true;
            }
            if (boxFaces.Contains(o)) {
                fi.box = true;
            }
        }
        fi.groups = !solidGroups.empty() ? solidGroups
                                         : (!toolGroups.empty() ? toolGroups : capGroups);
        return *infos.Bound(f, fi);
    };

    if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
        for (int i = 0; i < ncells; ++i) {
            if (!inBox[i]) {
                continue;
            }
            const gp_Pnt& p = points[i];
            double sp = gp_Vec(plane.Location(), p).Dot(gp_Vec(normal));
            double sn = gp_Vec(newPlane.Location(), p).Dot(gp_Vec(newNormal));
            GProp_GProps props;
            BRepGProp::VolumeProperties(cells[i], props);
            int nf = 0;
            for (TopExp_Explorer exp(cells[i], TopAbs_FACE); exp.More(); exp.Next()) {
                if (isBoundaryFace(exp.Current()) && info(exp.Current()).fromF) {
                    ++nf;
                }
            }
            FC_LOG("cell " << i << " vol " << props.Mass() << " inS " << int(inS[i]) << " sp "
                           << sp << " sn " << sn << " F pieces " << nf << " p (" << p.X()
                           << ", " << p.Y() << ", " << p.Z() << ")");
        }
    }

    // face -> the cells it bounds
    TopTools_IndexedDataMapOfShapeListOfShape faceCells;
    for (int i = 0; i < ncells; ++i) {
        for (TopExp_Explorer exp(cells[i], TopAbs_FACE); exp.More(); exp.Next()) {
            if (!isBoundaryFace(exp.Current())) {
                continue;
            }
            if (TopTools_ListOfShape* lst = faceCells.ChangeSeek(exp.Current())) {
                lst->Append(cells[i]);
            }
            else {
                TopTools_ListOfShape l;
                l.Append(cells[i]);
                faceCells.Add(exp.Current(), l);
            }
        }
    }
    auto across = [&](const TopoDS_Shape& f, int i) {
        std::vector<int> res;
        if (const auto* lst = faceCells.Seek(f)) {
            for (const auto& c : *lst) {
                int j = cellIndex.FindIndex(c) - 1;
                if (j != i) {
                    res.push_back(j);
                }
            }
        }
        return res;
    };

    // The swept region: seeds beside the pieces of the drafted face, on the
    // side the new plane lies; the fill crosses any face but those of the
    // new plane, the drafted face and the neighbours' surfaces.
    std::vector<char> inK(ncells, 0);
    std::vector<int> lastNeighbour(ncells, -1);
    std::vector<int> todo;
    // A cell of the region that is not between the old and the new plane,
    // or that reaches the box, means the region is not closed: the face
    // does not meet some neighbour (or the neighbour is not extended far
    // enough, which a larger box tries again).
    auto leak = [&](int i) {
        TopoDS_Face nbFace;
        if (lastNeighbour[i] > 0) {
            nbFace = TopoDS::Face(solidFaces(lastNeighbour[i]));
        }
        FC_LOG("leak at cell " << i);
        return fail(CellDraft::NoClosure,
                    "the face does not meet its neighbours: the region it sweeps is not closed",
                    fset.front(),
                    nbFace);
    };
    auto enter = [&](int j) {
        if (!inBox[j] || !between[j] || !hasPoint[j]) {
            return false;
        }
        inK[j] = 1;
        todo.push_back(j);
        return true;
    };
    // Seeds: beside each piece of the drafted face, the cell on the side
    // the new plane lies -- outside the solid where it leans out, inside
    // where it leans in. A piece on the new plane (the hinge) seeds nothing.
    for (int k = 1; k <= faceCells.Extent(); ++k) {
        const TopoDS_Face& piece = TopoDS::Face(faceCells.FindKey(k));
        if (!info(piece).fromF) {
            continue;
        }
        gp_Pnt q;
        if (!BRepClass3d_SolidExplorer::FindAPointInTheFace(piece, q)) {
            continue;
        }
        double sn = gp_Vec(newPlane.Location(), q).Dot(gp_Vec(newNormal));
        double tol = 10 * std::max(Precision::Confusion(), BRep_Tool::Tolerance(piece));
        if (std::abs(sn) <= tol) {
            continue;
        }
        bool wantOutside = sn < 0;
        for (const auto& c : faceCells(k)) {
            int i = cellIndex.FindIndex(c) - 1;
            if (inK[i]) {
                continue;
            }
            // the piece as it lies in the cell: its normal points out of the cell
            for (TopExp_Explorer exp(c, TopAbs_FACE); exp.More(); exp.Next()) {
                if (!exp.Current().IsSame(piece) || !isBoundaryFace(exp.Current())) {
                    continue;
                }
                gp_Dir nc;
                if (!faceNormal(TopoDS::Face(exp.Current()), q, nc)) {
                    break;
                }
                bool outside = nc.Dot(normal) < 0;
                if (outside == wantOutside) {
                    FC_LOG("seed " << i);
                    if (!enter(i)) {
                        return leak(i);
                    }
                }
                break;
            }
        }
    }
    while (!todo.empty()) {
        int i = todo.back();
        todo.pop_back();
        for (TopExp_Explorer exp(cells[i], TopAbs_FACE); exp.More(); exp.Next()) {
            const TopoDS_Shape& f = exp.Current();
            if (!isBoundaryFace(f)) {
                continue;
            }
            const FaceInfo& fi = info(f);
            if (fi.neighbour >= 0) {
                lastNeighbour[i] = fi.neighbour;
            }
        }
        for (TopExp_Explorer exp(cells[i], TopAbs_FACE); exp.More(); exp.Next()) {
            const TopoDS_Shape& f = exp.Current();
            if (!isBoundaryFace(f)) {
                continue;
            }
            const FaceInfo& fi = info(f);
            if (fi.box) {
                return leak(i);
            }
            if (fi.blocks) {
                continue;
            }
            if (inner[i] && fi.cap) {
                continue;
            }
            for (int j : across(f, i)) {
                if (inK[j]) {
                    continue;
                }
                lastNeighbour[j] = lastNeighbour[i];
                if (!enter(j)) {
                    if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
                        std::ostringstream ss;
                        for (int g : fi.groups) {
                            ss << g << ' ';
                        }
                        BRepAdaptor_Surface fs(TopoDS::Face(f), false);
                        Bnd_Box fb;
                        BRepBndLib::Add(f, fb, false);
                        double a0, b0, c0, a1, b1, c1;
                        fb.Get(a0, b0, c0, a1, b1, c1);
                        FC_LOG("crossed from cell " << i << " to " << j << " a face of groups "
                               << ss.str() << "type " << int(fs.GetType()) << " bb (" << a0
                               << ", " << b0 << ", " << c0 << ")-(" << a1 << ", " << b1 << ", "
                               << c1 << ")");
                    }
                    return leak(j);
                }
            }
        }
    }

    FC_TIME_LOG(t, "swept region");
    std::vector<char> keep(ncells, 0);
    std::vector<int> kept;
    for (int i = 0; i < ncells; ++i) {
        if ((inS[i] && !inK[i]) || (inK[i] && inner[i])) {
            keep[i] = 1;
            kept.push_back(i);
        }
    }
    FC_LOG("cells " << ncells << ", in the box " << std::count(inBox.begin(), inBox.end(), 1)
                    << ", K " << std::count(inK.begin(), inK.end(), 1) << ", kept "
                    << kept.size());
    if (kept.empty()) {
        return fail(CellDraft::SplitsSolid, "nothing is left of the solid");
    }

    // The kept cells must be one piece, joined across faces.
    {
        std::vector<char> reached(ncells, 0);
        std::vector<int> stack {kept.front()};
        reached[kept.front()] = 1;
        size_t count = 1;
        while (!stack.empty()) {
            int i = stack.back();
            stack.pop_back();
            for (TopExp_Explorer exp(cells[i], TopAbs_FACE); exp.More(); exp.Next()) {
                if (!isBoundaryFace(exp.Current())) {
                    continue;
                }
                for (int j : across(exp.Current(), i)) {
                    if (keep[j] && !reached[j]) {
                        reached[j] = 1;
                        ++count;
                        stack.push_back(j);
                    }
                }
            }
        }
        if (count != kept.size()) {
            return fail(CellDraft::SplitsSolid, "the draft cuts the solid in pieces");
        }
    }

    // The boundary of the kept cells: faces of exactly one kept cell, as
    // they lie in it.
    TopTools_IndexedDataMapOfShapeListOfShape keptFaces;
    for (int i : kept) {
        for (TopExp_Explorer exp(cells[i], TopAbs_FACE); exp.More(); exp.Next()) {
            if (!isBoundaryFace(exp.Current())) {
                continue;
            }
            if (TopTools_ListOfShape* lst = keptFaces.ChangeSeek(exp.Current())) {
                lst->Append(exp.Current());
            }
            else {
                TopTools_ListOfShape l;
                l.Append(exp.Current());
                keptFaces.Add(exp.Current(), l);
            }
        }
    }
    BOPAlgo_ShellSplitter splitter;
    bool hasNewFace = false;
    for (int i = 1; i <= keptFaces.Extent(); ++i) {
        const auto& lst = keptFaces(i);
        if (lst.Extent() != 1) {
            continue;
        }
        splitter.AddStartElement(lst.First());
        for (const auto& o : origins(lst.First())) {
            if (newPlaneFaces.Contains(o)) {
                hasNewFace = true;
            }
        }
    }
    if (!hasNewFace) {
        return fail(CellDraft::FaceVanishes,
                    "the face is not in the result: its neighbours meet across it");
    }
    splitter.Perform();
    if (splitter.HasErrors() || splitter.Shells().IsEmpty()) {
        return fail(CellDraft::NotASolid, "the chosen cells make no shell");
    }
    BRep_Builder builder;
    TopoDS_Solid built;
    builder.MakeSolid(built);
    for (const auto& sh : splitter.Shells()) {
        TopoDS_Shape shell = sh;
        shell.Closed(true);
        builder.Add(built, shell);
    }

    FC_TIME_LOG(t, "cells chosen");
    // Put the pieces of one origin back together: faces only across an edge
    // between two pieces of the same face, edges only at a vertex where two
    // pieces of one edge meet.
    auto groupOf = [&](const TopoDS_Shape& f) {
        const FaceInfo& fi = info(f);
        return fi.groups.size() == 1 ? *fi.groups.begin() : NoGroup - 1;
    };
    std::vector<TopoDS_Shape> keptVertices;
    ShapeUpgrade_UnifySameDomain unify(built, true, true, false);
    unify.AllowInternalEdges(false);
    {
        TopTools_IndexedDataMapOfShapeListOfShape ef;
        TopExp::MapShapesAndUniqueAncestors(built, TopAbs_EDGE, TopAbs_FACE, ef);
        for (int i = 1; i <= ef.Extent(); ++i) {
            const auto& lst = ef(i);
            if (lst.Extent() != 2 || groupOf(lst.First()) < 0
                || groupOf(lst.First()) != groupOf(lst.Last())) {
                unify.KeepShape(ef.FindKey(i));
            }
        }
        TopTools_IndexedDataMapOfShapeListOfShape ve;
        TopExp::MapShapesAndUniqueAncestors(built, TopAbs_VERTEX, TopAbs_EDGE, ve);
        for (int i = 1; i <= ve.Extent(); ++i) {
            const auto& lst = ve(i);
            bool split = false;
            if (lst.Extent() == 2) {
                TopTools_ListOfShape o1 = origins(lst.First());
                TopTools_ListOfShape o2 = origins(lst.Last());
                split = o1.Extent() == 1 && o2.Extent() == 1 && o1.First().IsSame(o2.First())
                    && !o1.First().IsSame(lst.First()) && !o2.First().IsSame(lst.Last());
            }
            if (!split) {
                unify.KeepShape(ve.FindKey(i));
                keptVertices.push_back(ve.FindKey(i));
            }
        }
    }
    unify.Build();
    result = unify.Shape();
    Handle(BRepTools_History) mergeHistory = unify.History();
    FC_TIME_LOG(t, "merge");
    // The faces of a result made of pieces of the solid's faces only (none
    // on a tool), merged by the given history: for the check, they are the
    // solid's. A draft on #334's Draft input makes 28 faces, 22 of them
    // pieces of faces the box cut, put back together: the check of the
    // other 6 takes 0.18 s, of all 28 0.91 s.
    TopTools_IndexedMapOfShape builtFaces;
    TopExp::MapShapes(built, TopAbs_FACE, builtFaces);
    auto inputLike = [&](const Handle(BRepTools_History)& merge) {
        NCollection_DataMap<TopoDS_Shape, bool, TopTools_ShapeMapHasher> piece;
        for (int i = 1; i <= builtFaces.Extent(); ++i) {
            const TopoDS_Shape& b = builtFaces(i);
            TopTools_ListOfShape from = origins(b);
            bool isPiece = !from.IsEmpty();
            for (const auto& o : from) {
                isPiece = isPiece && solidFaces.Contains(o);
            }
            TopTools_ListOfShape to;
            if (merge.IsNull() || (!merge->IsRemoved(b) && merge->Modified(b).IsEmpty())) {
                to.Append(b);
            }
            else if (!merge->IsRemoved(b)) {
                to = merge->Modified(b);
            }
            for (const auto& r : to) {
                if (bool* p = piece.ChangeSeek(r)) {
                    *p = *p && isPiece;
                }
                else {
                    piece.Bind(r, isPiece);
                }
            }
        }
        TopTools_MapOfShape res;
        for (int i = 1; i <= solidFaces.Extent(); ++i) {
            res.Add(solidFaces(i));
        }
        for (decltype(piece)::Iterator it(piece); it.More(); it.Next()) {
            if (it.Value()) {
                res.Add(it.Key());
            }
        }
        return res;
    };
    // Every result is checked both ways before it is used: here, so that a
    // failure is tried again with a fuzzy fuse. UnifySameDomain can break
    // what it merges -- a curved face (#876's cones: self-intersecting
    // wires), or an edge joined on one face and not on the next (#474's
    // ramp parts: two collinear edges overlapping). Then the planar pieces
    // only are merged, else none: the chosen cells' solid as it is.
    std::string wrong = checkResult(result, inputLike(mergeHistory));
    if (!wrong.empty()) {
        FC_LOG("merge: " << wrong << "; planar pieces only");
        ShapeUpgrade_UnifySameDomain planar(built, true, true, false);
        planar.AllowInternalEdges(false);
        TopTools_IndexedDataMapOfShapeListOfShape ef;
        TopExp::MapShapesAndUniqueAncestors(built, TopAbs_EDGE, TopAbs_FACE, ef);
        for (int i = 1; i <= ef.Extent(); ++i) {
            const auto& lst = ef(i);
            bool keepEdge = lst.Extent() != 2 || groupOf(lst.First()) < 0
                || groupOf(lst.First()) != groupOf(lst.Last());
            for (const auto& f : lst) {
                if (BRepAdaptor_Surface(TopoDS::Face(f), false).GetType() != GeomAbs_Plane) {
                    keepEdge = true;
                }
            }
            if (keepEdge) {
                planar.KeepShape(ef.FindKey(i));
            }
        }
        for (const auto& v : keptVertices) {
            planar.KeepShape(v);
        }
        planar.Build();
        std::string planarWrong = checkResult(planar.Shape(), inputLike(planar.History()));
        if (planarWrong.empty()) {
            result = planar.Shape();
            mergeHistory = planar.History();
        }
        else {
            FC_LOG("planar merge: " << planarWrong << "; pieces kept");
            std::string builtWrong = checkResult(built, inputLike(Handle(BRepTools_History)()));
            if (!builtWrong.empty()) {
                return fail(CellDraft::NotASolid, builtWrong);
            }
            result = built;
            mergeHistory = new BRepTools_History;
        }
    }
    FC_TIME_LOG(t, "check");
    // The history: the fuse's, from the solid's own shapes and from the
    // tools to their owners; then the choice of cells; then the merge.
    Handle(BRepTools_History) h1 = new BRepTools_History;
    TopTools_IndexedMapOfShape fusedMap;
    TopExp::MapShapes(fused, fusedMap);
    const Handle(BRepTools_History)& gfh = gf.History();
    auto addFrom = [&](const TopoDS_Shape& from, const TopoDS_Shape& owner) {
        if (!BRepTools_History::IsSupportedType(from)) {
            return;
        }
        const TopTools_ListOfShape& mod = gfh->Modified(from);
        if (mod.IsEmpty()) {
            if (!gfh->IsRemoved(from) && fusedMap.Contains(from)) {
                h1->AddModified(owner, from);
            }
        }
        for (const auto& m : mod) {
            h1->AddModified(owner, m);
        }
        for (const auto& g : gfh->Generated(from)) {
            if (!g.IsSame(owner)) {
                h1->AddGenerated(owner, g);
            }
        }
    };
    {
        TopTools_IndexedMapOfShape solidMap;
        TopExp::MapShapes(solid, solidMap);
        for (int i = 1; i <= solidMap.Extent(); ++i) {
            const TopoDS_Shape& s = solidMap(i);
            if (!BRepTools_History::IsSupportedType(s)) {
                continue;
            }
            const TopTools_ListOfShape& mod = gfh->Modified(s);
            for (const auto& m : mod) {
                h1->AddModified(s, m);
            }
            for (const auto& g : gfh->Generated(s)) {
                h1->AddGenerated(s, g);
            }
            if (gfh->IsRemoved(s)) {
                h1->Remove(s);
            }
        }
        // A face whose tool owns pieces keeps its own pieces too.
        for (const auto& t : tools) {
            const TopoDS_Face& owner = *t.second;
            if (gfh->Modified(owner).IsEmpty() && fusedMap.Contains(owner)) {
                h1->AddModified(owner, owner);
            }
        }
        for (const auto& t : tools) {
            for (TopExp_Explorer exp(t.first, TopAbs_FACE); exp.More(); exp.Next()) {
                const int* g = groups.Seek(exp.Current());
                if (!g || *g == NoGroup) {
                    continue;
                }
                if (newPlaneFaces.Contains(exp.Current())) {
                    for (const auto& f : fset) {
                        addFrom(exp.Current(), f);
                    }
                }
                else {
                    addFrom(exp.Current(), *t.second);
                }
            }
        }
    }
    Handle(BRepTools_History) hsel = new BRepTools_History;
    {
        TopTools_IndexedMapOfShape builtMap;
        TopExp::MapShapes(built, builtMap);
        for (int i : kept) {
            hsel->AddModified(cells[i], built);
        }
        for (int i = 1; i <= fusedMap.Extent(); ++i) {
            const TopoDS_Shape& s = fusedMap(i);
            if (!BRepTools_History::IsSupportedType(s) || builtMap.Contains(s)) {
                continue;
            }
            if (s.ShapeType() == TopAbs_SOLID && keep[cellIndex.FindIndex(s) - 1]) {
                continue;
            }
            hsel->Remove(s);
        }
    }
    h1->Merge(*hsel);
    h1->Merge(*mergeHistory);
    history = h1;
    FC_TIME_LOG(t, "history");
    return true;
}

bool CellDraftOne::run()
{
    FC_TIME_INIT(t);
    bool prepared = prepare();
    FC_TIME_LOG(t, "prepare");
    if (!prepared) {
        return false;
    }
    if (!result.IsNull()) {
        return true;  // nothing to do
    }
    // A region that reaches the box is tried again in a larger box before
    // it counts as open. Cells that make no valid solid are tried again
    // with a fuzzy fuse: neighbours tangent to each other (#876's cone
    // corners between drafted walls) touch along lines, which the exact
    // fuse can leave in a sliver it does not split.
    for (double fz : {0.0, 1e-6}) {
        fuzzy = fz;
        for (double scale : {1.0, 4.0, 16.0}) {
            error = CellDraft::NoError;
            if (attempt(scale)) {
                return true;
            }
            if (error != CellDraft::NoClosure) {
                break;
            }
            FC_LOG("swept region open at scale " << scale);
        }
        if (error != CellDraft::NotASolid && error != CellDraft::Boolean) {
            return false;
        }
        FC_LOG("no valid solid, fuzzy " << fz);
    }
    return false;
}

}  // namespace Part

CellDraft::CellDraft(const TopoDS_Shape& shape)
    : myInput(shape)
{}

void CellDraft::Add(const TopoDS_Face& face,
                    const gp_Dir& direction,
                    double angle,
                    const gp_Pln& neutralPlane)
{
    myFaces.push_back({face, direction, angle, neutralPlane});
}

const char* CellDraft::ErrorName(ErrorType error)
{
    switch (error) {
        case NoError:
            return "NoError";
        case ParallelToNeutral:
            return "ParallelToNeutral";
        case AngleTooSteep:
            return "AngleTooSteep";
        case TurnsOver:
            return "TurnsOver";
        case UnsupportedSurface:
            return "UnsupportedSurface";
        case TangentNeighbour:
            return "TangentNeighbour";
        case NoClosure:
            return "NoClosure";
        case FaceVanishes:
            return "FaceVanishes";
        case SplitsSolid:
            return "SplitsSolid";
        case NotASolid:
            return "NotASolid";
        case Boolean:
            return "Boolean";
    }
    return "Unknown";
}

void CellDraft::setError(ErrorType error,
                         const TopoDS_Shape& face,
                         const TopoDS_Shape& neighbour,
                         const std::string& message)
{
    myError = error;
    myErrorFace = face.IsNull() ? TopoDS_Face() : TopoDS::Face(face);
    myErrorNeighbour = neighbour.IsNull() ? TopoDS_Face() : TopoDS::Face(neighbour);
    myErrorMessage = message;
}

TopoDS_Face CellDraft::inputFace(const TopoDS_Shape& face,
                                 const Handle(BRepTools_History) & history) const
{
    if (face.IsNull()) {
        return TopoDS_Face();
    }
    if (history.IsNull()) {
        return TopoDS::Face(face);
    }
    for (TopExp_Explorer exp(myInput, TopAbs_FACE); exp.More(); exp.Next()) {
        if (exp.Current().IsSame(face)) {
            return TopoDS::Face(exp.Current());
        }
        for (const auto& m : history->Modified(exp.Current())) {
            if (m.IsSame(face)) {
                return TopoDS::Face(exp.Current());
            }
        }
    }
    return TopoDS_Face();
}

void CellDraft::Build(const Message_ProgressRange& /*theRange*/)
{
    NotDone();
    myShape.Nullify();
    myResultMap.Clear();
    myHistory.Nullify();
    myError = NoError;
    myErrorFace.Nullify();
    myErrorNeighbour.Nullify();
    myErrorMessage.clear();

    TopoDS_Shape cur;
    int nsolids = 0;
    for (TopExp_Explorer exp(myInput, TopAbs_SOLID); exp.More(); exp.Next()) {
        cur = exp.Current();
        ++nsolids;
    }
    if (nsolids != 1) {
        setError(NotASolid, TopoDS_Shape(), TopoDS_Shape(), "the shape is not one solid");
        return;
    }

    // Work on a copy: the general fuse is told not to touch its arguments,
    // but the merge (UnifySameDomain) edits the edges it shares with them,
    // and a shape of PartDesign's is shared. The draft must leave its input
    // as it was (measured: #334's Draft input grew by 10k characters of
    // BRep a draft, and each draft took longer than the last). The copy has
    // no mesh either, which BRepCheck would otherwise walk.
    Handle(BRepTools_History) total;
    bool changed = false;
    std::vector<TopoDS_Face> done;
    try {
        BRepBuilderAPI_Copy copier(cur, true, false);
        TopTools_ListOfShape args;
        args.Append(cur);
        total = new BRepTools_History(args, copier);
        cur = copier.Shape();
        for (const auto& fd : myFaces) {
            if (std::any_of(done.begin(), done.end(), [&fd](const TopoDS_Face& f) {
                    return f.IsSame(fd.face);
                })) {
                continue;
            }
            done.push_back(fd.face);

            // the face's pieces in the current solid
            TopTools_IndexedMapOfShape curFaces;
            TopExp::MapShapes(cur, TopAbs_FACE, curFaces);
            std::vector<TopoDS_Face> faces;
            if (total.IsNull() || !total->IsRemoved(fd.face)) {
                const TopTools_ListOfShape* mod = nullptr;
                if (!total.IsNull()) {
                    mod = &total->Modified(fd.face);
                }
                if (!mod || mod->IsEmpty()) {
                    if (curFaces.Contains(fd.face)) {
                        faces.push_back(fd.face);
                    }
                }
                else {
                    for (const auto& m : *mod) {
                        if (m.ShapeType() == TopAbs_FACE && curFaces.Contains(m)) {
                            faces.push_back(TopoDS::Face(m));
                        }
                    }
                }
            }
            if (faces.empty()) {
                setError(FaceVanishes,
                         fd.face,
                         TopoDS_Shape(),
                         !changed ? "the face is not a face of the shape"
                                  : "the face is gone after drafting another");
                return;
            }

            CellDraftOne one(cur, faces, fd, myStopAtBody);
            if (!one.run()) {
                setError(one.error,
                         fd.face,
                         inputFace(one.errorNeighbour, total),
                         one.message);
                return;
            }
            if (one.history.IsNull()) {
                continue;  // the face makes the angle already
            }
            total->Merge(*one.history);
            cur = one.result;
            changed = true;
        }
    }
    catch (Standard_Failure& e) {
        std::string msg = "exception: ";
        if (e.GetMessageString()) {
            msg += e.GetMessageString();
        }
        setError(Boolean, TopoDS_Shape(), TopoDS_Shape(), msg);
        return;
    }

    myShape = cur;
    myHistory = total;
    Done();
}

// The history's list without repeats, in the order of the result's own
// shapes. Merging histories can put one shape in twice, which element naming
// would count as two, and orders a list by the maps it goes through, which
// hash shapes by address: two recomputes named the same edges differently.
void CellDraft::uniqueList(const TopTools_ListOfShape& from, TopTools_ListOfShape& to)
{
    if (myResultMap.IsEmpty() && !myShape.IsNull()) {
        TopExp::MapShapes(myShape, myResultMap);
    }
    std::vector<std::pair<int, TopoDS_Shape>> items;
    TopTools_MapOfShape seen;
    for (const auto& s : from) {
        if (seen.Add(s)) {
            int idx = myResultMap.FindIndex(s);
            items.emplace_back(idx > 0 ? idx : myResultMap.Extent() + 1 + int(items.size()), s);
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    for (const auto& item : items) {
        to.Append(item.second);
    }
}

const TopTools_ListOfShape& CellDraft::Modified(const TopoDS_Shape& shape)
{
    myGenerated.Clear();
    if (!myHistory.IsNull()) {
        uniqueList(myHistory->Modified(shape), myGenerated);
    }
    return myGenerated;
}

const TopTools_ListOfShape& CellDraft::Generated(const TopoDS_Shape& shape)
{
    myGenerated.Clear();
    if (!myHistory.IsNull()) {
        uniqueList(myHistory->Generated(shape), myGenerated);
    }
    return myGenerated;
}

bool CellDraft::IsDeleted(const TopoDS_Shape& shape)
{
    return !myHistory.IsNull() && myHistory->IsRemoved(shape);
}

std::string CellDraft::CheckDraft(const TopoDS_Shape& input,
                                  const TopoDS_Shape& result,
                                  const std::vector<TopoDS_Face>& faces,
                                  bool stopAtBody)
{
    TopTools_IndexedMapOfShape inputFaces;
    TopExp::MapShapes(input, TopAbs_FACE, inputFaces);
    TopTools_MapOfShape inputMap;
    for (int i = 1; i <= inputFaces.Extent(); ++i) {
        inputMap.Add(inputFaces(i));
    }
    std::vector<TopoDS_Shape> made;
    TopoDS_Compound region = draftRegion(result, inputMap, made);
    if (made.empty()) {
        return std::string();
    }
    std::string wrong = regionCheck(region, made);
    if (!wrong.empty() || !stopAtBody) {
        return wrong;
    }

    // The stop of section 4.9: does a face the draft made reach past a
    // plane the cell draft would cap the added side at?
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndUniqueAncestors(input, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    auto adjacent = [&](const TopoDS_Face& f, std::vector<TopoDS_Face>& to) {
        for (TopExp_Explorer exp(f, TopAbs_EDGE); exp.More(); exp.Next()) {
            if (BRep_Tool::Degenerated(TopoDS::Edge(exp.Current()))) {
                continue;
            }
            for (const auto& s : edgeFaces.FindFromKey(exp.Current())) {
                to.push_back(TopoDS::Face(inputFaces.FindKey(inputFaces.FindIndex(s))));
            }
        }
    };
    std::vector<Cap> caps;
    for (const auto& d : faces) {
        int idx = inputFaces.FindIndex(d);
        if (idx == 0) {
            continue;
        }
        TopoDS_Face f = TopoDS::Face(inputFaces.FindKey(idx));
        gp_Pln pf;
        gp_Dir nf;
        if (!facePlane(f, pf, nf)) {
            continue;
        }
        // the face and the faces coplanar with it beside it, as the cell
        // draft drafts them (CellDraftOne::prepare)
        TopTools_MapOfShape fset;
        std::vector<TopoDS_Face> todo {f};
        while (!todo.empty()) {
            TopoDS_Face g = todo.back();
            todo.pop_back();
            if (!fset.Add(g)) {
                continue;
            }
            std::vector<TopoDS_Face> adj;
            adjacent(g, adj);
            for (const auto& h : adj) {
                gp_Pln ph;
                gp_Dir nh;
                if (!fset.Contains(h) && facePlane(h, ph, nh) && nh.Dot(nf) > 1 - 1e-9
                    && pf.Distance(ph.Location()) < CoplanarTol) {
                    todo.push_back(h);
                }
            }
        }
        std::vector<TopoDS_Face> neighbours;
        TopTools_MapOfShape seen;
        for (TopTools_MapOfShape::Iterator it(fset); it.More(); it.Next()) {
            std::vector<TopoDS_Face> adj;
            adjacent(TopoDS::Face(it.Key()), adj);
            for (const auto& h : adj) {
                if (!fset.Contains(h) && seen.Add(h)) {
                    neighbours.push_back(h);
                }
            }
        }
        for (auto& c : findCaps(input, inputFaces, edgeFaces, fset, neighbours, pf, nf)) {
            caps.push_back(c);
        }
    }
    double past = 0.0;
    for (const auto& m : made) {
        for (const auto& p : samplePoints(m)) {
            for (const auto& c : caps) {
                past = std::max(past, gp_Vec(c.plane.Location(), p).Dot(gp_Vec(c.outward)) - c.tol);
            }
        }
    }
    if (past > Precision::Confusion()) {
        std::ostringstream ss;
        ss << "the body grows past a face that bounds it (by " << past << ")";
        return ss.str();
    }
    return std::string();
}

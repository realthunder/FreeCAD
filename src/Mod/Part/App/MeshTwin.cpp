/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <BRep_Builder.hxx>
#include <BRep_CurveRepresentation.hxx>
#include <BRep_PointOnCurve.hxx>
#include <BRep_PointOnCurveOnSurface.hxx>
#include <BRep_PointOnSurface.hxx>
#include <BRep_PointRepresentation.hxx>
#include <BRep_TVertex.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_TShape.hxx>

#if __has_include(<BRep_RepresentationLock.hxx>)
# include <BRep_RepresentationLock.hxx>
#endif

#include "MeshTwin.h"

using namespace Part;

namespace
{

#if __has_include(<BRep_RepresentationLock.hxx>)
using RepresentationLock = BRep_RepresentationLock;
#else
/// An OCCT without the fork's lock has no frozen shapes to guard either.
struct RepresentationLock
{
    explicit RepresentationLock(const TopoDS_TShape*)
    {}
};
#endif

// Named off the accessors, so the element types follow whatever this OCCT
// spells them as.
using CurveReps = std::remove_reference_t<decltype(std::declval<BRep_TEdge&>().ChangeCurves())>;
using PointReps = std::remove_reference_t<decltype(std::declval<BRep_TVertex&>().ChangePoints())>;
using Triangulations =
    std::remove_cv_t<std::remove_reference_t<decltype(std::declval<BRep_TFace&>().Triangulations())>>;

bool holds(const Triangulations& list, const Handle(Poly_Triangulation) & tri)
{
    for (Triangulations::Iterator it(list); it.More(); it.Next()) {
        if (it.Value() == tri) {
            return true;
        }
    }
    return false;
}

/// The "active" purpose bit lives in the triangulation object, and a mesher
/// replacing a carried one in a twin clears it there.
void markActive(const Handle(BRep_TFace) & face)
{
    const Handle(Poly_Triangulation)& active = face->ActiveTriangulation();
    if (!active.IsNull() && !(active->MeshPurpose() & Poly_MeshPurpose_Active)) {
        active->SetMeshPurpose(active->MeshPurpose() | Poly_MeshPurpose_Active);
    }
}

bool hasCurve(const Handle(BRep_TEdge) & edge)
{
    for (CurveReps::Iterator it(edge->Curves()); it.More(); it.Next()) {
        if (it.Value()->IsCurve3D() && !it.Value()->Curve3D().IsNull()) {
            return true;
        }
    }
    return false;
}

}  // namespace

/// One new TShape for each of the original's; the map is what keeps a
/// sub-shape held at two places one sub-shape.
class MeshTwin::Maker
{
public:
    Maker(MeshTwin& out, Resident resident)
        : out(out)
        , resident(resident)
    {}

    Handle(TopoDS_TShape) copy(const Handle(TopoDS_TShape) & original)
    {
        auto found = map.find(original.get());
        if (found != map.end()) {
            return found->second;
        }
        Handle(TopoDS_TShape) twin;
        switch (original->ShapeType()) {
            case TopAbs_FACE:
                twin = face(Handle(BRep_TFace)::DownCast(original));
                break;
            case TopAbs_EDGE:
                twin = edge(Handle(BRep_TEdge)::DownCast(original));
                break;
            case TopAbs_VERTEX:
                twin = vertex(Handle(BRep_TVertex)::DownCast(original));
                break;
            default:
                twin = original->EmptyCopy();
                break;
        }
        // The children as the original stores them -- their own orientation
        // and location -- on the twin's TShapes.
        TopoDS_Shape from;
        from.TShape(original);
        from.Orientation(TopAbs_FORWARD);
        TopoDS_Shape to;
        to.TShape(twin);
        to.Orientation(TopAbs_FORWARD);
        BRep_Builder builder;
        for (TopoDS_Iterator it(from, false, false); it.More(); it.Next()) {
            TopoDS_Shape child = it.Value();
            child.TShape(copy(it.Value().TShape()));
            builder.Add(to, child);
        }
        // After the children: adding one marks the parent modified, and a
        // parent that is not free takes none.
        twin->Closed(original->Closed());
        twin->Orientable(original->Orientable());
        twin->Infinite(original->Infinite());
        twin->Convex(original->Convex());
        twin->Modified(original->Modified());
        twin->Checked(original->Checked());
        twin->Free(original->Free());
        twin->Locked(original->Locked());
        map.emplace(original.get(), twin);
        return twin;
    }

    /// The polygons, once every face is known: an edge is reached through
    /// the first face that holds it, which need not be the one whose
    /// triangulation its polygon indexes.
    void polygons()
    {
        for (EdgePair& pair : out.edges) {
            RepresentationLock guard(pair.original.get());
            const bool keepOwn = resident == Resident::Carry || !hasCurve(pair.original);
            CurveReps& reps = pair.twin->ChangeCurves();
            for (CurveReps::Iterator it(pair.original->Curves()); it.More(); it.Next()) {
                const auto& rep = it.Value();
                if (rep->IsPolygon3D()) {
                    if (pair.polygonAtCopy.IsNull()) {
                        pair.polygonAtCopy = rep->Polygon3D();
                    }
                    if (keepOwn) {
                        reps.Append(rep->Copy());
                    }
                }
                else if (rep->IsPolygonOnTriangulation()) {
                    if (carried.count(rep->Triangulation().get())) {
                        reps.Append(rep->Copy());
                    }
                }
                else if (rep->IsPolygonOnSurface()) {
                    if (resident == Resident::Carry) {
                        reps.Append(rep->Copy());
                    }
                }
            }
        }
    }

private:
    Handle(TopoDS_TShape) face(const Handle(BRep_TFace) & original)
    {
        RepresentationLock guard(original.get());
        // The surface, its location and the tolerance
        Handle(BRep_TFace) twin = Handle(BRep_TFace)::DownCast(original->EmptyCopy());
        twin->NaturalRestriction(original->NaturalRestriction());
        if (original->NbTriangulations() > 0
            && (resident == Resident::Carry || original->Surface().IsNull())) {
            twin->Triangulations(original->Triangulations(), original->ActiveTriangulation());
            for (Triangulations::Iterator it(original->Triangulations()); it.More(); it.Next()) {
                carried.insert(it.Value().get());
            }
        }
        out.faces.push_back({original, twin, original->ActiveTriangulation()});
        return twin;
    }

    Handle(TopoDS_TShape) edge(const Handle(BRep_TEdge) & original)
    {
        RepresentationLock guard(original.get());
        // The curves and the curves on surfaces -- new entries on the same
        // geometry -- the tolerance and the flags; no polygon
        Handle(BRep_TEdge) twin = Handle(BRep_TEdge)::DownCast(original->EmptyCopy());
        out.edges.push_back({original, twin, Handle(Poly_Polygon3D)()});
        return twin;
    }

    Handle(TopoDS_TShape) vertex(const Handle(BRep_TVertex) & original)
    {
        RepresentationLock guard(original.get());
        // The point and the tolerance
        Handle(BRep_TVertex) twin = Handle(BRep_TVertex)::DownCast(original->EmptyCopy());
        PointReps& reps = twin->ChangePoints();
        for (PointReps::Iterator it(original->Points()); it.More(); it.Next()) {
            const auto& rep = it.Value();
            if (rep->IsPointOnCurveOnSurface()) {
                reps.Append(new BRep_PointOnCurveOnSurface(rep->Parameter(),
                                                           rep->PCurve(),
                                                           rep->Surface(),
                                                           rep->Location()));
            }
            else if (rep->IsPointOnCurve()) {
                reps.Append(new BRep_PointOnCurve(rep->Parameter(), rep->Curve(), rep->Location()));
            }
            else if (rep->IsPointOnSurface()) {
                reps.Append(new BRep_PointOnSurface(rep->Parameter(),
                                                    rep->Parameter2(),
                                                    rep->Surface(),
                                                    rep->Location()));
            }
        }
        return twin;
    }

    MeshTwin& out;
    Resident resident;
    std::unordered_map<const TopoDS_TShape*, Handle(TopoDS_TShape)> map;
    /// The triangulations the twin's faces took from the original's.
    std::unordered_set<const Poly_Triangulation*> carried;
};

MeshTwin::MeshTwin(const TopoDS_Shape& original, Resident resident)
{
    if (original.IsNull()) {
        return;
    }
    Maker maker(*this, resident);
    Handle(TopoDS_TShape) copy = maker.copy(original.TShape());
    maker.polygons();
    source = original;
    twin = original;
    twin.TShape(copy);
}

MeshTwin::Landed MeshTwin::land() const
{
    Landed res;
    // What this landing put on a face, and what it took off one: the edges'
    // polygons follow those two and nothing else. A polygon that indexes a
    // mesh somebody made on the original in the meantime is not the twin's
    // to remove.
    std::unordered_set<const Poly_Triangulation*> installed;
    std::unordered_set<const Poly_Triangulation*> dropped;
    for (const FacePair& pair : faces) {
        RepresentationLock guard(pair.original.get());
        if (pair.original->ActiveTriangulation() != pair.activeAtCopy) {
            ++res.overtaken;
            continue;
        }
        if (pair.twin->ActiveTriangulation() == pair.activeAtCopy) {
            // Left alone by the twin's mesher
            markActive(pair.original);
            continue;
        }
        for (Triangulations::Iterator it(pair.original->Triangulations()); it.More(); it.Next()) {
            if (!holds(pair.twin->Triangulations(), it.Value())) {
                dropped.insert(it.Value().get());
            }
        }
        for (Triangulations::Iterator it(pair.twin->Triangulations()); it.More(); it.Next()) {
            if (!holds(pair.original->Triangulations(), it.Value())) {
                installed.insert(it.Value().get());
            }
        }
        pair.original->Triangulations(pair.twin->Triangulations(),
                                      pair.twin->ActiveTriangulation());
        pair.original->Modified(true);
        ++res.faces;
    }
    for (const EdgePair& pair : edges) {
        RepresentationLock guard(pair.original.get());
        CurveReps& mine = pair.original->ChangeCurves();
        bool changed = false;
        for (CurveReps::Iterator it(mine); it.More();) {
            if (it.Value()->IsPolygonOnTriangulation()
                && dropped.count(it.Value()->Triangulation().get())) {
                mine.Remove(it);
                ++res.polygonsRemoved;
                changed = true;
            }
            else {
                it.Next();
            }
        }
        for (CurveReps::Iterator jt(pair.twin->Curves()); jt.More(); jt.Next()) {
            const auto& rep = jt.Value();
            if (rep->IsPolygonOnTriangulation()) {
                if (installed.count(rep->Triangulation().get())) {
                    mine.Append(rep->Copy());
                    ++res.polygonsAdded;
                    changed = true;
                }
                continue;
            }
            if (!rep->IsPolygon3D() || rep->Polygon3D() == pair.polygonAtCopy) {
                continue;
            }
            // An edge on no face: its own polygon is all the mesh it has
            Handle(BRep_CurveRepresentation) own;
            for (CurveReps::Iterator it(mine); it.More() && own.IsNull(); it.Next()) {
                if (it.Value()->IsPolygon3D()) {
                    own = it.Value();
                }
            }
            if ((own.IsNull() ? Handle(Poly_Polygon3D)() : own->Polygon3D())
                != pair.polygonAtCopy) {
                continue;  // overtaken
            }
            if (own.IsNull()) {
                mine.Append(rep->Copy());
            }
            else {
                own->Polygon3D(rep->Polygon3D());
            }
            ++res.polygonsAdded;
            changed = true;
        }
        if (changed) {
            pair.original->Modified(true);
        }
    }
    return res;
}

void MeshTwin::abandon() const
{
    for (const FacePair& pair : faces) {
        RepresentationLock guard(pair.original.get());
        markActive(pair.original);
    }
}

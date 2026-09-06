/***************************************************************************
 *   Copyright (c) 2013 Luke Parry <l.parry@warwick.ac.uk>                 *
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

//! a class to the projection of shapes, removal/identifying hidden lines and
//! converting the output for OCC HLR into the BaseGeom intermediate representation.

#include "PreCompiled.h"

#ifndef _PreComp_
#include <BRepAlgo_NormalProjection.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepLProp_CLProps.hxx>

#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRAlgo_BiPoint.hxx>
#include <HLRAlgo_EdgeIterator.hxx>
#include <HLRAlgo_EdgeStatus.hxx>
#include <HLRBRep_Data.hxx>
#include <HLRBRep_EdgeData.hxx>
#include <HLRBRep_FaceIterator.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <BRepLib_MakeEdge2d.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <HLRBRep_PolyAlgo.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#endif// #ifndef _PreComp_

#include <algorithm>
#include <chrono>

#include <Base/Console.h>
#include <Mod/Part/App/PartFeature.h>

#include "Cosmetic.h"
#include "DrawUtil.h"
#include "DrawViewDetail.h"
#include "DrawViewPart.h"
#include "GeometryObject.h"
#include "DrawProjectSplit.h"
#include "ShapeUtils.h"

using namespace TechDraw;
using namespace std;

using DU = DrawUtil;

GeometryObject::GeometryObject(const string& /*parent*/, TechDraw::DrawView* parentObj)
    : m_parent(parentObj), m_isoCount(0), m_isPersp(false), m_focus(100.0),
      m_usePolygonHLR(false), m_scrubCount(0)

{}

GeometryObject::~GeometryObject() { clear(); }

const BaseGeomPtrVector GeometryObject::getVisibleFaceEdges(const bool smooth,
                                                            const bool seam) const
{
    BaseGeomPtrVector result;
    bool smoothOK = smooth;
    bool seamOK = seam;

    for (auto& e : edgeGeom) {
        if (e->getHlrVisible()) {
            switch (e->getClassOfEdge()) {
                case ecHARD:
                case ecOUTLINE:
                    result.push_back(e);
                    break;
                case ecSMOOTH:
                    if (smoothOK) {
                        result.push_back(e);
                    }
                    break;
                case ecSEAM:
                    if (seamOK) {
                        result.push_back(e);
                    }
                    break;
                default:;
            }
        }
    }
    //debug
    //make compound of edges and save as brep file
    //    BRep_Builder builder;
    //    TopoDS_Compound comp;
    //    builder.MakeCompound(comp);
    //    for (auto& r: result) {
    //        builder.Add(comp, r->getOCCEdge());
    //    }
    //    BRepTools::Write(comp, "GOVizFaceEdges.brep");            //debug

    return result;
}


void GeometryObject::clear()
{
    //shared pointers will delete v/e/f when reference counts go to zero.

    vertexGeom.clear();
    faceGeom.clear();
    edgeGeom.clear();
    m_edgeSource.Clear();
}


//===========================================================================
// The HLR traversals, reimplemented so they can say where an edge came from
//===========================================================================
//
// HLRBRep_HLRToShape and HLRBRep_PolyHLRToShape both know the source element
// of every edge they emit and then throw it away: their public API returns a
// bare compound.  These two traversals are the same walks, in the same order,
// emitting the same edges, and they hand the source back with each one
// (docs/TopoNamingEnhance.md sec 3.5).  Nothing here is a heuristic and there
// is no second HLR pass -- the correspondence is the algorithm's own.
//
// The traversals only ever record indices and shapes.  Turning those into
// element names is done on the main thread by nameEdgeGeometry, because
// reading an element map adopts the document's App::StringHasher and that has
// no locking (sec 8.2).

namespace
{

//! HLRBRep_HLRToShape::DrawEdge.  ie is the index of ed in the data
//! structure's EDataArray, which is also its index in EdgeMap.
void hlrDrawEdge(bool visible, bool inFace, int typ, HLRBRep_EdgeData& ed, int ie,
                 TopoDS_Shape& result, bool& added,
                 std::vector<std::pair<TopoDS_Shape, int>>& produced)
{
    bool todraw = false;
    if (inFace) {
        todraw = true;
    }
    else if (typ == 3) {
        todraw = ed.Rg1Line() && !ed.RgNLine();
    }
    else if (typ == 4) {
        todraw = ed.RgNLine();
    }
    else {
        todraw = !ed.Rg1Line();
    }
    if (!todraw) {
        return;
    }

    double sta, end;
    float tolsta, tolend;
    BRep_Builder builder;
    HLRAlgo_EdgeIterator it;
    if (visible) {
        for (it.InitVisible(ed.Status()); it.MoreVisible(); it.NextVisible()) {
            it.Visible(sta, tolsta, end, tolend);
            TopoDS_Edge edge = HLRBRep::MakeEdge(ed.Geometry(), sta, end);
            if (!edge.IsNull()) {
                builder.Add(result, edge);
                produced.emplace_back(edge, ie);
                added = true;
            }
        }
    }
    else {
        for (it.InitHidden(ed.Status()); it.MoreHidden(); it.NextHidden()) {
            it.Hidden(sta, tolsta, end, tolend);
            TopoDS_Edge edge = HLRBRep::MakeEdge(ed.Geometry(), sta, end);
            if (!edge.IsNull()) {
                builder.Add(result, edge);
                produced.emplace_back(edge, ie);
                added = true;
            }
        }
    }
}

//! HLRBRep_HLRToShape::DrawFace
void hlrDrawFace(bool visible, int typ, int iface, const Handle(HLRBRep_Data)& ds,
                 TopoDS_Shape& result, bool& added,
                 std::vector<std::pair<TopoDS_Shape, int>>& produced)
{
    HLRBRep_FaceIterator itf;
    for (itf.InitEdge(ds->FDataArray().ChangeValue(iface)); itf.MoreEdge(); itf.NextEdge()) {
        int ie = itf.Edge();
        HLRBRep_EdgeData& edf = ds->EDataArray().ChangeValue(ie);
        if (edf.Used()) {
            continue;
        }

        bool todraw;
        if (typ == 1) {
            todraw = itf.IsoLine();
        }
        else if (typ == 2) {// outlines
            todraw = itf.Internal();
        }
        else if (typ == 3) {
            todraw = edf.Rg1Line() && !edf.RgNLine() && !itf.OutLine();
        }
        else if (typ == 4) {
            todraw = edf.RgNLine() && !itf.OutLine();
        }
        else {
            todraw = !itf.IsoLine() && !itf.Internal() && (!edf.Rg1Line() || itf.OutLine());
        }

        if (todraw) {
            hlrDrawEdge(visible, true, typ, edf, ie, result, added, produced);
            edf.Used(true);
        }
        else if ((typ > 4 || typ == 2) && edf.Rg1Line() && !itf.OutLine()) {
            //sharp or outlines: give the edge a second face to be drawn from
            int hc = edf.HideCount();
            if (hc > 0) {
                edf.Used(true);
            }
            else {
                edf.HideCount(hc + 1);
            }
        }
        else {
            edf.Used(true);
        }
    }
}

//! HLRBRep_HLRToShape::InternalCompound for the whole projection (no shape
//! filter, and 2D output), plus the source edge index of every edge emitted.
TopoDS_Shape hlrInternalCompound(const Handle(HLRBRep_Algo)& algo, int typ, bool visible,
                                 std::vector<std::pair<TopoDS_Shape, int>>& produced)
{
    Handle(HLRBRep_Data) ds = algo->DataStructure();
    if (ds.IsNull()) {
        return TopoDS_Shape();
    }

    ds->Projector().Scaled(true);
    const int e1 = 1;
    const int e2 = ds->NbEdges();
    const int f1 = 1;
    const int f2 = ds->NbFaces();

    TopoDS_Shape result;
    BRep_Builder builder;
    builder.MakeCompound(TopoDS::Compound(result));

    for (int ie = e1; ie <= e2; ie++) {
        HLRBRep_EdgeData& ed = ds->EDataArray().ChangeValue(ie);
        if (ed.Selected() && !ed.Vertical()) {
            ed.Used(false);
            ed.HideCount(0);
        }
        else {
            ed.Used(true);
        }
    }

    bool added = false;
    for (int iface = f1; iface <= f2; iface++) {
        hlrDrawFace(visible, typ, iface, ds, result, added, produced);
    }
    if (typ >= 3) {
        for (int ie = e1; ie <= e2; ie++) {
            HLRBRep_EdgeData& ed = ds->EDataArray().ChangeValue(ie);
            if (!ed.Used()) {
                hlrDrawEdge(visible, false, typ, ed, ie, result, added, produced);
                ed.Used(true);
            }
        }
    }
    ds->Projector().Scaled(false);

    if (!added) {
        produced.clear();
        return TopoDS_Shape();
    }
    return result;
}

//! One segment of the polygon algorithm's output, with the shape it came from.
struct PolySegment
{
    gp_Pnt2d first;
    gp_Pnt2d last;
    bool rg1Line;
    bool rgNLine;
    bool outLine;
    bool intLine;
    bool visible;
    TopoDS_Shape source;
};

//! HLRBRep_PolyHLRToShape::Update -- the pass that turns the polygon
//! algorithm's hidden line result into 2D segments.  Every segment reports
//! the shape it came from, which is what this keeps.
void polySegments(const Handle(HLRBRep_PolyAlgo)& algo, std::vector<PolySegment>& segments)
{
    double sta = 0.0, end = 0.0;
    float tolsta = 0.0F, tolend = 0.0F;
    HLRAlgo_EdgeIterator it;
    HLRAlgo_EdgeStatus status;
    TopoDS_Shape source;
    bool reg1 = false, regn = false, outl = false, intl = false;
    const gp_Trsf& projection = algo->Projector().Transformation();

    for (algo->InitHide(); algo->MoreHide(); algo->NextHide()) {
        HLRAlgo_BiPoint::PointsT& points = algo->Hide(status, source, reg1, regn, outl, intl);
        gp_XYZ start3d = points.Pnt1;
        gp_XYZ end3d = points.Pnt2;
        projection.Transforms(start3d);
        projection.Transforms(end3d);
        const gp_XY start2d(start3d.X(), start3d.Y());
        const gp_XY end2d(end3d.X(), end3d.Y());
        const gp_XY along = end2d - start2d;
        if (along.Modulus() <= 1.e-10) {
            continue;
        }
        for (it.InitVisible(status); it.MoreVisible(); it.NextVisible()) {
            it.Visible(sta, tolsta, end, tolend);
            segments.push_back({gp_Pnt2d(start2d + sta * along), gp_Pnt2d(start2d + end * along),
                                reg1, regn, outl, intl, true, source});
        }
        for (it.InitHidden(status); it.MoreHidden(); it.NextHidden()) {
            it.Hidden(sta, tolsta, end, tolend);
            segments.push_back({gp_Pnt2d(start2d + sta * along), gp_Pnt2d(start2d + end * along),
                                reg1, regn, outl, intl, false, source});
        }
    }
}

//! HLRBRep_PolyHLRToShape::InternalCompound, over the segments above.  The
//! type codes are the polygon algorithm's own and differ from the exact one's:
//! 1 outline, 2 smooth, 3 seam, 4 hard.
TopoDS_Shape polyInternalCompound(const std::vector<PolySegment>& segments, int typ, bool visible,
                                  std::vector<std::pair<TopoDS_Shape, TopoDS_Shape>>& produced)
{
    TopoDS_Shape result;
    BRep_Builder builder;
    builder.MakeCompound(TopoDS::Compound(result));

    bool added = false;
    for (const auto& segment : segments) {
        if (segment.visible != visible) {
            continue;
        }
        bool todraw;
        if (typ == 1) {
            todraw = segment.intLine;
        }
        else if (typ == 2) {
            todraw = segment.rg1Line && !segment.rgNLine && !segment.outLine;
        }
        else if (typ == 3) {
            todraw = segment.rgNLine && !segment.outLine;
        }
        else {
            todraw = !segment.intLine && (!segment.rg1Line || segment.outLine);
        }
        if (!todraw || segment.first.SquareDistance(segment.last) <= 1.e-20) {
            continue;
        }
        TopoDS_Edge edge = BRepLib_MakeEdge2d(segment.first, segment.last);
        builder.Add(result, edge);
        produced.emplace_back(edge, segment.source);
        added = true;
    }

    if (!added) {
        produced.clear();
        return TopoDS_Shape();
    }
    return result;
}

}// namespace

//! The mirror of ShapeUtils::invertGeometry, with the per-edge association
//! carried across the copy that mirror makes.  The transformation is composed
//! exactly as mirrorShape composes it, so the geometry is unchanged.
TopoDS_Shape GeometryObject::invertAndTrack(const TopoDS_Shape& compound,
                                            GeometryObject::ShapeIndexMap& sourceOf)
{
    if (compound.IsNull()) {
        return compound;
    }

    gp_Trsf transform;
    transform.SetScale(gp_Pnt(0.0, 0.0, 0.0), 1.0);
    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0, -1, 0)));
    transform.Multiply(mirror);

    BRepBuilderAPI_Transform mkTrf(compound, transform);
    TopoDS_Shape mirrored = mkTrf.Shape();

    ShapeIndexMap moved;
    for (ShapeIndexMap::Iterator it(sourceOf); it.More(); it.Next()) {
        TopoDS_Shape after = mkTrf.ModifiedShape(it.Key());
        if (after.IsNull()) {
            after = it.Key();
        }
        moved.Bind(after, it.Value());
    }
    sourceOf = moved;

    return mirrored;
}

//! Give every projected edge the name of the element it was projected from.
//! Called on the main thread once the projection has landed.
void GeometryObject::nameEdgeGeometry()
{
    if (m_projectionShape.isNull() || m_projectionShape.getElementMapSize() == 0) {
        return;
    }

    std::map<std::string, int> ordinals;
    for (auto& geom : edgeGeom) {
        if (!geom || geom->getRef3d() <= 0) {
            continue;
        }
        Data::MappedName source = m_projectionShape.getMappedName(
            Data::IndexedName::fromConst("Edge", geom->getRef3d()));
        if (source.empty()) {
            continue;
        }

        std::string sourceName = source.toString();
        std::ostringstream stem;
        stem << sourceName << ";HLR:" << (geom->getHlrVisible() ? 'V' : 'H')
             << edgeClassLetter(geom->getClassOfEdge()) << ':';
        //one source edge can be broken into several fragments by hiding, so
        //the ordinal is what tells the fragments apart
        int ordinal = ordinals[stem.str()]++;

        geom->setSource3d(sourceName);
        geom->setHlrName(stem.str() + std::to_string(ordinal));
    }
}

//! the single letter a projected edge's name carries for its class
char GeometryObject::edgeClassLetter(edgeClass category)
{
    switch (category) {
        case ecUVISO: return 'I';
        case ecOUTLINE: return 'O';
        case ecSMOOTH: return 'S';
        case ecSEAM: return 'E';
        case ecHARD: return 'H';
        default: return 'X';
    }
}

void GeometryObject::projectShape(const Part::TopoShape& inShape, const gp_Ax2& viewAxis)
{
//    Base::Console().Message("GO::projectShape()\n");
    clear();
    m_projectionShape = inShape;

    Handle(HLRBRep_Algo) brep_hlr;
    try {
        brep_hlr = new HLRBRep_Algo();
        //        brep_hlr->Debug(true);
        brep_hlr->Add(inShape.getShape(), m_isoCount);
        if (m_isPersp) {
            double fLength = std::max(Precision::Confusion(), m_focus);
            HLRAlgo_Projector projector(viewAxis, fLength);
            brep_hlr->Projector(projector);
        }
        else {
            HLRAlgo_Projector projector(viewAxis);
            brep_hlr->Projector(projector);
        }
        brep_hlr->Update();
        brep_hlr->Hide();
    }
    catch (const Standard_Failure& e) {
        Base::Console().Error("GO::projectShape - OCC error - %s - while projecting shape\n",
                              e.GetMessageString());
        THROWM(Base::RuntimeError, "GeometryObject::projectShape - OCC error")
    }
    catch (...) {
        THROWM(Base::RuntimeError, "GeometryObject::projectShape - unknown error")
    }

    try {
        //the traversal above HLRToShape's, which reports the source edge of
        //every edge it emits.  The type codes are HLRToShape's own:
        //1 iso, 2 outline, 3 smooth, 4 seam, 5 hard.
        TopTools_IndexedMapOfShape sourceEdges;
        TopExp::MapShapes(m_projectionShape.getShape(), TopAbs_EDGE, sourceEdges);
        Handle(HLRBRep_Data) ds = brep_hlr->DataStructure();

        auto emit = [&](int typ, bool visible, TopoDS_Shape& target) {
            std::vector<std::pair<TopoDS_Shape, int>> produced;
            TopoDS_Shape compound = hlrInternalCompound(brep_hlr, typ, visible, produced);
            if (compound.IsNull()) {
                return;
            }
            BRepLib::BuildCurves3d(compound);

            //the HLR edge index becomes the projection shape's own Edge<n>
            //index; a silhouette is an edge HLR invented and has none
            ShapeIndexMap sourceOf;
            if (!ds.IsNull()) {
                auto& edgeMap = ds->EdgeMap();
                for (const auto& item : produced) {
                    if (item.second < 1 || item.second > edgeMap.Extent()) {
                        continue;
                    }
                    int index = sourceEdges.FindIndex(edgeMap.FindKey(item.second));
                    if (index > 0) {
                        sourceOf.Bind(item.first, index);
                    }
                }
            }

            target = invertAndTrack(compound, sourceOf);
            for (ShapeIndexMap::Iterator it(sourceOf); it.More(); it.Next()) {
                m_edgeSource.Bind(it.Key(), it.Value());
            }
        };

        emit(5, true, visHard);
        emit(3, true, visSmooth);
        emit(4, true, visSeam);
        emit(2, true, visOutline);
        emit(1, true, visIso);
        emit(5, false, hidHard);
        emit(3, false, hidSmooth);
        emit(4, false, hidSeam);
        emit(2, false, hidOutline);
        emit(1, false, hidIso);
    }
    catch (const Standard_Failure&) {
        throw Base::RuntimeError(
            "GeometryObject::projectShape - OCC error occurred while extracting edges");
    }
    catch (...) {
        throw Base::RuntimeError(
            "GeometryObject::projectShape - unknown error occurred while extracting edges");
    }

    makeTDGeometry();
}

//convert the hlr output into TD Geometry
void GeometryObject::makeTDGeometry()
{
//    Base::Console().Message("GO::makeTDGeometry()\n");
    extractGeometry(TechDraw::ecHARD,                   //always show the hard&outline visible lines
                        true);
    extractGeometry(TechDraw::ecOUTLINE,
                        true);

    const DrawViewPart* dvp = Base::freecad_dynamic_cast<const DrawViewPart>(m_parent.getObject());
    if (!dvp) {
        return;//some routines do not have a dvp (ex shape outline)
    }

    if (dvp->SmoothVisible.getValue()) {
        extractGeometry(TechDraw::ecSMOOTH, true);
    }
    if (dvp->SeamVisible.getValue()) {
        extractGeometry(TechDraw::ecSEAM, true);
    }
    if ((dvp->IsoVisible.getValue()) && (dvp->IsoCount.getValue() > 0)) {
        extractGeometry(TechDraw::ecUVISO, true);
    }
    if (dvp->HardHidden.getValue()) {
        extractGeometry(TechDraw::ecHARD, false);
        extractGeometry(TechDraw::ecOUTLINE, false);
    }
    if (dvp->SmoothHidden.getValue()) {
        extractGeometry(TechDraw::ecSMOOTH, false);
    }
    if (dvp->SeamHidden.getValue()) {
        extractGeometry(TechDraw::ecSEAM, false);
    }
    if (dvp->IsoHidden.getValue() && (dvp->IsoCount.getValue() > 0)) {
        extractGeometry(TechDraw::ecUVISO, false);
    }
}

//mirror a shape thru XZ plane for Qt's inverted Y coordinate
TopoDS_Shape ShapeUtils::invertGeometry(const TopoDS_Shape s)
{
    if (s.IsNull()) {
        return s;
    }

    gp_Trsf mirrorY;
    gp_Pnt org(0.0, 0.0, 0.0);
    gp_Dir Y(0.0, 1.0, 0.0);
    gp_Ax2 mirrorPlane(org, Y);
    mirrorY.SetMirror(mirrorPlane);
    BRepBuilderAPI_Transform mkTrf(s, mirrorY, true);
    return mkTrf.Shape();
}

//!set up a hidden line remover and project a shape with it
void GeometryObject::projectShapeWithPolygonAlgo(const Part::TopoShape& input,
                                                 const gp_Ax2& viewAxis)
{
//    Base::Console().Message("GO::projectShapeWithPolygonAlgo()\n");
    // Clear previous Geometry
    clear();

    //work around for Mantis issue #3332
    //if 3332 gets fixed in OCC, this will produce shifted views and will need
    //to be reverted.
    Part::TopoShape inCopy;
    if (!m_isPersp) {
        gp_Pnt gCenter = ShapeUtils::findCentroid(input.getShape(), viewAxis);
        Base::Vector3d motion(-gCenter.X(), -gCenter.Y(), -gCenter.Z());
        inCopy = ShapeUtils::moveShape(input, motion);
    }
    else {
        inCopy = input.makECopy();
    }
    m_projectionShape = inCopy;

    Handle(HLRBRep_PolyAlgo) brep_hlrPoly;

    try {
        // HLRBRep_PolyAlgo will fail if the whole input shape has not been meshed.
        // meshing the faces is not sufficient.
        BRepMesh_IncrementalMesh(inCopy.getShape(), 0.10);

        brep_hlrPoly = new HLRBRep_PolyAlgo();
        brep_hlrPoly->Load(inCopy.getShape());

        if (m_isPersp) {
            double fLength = std::max(Precision::Confusion(), m_focus);
            HLRAlgo_Projector projector(viewAxis, fLength);
            brep_hlrPoly->Projector(projector);
        }
        else {// non perspective
            HLRAlgo_Projector projector(viewAxis);
            brep_hlrPoly->Projector(projector);
        }
        brep_hlrPoly->Update();
    }
    catch (const Standard_Failure& e) {
        Base::Console().Error(
            "GO::projectShapeWithPolygonAlgo - OCC error - %s - while projecting shape\n",
            e.GetMessageString());
        THROWM(Base::RuntimeError, "GeometryObject::projectShapeWithPolygonAlgo - OCC error")
    }
    catch (...) {
        THROWM(Base::RuntimeError, "GeometryObject::projectShapeWithPolygonAlgo - unknown error")
    }

    try {
        //PolyHLRToShape's Update and InternalCompound, done here so the shape
        //each segment came from is kept.  The polygon algorithm's type codes
        //are its own and differ from the exact one's: 1 outline, 2 smooth,
        //3 seam, 4 hard.  It produces no isoparametric lines.
        std::vector<PolySegment> segments;
        polySegments(brep_hlrPoly, segments);

        TopTools_IndexedMapOfShape sourceEdges;
        TopExp::MapShapes(m_projectionShape.getShape(), TopAbs_EDGE, sourceEdges);

        auto emit = [&](int typ, bool visible, TopoDS_Shape& target) {
            std::vector<std::pair<TopoDS_Shape, TopoDS_Shape>> produced;
            TopoDS_Shape compound = polyInternalCompound(segments, typ, visible, produced);
            if (compound.IsNull()) {
                return;
            }
            BRepLib::BuildCurves3d(compound);

            ShapeIndexMap sourceOf;
            for (const auto& item : produced) {
                //a segment off a silhouette reports the face, not an edge
                if (item.second.IsNull() || item.second.ShapeType() != TopAbs_EDGE) {
                    continue;
                }
                int index = sourceEdges.FindIndex(item.second);
                if (index > 0) {
                    sourceOf.Bind(item.first, index);
                }
            }

            target = invertAndTrack(compound, sourceOf);
            for (ShapeIndexMap::Iterator it(sourceOf); it.More(); it.Next()) {
                m_edgeSource.Bind(it.Key(), it.Value());
            }
        };

        emit(4, true, visHard);
        emit(2, true, visSmooth);
        emit(3, true, visSeam);
        emit(1, true, visOutline);
        emit(4, false, hidHard);
        emit(2, false, hidSmooth);
        emit(3, false, hidSeam);
        emit(1, false, hidOutline);
    }
    catch (const Standard_Failure& e) {
        Base::Console().Error(
            "GO::projectShapeWithPolygonAlgo - OCC error - %s - while extracting edges\n",
            e.GetMessageString());
        throw Base::RuntimeError("GeometryObject::projectShapeWithPolygonAlgo - OCC error occurred "
                                 "while extracting edges");
    }
    catch (...) {
        throw Base::RuntimeError("GeometryObject::projectShapeWithPolygonAlgo - unknown error "
                                 "occurred while extracting edges");
    }

    makeTDGeometry();
}

//project the edges in shape onto XY.mirrored plane of CS.  mimics the projection
//of the main hlr routine. Only the visible hard edges are returned, so this method
//is only suitable for simple shapes that have no hidden edges, like faces or wires.
//TODO: allow use of perspective projector
TopoDS_Shape GeometryObject::projectSimpleShape(const TopoDS_Shape& shape, const gp_Ax2& CS)
{
    //    Base::Console().Message("GO::()\n");
    if (shape.IsNull()) {
        THROWM(Base::ValueError, "GO::projectSimpleShape - input shape is NULL")
    }

    HLRBRep_Algo* brep_hlr = new HLRBRep_Algo();
    brep_hlr->Add(shape);
    HLRAlgo_Projector projector(CS);
    brep_hlr->Projector(projector);
    brep_hlr->Update();
    brep_hlr->Hide();

    HLRBRep_HLRToShape hlrToShape(brep_hlr);
    TopoDS_Shape hardEdges = hlrToShape.VCompound();
    BRepLib::BuildCurves3d(hardEdges);
    hardEdges =ShapeUtils::invertGeometry(hardEdges);

    return hardEdges;
}

//project the edges of a shape onto the XY plane of projCS. This does not give
//the same result as the hlr projections
TopoDS_Shape GeometryObject::simpleProjection(const TopoDS_Shape& shape, const gp_Ax2& projCS)
{
    gp_Pln plane(projCS);
    TopoDS_Face paper = BRepBuilderAPI_MakeFace(plane);
    BRepAlgo_NormalProjection projector(paper);
    projector.Add(shape);
    projector.Build();
    return projector.Projection();
}

TopoDS_Shape GeometryObject::projectFace(const TopoDS_Shape& face, const gp_Ax2& CS)
{
    //    Base::Console().Message("GO::projectFace()\n");
    if (face.IsNull()) {
        THROWM(Base::ValueError, "GO::projectFace - input Face is NULL")
    }

    HLRBRep_Algo* brep_hlr = new HLRBRep_Algo();
    brep_hlr->Add(face);
    HLRAlgo_Projector projector(CS);
    brep_hlr->Projector(projector);
    brep_hlr->Update();
    brep_hlr->Hide();

    HLRBRep_HLRToShape hlrToShape(brep_hlr);
    TopoDS_Shape hardEdges = hlrToShape.VCompound();
    BRepLib::BuildCurves3d(hardEdges);
    hardEdges =ShapeUtils::invertGeometry(hardEdges);

    return hardEdges;
}

//!add edges meeting filter criteria for category, visibility
void GeometryObject::extractGeometry(edgeClass category, bool hlrVisible)
{
    //    Base::Console().Message("GO::extractGeometry(%d, %d)\n", category, hlrVisible);
    TopoDS_Shape filtEdges;
    if (hlrVisible) {
        switch (category) {
            case ecHARD:
                filtEdges = visHard;
                break;
            case ecOUTLINE:
                filtEdges = visOutline;
                break;
            case ecSMOOTH:
                filtEdges = visSmooth;
                break;
            case ecSEAM:
                filtEdges = visSeam;
                break;
            case ecUVISO:
                filtEdges = visIso;
                break;
            default:
                Base::Console().Warning(
                    "GeometryObject::ExtractGeometry - unsupported hlrVisible edgeClass: %d\n",
                    static_cast<int>(category));
                return;
        }
    }
    else {
        switch (category) {
            case ecHARD:
                filtEdges = hidHard;
                break;
            case ecOUTLINE:
                filtEdges = hidOutline;
                break;
            case ecSMOOTH:
                filtEdges = hidSmooth;
                break;
            case ecSEAM:
                filtEdges = hidSeam;
                break;
            case ecUVISO:
                filtEdges = hidIso;
                break;
            default:
                Base::Console().Warning(
                    "GeometryObject::ExtractGeometry - unsupported hidden edgeClass: %d\n",
                    static_cast<int>(category));
                return;
        }
    }

    addGeomFromCompound(filtEdges, category, hlrVisible);
}

//! update edgeGeom and vertexGeom from Compound of edges
void GeometryObject::addGeomFromCompound(TopoDS_Shape edgeCompound, edgeClass category,
                                         bool hlrVisible)
{
//    Base::Console().Message("GO::addGeomFromCompound(%d, %d)\n", category, hlrVisible);
    if (edgeCompound.IsNull()) {
        return;    // There is no OpenCascade Geometry to be calculated
    }

    // remove overlapping edges
    TopoDS_Shape cleanShape;
    if (m_scrubCount > 0) {
        std::vector<TopoDS_Edge> edgeVector = DU::shapeToVector(edgeCompound);
        for (int iPass = 0; iPass < m_scrubCount; iPass++)  {
            edgeVector = DrawProjectSplit::removeOverlapEdges(edgeVector);
        }
        bool invertResult = false;
        cleanShape = DU::vectorToCompound(edgeVector, invertResult);

    } else {
        cleanShape = edgeCompound;
    }

    BaseGeomPtr base;
    TopExp_Explorer edges(cleanShape, TopAbs_EDGE);
    int i = 1;
    for (; edges.More(); edges.Next(), i++) {
        const TopoDS_Edge& edge = TopoDS::Edge(edges.Current());
        if (edge.IsNull()) {
            continue;
        }
        if (DU::isZeroEdge(edge)) {
            continue;
        }
        if (DU::isCrazy(edge)) {
            continue;
        }

        base = BaseGeom::baseFactory(edge);
        if (!base) {
            continue;
            //            throw Base::ValueError("GeometryObject::addGeomFromCompound - baseFactory failed");
        }

        base->source(0);//object geometry
        base->sourceIndex(i - 1);
        base->setClassOfEdge(category);
        base->setHlrVisible(hlrVisible);
        //the source element, recorded by the traversal.  Only an index here --
        //the name it stands for is resolved on the main thread.
        if (m_edgeSource.IsBound(edge)) {
            base->setRef3d(m_edgeSource.Find(edge));
        }
        edgeGeom.push_back(base);

        //add vertices of new edge if not already in list
        if (hlrVisible) {
            BaseGeomPtr lastAdded = edgeGeom.back();
            bool v1Add = true, v2Add = true;
            bool c1Add = true;
            TechDraw::VertexPtr v1 = std::make_shared<TechDraw::Vertex>(lastAdded->getStartPoint());
            TechDraw::VertexPtr v2 = std::make_shared<TechDraw::Vertex>(lastAdded->getEndPoint());
            TechDraw::CirclePtr circle = std::dynamic_pointer_cast<TechDraw::Circle>(lastAdded);
            TechDraw::VertexPtr c1;
            if (circle) {
                c1 = std::make_shared<TechDraw::Vertex>(circle->center);
                c1->isCenter(true);
                c1->setHlrVisible(true);
            }

            std::vector<VertexPtr>::iterator itVertex = vertexGeom.begin();
            for (; itVertex != vertexGeom.end(); itVertex++) {
                if ((*itVertex)->isEqual(*v1, Precision::Confusion())) {
                    v1Add = false;
                }
                if ((*itVertex)->isEqual(*v2, Precision::Confusion())) {
                    v2Add = false;
                }
                if (circle) {
                    if ((*itVertex)->isEqual(*c1, Precision::Confusion())) {
                        c1Add = false;
                    }
                }
            }
            if (v1Add) {
                vertexGeom.push_back(v1);
                v1->setHlrVisible( true);
            }
            else {
                //    delete v1;
            }
            if (v2Add) {
                vertexGeom.push_back(v2);
                v2->setHlrVisible( true);
            }
            else {
                //    delete v2;
            }

            if (circle) {
                if (c1Add) {
                    vertexGeom.push_back(c1);
                    c1->setHlrVisible( true);
                }
                else {
                    //    delete c1;
                }
            }
        }
    }//end TopExp
}

void GeometryObject::addVertex(TechDraw::VertexPtr v) { vertexGeom.push_back(v); }

void GeometryObject::addEdge(TechDraw::BaseGeomPtr bg) { edgeGeom.push_back(bg); }

double GeometryObject::getScale() const
{
    auto parent = Base::freecad_dynamic_cast<const DrawView>(m_parent.getObject());
    if (parent) {
        //    Base::Console().Message("GO::addCosmeticVertex(%X)\n", cv);
        m_scale = parent->getScale();
    }
    return m_scale;
}

//********** Cosmetic Vertex ***************************************************

//adds a new GeomVert surrogate for CV
//returns GeomVert selection index  ("Vertex3")
// insertGeomForCV(cv)
int GeometryObject::addCosmeticVertex(CosmeticVertex* cv)
{
    Base::Vector3d pos = cv->scaled(getScale());
    TechDraw::VertexPtr v(std::make_shared<TechDraw::Vertex>(pos.x, pos.y));
    v->setCosmetic(true);
//    v->setCosmeticLink = -1;//obs??
    v->setCosmeticTag(cv->getTagAsString());
    v->setHlrVisible(true);
    int idx = vertexGeom.size();
    vertexGeom.push_back(v);
    return idx;
}

//adds a new GeomVert to list
//should probably be called addVertex since not connect to CV by tag
int GeometryObject::addCosmeticVertex(Base::Vector3d pos)
{
    Base::Console().Message("GO::addCosmeticVertex() 1 - deprec?\n");
    TechDraw::VertexPtr v(std::make_shared<TechDraw::Vertex>(pos.x, pos.y));
    v->setCosmetic(true);
    v->setCosmeticTag("tbi");//not connected to CV
    v->setHlrVisible(true);
    int idx = vertexGeom.size();
    vertexGeom.push_back(v);
    return idx;
}

int GeometryObject::addCosmeticVertex(Base::Vector3d pos, std::string tagString)
{
    //    Base::Console().Message("GO::addCosmeticVertex() 2\n");
    TechDraw::VertexPtr v(std::make_shared<TechDraw::Vertex>(pos.x, pos.y));
    v->setCosmetic(true);
    v->setCosmeticTag(tagString);//connected to CV
    v->setHlrVisible(true);
    int idx = vertexGeom.size();
    vertexGeom.push_back(v);
    return idx;
}

//********** Cosmetic Edge *****************************************************

//adds a new GeomEdge surrogate for CE
//returns GeomEdge selection index  ("Edge3")
// insertGeomForCE(ce)
int GeometryObject::addCosmeticEdge(CosmeticEdge* ce)
{
    //    Base::Console().Message("GO::addCosmeticEdge(%X) 0\n", ce);
    double scale = getScale();
    TechDraw::BaseGeomPtr e = ce->scaledGeometry(scale);
    e->setCosmetic(true);
    e->setCosmeticTag(ce->getTagAsString());
    e->setHlrVisible(true);
    int idx = edgeGeom.size();
    edgeGeom.push_back(e);
    return idx;
}

//adds a new GeomEdge to list for ce[link]
//this should be made obsolete and the variant with tag used instead
int GeometryObject::addCosmeticEdge(Base::Vector3d start, Base::Vector3d end)
{
    //    Base::Console().Message("GO::addCosmeticEdge() 1 - deprec?\n");
    gp_Pnt gp1(start.x, start.y, start.z);
    gp_Pnt gp2(end.x, end.y, end.z);
    TopoDS_Edge occEdge = BRepBuilderAPI_MakeEdge(gp1, gp2);
    TechDraw::BaseGeomPtr e = BaseGeom::baseFactory(occEdge);
    e->setCosmetic(true);
    //    e->cosmeticLink = link;
    e->setCosmeticTag("tbi");
    e->setHlrVisible(true);
    int idx = edgeGeom.size();
    edgeGeom.push_back(e);
    return idx;
}

int GeometryObject::addCosmeticEdge(Base::Vector3d start, Base::Vector3d end, std::string tagString)
{
    //    Base::Console().Message("GO::addCosmeticEdge() 2\n");
    gp_Pnt gp1(start.x, start.y, start.z);
    gp_Pnt gp2(end.x, end.y, end.z);
    TopoDS_Edge occEdge = BRepBuilderAPI_MakeEdge(gp1, gp2);
    TechDraw::BaseGeomPtr base = BaseGeom::baseFactory(occEdge);
    base->setCosmetic(true);
    base->setCosmeticTag(tagString);
    base->source(1);//1-CosmeticEdge, 2-CenterLine
    base->setHlrVisible(true);
    int idx = edgeGeom.size();
    edgeGeom.push_back(base);
    return idx;
}

int GeometryObject::addCosmeticEdge(TechDraw::BaseGeomPtr base, std::string tagString)
{
    //    Base::Console().Message("GO::addCosmeticEdge(%X, %s) 3\n", base, tagString.c_str());
    base->setCosmetic(true);
    base->setHlrVisible(true);
    base->source(1);//1-CosmeticEdge, 2-CenterLine
    base->setCosmeticTag(tagString);
    base->sourceIndex(-1);
    int idx = edgeGeom.size();
    edgeGeom.push_back(base);
    return idx;
}

int GeometryObject::addCenterLine(TechDraw::BaseGeomPtr base, std::string tag)
//                                    int s, int si)
{
    //    Base::Console().Message("GO::addCenterLine()\n");
    base->setCosmetic(true);
    base->setCosmeticTag(tag);
    base->source(2);
    //    base->sourceIndex(si);     //index into source;
    int idx = edgeGeom.size();
    edgeGeom.push_back(base);
    return idx;
}


//! empty Face geometry
void GeometryObject::clearFaceGeom() { faceGeom.clear(); }

//! add a Face to Face Geometry
void GeometryObject::addFaceGeom(FacePtr f) { faceGeom.push_back(f); }

void GeometryObject::setFaces(std::vector<FacePtr> &&faces)
{
    faceGeom = std::move(faces);
}

TechDraw::DrawViewDetail* GeometryObject::isParentDetail()
{
    return Base::freecad_dynamic_cast<TechDraw::DrawViewDetail>(m_parent.getObject());
}


bool GeometryObject::isWithinArc(double theta, double first, double last, bool cw) const
{
    if (fabs(last - first) >= 2 * M_PI) {
        return true;
    }

    // Put params within [0, 2*pi) - not totally sure this is necessary
    theta = fmod(theta, 2 * M_PI);
    if (theta < 0) {
        theta += 2 * M_PI;
    }

    first = fmod(first, 2 * M_PI);
    if (first < 0) {
        first += 2 * M_PI;
    }

    last = fmod(last, 2 * M_PI);
    if (last < 0) {
        last += 2 * M_PI;
    }

    if (cw) {
        if (first > last) {
            return theta <= first && theta >= last;
        }
        else {
            return theta <= first || theta >= last;
        }
    }
    else {
        if (first > last) {
            return theta >= first || theta <= last;
        }
        else {
            return theta >= first && theta <= last;
        }
    }
}

//note bbx is scaled
Base::BoundBox3d GeometryObject::calcBoundingBox() const
{
    //    Base::Console().Message("GO::calcBoundingBox() - edges: %d\n", edgeGeom.size());
    Bnd_Box testBox;
    testBox.SetGap(0.0);
    if (!edgeGeom.empty()) {
        for (BaseGeomPtrVector::const_iterator it(edgeGeom.begin()); it != edgeGeom.end(); ++it) {
            BRepBndLib::AddOptimal((*it)->getOCCEdge(), testBox);
        }
    }

    double xMin = 0, xMax = 0, yMin = 0, yMax = 0, zMin = 0, zMax = 0;
    if (!testBox.IsVoid()) {
        testBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    }
    Base::BoundBox3d bbox(xMin, yMin, zMin, xMax, yMax, zMax);
    return bbox;
}

void GeometryObject::pruneVertexGeom(Base::Vector3d center, double radius)
{
    const std::vector<VertexPtr>& oldVerts = getVertexGeometry();
    std::vector<VertexPtr> newVerts;
    for (auto& v : oldVerts) {
        Base::Vector3d v3 = v->point();
        double length = (v3 - center).Length();
        if (length < Precision::Confusion()) {
            continue;
        }
        else if (length < radius) {
            newVerts.push_back(v);
        }
    }
    vertexGeom = newVerts;
}

//! does this GeometryObject already have this vertex
bool GeometryObject::findVertex(Base::Vector3d v)
{
    std::vector<VertexPtr>::iterator it = vertexGeom.begin();
    for (; it != vertexGeom.end(); it++) {
        double dist = (v - (*it)->point()).Length();
        if (dist < Precision::Confusion()) {
            return true;
        }
    }
    return false;
}


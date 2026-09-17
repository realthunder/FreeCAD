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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <BRep_Builder.hxx>
# include <BRepLib.hxx>
# include <gp_Trsf.hxx>
# include <HLRAlgo_EdgeIterator.hxx>
# include <HLRBRep.hxx>
# include <HLRBRep_Data.hxx>
# include <HLRBRep_EdgeData.hxx>
# include <HLRBRep_FaceIterator.hxx>
# include <HLRBRep_ShapeBounds.hxx>
# include <HLRTopoBRep_Data.hxx>
# include <HLRTopoBRep_OutLiner.hxx>
# include <NCollection_DataMap.hxx>
# include <Standard_Failure.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Compound.hxx>
# include <TopoDS_Edge.hxx>
# include <TopoDS_Face.hxx>
# include <TopTools_ShapeMapHasher.hxx>
#endif

#include <Base/Exception.h>

#include "HLRProjector.h"
#include "TopoShapeOpCode.h"

using namespace Part;

namespace {

/// One edge the traversal emitted, with what the traversal knew about it
struct Produced {
    TopoDS_Edge edge;
    int ie;              // index in the data structure's EDataArray / EdgeMap
    int type;            // HLRToShape's category code
    bool visible;
};

//===========================================================================
// HLRBRep_HLRToShape's traversal, replayed so it can say where an edge came
// from. The three functions below are HLRBRep_HLRToShape::DrawEdge, DrawFace
// and InternalCompound (no shape filter), in the same order, emitting the same
// edges, plus the data structure index of the source with each one.
//===========================================================================

void drawEdge(bool visible, bool inFace, int typ, bool in3d, HLRBRep_EdgeData &ed, int ie,
              std::vector<Produced> &produced)
{
    bool todraw = false;
    if (inFace)
        todraw = true;
    else if (typ == 3)
        todraw = ed.Rg1Line() && !ed.RgNLine();
    else if (typ == 4)
        todraw = ed.RgNLine();
    else
        todraw = !ed.Rg1Line();
    if (!todraw)
        return;

    double sta, end;
    float tolsta, tolend;
    HLRAlgo_EdgeIterator it;
    auto emit = [&](double s, double e) {
        TopoDS_Edge edge = in3d ? HLRBRep::MakeEdge3d(ed.Geometry(), s, e)
                                : HLRBRep::MakeEdge(ed.Geometry(), s, e);
        if (!edge.IsNull())
            produced.push_back({edge, ie, typ, visible});
    };
    if (visible) {
        for (it.InitVisible(ed.Status()); it.MoreVisible(); it.NextVisible()) {
            it.Visible(sta, tolsta, end, tolend);
            emit(sta, end);
        }
    }
    else {
        for (it.InitHidden(ed.Status()); it.MoreHidden(); it.NextHidden()) {
            it.Hidden(sta, tolsta, end, tolend);
            emit(sta, end);
        }
    }
}

void drawFace(bool visible, int typ, bool in3d, int iface, const Handle(HLRBRep_Data) &ds,
              std::vector<Produced> &produced)
{
    HLRBRep_FaceIterator itf;
    for (itf.InitEdge(ds->FDataArray().ChangeValue(iface)); itf.MoreEdge(); itf.NextEdge()) {
        int ie = itf.Edge();
        HLRBRep_EdgeData &edf = ds->EDataArray().ChangeValue(ie);
        if (edf.Used())
            continue;

        bool todraw;
        if (typ == 1)
            todraw = itf.IsoLine();
        else if (typ == 2) // outlines
            todraw = in3d ? (itf.Internal() || itf.OutLine()) : itf.Internal();
        else if (typ == 3)
            todraw = edf.Rg1Line() && !edf.RgNLine() && !itf.OutLine();
        else if (typ == 4)
            todraw = edf.RgNLine() && !itf.OutLine();
        else
            todraw = !itf.IsoLine() && !itf.Internal() && (!edf.Rg1Line() || itf.OutLine());

        if (todraw) {
            drawEdge(visible, true, typ, in3d, edf, ie, produced);
            edf.Used(true);
        }
        else if ((typ > 4 || typ == 2) && edf.Rg1Line() && !itf.OutLine()) {
            // sharp or outlines: give the edge a second face to be drawn from
            int hc = edf.HideCount();
            if (hc > 0)
                edf.Used(true);
            else
                edf.HideCount(hc + 1);
        }
        else {
            edf.Used(true);
        }
    }
}

void internalCompound(const Handle(HLRBRep_Data) &ds, int typ, bool visible, bool in3d,
                      std::vector<Produced> &produced)
{
    ds->Projector().Scaled(true);
    const int e1 = 1;
    const int e2 = ds->NbEdges();
    const int f1 = 1;
    const int f2 = ds->NbFaces();

    for (int ie = e1; ie <= e2; ie++) {
        HLRBRep_EdgeData &ed = ds->EDataArray().ChangeValue(ie);
        if (ed.Selected() && !ed.Vertical()) {
            ed.Used(false);
            ed.HideCount(0);
        }
        else {
            ed.Used(true);
        }
    }

    for (int iface = f1; iface <= f2; iface++)
        drawFace(visible, typ, in3d, iface, ds, produced);
    if (typ >= 3) {
        for (int ie = e1; ie <= e2; ie++) {
            HLRBRep_EdgeData &ed = ds->EDataArray().ChangeValue(ie);
            if (!ed.Used()) {
                drawEdge(visible, false, typ, in3d, ed, ie, produced);
                ed.Used(true);
            }
        }
    }
    ds->Projector().Scaled(false);
}

/// The projected edges as makESHAPE's history: each source element of the
/// input generates its projected pieces, in the order they were produced.
struct HLRMapper : TopoShape::Mapper {
    NCollection_DataMap<TopoDS_Shape, std::vector<TopoDS_Shape>, TopTools_ShapeMapHasher> map;

    const std::vector<TopoDS_Shape> &generated(const TopoDS_Shape &s) const override {
        if (const auto *res = map.Seek(s))
            return *res;
        return _res;
    }
};

} // namespace

struct HLRProjector::Private {
    std::vector<TopoShape> inputs;
    Handle(HLRBRep_Algo) algo;
    bool done = false;

    /// every edge produced, in traversal order
    std::vector<Produced> produced;
    /// where each produced edge came from, parallel to produced
    std::vector<Edge> info;
    /// produced edge -> index into produced/info
    NCollection_DataMap<TopoDS_Shape, int, TopTools_ShapeMapHasher> index;

    void resolveSources();
};

HLRProjector::HLRProjector()
    : d(new Private)
{
}

HLRProjector::~HLRProjector() = default;

void HLRProjector::add(const TopoShape &shape)
{
    if (shape.isNull())
        return;
    d->inputs.push_back(shape);
}

void HLRProjector::build(const gp_Ax3 &view, const Params &params)
{
    gp_Trsf trsf;
    trsf.SetTransformation(view);
    HLRAlgo_Projector projector(trsf, params.perspective, params.focus);
    build(projector, params);
}

void HLRProjector::build(const HLRAlgo_Projector &projector, const Params &params)
{
    d->done = false;
    d->produced.clear();
    d->info.clear();
    d->index.Clear();
    d->algo.Nullify();

    try {
        d->algo = new HLRBRep_Algo();
        for (const auto &input : d->inputs)
            d->algo->Add(input.getShape(), params.isoCount);
        d->algo->Projector(projector);
        d->algo->Update();
        d->algo->Hide();
    }
    catch (const Standard_Failure &e) {
        d->algo.Nullify();
        FC_THROWM(Base::CADKernelError, "Hidden line projection failed: " << e.GetMessageString());
    }

    try {
        Handle(HLRBRep_Data) ds = d->algo->DataStructure();
        if (!ds.IsNull()) {
            // HLRToShape's order: visible then hidden, each hard, smooth,
            // seam, outline, iso
            for (bool visible : {true, false}) {
                for (int typ : {5, 3, 4, 2, 1})
                    internalCompound(ds, typ, visible, params.onShape, d->produced);
            }
        }
        if (!params.onShape) {
            // the edges come with 2D curves on the projection plane only
            for (auto &p : d->produced)
                BRepLib::BuildCurves3d(p.edge);
        }
        d->resolveSources();
    }
    catch (const Standard_Failure &e) {
        d->produced.clear();
        d->info.clear();
        d->index.Clear();
        FC_THROWM(Base::CADKernelError, "Hidden line extraction failed: " << e.GetMessageString());
    }
    d->done = true;
}

void HLRProjector::Private::resolveSources()
{
    // Every edge and face of the inputs, with the input it belongs to
    NCollection_DataMap<TopoDS_Shape, int, TopTools_ShapeMapHasher> inputOf;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        for (auto type : {TopAbs_EDGE, TopAbs_FACE}) {
            for (TopExp_Explorer xp(inputs[i].getShape(), type); xp.More(); xp.Next()) {
                if (!inputOf.IsBound(xp.Current()))
                    inputOf.Bind(xp.Current(), static_cast<int>(i));
            }
        }
    }

    // The algorithm works on an outlined copy of each input: a face with a
    // silhouette is rebuilt with the outline as an internal edge, and an
    // input edge an outline vertex lands on is split. The outliner's data
    // structure keeps both relations keyed by the ORIGINAL element, which
    // is what the element map has to name. An edge it did not touch is its
    // own key in the algorithm's edge map.
    NCollection_DataMap<TopoDS_Shape, TopoDS_Shape, TopTools_ShapeMapHasher> sourceOf;
    for (int i = 1; i <= algo->NbShapes(); ++i) {
        const Handle(HLRTopoBRep_OutLiner) &outliner = algo->ShapeBounds(i).Shape();
        if (outliner.IsNull())
            continue;
        HLRTopoBRep_Data &tds = outliner->DataStructure();
        const TopoDS_Shape &original = outliner->OriginalShape();
        for (TopExp_Explorer xp(original, TopAbs_FACE); xp.More(); xp.Next()) {
            const TopoDS_Face &face = TopoDS::Face(xp.Current());
            auto bindAll = [&](const NCollection_List<TopoDS_Shape> &edges) {
                for (NCollection_List<TopoDS_Shape>::Iterator it(edges); it.More(); it.Next())
                    sourceOf.Bind(it.Value(), face);
            };
            if (tds.FaceHasOutL(face))
                bindAll(tds.FaceOutL(face));
            if (tds.FaceHasIntL(face))
                bindAll(tds.FaceIntL(face));
            if (tds.FaceHasIsoL(face))
                bindAll(tds.FaceIsoL(face));
        }
        for (TopExp_Explorer xp(original, TopAbs_EDGE); xp.More(); xp.Next()) {
            const TopoDS_Edge &edge = TopoDS::Edge(xp.Current());
            if (!tds.EdgeHasSplE(edge))
                continue;
            for (NCollection_List<TopoDS_Shape>::Iterator it(tds.EdgeSplE(edge)); it.More(); it.Next())
                sourceOf.Bind(it.Value(), edge);
        }
    }

    Handle(HLRBRep_Data) ds = algo->DataStructure();
    auto &edgeMap = ds->EdgeMap();
    NCollection_DataMap<TopoDS_Shape, int, TopTools_ShapeMapHasher> fragments;
    info.reserve(produced.size());
    for (std::size_t n = 0; n < produced.size(); ++n) {
        const auto &p = produced[n];
        Edge edge;
        edge.type = static_cast<EdgeType>(p.type);
        edge.visible = p.visible;
        if (p.ie >= 1 && p.ie <= edgeMap.Extent()) {
            const TopoDS_Shape &key = edgeMap.FindKey(p.ie);
            const TopoDS_Shape *src = sourceOf.Seek(key);
            const TopoDS_Shape &source = src ? *src : key;
            if (const int *input = inputOf.Seek(source)) {
                edge.source = source;
                edge.input = *input;
                int *count = fragments.ChangeSeek(source);
                if (count)
                    edge.fragment = ++*count;
                else
                    fragments.Bind(source, 1);
            }
        }
        info.push_back(edge);
        index.Bind(p.edge, static_cast<int>(n));
    }
}

bool HLRProjector::isDone() const
{
    return d->done;
}

const Handle(HLRBRep_Algo) &HLRProjector::algo() const
{
    return d->algo;
}

const HLRProjector::Edge *HLRProjector::info(const TopoDS_Shape &edge) const
{
    if (const int *n = d->index.Seek(edge))
        return &d->info[*n];
    return nullptr;
}

TopoShape HLRProjector::edges(const Params &params, const char *op,
                              App::StringHasherRef hasher) const
{
    if (!d->done)
        FC_THROWM(Base::RuntimeError, "Hidden line projection not built");
    if (!op)
        op = OpCodes::HLR;

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    HLRMapper mapper;
    for (std::size_t n = 0; n < d->produced.size(); ++n) {
        const auto &p = d->produced[n];
        const auto &edge = d->info[n];
        if (!(params.types & (1u << p.type)))
            continue;
        if (p.visible ? !params.visible : !params.hidden)
            continue;
        builder.Add(compound, p.edge);
        if (edge.source.IsNull())
            continue;
        if (auto *list = mapper.map.ChangeSeek(edge.source))
            list->push_back(p.edge);
        else
            mapper.map.Bound(edge.source, std::vector<TopoDS_Shape>())->push_back(p.edge);
    }

    TopoShape res(0, hasher);
    res.setShape(compound);
    bool canMap = false;
    for (const auto &input : d->inputs) {
        if (res.canMapElement(input)) {
            canMap = true;
            break;
        }
    }
    // an input without an element map has nothing to name the output from;
    // that is the plain HLRToShape result, not a fault to report
    if (canMap)
        res.makESHAPE(compound, mapper, d->inputs, op);
    return res;
}

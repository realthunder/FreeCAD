/***************************************************************************
 *   Copyright (c) 2026 David Kaufman <davidgilkaufman@gmail.com>          *
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
# include <BRep_Builder.hxx>
# include <ShapeAnalysis_FreeBounds.hxx>
# include <Standard_Version.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Iterator.hxx>
# include <TopoDS_Wire.hxx>
# include <TopTools_HSequenceOfShape.hxx>
#endif

#include "ShapeAnalysis_FreeBoundsFix.h"

namespace
{

inline bool isInternalOrExternal(const TopoDS_Shape& shape)
{
    TopAbs_Orientation ori = shape.Orientation();
    return ori == TopAbs_INTERNAL || ori == TopAbs_EXTERNAL;
}

}  // namespace

namespace Part
{

void Fix_ShapeAnalysis_FreeBounds_ConnectEdgesToWires(
        Handle(TopTools_HSequenceOfShape)& edges,
        double toler,
        bool shared,
        Handle(TopTools_HSequenceOfShape)& wires)
{
    bool needsFiltering = false;
#if OCC_VERSION_HEX < 0x080001
    for (int i = 1; i <= edges->Length(); ++i) {
        if (isInternalOrExternal(edges->Value(i))) {
            needsFiltering = true;
            break;
        }
    }
#endif

    if (!needsFiltering) {
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, toler, shared, wires);
        return;
    }

    Handle(TopTools_HSequenceOfShape) filtered = new TopTools_HSequenceOfShape;
    for (int i = 1; i <= edges->Length(); ++i) {
        if (!isInternalOrExternal(edges->Value(i))) {
            filtered->Append(edges->Value(i));
        }
    }

    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(filtered, toler, shared, wires);
}

void Fix_ShapeAnalysis_FreeBounds_ConnectWiresToWires(
        Handle(TopTools_HSequenceOfShape)& iwires,
        double toler,
        bool shared,
        Handle(TopTools_HSequenceOfShape)& owires)
{
    bool needsFiltering = false;
#if OCC_VERSION_HEX < 0x080001
    for (int i = 1; i <= iwires->Length() && !needsFiltering; ++i) {
        for (TopoDS_Iterator it(TopoDS::Wire(iwires->Value(i))); it.More(); it.Next()) {
            if (isInternalOrExternal(it.Value())) {
                needsFiltering = true;
                break;
            }
        }
    }
#endif

    if (!needsFiltering) {
        ShapeAnalysis_FreeBounds::ConnectWiresToWires(iwires, toler, shared, owires);
        return;
    }

    // Rebuild each wire without the offending edges. Wires left with no edges
    // at all are dropped.
    Handle(TopTools_HSequenceOfShape) filteredWires = new TopTools_HSequenceOfShape;
    for (int i = 1; i <= iwires->Length(); ++i) {
        const TopoDS_Wire& orig = TopoDS::Wire(iwires->Value(i));

        BRep_Builder builder;
        TopoDS_Wire filteredWire;
        builder.MakeWire(filteredWire);
        bool hasEdges = false;
        for (TopoDS_Iterator it(orig); it.More(); it.Next()) {
            if (!isInternalOrExternal(it.Value())) {
                builder.Add(filteredWire, it.Value());
                hasEdges = true;
            }
        }

        if (hasEdges) {
            filteredWires->Append(filteredWire);
        }
    }

    ShapeAnalysis_FreeBounds::ConnectWiresToWires(filteredWires, toler, shared, owires);
}

}  // namespace Part

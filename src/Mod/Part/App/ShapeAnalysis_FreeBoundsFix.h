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

#ifndef PART_SHAPEANALYSIS_FREEBOUNDSFIX_H
#define PART_SHAPEANALYSIS_FREEBOUNDSFIX_H

#include <TopTools_HSequenceOfShape.hxx>
#include <Mod/Part/PartGlobal.h>

namespace Part
{

/// Wrapper of ShapeAnalysis_FreeBounds::ConnectEdgesToWires that works around
/// OCCT bug 1330: edges with INTERNAL or EXTERNAL orientation do not
/// contribute to the boundary, but until 1330 is fixed they are handled
/// improperly, so filter them out first. Fixed in OCCT 8.0.1.
PartExport void Fix_ShapeAnalysis_FreeBounds_ConnectEdgesToWires(
        Handle(TopTools_HSequenceOfShape)& edges,
        double toler,
        bool shared,
        Handle(TopTools_HSequenceOfShape)& wires);

/// Wrapper of ShapeAnalysis_FreeBounds::ConnectWiresToWires that works around
/// OCCT bug 1330, filtering INTERNAL and EXTERNAL edges out of each input wire.
/// See Fix_ShapeAnalysis_FreeBounds_ConnectEdgesToWires.
PartExport void Fix_ShapeAnalysis_FreeBounds_ConnectWiresToWires(
        Handle(TopTools_HSequenceOfShape)& iwires,
        double toler,
        bool shared,
        Handle(TopTools_HSequenceOfShape)& owires);

}  // namespace Part

#endif  // PART_SHAPEANALYSIS_FREEBOUNDSFIX_H

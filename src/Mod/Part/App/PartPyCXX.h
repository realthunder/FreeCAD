/***************************************************************************
 *   Copyright (c) 2017 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef PART_PYCXX_H
#define PART_PYCXX_H

#include <CXX/Extensions.hxx>

#include <Mod/Part/App/TopoShapePy.h>

namespace App {
class DocumentObjectT;
}

namespace Py {
    using TopoShape = ExtensionObject<Part::TopoShapePy>;
    template<>
    bool TopoShape::accepts (PyObject *pyob) const;
}

class Standard_Transient;
class GeomTools_Curve2dSet;
class BinTools_ShapeSet;

namespace Part {
    PartExport Py::Object shape2pyshape(const TopoShape &shape);
    PartExport Py::Object shape2pyshape(const TopoDS_Shape &shape);
    PartExport void getPyShapes(PyObject *obj, std::vector<TopoShape> &shapes, std::vector<App::DocumentObjectT> *objs = nullptr);
    PartExport std::vector<TopoShape> getPyShapes(PyObject *obj, std::vector<App::DocumentObjectT> *objs = nullptr);
    PartExport int initOCCTExtension();
    /// A function of one of the OCCT fork's toolkits ("TKBRep", ...) by name,
    /// looked up at run time; null where the libraries are upstream's. A fork
    /// function FreeCAD needs is a C function reached this way, never called
    /// by its C++ name, so that FreeCAD loads with upstream's OCCT as well.
    PartExport void *lookUpOCCT(const char *toolkit, const char *name);
    /// A function of the OCCT fork's TKFillet by name, null without it
    PartExport void *lookUpTKFillet(const char *name);
    /// A function of the OCCT fork's TKBRep by name, null without it
    PartExport void *lookUpTKBRep(const char *name);
    /// Whether a BRep curve or point representation is a cache of a frozen
    /// shape (the OCCT fork's BRep_Builder_IsCache); false without the fork
    PartExport bool isOCCTCacheRepresentation(const Standard_Transient *rep);
    /// Whether the set merges equal 2D curves (the fork's
    /// GeomTools_Curve2dSet_SetMerging); false, and nothing set, without it
    PartExport bool setOCCTCurve2dSetMerging(GeomTools_Curve2dSet &set, bool merging);
    /// Whether the binary set leaves out pcurves on a plane (the fork's
    /// BinTools_ShapeSet_SetOmitPCurvesOnPlane); false without it
    PartExport bool setOCCTOmitPCurvesOnPlane(BinTools_ShapeSet &set, bool omit);
}

#endif //PART_PYCXX_H

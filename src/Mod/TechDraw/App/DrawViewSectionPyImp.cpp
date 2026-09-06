/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
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

#include <Base/Console.h>

#include <Mod/Part/App/PartPyCXX.h>
#include <Mod/Part/App/TopoShape.h>

#include "DrawViewSection.h"
// inclusion of the generated files
#include <Mod/TechDraw/App/DrawViewPartPy.h>
#include <Mod/TechDraw/App/DrawViewSectionPy.h>
#include <Mod/TechDraw/App/DrawViewSectionPy.cpp>

using namespace TechDraw;

// returns a string which represents the object e.g. when printed in python
std::string DrawViewSectionPy::representation() const
{
    return std::string("<DrawViewSection object>");
}

PyObject* DrawViewSectionPy::getCutPieces(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    DrawViewSection* dvs = getDrawViewSectionPtr();
    return Py::new_reference_to(Part::shape2pyshape(dvs->getCutPieces()));
}

PyObject* DrawViewSectionPy::getCuttingTool(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    DrawViewSection* dvs = getDrawViewSectionPtr();
    return Py::new_reference_to(Part::shape2pyshape(dvs->getCuttingToolAsBuilt()));
}

PyObject* DrawViewSectionPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int DrawViewSectionPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}

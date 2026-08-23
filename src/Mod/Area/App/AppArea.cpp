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

// The 2D area engine as a module of its own.
//
// It used to be part of Path, which meant that anything wanting a robust 2D
// boolean -- the BIM IFC importer wants one for opening subtractions -- had to
// depend on the whole CAM workbench. Path is an option a user can switch off,
// and REQUIRES_MODS turns a dependent module off silently when a prerequisite
// is missing, so that dependency would have made BIM vanish from any build
// without CAM. The engine itself only ever needed Part.
//
// The class keeps the name Path::Area, and Path still publishes it as
// Path.Area, so existing scripts do not care where it lives.

#include "PreCompiled.h"

#ifndef _PreComp_
#include <CXX/Extensions.hxx>
#include <CXX/Objects.hxx>
#endif

#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>

#include "Area.h"
#include "AreaPy.h"

namespace AreaApp
{

class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("Area")
    {
        initialize("The 2D area engine, a wrapper around libarea/Clipper.");
    }

    ~Module() override = default;
};

PyObject* initModule()
{
    return Base::Interpreter().addModule(new Module);
}

}  // namespace AreaApp

/* Python entry */
PyMOD_INIT_FUNC(Area)
{
    // load dependent module
    try {
        Base::Interpreter().runString("import Part");
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PyExc_ImportError, e.what());
        PyMOD_Return(nullptr);
    }

    PyObject* areaModule = AreaApp::initModule();
    Base::Console().Log("Loading Area module... done\n");

    Base::Interpreter().addType(&AreaLib::AreaPy::Type, areaModule, "Area");

    // Finish initialising the type object, as PyType_Ready does for the
    // inherited slots.
    AreaLib::Area::init();

    PyMOD_Return(areaModule);
}

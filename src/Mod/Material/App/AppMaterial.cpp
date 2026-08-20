// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>

#include <App/CleanupProcess.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectPy.h>

#include "MaterialLoader.h"
#include "MaterialManagerLocal.h"
#include "ModelManagerLocal.h"
#include "PropertyMaterial.h"
#if defined(BUILD_MATERIAL_EXTERNAL)
#include "ModelManagerExternal.h"
#include "MaterialManagerExternal.h"
#endif

#include "Array2DPy.h"
#include "Array3DPy.h"
#include "ModelManagerPy.h"
#include "ModelPropertyPy.h"
#include "ModelPy.h"
#include "UUIDsPy.h"

#include "MaterialFilterPy.h"
#include "MaterialFilterOptionsPy.h"
#include "MaterialLibraryPy.h"
#include "MaterialManagerPy.h"
#include "MaterialPropertyPy.h"
#include "MaterialCards.h"
#include "MaterialPy.h"

namespace Materials
{
class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("Materials")
    {
        add_varargs_method("cardCacheSize",
                           &Module::cardCacheSize,
                           "cardCacheSize() -> int\n\n"
                           "Number of distinct material cards currently held in memory.\n"
                           "Objects assigned the same card share one, so this counts cards\n"
                           "rather than assignments.");
        add_varargs_method("libraryStatus",
                           &Module::libraryStatus,
                           "libraryStatus(object, property) -> str\n\n"
                           "How the card a property holds compares with the library card it\n"
                           "came from: NoCard, Unanchored, Absent, Current or Diverged.\n"
                           "The document's card always wins, so the two are free to\n"
                           "disagree; this says whether they do.");
        add_varargs_method("updateFromLibrary",
                           &Module::updateFromLibrary,
                           "updateFromLibrary(object, property) -> bool\n\n"
                           "Take the library's current card as the property's value, the\n"
                           "deliberate pull that a library edit never does on its own.\n"
                           "Returns False, changing nothing, unless the library holds other\n"
                           "content under this card's uuid.");
        add_varargs_method("saveToLibrary",
                           &Module::saveToLibrary,
                           "saveToLibrary(object, property) -> bool\n\n"
                           "Write the property's card over the library card it came from,\n"
                           "in place and keeping its uuid. Returns False when there is no\n"
                           "such card or its library is read only, which is when choosing\n"
                           "somewhere to put it needs a user.");
        initialize("This module is the Materials module.");  // register with Python
    }

    ~Module() override = default;

private:
    /** The material property named by (object, property).
     *
     * Properties have no Python object of their own, so the owning object
     * plus the property name is how one is addressed from a script.
     */
    static PropertyMaterial& materialProperty(const Py::Tuple& args)
    {
        PyObject* object {};
        const char* name {};
        if (!PyArg_ParseTuple(args.ptr(), "O!s", &App::DocumentObjectPy::Type, &object, &name)) {
            throw Py::Exception();
        }
        auto owner = static_cast<App::DocumentObjectPy*>(object)->getDocumentObjectPtr();
        auto property = owner->getPropertyByName(name);
        if (!property) {
            throw Py::AttributeError(std::string("no property named '") + name + "'");
        }
        if (!property->isDerivedFrom<PropertyMaterial>()) {
            throw Py::TypeError(std::string("'") + name + "' is not a material property");
        }
        return *static_cast<PropertyMaterial*>(property);
    }

    Py::Object libraryStatus(const Py::Tuple& args)
    {
        auto& property = materialProperty(args);
        return Py::String(PropertyMaterial::statusName(property.libraryStatus()));
    }

    Py::Object updateFromLibrary(const Py::Tuple& args)
    {
        return Py::Boolean(materialProperty(args).updateFromLibrary());
    }

    Py::Object saveToLibrary(const Py::Tuple& args)
    {
        return Py::Boolean(materialProperty(args).saveToLibrary());
    }

    Py::Object cardCacheSize(const Py::Tuple& args)
    {
        if (!PyArg_ParseTuple(args.ptr(), "")) {
            throw Py::Exception();
        }
        return Py::Long(static_cast<long>(MaterialCards::size()));
    }
};

PyObject* initModule()
{
    return Base::Interpreter().addModule(new Module);
}

}  // namespace Materials

PyMOD_INIT_FUNC(Materials)
{
#ifdef FC_DEBUG
    App::CleanupProcess::registerCleanup([]() {
        Materials::MaterialManager::cleanup();
        Materials::ModelManager::cleanup();
    });
#endif
    PyObject* module = Materials::initModule();

    Base::Console().log("Loading Material module… done\n");

    Base::Interpreter().addType(&Materials::Array2DPy::Type, module, "Array2D");
    Base::Interpreter().addType(&Materials::Array3DPy::Type, module, "Array3D");
    Base::Interpreter().addType(&Materials::MaterialFilterPy::Type, module, "MaterialFilter");
    Base::Interpreter().addType(&Materials::MaterialFilterOptionsPy::Type, module, "MaterialFilterOptions");
    Base::Interpreter().addType(&Materials::MaterialLibraryPy::Type, module, "MaterialLibrary");
    Base::Interpreter().addType(&Materials::MaterialManagerPy::Type, module, "MaterialManager");
    Base::Interpreter().addType(&Materials::MaterialPropertyPy::Type, module, "MaterialProperty");
    Base::Interpreter().addType(&Materials::MaterialPy::Type, module, "Material");
    Base::Interpreter().addType(&Materials::ModelManagerPy::Type, module, "ModelManager");
    Base::Interpreter().addType(&Materials::ModelPropertyPy::Type, module, "ModelProperty");
    Base::Interpreter().addType(&Materials::ModelPy::Type, module, "Model");
    Base::Interpreter().addType(&Materials::UUIDsPy::Type, module, "UUIDs");


    // clang-format off
    // Initialize types

    Materials::Material                 ::init();
    Materials::MaterialFilter           ::init();
    Materials::MaterialFilterOptions    ::init();
    Materials::MaterialManager          ::init();
    Materials::MaterialManagerLocal     ::init();
    Materials::Model                    ::init();
    Materials::ModelManager             ::init();
#if defined(BUILD_MATERIAL_EXTERNAL)
    Materials::MaterialManagerExternal  ::init();
    Materials::ModelManagerExternal     ::init();
#endif
    Materials::ModelManagerLocal        ::init();
    Materials::ModelUUIDs               ::init();

    Materials::Library                  ::init();
    Materials::MaterialLibrary          ::init();
    Materials::MaterialLibraryLocal     ::init();
    Materials::ModelLibrary             ::init();

    Materials::ModelProperty            ::init();
    Materials::MaterialProperty         ::init();

    Materials::MaterialValue            ::init();
    Materials::Array2D                  ::init();
    Materials::Array3D                  ::init();

    Materials::PropertyMaterial         ::init();
    // clang-format on

    PyMOD_Return(module);
}

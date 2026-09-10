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
#include <Gui/Application.h>
#include <Gui/Language/Translator.h>
#include <Gui/WidgetFactory.h>

#include <Gui/BitmapFactory.h>

#include <Mod/Material/App/Exceptions.h>
#include <Mod/Material/App/MaterialManager.h>

#include "DlgSettingsDefaultMaterial.h"
#include "DlgSettingsMaterial.h"
#include "MaterialIcons.h"
#include "Workbench.h"
#include "WorkbenchManipulator.h"
#include "MaterialTreeWidget.h"
#include "MaterialTreeWidgetPy.h"

#if defined(BUILD_MATERIAL_EXTERNAL)
#include "DlgSettingsExternal.h"
#endif

// use a different name to CreateCommand()
void CreateMaterialCommands();

void loadMaterialResource()
{
    // add resources and reloads the translators
    Q_INIT_RESOURCE(Material);
    Q_INIT_RESOURCE(Material_translation);
    Gui::Translator::instance()->refresh();

    // The bundled appearance and surface finish icons, LAST on the icon
    // search path so that everything already on it -- the user's own icon
    // directory, a custom Bitmaps path, a theme's icon set -- overrides
    // them, which is what makes a bundled icon a default and not a
    // fixture. MatGui::MaterialIcons looks them up by name from here.
    Gui::BitmapFactory().addPath(QStringLiteral(":/icons/materials"));
}

namespace MatGui
{
class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("MatGui")
    {
        add_varargs_method("renderMaterialIcon",
                           &Module::renderMaterialIcon,
                           "renderMaterialIcon(uuid, path) -> bool\n\n"
                           "Render the appearance of the material with the given uuid into\n"
                           "path as a PNG. What generates the bundled preset icons; see\n"
                           "scripts/material-icons.py.\n"
                           "False where there is nothing to render with.");
        add_varargs_method("appearanceDigest",
                           &Module::appearanceDigest,
                           "appearanceDigest(uuid) -> str\n\n"
                           "The digest of that material's appearance -- what the bundled\n"
                           "icons are named by, and what 'two cards look the same' means.\n"
                           "Cards sharing a digest share an icon; see\n"
                           "scripts/material-icons.py.");
        add_varargs_method("renderFinishIcon",
                           &Module::renderFinishIcon,
                           "renderFinishIcon(pattern, path, pitch=0, depth=0) -> bool\n\n"
                           "Render the named surface finish pattern into path as a PNG, on\n"
                           "the neutral material. At the pitch and depth the bundled icons\n"
                           "state unless both are given -- a finish is its scale as much as\n"
                           "it is its pattern, so stating one is how you choose what the\n"
                           "icon shows. False where there is nothing to render with.");
        initialize("This module is the MatGui module.");  // register with Python
    }

    ~Module() = default;

private:
    Py::Object renderMaterialIcon(const Py::Tuple& args)
    {
        char* uuid {};
        char* path {};
        if (!PyArg_ParseTuple(args.ptr(), "ss", &uuid, &path)) {
            throw Py::Exception();
        }
        try {
            auto material = Materials::MaterialManager::getManager().getMaterial(
                QString::fromUtf8(uuid));
            const App::MaterialAppearance appearance = material->getMaterialAppearance();
            return Py::Boolean(MatGui::MaterialIcons::instance().renderToFile(
                appearance, appearance.finish, QString::fromUtf8(path),
                material->getRenderProperties()));
        }
        catch (const Materials::MaterialNotFound&) {
            throw Py::KeyError("No material with that uuid");
        }
    }

    Py::Object appearanceDigest(const Py::Tuple& args)
    {
        char* uuid {};
        if (!PyArg_ParseTuple(args.ptr(), "s", &uuid)) {
            throw Py::Exception();
        }
        try {
            auto material = Materials::MaterialManager::getManager().getMaterial(
                QString::fromUtf8(uuid));
            const App::MaterialAppearance appearance = material->getMaterialAppearance();
            return Py::String(MatGui::MaterialIcons::digestOf(
                                  appearance, appearance.finish,
                                  material->getRenderProperties())
                                  .toStdString());
        }
        catch (const Materials::MaterialNotFound&) {
            throw Py::KeyError("No material with that uuid");
        }
    }

    Py::Object renderFinishIcon(const Py::Tuple& args)
    {
        char* pattern {};
        char* path {};
        float pitch {};
        float depth {};
        if (!PyArg_ParseTuple(args.ptr(), "ss|ff", &pattern, &path, &pitch, &depth)) {
            throw Py::Exception();
        }
        const uint8_t value = App::SurfaceFinish::patternFromName(pattern);
        if (value == App::SurfaceFinish::None) {
            throw Py::ValueError("Not a surface finish pattern");
        }
        App::SurfaceFinish finish = MatGui::MaterialIcons::defaultFinish(value);
        if (pitch > 0.0F && depth > 0.0F) {
            finish.pitch = pitch;
            finish.depth = depth;
            finish.normalize();
        }
        return Py::Boolean(MatGui::MaterialIcons::instance().renderToFile(
            MatGui::MaterialIcons::finishMaterial(value), finish,
            QString::fromUtf8(path), {}, MatGui::IconShape::Cylinder));
    }
};

PyObject* initModule()
{
    return Base::Interpreter().addModule(new Module);
}

}  // namespace MatGui

PyMOD_INIT_FUNC(MatGui)
{
    if (!Gui::Application::Instance) {
        PyErr_SetString(PyExc_ImportError, "Cannot load Gui module in console application.");
        PyMOD_Return(nullptr);
    }

    // load needed modules
    try {
        Base::Interpreter().runString("import Materials");
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PyExc_ImportError, e.what());
        PyMOD_Return(nullptr);
    }

    PyObject* matGuiModule = MatGui::initModule();

    Base::Console().log("Loading GUI of Material module… done\n");

    MatGui::Workbench ::init();
    auto manip = std::make_shared<MatGui::WorkbenchManipulator>();
    Gui::WorkbenchManipulator::installManipulator(manip);

    // instantiating the commands
    CreateMaterialCommands();

    // register preferences pages on Material, the order here will be the order of the tabs in pref
    // widget
    Gui::Dialog::DlgPreferencesImp::setGroupData("Material",
                                                 "Material",
                                                 QObject::tr("Material Workbench"));
    new Gui::PrefPageProducer<MatGui::DlgSettingsMaterial>(
        QT_TRANSLATE_NOOP("QObject", "Material"));
    new Gui::PrefPageProducer<MatGui::DlgSettingsDefaultMaterial>(
        QT_TRANSLATE_NOOP("QObject", "Material"));
#if defined(BUILD_MATERIAL_EXTERNAL)
    new Gui::PrefPageProducer<MatGui::DlgSettingsExternal>(
        QT_TRANSLATE_NOOP("QObject", "Material"));
#endif

    // add resources and reloads the translators
    loadMaterialResource();

    Base::Interpreter().addType(&MatGui::MaterialTreeWidgetPy::Type,
                                matGuiModule,
                                "MaterialTreeWidget");


    // Initialize types

    MatGui::MaterialTreeWidget::init();

    // Add custom widgets
    new Gui::WidgetProducer<MatGui::MaterialTreeWidget>;


    PyMOD_Return(matGuiModule);
}

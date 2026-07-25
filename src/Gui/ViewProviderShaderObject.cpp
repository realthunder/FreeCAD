/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
# include <Inventor/nodes/SoCone.h>
# include <Inventor/nodes/SoCube.h>
# include <Inventor/nodes/SoCylinder.h>
# include <Inventor/nodes/SoFragmentShader.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoRotation.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoShaderProgram.h>
# include <Inventor/nodes/SoSphere.h>
# include <Inventor/nodes/SoVertexShader.h>
#endif

#include <App/ShaderObject.h>

#include "ViewProviderShaderObject.h"
#include "Application.h"


using namespace Gui;

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(Gui::ViewProviderShaderProgram, Gui::ViewProviderDocumentObject)

ViewProviderShaderProgram::ViewProviderShaderProgram()
{
    pcShaderProgram = new SoShaderProgram;
    pcVertexShader = new SoVertexShader;
    pcFragmentShader = new SoFragmentShader;
}

ViewProviderShaderProgram::~ViewProviderShaderProgram() = default;

SoShaderProgram *ViewProviderShaderProgram::getShaderNode() const
{
    return pcShaderProgram;
}

void ViewProviderShaderProgram::attach(App::DocumentObject *obj)
{
    ViewProviderDocumentObject::attach(obj);
    updateShaderNode();
    // The owning App::Shader's view provider may have attached before this
    // one existed (document restore order) — let it pick up the node now.
    for (auto parent : obj->getInList()) {
        if (!parent->isDerivedFrom(App::Shader::getClassTypeId()))
            continue;
        auto vp = dynamic_cast<ViewProviderShader*>(
                Application::Instance->getViewProvider(parent));
        if (vp)
            vp->updateDemo();
    }
}

void ViewProviderShaderProgram::updateData(const App::Property *prop)
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (obj && (prop == &obj->Stage
                || prop == &obj->Dialect
                || prop == &obj->VertexProgram
                || prop == &obj->FragmentProgram)) {
        updateShaderNode();
        if (prop == &obj->Stage) {
            // stage decides whether the demo preview includes the program
            for (auto parent : obj->getInList()) {
                if (!parent->isDerivedFrom(App::Shader::getClassTypeId()))
                    continue;
                auto vp = dynamic_cast<ViewProviderShader*>(
                        Application::Instance->getViewProvider(parent));
                if (vp)
                    vp->updateDemo();
            }
        }
    }
    ViewProviderDocumentObject::updateData(prop);
}

void ViewProviderShaderProgram::updateShaderNode()
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (!obj)
        return;

    SbName stage(obj->Stage.getValue());
    if (pcShaderProgram->stage.getValue() != stage)
        pcShaderProgram->stage = stage;

    int32_t sourcetype = obj->Dialect.getValue() == 0 ? SoShaderObject::BGFX_SC
                                                      : SoShaderObject::GLSL_PROGRAM;
    const char *vs = obj->VertexProgram.getValue();
    const char *fs = obj->FragmentProgram.getValue();

    if (pcVertexShader->sourceType.getValue() != sourcetype)
        pcVertexShader->sourceType = sourcetype;
    if (pcVertexShader->sourceProgram.getValue() != vs)
        pcVertexShader->sourceProgram = vs;
    if (pcFragmentShader->sourceType.getValue() != sourcetype)
        pcFragmentShader->sourceType = sourcetype;
    if (pcFragmentShader->sourceProgram.getValue() != fs)
        pcFragmentShader->sourceProgram = fs;

    SoNode *nodes[2];
    int num = 0;
    if (vs && vs[0])
        nodes[num++] = pcVertexShader;
    if (fs && fs[0])
        nodes[num++] = pcFragmentShader;
    bool changed = pcShaderProgram->shaderObject.getNum() != num;
    for (int i = 0; !changed && i < num; ++i)
        changed = pcShaderProgram->shaderObject[i] != nodes[i];
    if (changed) {
        pcShaderProgram->shaderObject.setNum(num);
        for (int i = 0; i < num; ++i)
            pcShaderProgram->shaderObject.set1Value(i, nodes[i]);
    }
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(Gui::ViewProviderShader, Gui::ViewProviderDocumentObject)

ViewProviderShader::ViewProviderShader() = default;

ViewProviderShader::~ViewProviderShader() = default;

void ViewProviderShader::attach(App::DocumentObject *obj)
{
    ViewProviderDocumentObject::attach(obj);
    pcDemoRoot = new SoSeparator;
    addDisplayMaskMode(pcDemoRoot, "Demo");
    updateDemo();
}

std::vector<std::string> ViewProviderShader::getDisplayModes() const
{
    return {"Demo"};
}

void ViewProviderShader::setDisplayMode(const char *ModeName)
{
    if (strcmp(ModeName, "Demo") == 0)
        setDisplayMaskMode("Demo");
    ViewProviderDocumentObject::setDisplayMode(ModeName);
}

void ViewProviderShader::updateData(const App::Property *prop)
{
    auto obj = dynamic_cast<App::Shader*>(getObject());
    if (obj && (prop == &obj->Programs
                || prop == &obj->Demo
                || prop == &obj->DemoSize
                || prop == &obj->DemoRadius
                || prop == &obj->DemoHeight))
        updateDemo();
    ViewProviderDocumentObject::updateData(prop);
}

void ViewProviderShader::updateDemo()
{
    auto obj = dynamic_cast<App::Shader*>(getObject());
    if (!obj || !pcDemoRoot)
        return;

    pcDemoRoot->removeAllChildren();

    long demo = obj->Demo.getValue();
    if (demo == 0) // None
        return;

    // Shader programs ahead of the shape: the SoFCRenderMaterial placement
    // rules — they apply to the shapes captured after them in this cache.
    // Scene-level ("post") programs are skipped: activating those is the
    // Appearance object's job, and nested placement would be unreliable
    // anyway (pruned from recapture inside a valid cached separator).
    for (auto prog : obj->Programs.getValues()) {
        auto progObj = dynamic_cast<App::ShaderProgram*>(prog);
        if (!progObj || strcmp(progObj->Stage.getValue(), "post") == 0)
            continue;
        auto vp = dynamic_cast<ViewProviderShaderProgram*>(
                Application::Instance->getViewProvider(progObj));
        if (vp && vp->getShaderNode())
            pcDemoRoot->addChild(vp->getShaderNode());
    }

    // Demo shapes carry no view-provider material chain; without an
    // explicit material the captured diffuse goes dark.
    auto mat = new SoMaterial;
    mat->diffuseColor = SbColor(0.8f, 0.8f, 0.8f);
    pcDemoRoot->addChild(mat);

    switch (demo) {
    case 1: { // Box
        auto cube = new SoCube;
        const auto &size = obj->DemoSize.getValue();
        cube->width = (float)size.x;
        cube->height = (float)size.y;
        cube->depth = (float)size.z;
        pcDemoRoot->addChild(cube);
        break;
    }
    case 2: { // Sphere
        auto sphere = new SoSphere;
        sphere->radius = (float)obj->DemoRadius.getValue();
        pcDemoRoot->addChild(sphere);
        break;
    }
    case 3: case 4: { // Cylinder / Cone: Coin's axis is +Y, rotate to Z-up
        auto rot = new SoRotation;
        rot->rotation = SbRotation(SbVec3f(1, 0, 0), (float)(M_PI / 2));
        pcDemoRoot->addChild(rot);
        if (demo == 3) {
            auto cyl = new SoCylinder;
            cyl->radius = (float)obj->DemoRadius.getValue();
            cyl->height = (float)obj->DemoHeight.getValue();
            pcDemoRoot->addChild(cyl);
        }
        else {
            auto cone = new SoCone;
            cone->bottomRadius = (float)obj->DemoRadius.getValue();
            cone->height = (float)obj->DemoHeight.getValue();
            pcDemoRoot->addChild(cone);
        }
        break;
    }
    default:
        break;
    }
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(Gui::ViewProviderAppearance, Gui::ViewProviderDocumentObject)

ViewProviderAppearance::ViewProviderAppearance() = default;

ViewProviderAppearance::~ViewProviderAppearance() = default;

// Python features ------------------------------------------------------------

namespace Gui {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderShaderProgramPython, Gui::ViewProviderShaderProgram)
PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderShaderPython, Gui::ViewProviderShader)
PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderAppearancePython, Gui::ViewProviderAppearance)
/// @endcond

// explicit template instantiation
template class GuiExport ViewProviderPythonFeatureT<ViewProviderShaderProgram>;
template class GuiExport ViewProviderPythonFeatureT<ViewProviderShader>;
template class GuiExport ViewProviderPythonFeatureT<ViewProviderAppearance>;
}

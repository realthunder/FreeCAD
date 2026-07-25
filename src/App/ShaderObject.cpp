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

#include "ShaderObject.h"


using namespace App;

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(App::ShaderProgram, App::DocumentObject)

const char* ShaderProgram::DialectEnums[] = {"BGFX_SC", "GLSL", nullptr};

ShaderProgram::ShaderProgram()
{
    ADD_PROPERTY_TYPE(Stage, ("material"), "Shader", Prop_None,
            "Renderer pipeline stage this program attaches to,\n"
            "e.g. 'material' (surface shading) or 'post' (full screen pass)");
    Dialect.setEnums(DialectEnums);
    ADD_PROPERTY_TYPE(Dialect, ((long)0), "Shader", Prop_None,
            "Source dialect of the program text");
    ADD_PROPERTY_TYPE(VertexProgram, (""), "Shader", Prop_None,
            "Vertex stage source; leave empty to use the renderer's\n"
            "stock vertex stage of the target pipeline stage");
    ADD_PROPERTY_TYPE(FragmentProgram, (""), "Shader", Prop_None,
            "Fragment stage source");
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(App::Shader, App::DocumentObject)

const char* Shader::DemoEnums[] = {"None", "Box", "Sphere", "Cylinder", "Cone", nullptr};

Shader::Shader()
{
    ADD_PROPERTY_TYPE(Programs, (nullptr), "Shader", Prop_None,
            "The shader programs forming this effect, one per pipeline stage");
    Demo.setEnums(DemoEnums);
    ADD_PROPERTY_TYPE(Demo, ((long)2), "Demo", Prop_None,  // default Sphere
            "Built-in shape for previewing the effect");
    ADD_PROPERTY_TYPE(DemoSize, (Base::Vector3d(10.0, 10.0, 10.0)), "Demo", Prop_None,
            "Dimensions of the Box demo shape");
    ADD_PROPERTY_TYPE(DemoRadius, (5.0), "Demo", Prop_None,
            "Radius of the Sphere/Cylinder/Cone demo shape");
    ADD_PROPERTY_TYPE(DemoHeight, (10.0), "Demo", Prop_None,
            "Height of the Cylinder/Cone demo shape");
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(App::Appearance, App::DocumentObject)

Appearance::Appearance()
{
    ADD_PROPERTY_TYPE(Targets, (nullptr), "Appearance", Prop_None,
            "Objects the shader applies to, with full instance paths;\n"
            "leave empty to apply scene-level (post) programs globally");
    ADD_PROPERTY_TYPE(Shader, (nullptr), "Appearance", Prop_None,
            "The shader to apply, possibly from a library document");
}

// Python features ------------------------------------------------------------

namespace App {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(App::ShaderProgramPython, App::ShaderProgram)
template<> const char* App::ShaderProgramPython::getViewProviderName() const {
    return "Gui::ViewProviderShaderProgramPython";
}

PROPERTY_SOURCE_TEMPLATE(App::ShaderPython, App::Shader)
template<> const char* App::ShaderPython::getViewProviderName() const {
    return "Gui::ViewProviderShaderPython";
}

PROPERTY_SOURCE_TEMPLATE(App::AppearancePython, App::Appearance)
template<> const char* App::AppearancePython::getViewProviderName() const {
    return "Gui::ViewProviderAppearancePython";
}
/// @endcond

// explicit template instantiation
template class AppExport FeaturePythonT<App::ShaderProgram>;
template class AppExport FeaturePythonT<App::Shader>;
template class AppExport FeaturePythonT<App::Appearance>;
}

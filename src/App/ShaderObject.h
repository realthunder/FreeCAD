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

#ifndef APP_ShaderObject_H
#define APP_ShaderObject_H

#include "DocumentObject.h"
#include "FeaturePython.h"
#include "PropertyGeo.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"

namespace App
{

/** One stage-tagged shader program of a user effect (docs/RenderDebug.md §6.5).
 *
 * Carries the source of a single program for one renderer pipeline stage.
 * Shader parameters are user-added dynamic properties in the "Param"
 * group — Param_<Name> feeds "uniform vec4 u_<Name>" (§6.4). Grouped
 * into an effect by App::Shader; applies to nothing by itself.
 */
class AppExport ShaderProgram : public DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::ShaderProgram);

public:
    ShaderProgram();

    /// Renderer pipeline stage the program attaches to ("material", "post", ...)
    PropertyString Stage;
    /// Source dialect of the program text
    PropertyEnumeration Dialect;
    /// Vertex stage source; empty uses the renderer's stock vertex stage
    PropertyString VertexProgram;
    /// Fragment stage source
    PropertyString FragmentProgram;

    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderShaderProgram";
    }

private:
    static const char* DialectEnums[];
};

using ShaderProgramPython = App::FeaturePythonT<ShaderProgram>;


/** A user effect/material: a list of ShaderProgram objects (§6.5).
 *
 * One effect may need several programs (surface shading + matching
 * depth/shadow programs, material + post combinations, multi-pass post
 * chains). Inert on its own — a shader-library document applies nothing.
 * The Demo properties select a built-in preview shape the view provider
 * displays with the effect applied.
 */
class AppExport Shader : public DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::Shader);

public:
    Shader();

    /// The ShaderProgram objects forming this effect
    PropertyLinkList Programs;
    /// Built-in preview shape (None/Box/Sphere/Cylinder/Cone)
    PropertyEnumeration Demo;
    /// Box demo dimensions
    PropertyVector DemoSize;
    /// Sphere/Cylinder/Cone demo radius
    PropertyFloat DemoRadius;
    /// Cylinder/Cone demo height
    PropertyFloat DemoHeight;

    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderShader";
    }

private:
    static const char* DemoEnums[];
};

using ShaderPython = App::FeaturePythonT<Shader>;


/** Binds a Shader to a list of target objects, activating the effect (§6.5).
 *
 * Targets carry full instance paths (hierarchy-dependent application: one
 * instance of a linked object, not all instances). The Shader may live in
 * another document, so users can build shader library documents. An empty
 * target list applies the shader's scene-level (post) programs globally.
 * Like-named dynamic properties override the shader's parameter values for
 * this binding only. Identical-path collisions between Appearance objects
 * are tie-broken by TreeRank.
 */
class AppExport Appearance : public DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::Appearance);

public:
    Appearance();

    /// Full-path targets the shader applies to; empty = scene level
    PropertyXLinkSubList Targets;
    /// The Shader object to apply, possibly from another document
    PropertyXLink Shader;

    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderAppearance";
    }
};

using AppearancePython = App::FeaturePythonT<Appearance>;

}  // namespace App


#endif  // APP_ShaderObject_H

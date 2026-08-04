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
const char* ShaderProgram::BlendEnums[] = {"Default", "Alpha", "Additive",
                                           nullptr};

ShaderProgram::ShaderProgram()
{
    ADD_PROPERTY_TYPE(Stage, ("material"), "Shader", Prop_None,
            "Renderer pipeline stage this program attaches to,\n"
            "e.g. 'material' (surface shading), 'water' (water-surface\n"
            "shading, makes the bound object a water body), 'volume'\n"
            "(per-point medium function, makes the bound object an\n"
            "emissive volume body), 'particle' (billboard program of\n"
            "generated seed quads, with the Emitter properties) or\n"
            "'post' (full screen pass)");
    Dialect.setEnums(DialectEnums);
    ADD_PROPERTY_TYPE(Dialect, ((long)0), "Shader", Prop_None,
            "Source dialect of the program text");
    ADD_PROPERTY_TYPE(VertexProgram, (""), "Shader", Prop_None,
            "Vertex stage source; leave empty to use the renderer's\n"
            "stock vertex stage of the target pipeline stage");
    ADD_PROPERTY_TYPE(FragmentProgram, (""), "Shader", Prop_None,
            "Fragment stage source");
    ADD_PROPERTY_TYPE(SimulateProgram, (""), "Emitter", Prop_None,
            "Particle state step source: a fragment program advancing\n"
            "the emitter's particle state (position/life, velocity) by\n"
            "one fixed time step, reading the previous state from\n"
            "s_pstate0/s_pstate1 and writing the next. Leave empty for\n"
            "a stateless emitter, whose vertex stage computes position\n"
            "from the seed and the clock alone");
    Blend.setEnums(BlendEnums);
    ADD_PROPERTY_TYPE(Blend, ((long)0), "Shader", Prop_None,
            "Blend override of the material-stage beauty draw:\n"
            "Default keeps the draw's stock state, Alpha blends by the\n"
            "fragment's alpha, Additive adds onto the framebuffer\n"
            "(typical for particles/glow)");
    ADD_PROPERTY_TYPE(DepthWrite, (true), "Shader", Prop_None,
            "Whether the material-stage beauty draw writes depth;\n"
            "turn off for blended effects that should not occlude");
    ADD_PROPERTY_TYPE(Enabled, (true), "Shader", Prop_None,
            "Whether the program takes part when its Shader is bound;\n"
            "disabled programs are skipped everywhere (the toggle for\n"
            "an effect's optional companion programs, e.g. particles)");
    ADD_PROPERTY_TYPE(EmitterCount, ((long)0), "Emitter", Prop_None,
            "> 0 marks a particle companion program: this many seed\n"
            "quads are generated at each Object-scope bound target,\n"
            "fit to the target's bounding box, and rendered with this\n"
            "program's billboard vertex stage");
    ADD_PROPERTY_TYPE(EmitterSeed, ((long)1), "Emitter", Prop_None,
            "Random seed of the generated particles");
    ADD_PROPERTY_TYPE(EmitterSpread, (Base::Vector3d(1.0, 1.0, 1.0)),
            "Emitter", Prop_None,
            "Seed box size as factors of the target bounding box size");
    ADD_PROPERTY_TYPE(EmitterOffset, (Base::Vector3d(0.0, 0.0, 0.0)),
            "Emitter", Prop_None,
            "Seed box center offset in target-bounding-box-size units\n"
            "(z = 0.5 centers the box on the target's top face)");
    ADD_PROPERTY_TYPE(EmitterMargin, (0.5), "Emitter", Prop_None,
            "Travel headroom as a fraction of the seed box diagonal,\n"
            "folded into the generated bounds so displaced billboards\n"
            "are not clipped by the auto near/far planes");
    ADD_PROPERTY_TYPE(EmitterRate, (60.0), "Emitter", Prop_None,
            "Fixed simulation steps per second of a stateful emitter\n"
            "(SimulateProgram). The step length is constant, so the\n"
            "motion is the same on every machine; a frame too slow to\n"
            "afford its steps lets the simulation fall behind rather\n"
            "than stretching them");
    ADD_PROPERTY_TYPE(EmitterWarmup, (0.0), "Emitter", Prop_None,
            "Seconds of simulation run from the reset state before a\n"
            "frozen frame is drawn (RenderDebug_FreezeFrame). Gives a\n"
            "deterministic capture settled motion instead of particles\n"
            "at their spawn points");
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(App::Shader, App::DocumentObject)

const char* Shader::DemoEnums[] = {"None", "Box", "Sphere", "Cylinder", "Cone",
                                   "Emitter", nullptr};

Shader::Shader()
{
    ADD_PROPERTY_TYPE(Programs, (nullptr), "Shader", Prop_None,
            "The shader programs forming this effect, one per pipeline stage");
    Demo.setEnums(DemoEnums);
    ADD_PROPERTY_TYPE(Demo, ((long)2), "Demo", Prop_None,  // default Sphere
            "Built-in shape for previewing the effect");
    ADD_PROPERTY_TYPE(DemoPlacement, (Base::Placement()), "Demo", Prop_None,
            "Placement of the demo shape, so several standalone demo\n"
            "shaders can compose a scene");
    ADD_PROPERTY_TYPE(DemoColor, (0.8f, 0.8f, 0.8f), "Demo", Prop_None,
            "Diffuse color of the demo shape");
    ADD_PROPERTY_TYPE(DemoSize, (Base::Vector3d(10.0, 10.0, 10.0)), "Demo", Prop_None,
            "Dimensions of the Box demo shape");
    ADD_PROPERTY_TYPE(DemoRadius, (5.0), "Demo", Prop_None,
            "Radius of the Sphere/Cylinder/Cone demo shape");
    ADD_PROPERTY_TYPE(DemoHeight, (10.0), "Demo", Prop_None,
            "Height of the Cylinder/Cone demo shape");
    ADD_PROPERTY_TYPE(EmitterCount, ((long)500), "Demo", Prop_None,
            "Number of particle seed quads of the Emitter demo shape");
    ADD_PROPERTY_TYPE(EmitterSeed, ((long)0), "Demo", Prop_None,
            "Random seed of the Emitter demo shape (deterministic\n"
            "for a given seed/count/spread)");
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(App::Appearance, App::LinkGroup)

const char* Appearance::ScopeEnums[] = {"Object", "Instance", "Element", nullptr};

Appearance::Appearance()
{
    Scope.setEnums(ScopeEnums);
    ADD_PROPERTY_TYPE(Scope, ((long)0), "Appearance", Prop_None,
            "How the shader applies to the target children:\n"
            "Object: attach directly to each target's resolved object —\n"
            "  cheap, all instances everywhere, inherited by children\n"
            "Instance: override every scene occurrence whose resolved\n"
            "  chain ends in the target link's chain (suffix-anchored)\n"
            "Element: like Instance, but a target subname ending in a\n"
            "  face element (e.g. Face3) shades only that face");
}

Shader *Appearance::resolveShader(DocumentObject **shaderChild) const
{
    if (shaderChild)
        *shaderChild = nullptr;
    for (auto child : ElementList.getValues()) {
        if (!child || !child->isAttachedToDocument())
            continue;
        auto resolved = child->getLinkedObject(true);
        if (auto shader = Base::freecad_dynamic_cast<Shader>(resolved)) {
            if (shaderChild)
                *shaderChild = child;
            return shader;
        }
    }
    return nullptr;
}

std::vector<DocumentObject *> Appearance::getTargets() const
{
    DocumentObject *shaderChild = nullptr;
    resolveShader(&shaderChild);
    std::vector<DocumentObject *> res;
    for (auto child : ElementList.getValues()) {
        if (child && child != shaderChild && child->isAttachedToDocument())
            res.push_back(child);
    }
    return res;
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

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
#include "Link.h"
#include "PropertyFile.h"
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
    /// Source dialect of the program text: shading-language text
    /// (BGFX_SC, GLSL) or, for a "material"-stage program, a MaterialX
    /// document in FragmentProgram (MATERIALX -- see
    /// docs/CyclesIntegration.md sec 8 item 15)
    PropertyEnumeration Dialect;
    /// Vertex stage source; empty uses the renderer's stock vertex stage
    PropertyStringIncluded VertexProgram;
    /// Fragment stage source
    PropertyStringIncluded FragmentProgram;
    /// Particle state step of a stateful emitter: a fragment program
    /// advancing the state textures by one fixed step. Empty = the
    /// emitter is stateless (docs/RenderEngine.md §5.8)
    PropertyStringIncluded SimulateProgram;
    /// Which surface of a MATERIALX document this program is shaded
    /// by: the name of one of the document's surfacematerial nodes, or
    /// of a bare surface shader node where it states no material.
    /// Empty renders the first surface the document states, which is
    /// what a single-material document has. An asset's whole material
    /// set is usually ONE document, and this is what picks one out of
    /// it (docs/MaterialStorage.md sec 17.13).
    PropertyString Surface;
    /// Files a MATERIALX document refers to, each under the name the
    /// document calls it by. What makes a document with image maps
    /// self-contained: the bytes ride in the .FCStd, and the text handed
    /// to the renderers names them where they actually are on the
    /// machine that opened it (docs/MaterialStorage.md sec 16). Kept in
    /// step with the document by the view provider, which is the side
    /// that can read one.
    PropertyFileIncludedList Images;
    /// Blend override of the material-stage beauty draw
    /// (Default keeps the draw's stock state)
    PropertyEnumeration Blend;
    /// Depth write of the material-stage beauty draw
    PropertyBool DepthWrite;
    /// Whether the program takes part when its Shader resolves; a
    /// disabled program is skipped everywhere (the switch a bundled
    /// effect's optional companion programs — e.g. particles — ship
    /// turned off on)
    PropertyBool Enabled;
    /// > 0 marks a PARTICLE companion program: when the effect is
    /// bound with Object scope, this many seed quads are generated at
    /// each target, fit to the target's bounding box, and rendered
    /// with this program (the billboard vertex stage expands them).
    /// A particle program never becomes the effect's main program.
    PropertyInteger EmitterCount;
    /// Random seed of the generated particles
    PropertyInteger EmitterSeed;
    /// Seed box size as factors of the target bounding box size
    PropertyVector EmitterSpread;
    /// Seed box center offset in target-bounding-box-size units
    /// (e.g. z = 0.5 centers the box on the target's top face)
    PropertyVector EmitterOffset;
    /// Travel headroom as a fraction of the seed box diagonal, folded
    /// into the generated geometry's bounds so displaced billboards
    /// are not clipped by the auto near/far planes
    PropertyFloat EmitterMargin;
    /// Fixed simulation steps per second of a stateful emitter
    PropertyFloat EmitterRate;
    /// Rate of the emitter's clock against the wall clock (1 = real
    /// time). Scales only how fast the motion plays, never its shape:
    /// the step length and every step-program parameter stay put, so
    /// the trajectory is the same one, traced faster or slower
    PropertyFloat EmitterTimeScale;
    /// Seconds of simulation run from the reset state before a frozen
    /// frame is drawn, so a freeze-frame capture of a stateful effect
    /// shows settled motion and still reproduces byte for byte
    PropertyFloat EmitterWarmup;

    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderShaderProgram";
    }

    /** Restore a document written while the sources were plain strings.
     *
     * PropertyStringIncluded writes the same <String> element, so the value
     * itself needs no conversion -- only the type-changed door has to be
     * opened, or the container drops it without a word.
     */
    void handleChangedPropertyType(Base::XMLReader &reader,
                                   const char *TypeName,
                                   Property *prop) override;

protected:
    void onChanged(const Property *prop) override;

private:
    /// Name the stored sources after what they hold, which is what an
    /// unpacked project shows: the dialect decides the extension.
    void updateSourceExtensions();

    static const char* DialectEnums[];
    static const char* BlendEnums[];
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
    /// Built-in preview shape (None/Box/Sphere/Cylinder/Cone/Emitter)
    PropertyEnumeration Demo;
    /// Placement of the preview shape, so several standalone demo
    /// shaders can compose a scene
    PropertyPlacement DemoPlacement;
    /// Diffuse color of the preview shape (e.g. the body color a
    /// water demo absorbs toward)
    PropertyColor DemoColor;
    /// Box demo dimensions
    PropertyVector DemoSize;
    /// Sphere/Cylinder/Cone demo radius
    PropertyFloat DemoRadius;
    /// Cylinder/Cone demo height
    PropertyFloat DemoHeight;
    /// Number of particle seed quads of the Emitter demo shape
    PropertyInteger EmitterCount;
    /// Random seed of the Emitter demo shape
    PropertyInteger EmitterSeed;

    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderShader";
    }

private:
    static const char* DemoEnums[];
};

using ShaderPython = App::FeaturePythonT<Shader>;


/** Groups a Shader with the target objects it applies to (§6.5).
 *
 * A link group whose children carry both the effect and its scope: the
 * shader is the first child that resolves (through any chain of links) to
 * an App::Shader — use an App::Link child to pull the effect from a shader
 * library document — and every other child is a target. Plain children are
 * claimed into the group in the tree; use App::Link children to bind
 * objects without restructuring the document, and to carry instance
 * context (a link whose subname path points into an assembly).
 *
 * Scope selects how targets are resolved:
 * - "Object" (default): the shader attaches directly to each target's
 *   resolved final object — cheap, applies to every instance of that
 *   object everywhere, and is inherited by all of the object's children
 *   (link material-override semantics).
 * - "Instance": each target contributes its resolved object chain (e.g.
 *   Link001 -> A1.A2.Box registers [A1, A2, Box]); the shader applies to
 *   every scene occurrence whose resolved chain ends in that chain —
 *   suffix-anchored, per-occurrence override (persistent-selection
 *   style; costs scale with occurrence count, and matched draws leave
 *   the instancing fast path).
 * - "Element": like Instance, but a target subname ending in a face
 *   element (e.g. A1.A2.Box.Face3) restricts the override to that face
 *   of each matched occurrence; a target without an element part
 *   behaves like Instance. Face elements only — edge/vertex elements
 *   have no material stage to replace.
 *
 * No target children = the shader's post-stage programs apply scene-wide.
 * Like-named dynamic properties override shader parameters per binding.
 * Overlaps: longest chain wins, then TreeRank, then name.
 */
class AppExport ShaderBinding : public LinkGroup
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::ShaderBinding);

public:
    ShaderBinding();

    /// Target scope: whole object (direct attachment), matched
    /// occurrences (per-instance chain override), or a single face
    /// element of each matched occurrence
    PropertyEnumeration Scope;

    /// Scope enum indices
    enum class ScopeMode {
        Object = 0,
        Instance = 1,
        Element = 2,
    };
    ScopeMode scopeMode() const {
        return static_cast<ScopeMode>(Scope.getValue());
    }

    /// The first child resolving to an App::Shader, or null
    Shader *resolveShader(DocumentObject **shaderChild = nullptr) const;
    /// All children other than the shader child (the binding targets)
    std::vector<DocumentObject *> getTargets() const;

    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderShaderBinding";
    }

private:
    static const char* ScopeEnums[];
};

using ShaderBindingPython = App::FeaturePythonT<ShaderBinding>;

}  // namespace App


#endif  // APP_ShaderObject_H

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

/*
 * The raster half of MaterialX (docs/CyclesIntegration.md sec 6.10):
 * a document generates a MATERIAL-INPUTS function, not a shader.
 *
 * MaterialX's own hardware generator emits a complete lit program --
 * its lighting, its environment, its uniform blocks. None of that is
 * wanted here: the engine already owns shadows, IBL, the section clip,
 * per-face palettes and the rest, and a document that replaced them
 * would lose every one. What is wanted is the surface, so the generated
 * code states the OpenPBR parameters (fc_openpbr.sh) and stops:
 *
 *     void fcUserMaterialInputs(inout FcOpenPbr m, FcMtlxGeom g)
 *
 * spliced into the stock mesh fragment shader the way the volume stage
 * splices its medium function (docs/RenderEngine.md sec 5.3).
 *
 * That shape falls out of one fact about the library: OpenPBR's
 * reference implementation IS a MaterialX nodegraph, so the stock
 * generator emits a call to it with every OpenPBR parameter as an
 * argument, and the 2100 lines that follow are that nodegraph. Give the
 * surface node an implementation of our own and the pattern graph above
 * it still generates exactly as MaterialX would -- with the whole
 * standard library behind it -- while the closure below it is never
 * expanded. GenContext::addNodeImplementation is the documented door:
 * a pre-registered implementation wins over expanding the nodegraph.
 *
 * What the raster path cannot do it REPORTS, and the draw keeps its
 * stock appearance (the sandboxed-failure rule the Cycles interpreter
 * follows). Two such cases today: a shading model with no translation
 * to OpenPBR, and an image node -- the generated code declares no
 * samplers because nothing binds textures to a user shader yet.
 */

#include "PreCompiled.h"

#ifndef HAVE_MATERIALX

#include "MaterialXSupport.h"

namespace Render::MaterialX {

GeneratedMaterial generate(const std::string &, const std::string &)
{
    GeneratedMaterial out;
    out.error = "MaterialX support is not built (BUILD_MATERIALX)";
    return out;
}

}  // namespace Render::MaterialX

#else  // HAVE_MATERIALX

#include "MaterialXSupportP.h"

#include <MaterialXCore/Material.h>
#include <MaterialXGenGlsl/EsslShaderGenerator.h>
#include <MaterialXGenHw/HwConstants.h>
#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/ShaderNodeImpl.h>
#include <MaterialXGenShader/ShaderStage.h>

#include <Base/Console.h>

namespace Render::MaterialX {

namespace {

/// The OpenPBR inputs the rasterizer expresses, and the FcOpenPbr field
/// each lands in. Deliberately a subset: transmission, subsurface,
/// anisotropy and thin film have no raster lobe (fc_openpbr.sh states
/// why), and an input left out here simply keeps the spec default the
/// shader starts from -- which is what makes leaving it out safe.
const struct { const char *input, *field; } FIELDS[] = {
    { "base_color", "baseColor" },
    { "base_weight", "baseWeight" },
    { "base_metalness", "baseMetalness" },
    { "base_diffuse_roughness", "baseDiffuseRoughness" },
    { "specular_color", "specularColor" },
    { "specular_weight", "specularWeight" },
    { "specular_roughness", "specularRoughness" },
    { "specular_ior", "specularIor" },
    { "coat_color", "coatColor" },
    { "coat_weight", "coatWeight" },
    { "coat_roughness", "coatRoughness" },
    { "coat_ior", "coatIor" },
    { "coat_darkening", "coatDarkening" },
    { "fuzz_color", "fuzzColor" },
    { "fuzz_weight", "fuzzWeight" },
    { "fuzz_roughness", "fuzzRoughness" },
    { "emission_color", "emissionColor" },
    { "emission_luminance", "emissionLuminance" },
    { "geometry_opacity", "geometryOpacity" },
};

/// The surface node, emitted as assignments into the caller's FcOpenPbr
/// instead of as the OpenPBR closure nodegraph.
class OpenPbrInputs : public mx::ShaderNodeImpl
{
public:
    static mx::ShaderNodeImplPtr create()
    {
        return std::make_shared<OpenPbrInputs>();
    }

    void emitFunctionCall(const mx::ShaderNode &node, mx::GenContext &ctx,
                          mx::ShaderStage &stage) const override
    {
        const mx::ShaderGenerator &gen = ctx.getShaderGenerator();
        const mx::Syntax &syntax = gen.getSyntax();
        const mx::ShaderNode *graph = node.getParent();
        for (const auto &f : FIELDS) {
            const mx::ShaderInput *in = node.getInput(f.input);
            if (!in)
                continue;
            std::string value;
            const mx::ShaderOutput *conn = in->getConnection();
            if (conn && conn->getNode() == graph) {
                // The graph's own interface socket with nothing
                // upstream of it: the document states a constant here,
                // so it is emitted as a literal rather than as a
                // uniform that nothing would ever write. What a
                // document leaves connectable becomes a Param_* in a
                // later step; until then a value change regenerates.
                value = conn->getValue()
                    ? syntax.getValue(conn->getType(), *conn->getValue())
                    : syntax.getDefaultValue(conn->getType());
            }
            else {
                value = gen.getUpstreamResult(in, ctx);
            }
            gen.emitLine(std::string("m.") + f.field + " = " + value, stage);
        }
    }
};

/// The bgfx target. ESSL is the closest stock flavour to what shaderc
/// accepts, so the syntax and the whole node library come from it; what
/// this overrides is the SHAPE of the pixel stage -- a function instead
/// of a program.
class BgfxShaderGenerator : public mx::EsslShaderGenerator
{
public:
    explicit BgfxShaderGenerator(mx::TypeSystemPtr ts)
        : mx::EsslShaderGenerator(ts)
    {}

    static mx::ShaderGeneratorPtr create(mx::TypeSystemPtr ts = nullptr)
    {
        return std::make_shared<BgfxShaderGenerator>(ts ? ts
                                                        : mx::TypeSystem::create());
    }

    /// Vertex data the generated code asked for and the mesh shader
    /// cannot answer; the value reads as zero and the caller reports it.
    mutable std::vector<std::string> unmapped;

protected:
    void emitPixelStage(const mx::ShaderGraph &graph, mx::GenContext &ctx,
                        mx::ShaderStage &stage) const override
    {
        // No directives, no uniform blocks, no lighting and no output
        // declaration: this is a function inside someone else's shader.
        //
        // bgfx_shader.sh defines M_PI as well, and not with identical
        // text, which the preprocessor calls a redefinition and stops on.
        emitLine("#ifdef M_PI", stage, false);
        emitLine("#undef M_PI", stage, false);
        emitLine("#endif", stage, false);
        // The uv transform the image nodes include by token. The stock
        // pixel stage sets this substitution, and this one replaces
        // that stage, so it has to be set here or the include fails.
        _tokenSubstitutions[mx::ShaderGenerator::T_FILE_TRANSFORM_UV] =
            ctx.getOptions().fileTextureVerticalFlip ? "mx_transform_uv_vflip.glsl"
                                                     : "mx_transform_uv.glsl";
        emitLibraryInclude("stdlib/genglsl/lib/mx_math.glsl", ctx, stage);
        emitLineBreak(stage);
        emitFunctionDefinitions(graph, ctx, stage);

        setFunctionName("fcUserMaterialInputs", stage);
        emitLine("void fcUserMaterialInputs(inout FcOpenPbr m, FcMtlxGeom g)",
                 stage, false);
        emitFunctionBodyBegin(graph, ctx, stage);
        emitGeometryPreamble(stage);
        emitFunctionCalls(graph, ctx, stage, mx::ShaderNode::Classification::TEXTURE);
        for (mx::ShaderGraphOutputSocket *socket : graph.getOutputSockets()) {
            if (!socket->getConnection())
                continue;
            const mx::ShaderNode *up = socket->getConnection()->getNode();
            if (up->getParent() == &graph)
                emitFunctionCall(*up, ctx, stage);
        }
        emitFunctionBodyEnd(graph, ctx, stage);
    }

private:
    /// Whatever the graph asks of the geometry, answered from the mesh
    /// shader's own varyings. Driven by MaterialX's vertex-data block
    /// rather than by a guessed list, so a node reaching for something
    /// new is reported instead of silently reading an undeclared name.
    void emitGeometryPreamble(mx::ShaderStage &stage) const
    {
        const mx::VariableBlock &vd = stage.getInputBlock(mx::HW::VERTEX_DATA);
        for (size_t i = 0; i < vd.size(); ++i) {
            const mx::ShaderPort *v = vd[i];
            const std::string &type = getSyntax().getTypeName(v->getType());
            std::string src = geomSource(v->getName());
            if (src.empty()) {
                unmapped.push_back(v->getName());
                src = getSyntax().getDefaultValue(v->getType());
            }
            emitLine(type + " " + v->getName() + " = " + src, stage);
        }
        emitLineBreak(stage);
    }

    /// What the mesh shader can answer for one piece of vertex data.
    /// Empty means it cannot.
    static std::string geomSource(const std::string &raw)
    {
        // A vertex-data port is named by its SUBSTITUTION TOKEN
        // ("$normalWorld"), not by the text the emitted code carries;
        // the marker has to come off before the name means anything.
        std::string name = (!raw.empty() && raw[0] == '$') ? raw.substr(1) : raw;
        // Only the first set is fed: a CAD mesh carries one texture
        // coordinate and one colour.
        if (name.rfind("texcoord_", 0) == 0)
            return name == "texcoord_0" ? "g.texcoord0" : std::string();
        if (name.rfind("color_", 0) == 0)
            return name == "color_0" ? "g.color0" : std::string();
        if (name == "positionWorld")   return "g.positionWorld";
        if (name == "normalWorld")     return "g.normalWorld";
        if (name == "tangentWorld")    return "g.tangentWorld";
        if (name == "bitangentWorld")
            return "cross(g.normalWorld, g.tangentWorld)";
        if (name == "positionObject")  return "g.positionObject";
        if (name == "normalObject")    return "g.normalObject";
        if (name == "tangentObject")   return "vec3(1.0, 0.0, 0.0)";
        if (name == "bitangentObject") return "vec3(0.0, 1.0, 0.0)";
        return std::string();
    }
};

/// Image nodes reach the generated code as sampler uniforms, which
/// nothing declares or binds on the raster side yet. Reported so the
/// draw falls back whole rather than compiling against an undeclared
/// name -- Cycles renders such a document properly, loading the files
/// itself (section 6.9).
bool usesImages(const mx::ShaderPtr &shader, std::string &which)
{
    const mx::ShaderStage &ps = shader->getStage(mx::Stage::PIXEL);
    // The PUBLIC block only: MaterialX declares samplers of its own in
    // the private block (its environment radiance and irradiance maps),
    // and those are not the document's doing -- reading them as image
    // nodes would refuse every document ever written.
    const mx::VariableBlock &block = ps.getUniformBlock(mx::HW::PUBLIC_UNIFORMS);
    for (size_t i = 0; i < block.size(); ++i) {
        if (block[i]->getType() == mx::Type::FILENAME) {
            which = block[i]->getName();
            return true;
        }
    }
    return false;
}

}  // namespace

GeneratedMaterial generate(const std::string &xml, const std::string &sourcePath)
{
    GeneratedMaterial out;
    if (!available()) {
        out.error = "this build carries no MaterialX";
        return out;
    }
    try {
        mx::DocumentPtr doc = loadDocument(xml, sourcePath, out.error);
        if (!doc)
            return out;
        mx::NodePtr surface = openPbrSurface(doc, out.error, out.warnings);
        if (!surface)
            return out;

        auto gen = std::make_shared<BgfxShaderGenerator>(mx::TypeSystem::create());
        mx::GenContext ctx(gen);
        // The data library holds the node implementations' source.
        ctx.registerSourceCodeSearchPath(
            mx::FilePath(dataLibraryPath()).getParentPath());
        // REDUCED publishes only what a document really left open, so
        // every stated constant folds into the code as a literal.
        ctx.getOptions().shaderInterfaceType = mx::SHADER_INTERFACE_REDUCED;
        auto cms = mx::DefaultColorManagementSystem::create(gen->getTarget());
        cms->loadLibrary(doc);
        gen->setColorManagementSystem(cms);
        // The interception: OpenPBR's implementation is a nodegraph, and
        // a pre-registered implementation wins over expanding it.
        ctx.addNodeImplementation("NG_open_pbr_surface_surfaceshader",
                                  OpenPbrInputs::create());

        mx::ShaderPtr shader = gen->generate("fcmtlx", surface, ctx);
        if (!shader) {
            out.error = "the document generated no shader";
            return out;
        }
        std::string image;
        if (usesImages(shader, image)) {
            out.error = "image nodes are not bound in the raster path yet ('"
                + image + "'); the path tracer renders this document";
            return out;
        }
        for (const std::string &u : gen->unmapped) {
            out.warnings.push_back(
                "the mesh shader cannot answer '" + u
                + "'; the document reads it as zero");
        }
        out.source = shader->getSourceCode(mx::Stage::PIXEL);
        out.valid = !out.source.empty();
        if (!out.valid)
            out.error = "the generated shader is empty";
    }
    catch (const std::exception &e) {
        out.valid = false;
        out.source.clear();
        out.error = e.what();
    }
    return out;
}

}  // namespace Render::MaterialX

#endif  // HAVE_MATERIALX

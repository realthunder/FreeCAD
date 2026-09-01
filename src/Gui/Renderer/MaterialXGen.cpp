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
 * follows). One such case today: a shading model with no translation to
 * OpenPBR. Image nodes DO generate -- each becomes a SAMPLER2D the
 * engine binds a loaded texture to (sec 6.12) -- but only up to the
 * handful of texture units the mesh shader leaves free, and a document
 * wanting more than that is reported rather than drawn wrong.
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

#include <map>
#include <set>
#include <vector>

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
                // uniform that nothing would ever write. A value
                // stated on the surface node is the document's
                // statement, not a declared interface -- what a graph
                // DECLARES is what becomes a parameter (sec 6.11) --
                // so a change to one regenerates the shader.
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

    /// The document being generated, for resolving an image node's
    /// filename against the document's own search path -- which is
    /// where a material states its images (sec 6.12).
    mx::DocumentPtr document;
    /// The samplers emitted for the document's image nodes, in
    /// declaration order, with the unit each one claimed.
    mutable std::vector<GeneratedMaterial::Image> images;
    /// Set when the document needs more image units than there are.
    /// The caller refuses the whole document rather than drawing it
    /// with some of its maps silently missing.
    mutable std::string imageOverflow;

    /// The document's public interface (docs/CyclesIntegration.md sec
    /// 6.11), keyed by the namepath of the declaring input -- which is
    /// what MaterialX puts on the published uniform it generates for
    /// it, whether the value reaches the graph once or a dozen times.
    /// Everything else the graph publishes is a value the document
    /// stated and nothing will write, so it is emitted as a constant.
    std::map<std::string, const MaterialInput *> declaredInputs;
    /// The same inputs by NAME, for the one case where MaterialX does
    /// not leave the namepath behind: a graph interface input named
    /// exactly as an input of the surface shader is FUSED with the
    /// surface's own socket -- interface names resolve in the enclosing
    /// graph's socket namespace, and the surface's nodedef put every
    /// one of its inputs in there first. `base_color` on a graph
    /// feeding `base_color` on the surface is not an exotic document,
    /// it is the obvious one to write.
    std::map<std::string, const MaterialInput *> declaredByName;
    /// Which of them the generated code actually bound, so the caller
    /// can say what the document declares but the raster path folded.
    mutable std::set<std::string> bound;

    /// Namepaths of the surface shader node and of its nodedef. Every
    /// value published under either of those is an input of the
    /// surface itself, which OpenPbrInputs emits as a literal and no
    /// generated line ever names -- so declaring them would be a few
    /// dozen dead globals per material, in the same names the mesh
    /// shader uses.
    std::string surfacePath, surfaceDefPath;

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
        // MaterialX writes the GLSL builtins texture() and textureGrad();
        // bgfx spells the portable forms texture2D() and texture2DGrad(),
        // and defines them ONLY on the backends where the builtin is
        // missing. So the shim goes the other way and only on those
        // backends -- the same condition bgfx_shader.sh switches on --
        // leaving the GLSL and ESSL builds reading their own builtin.
        // (The preprocessor's own re-entry rule is what stops
        // texture -> texture2D -> texture from looping.)
        emitLine("#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_PSSL "
                 "|| BGFX_SHADER_LANGUAGE_SPIRV || BGFX_SHADER_LANGUAGE_METAL "
                 "|| BGFX_SHADER_LANGUAGE_WGSL",
                 stage, false);
        emitLine("#define texture(_s, _c) texture2D(_s, _c)", stage, false);
        emitLine("#define textureGrad(_s, _c, _dx, _dy) "
                 "texture2DGrad(_s, _c, _dx, _dy)",
                 stage, false);
        emitLine("#endif", stage, false);
        // The uv transform the image nodes include by token. The stock
        // pixel stage sets this substitution, and this one replaces
        // that stage, so it has to be set here or the include fails.
        _tokenSubstitutions[mx::ShaderGenerator::T_FILE_TRANSFORM_UV] =
            ctx.getOptions().fileTextureVerticalFlip ? "mx_transform_uv_vflip.glsl"
                                                     : "mx_transform_uv.glsl";
        emitUniformDeclarations(stage);
        emitLibraryInclude("stdlib/genglsl/lib/mx_math.glsl", ctx, stage);
        emitLineBreak(stage);
        emitFunctionDefinitions(graph, ctx, stage);

        setFunctionName("fcUserMaterialInputs", stage);
        emitLine("void fcUserMaterialInputs(inout FcOpenPbr m, FcMtlxGeom g)",
                 stage, false);
        emitFunctionBodyBegin(graph, ctx, stage);
        emitGeometryPreamble(stage);
        emitInterfacePreamble(stage);
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
    /// Declare one image node's sampler, and record what has to be
    /// bound to it.
    ///
    /// The unit is claimed from kImageUnitBase upward: the mesh
    /// fragment stage this function is spliced into holds 0..10 and a
    /// stateful particle emitter binds 11 and 12, so what is left is
    /// small and a document wanting more is refused whole rather than
    /// drawn with some of its maps reading another pass's texture. A
    /// 2D array over one unit is what lifts the limit when it starts
    /// to bite (sec 6.12).
    void emitImageSampler(const mx::ShaderPort *port,
                          mx::ShaderStage &stage) const
    {
        GeneratedMaterial::Image image;
        image.name = port->getVariable();
        image.unit = kImageUnitBase + int(images.size());
        if (port->getValue())
            image.path = resolveFile(document, port->getValue()->getValueString());
        image.colorSpace = port->getColorSpace();
        if (image.unit > kImageUnitLast) {
            if (imageOverflow.empty())
                imageOverflow = "the document needs more image units than the "
                                "raster path has free (" + std::to_string(
                                    kImageUnitLast - kImageUnitBase + 1) + ")";
            return;
        }
        // SAMPLER2D is bgfx's own declaration macro, and on the
        // backends that split texture from sampler it also defines
        // `sampler2D` as the struct pair -- which is why MaterialX's
        // stock signature token, "sampler2D tex_sampler", passes one
        // of these to mx_image_* unchanged.
        emitLine("SAMPLER2D(" + image.name + ", " + std::to_string(image.unit)
                     + ")",
                 stage);
        images.push_back(std::move(image));
    }

    /// The declarations of everything the graph publishes. MaterialX
    /// would emit a uniform for each; here a value the document stated
    /// and nothing will write becomes a file-scope constant, and only
    /// what the document DECLARES as its interface becomes a uniform --
    /// one `vec4` lane group per parameter, which is the packing every
    /// other user-shader parameter already travels in
    /// (docs/RenderDebug.md sec 6.4).
    void emitUniformDeclarations(mx::ShaderStage &stage) const
    {
        const mx::VariableBlock &block =
            stage.getUniformBlock(mx::HW::PUBLIC_UNIFORMS);
        std::set<std::string> declared;
        bool any = false;
        for (size_t i = 0; i < block.size(); ++i) {
            const mx::ShaderPort *port = block[i];
            // An image node reaches the generated code as a sampler
            // passed to mx_image_*, and MaterialX writes that parameter
            // through its own token, so the declaration is all this
            // side owes it (sec 6.12). The file itself is not opened
            // here: the path is reported and the engine binds a texture
            // loaded elsewhere.
            if (port->getType() == mx::Type::FILENAME) {
                emitImageSampler(port, stage);
                any = true;
                continue;
            }
            const MaterialInput *param = lookup(port);
            if (!param) {
                if (ownedBySurface(port))
                    continue;
                const std::string value =
                    port->getValue()
                        ? getSyntax().getValue(port->getType(), *port->getValue())
                        : getSyntax().getDefaultValue(port->getType());
                emitLine("const " + getSyntax().getTypeName(port->getType()) + " "
                             + port->getVariable() + " = " + value,
                         stage);
                any = true;
                continue;
            }
            // Two node inputs may name the same declared input, and
            // each gets a published uniform of its own; they are one
            // parameter and read one lane group.
            if (declared.insert(param->name).second) {
                emitLine("uniform vec4 u_" + param->name, stage);
                any = true;
            }
        }
        if (any)
            emitLineBreak(stage);
    }

    /// The declared inputs, read out of their lanes at the top of the
    /// function. Inside the body rather than at file scope because a
    /// uniform is not a constant expression, and because the SPIR-V
    /// path cannot read one outside a function at all -- the same
    /// restriction the geometry preamble is written around.
    void emitInterfacePreamble(mx::ShaderStage &stage) const
    {
        const mx::VariableBlock &block =
            stage.getUniformBlock(mx::HW::PUBLIC_UNIFORMS);
        bool any = false;
        for (size_t i = 0; i < block.size(); ++i) {
            const mx::ShaderPort *port = block[i];
            const MaterialInput *param = lookup(port);
            if (!param)
                continue;
            bound.insert(param->name);
            emitLine(getSyntax().getTypeName(port->getType()) + " "
                         + port->getVariable() + " = "
                         + lane("u_" + param->name, param->type),
                     stage);
            any = true;
        }
        if (any)
            emitLineBreak(stage);
    }

    /// Whether a published value is an input of the surface node
    /// itself (stated on it, or its nodedef's default).
    bool ownedBySurface(const mx::ShaderPort *port) const
    {
        const std::string &path = port->getPath();
        auto under = [&path](const std::string &owner) {
            return !owner.empty() && path.size() > owner.size()
                && path.compare(0, owner.size(), owner) == 0
                && path[owner.size()] == '/';
        };
        return under(surfacePath) || under(surfaceDefPath);
    }

    /// The declared input a published uniform stands for, or null when
    /// it stands for a stated value.
    const MaterialInput *lookup(const mx::ShaderPort *port) const
    {
        auto it = declaredInputs.find(port->getPath());
        if (it != declaredInputs.end())
            return it->second;
        // The fused case above. Matching by name is safe because the
        // fusion is itself by name: after it there is exactly one
        // socket carrying that name, and every read of the declared
        // input goes through it.
        auto byName = declaredByName.find(port->getName());
        return byName == declaredByName.end() ? nullptr : byName->second;
    }

    /// One parameter read out of its vec4 lanes, as its own type.
    static std::string lane(const std::string &uniform, const std::string &type)
    {
        if (type == "integer")
            return "int(" + uniform + ".x)";
        if (type == "boolean")
            return uniform + ".x != 0.0";
        if (type == "vector2")
            return uniform + ".xy";
        if (type == "vector3" || type == "color3")
            return uniform + ".xyz";
        if (type == "vector4" || type == "color4")
            return uniform;
        return uniform + ".x";
    }

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
        // Read before the translation, because the translation rewrites
        // the surface: this is the node the document itself states, and
        // the one inspect() and the path tracer read their interface
        // from.
        std::vector<mx::NodePtr> authored = surfaceShaders(doc);
        mx::NodePtr surface = openPbrSurface(doc, out.error, out.warnings);
        if (!surface)
            return out;

        auto gen = std::make_shared<BgfxShaderGenerator>(mx::TypeSystem::create());
        // What the document DECLARES becomes a uniform; everything
        // else it states is emitted as a constant, which is what the
        // interface map decides port by port (sec 6.11). The interface
        // is read from the surface the document was AUTHORED with --
        // the same one the property editor and the path tracer read --
        // and a graph interface survives the OpenPBR translation
        // untouched, so the two enumerations name the same inputs.
        std::vector<MaterialInput> inputs =
            authored.empty() ? std::vector<MaterialInput>()
                             : publicInputs(doc, authored.front());
        for (const auto &input : inputs) {
            gen->declaredInputs[input.path] = &input;
            gen->declaredByName[input.name] = &input;
        }
        gen->document = doc;
        gen->surfacePath = surface->getNamePath();
        if (mx::NodeDefPtr def = surface->getNodeDef())
            gen->surfaceDefPath = def->getNamePath();
        mx::GenContext ctx(gen);
        // The data library holds the node implementations' source.
        ctx.registerSourceCodeSearchPath(
            mx::FilePath(dataLibraryPath()).getParentPath());
        // COMPLETE publishes every value the graph did not connect,
        // which is the only way a DECLARED input reaches the generated
        // code as a name rather than folded into it as a number. The
        // rest is folded here instead, by emitUniformDeclarations.
        ctx.getOptions().shaderInterfaceType = mx::SHADER_INTERFACE_COMPLETE;
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
        // Only the PUBLIC uniform block is read for images: MaterialX
        // declares samplers of its own in the private block (its
        // environment radiance and irradiance maps), and those are not
        // the document's doing.
        if (!gen->imageOverflow.empty()) {
            out.error = gen->imageOverflow
                + "; the path tracer renders this document";
            return out;
        }
        out.images = std::move(gen->images);
        for (const auto &image : out.images) {
            if (image.path.empty())
                out.warnings.push_back(
                    "the image for '" + image.name
                    + "' is not where the document says; it draws as black");
        }
        for (const std::string &u : gen->unmapped) {
            out.warnings.push_back(
                "the mesh shader cannot answer '" + u
                + "'; the document reads it as zero");
        }
        // A declared input the generated code never read is one the
        // graph folded away -- the path tracer still answers to it, so
        // the parameter is not withdrawn, but on this side it is inert
        // and saying so beats a knob that silently does nothing.
        for (const auto &input : inputs) {
            if (!gen->bound.count(input.name))
                out.warnings.push_back(
                    "the raster path folded the declared input '" + input.name
                    + "' into the shader; it takes no parameter here");
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

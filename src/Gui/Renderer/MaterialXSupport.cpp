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

#ifdef HAVE_MATERIALX
#include "MaterialXSupportP.h"
#else
#include "MaterialXSupport.h"
#endif

#ifdef HAVE_MATERIALX

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <map>
#include <mutex>
#include <set>

#include <MaterialXCore/Material.h>
#include <MaterialXCore/Value.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXGenShader/ShaderTranslator.h>

#include <Base/Console.h>

#endif  // HAVE_MATERIALX

namespace Render::MaterialX
{

std::string dataLibraryPath()
{
#ifdef HAVE_MATERIALX
    return RendererFactory::resourcePath() + "materialx/libraries";
#else
    return {};
#endif
}

bool available()
{
#ifdef HAVE_MATERIALX
    return true;
#else
    return false;
#endif
}

#ifndef HAVE_MATERIALX

DocumentInfo inspect(const std::string &, const std::string &, const std::string &)
{
    DocumentInfo info;
    info.error = "MaterialX support is not built (BUILD_MATERIALX)";
    return info;
}

std::vector<ImageReference> imageReferences(const std::string &, const std::string &)
{
    return {};
}

std::vector<Look> looks(const std::string &, const std::string &)
{
    return {};
}

std::string substituteImages(const std::string &xml, const std::vector<ImageReference> &)
{
    // Without the library there is no way to find the references, and a
    // document handed back half-rewritten would be worse than one left
    // alone. Nothing consumes it in this build either way.
    return xml;
}

std::string applyInputsToDocument(const std::string &xml,
                                  const std::vector<RenderDebugConfig::UserParam> &,
                                  const std::string &)
{
    return xml;
}

std::vector<MaterialInput> publicInputs(const std::string &, const std::string &)
{
    return {};
}

#else  // HAVE_MATERIALX

mx::ConstDocumentPtr dataLibrary()
{
    // Parsed once for the process: it is ~2.5MB of node definitions
    // every document imports, and both consumers want the same one.
    // Held by value in a function-local static so the loading is
    // thread-safe without a lock of our own.
    static mx::DocumentPtr library = [] {
        mx::DocumentPtr doc = mx::createDocument();
        mx::FilePath dir(dataLibraryPath());
        try {
            // loadLibraries names the folders relative to a search
            // path, so the search path is the PARENT of libraries/.
            mx::FileSearchPath search(dir.getParentPath());
            mx::StringSet loaded =
                mx::loadLibraries({ dir.getBaseName() }, search, doc);
            if (loaded.empty()) {
                Base::Console().Error(
                    "MaterialX: no data library at %s; material "
                    "documents cannot be resolved\n", dir.asString().c_str());
                return mx::DocumentPtr();
            }
        }
        catch (const std::exception &e) {
            Base::Console().Error("MaterialX: data library at %s failed to "
                                  "load: %s\n", dir.asString().c_str(),
                                  e.what());
            return mx::DocumentPtr();
        }
        return doc;
    }();
    return library;
}

mx::DocumentPtr loadDocument(const std::string &xml,
                             const std::string &sourcePath,
                             std::string &error)
{
    error.clear();
    mx::ConstDocumentPtr library = dataLibrary();
    if (!library) {
        error = "the MaterialX data library is missing from this install";
        return {};
    }
    mx::DocumentPtr doc = mx::createDocument();
    try {
        mx::readFromXmlString(doc, xml);
    }
    catch (const std::exception &e) {
        // Every parse failure arrives as an exception, and the message
        // is the only account of where in the text it went wrong.
        error = e.what();
        return {};
    }
    // importLibrary brings in the node DEFINITIONS the document's node
    // instances refer to; without it every reference is dangling and
    // validate() reports the whole document as unresolved rather than
    // whatever is actually wrong with it.
    try {
        doc->importLibrary(library);
    }
    catch (const std::exception &e) {
        error = std::string("data library import failed: ") + e.what();
        return {};
    }
    // The source URI is what MaterialX resolves a relative file
    // reference against, and readFromXmlString cannot know it.
    if (!sourcePath.empty())
        doc->setSourceUri(sourcePath);
    // Every filename in the document becomes one absolute path here:
    // flattenFilenames applies the fileprefix attributes -- which are
    // INHERITED, so a document states its images relative to a prefix
    // set on an ancestor and no single element's value is the answer
    // -- and then resolves what is left against the search path. Both
    // consumers and the missing-image check then read a plain path.
    try {
        mx::flattenFilenames(doc, searchPath(doc));
    }
    catch (const std::exception &e) {
        error = std::string("filename resolution failed: ") + e.what();
        return {};
    }
    std::string message;
    if (!doc->validate(&message)) {
        error = message;
        return {};
    }
    return doc;
}

mx::FileSearchPath searchPath(const mx::DocumentPtr &doc)
{
    mx::FileSearchPath path;
    if (doc && doc->hasSourceUri())
        path.append(mx::FilePath(doc->getSourceUri()).getParentPath());
    mx::FilePath libs(dataLibraryPath());
    path.append(libs);
    path.append(libs.getParentPath());
    return path;
}

std::string resolveFile(const mx::DocumentPtr &doc, const std::string &name)
{
    if (name.empty())
        return {};
    mx::FilePath file(name);
    if (file.isAbsolute())
        return file.exists() ? file.asString() : std::string();
    mx::FilePath found = searchPath(doc).find(file);
    // find() hands back what it was given when nothing matched, so
    // existence is the only answer to whether it resolved.
    return found.exists() ? found.asString() : std::string();
}

std::vector<mx::NodePtr> surfaceShaders(const mx::DocumentPtr &doc)
{
    std::vector<mx::NodePtr> shaders;
    if (!doc)
        return shaders;
    // A document states its surface either through a material node
    // (surfacematerial, whose surfaceshader input names the shader) or
    // as a bare shader node. Both shapes appear in the library's own
    // example materials, so both are read here.
    for (const auto &material : doc->getMaterialNodes()) {
        for (const auto &shader : mx::getShaderNodes(material))
            shaders.push_back(shader);
    }
    if (shaders.empty())
        shaders = doc->getNodesOfType(mx::SURFACE_SHADER_TYPE_STRING);
    return shaders;
}

std::vector<std::string> surfaceNames(const mx::DocumentPtr &doc)
{
    std::vector<std::string> names;
    if (!doc)
        return names;
    // In step with surfaceShaders(): one name per shader it lists, and
    // the two walk the document the same way. A material node with more
    // than one surface shader repeats its name, so the two lists stay
    // index for index.
    for (const auto &material : doc->getMaterialNodes()) {
        const std::size_t count = mx::getShaderNodes(material).size();
        for (std::size_t i = 0; i < count; ++i)
            names.push_back(material->getNamePath());
    }
    if (names.empty()) {
        for (const auto &shader : doc->getNodesOfType(mx::SURFACE_SHADER_TYPE_STRING))
            names.push_back(shader->getNamePath());
    }
    return names;
}

int surfaceIndex(const mx::DocumentPtr &doc, const std::string &name)
{
    std::vector<mx::NodePtr> shaders = surfaceShaders(doc);
    if (shaders.empty())
        return -1;
    if (name.empty())
        return 0;
    const std::vector<std::string> names = surfaceNames(doc);
    // The material node's namepath first: that is what surfaceNames()
    // reports and what a <look> assigns by.
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name)
            return int(i);
    }
    // Then the bare name of the same, and the shader node's own two
    // spellings. One string is written down and it should resolve
    // whichever of them it was copied from.
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::size_t slash = names[i].rfind('/');
        if (slash != std::string::npos && names[i].substr(slash + 1) == name)
            return int(i);
    }
    for (std::size_t i = 0; i < shaders.size(); ++i) {
        if (shaders[i]->getNamePath() == name || shaders[i]->getName() == name)
            return int(i);
    }
    return -1;
}

mx::NodePtr openPbrSurface(const mx::DocumentPtr &doc,
                           const std::string &surfaceName, std::string &error,
                           std::vector<std::string> &warnings)
{
    error.clear();
    if (!doc) {
        error = "no document";
        return mx::NodePtr();
    }
    std::vector<mx::NodePtr> shaders = surfaceShaders(doc);
    if (shaders.empty()) {
        error = "the document describes no surface";
        return mx::NodePtr();
    }
    // Which one is worn (sec 17.13). Kept as an INDEX, not as the node:
    // the OpenPBR translation below replaces every shader node, and the
    // position in document order is what survives it.
    const int index = surfaceIndex(doc, surfaceName);
    if (index < 0) {
        std::string known;
        for (const auto &name : surfaceNames(doc))
            known += (known.empty() ? "" : ", ") + name;
        error = "the document states no surface '" + surfaceName + "'; it has "
            + known;
        return mx::NodePtr();
    }
    mx::NodePtr surface = shaders[std::size_t(index)];
    if (surface->getCategory() == "open_pbr_surface")
        return surface;

    // Every other shading model arrives through MaterialX's own
    // translation graphs. The library carries them one way only --
    // standard_surface translates TO the others, and UsdPreviewSurface
    // and the hair models translate to nothing -- so a document stating
    // one of those is reported rather than rendered wrong.
    //
    // The CHOSEN shader alone, not translateAllMaterials: that walks
    // every material in the document and throws on the first one that
    // is already OpenPBR ("category is already open_pbr_surface"), so
    // a document carrying one OpenPBR surface beside a standard_surface
    // rendered NEITHER of them, in both consumers. One document, many
    // surfaces is the chess set's shape (sec 17.13), and nothing says
    // they all state the same model.
    const std::string original = surface->getCategory();

    // What the translator does with the shader's inputs, and the two
    // things it gets wrong for a document that leaves inputs unstated
    // -- which is every authored one:
    //
    // It forwards the inputs the shader STATES onto its translation
    // node and reads the rest off the TRANSLATION nodedef's defaults,
    // which are not the source model's. standard_surface says base
    // 1.0 and base_color 0.8 grey; standard_surface_to_open_pbr_surface
    // says base 0.8 and base_color white. The chess set states no
    // base, so every piece rendered at 0.8 of its albedo in both
    // consumers (fcad-probes/mtlxbase_run.sh: byte 115 where the
    // closed form says 128), which is the 20 per cent the path tracer
    // sat under Blender by (sec 17.18). An unstated input means the
    // source's own default, so that is stated here before translating.
    //
    // And it REMOVES every input of the source, forwarded or not, so an
    // input the translation nodedef has no socket for is lost -- and
    // it has none for normal, tangent or coat_normal. That is every
    // normal map of every standard_surface document. OpenPBR has the
    // same three (geometry_normal, geometry_tangent,
    // geometry_coat_normal), so they are carried across by hand.
    mx::NodeDefPtr sourceDef = surface->getNodeDef();
    std::vector<mx::NodeDefPtr> translationDefs =
        doc->getMatchingNodeDefs(original + "_to_open_pbr_surface");
    mx::NodeDefPtr translationDef =
        translationDefs.empty() ? mx::NodeDefPtr() : translationDefs.front();
    if (sourceDef && translationDef) {
        for (const mx::InputPtr &in : sourceDef->getActiveInputs()) {
            const std::string &name = in->getName();
            if (surface->getInput(name) || !in->hasValueString()
                || !translationDef->getActiveInput(name))
                continue;
            surface->addInput(name, in->getType())
                ->setValueString(in->getValueString());
        }
    }
    static const struct { const char *from, *to; } kGeometry[] = {
        {"normal", "geometry_normal"},
        {"tangent", "geometry_tangent"},
        {"coat_normal", "geometry_coat_normal"},
    };
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> carried;
    for (const auto &g : kGeometry) {
        mx::InputPtr in = surface->getInput(g.from);
        if (!in || (translationDef && translationDef->getActiveInput(g.from)))
            continue;
        std::map<std::string, std::string> attrs;
        for (const std::string &attr : in->getAttributeNames())
            attrs[attr] = in->getAttribute(attr);
        carried.emplace_back(g.to, std::move(attrs));
    }

    try {
        mx::ShaderTranslatorPtr translator = mx::ShaderTranslator::create();
        translator->translateShader(surface, "open_pbr_surface");
    }
    catch (const std::exception &e) {
        error = "surface model '" + original + "' does not translate to OpenPBR: "
            + e.what();
        return mx::NodePtr();
    }
    shaders = surfaceShaders(doc);
    if (std::size_t(index) >= shaders.size()
        || shaders[std::size_t(index)]->getCategory() != "open_pbr_surface") {
        error = "surface model '" + original + "' did not translate to OpenPBR";
        return mx::NodePtr();
    }
    surface = shaders[std::size_t(index)];
    for (const auto &c : carried) {
        if (surface->getInput(c.first))
            continue;
        auto type = c.second.find(mx::TypedElement::TYPE_ATTRIBUTE);
        mx::InputPtr in = surface->addInput(
            c.first, type == c.second.end() ? std::string("vector3") : type->second);
        for (const auto &[attr, value] : c.second)
            in->setAttribute(attr, value);
    }
    warnings.push_back("surface model '" + original
                       + "' translated to OpenPBR by MaterialX; a translation is "
                         "an approximation, not an identity");
    return surface;
}

namespace
{

/// The characters a FreeCAD property name and a GLSL uniform name can
/// both carry. A MaterialX input name is already almost always one of
/// those, so this changes nothing in practice and refuses nothing.
std::string identifier(const std::string &raw)
{
    std::string out;
    for (char c : raw) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
        out.insert(out.begin(), '_');
    return out;
}

/// A MaterialX value as floats, one per component. False for the types
/// a parameter cannot carry -- a string, a matrix, and a filename,
/// which is an image and has no binding on either consumer yet.
bool valueFloats(const mx::ValuePtr &value, std::vector<float> &out)
{
    if (!value)
        return false;
    if (value->isA<float>())
        out = { value->asA<float>() };
    else if (value->isA<int>())
        out = { float(value->asA<int>()) };
    else if (value->isA<bool>())
        out = { value->asA<bool>() ? 1.0f : 0.0f };
    else if (value->isA<mx::Vector2>()) {
        auto v = value->asA<mx::Vector2>();
        out = { v[0], v[1] };
    }
    else if (value->isA<mx::Vector3>()) {
        auto v = value->asA<mx::Vector3>();
        out = { v[0], v[1], v[2] };
    }
    else if (value->isA<mx::Color3>()) {
        auto v = value->asA<mx::Color3>();
        out = { v[0], v[1], v[2] };
    }
    else if (value->isA<mx::Vector4>()) {
        auto v = value->asA<mx::Vector4>();
        out = { v[0], v[1], v[2], v[3] };
    }
    else if (value->isA<mx::Color4>()) {
        auto v = value->asA<mx::Color4>();
        out = { v[0], v[1], v[2], v[3] };
    }
    else
        return false;
    return true;
}

/// Write floats back onto an input as the value of its own type.
void setInputValue(const mx::InputPtr &input, const std::string &type,
                   const std::vector<float> &v)
{
    auto at = [&v](size_t i) { return i < v.size() ? v[i] : 0.0f; };
    if (type == "float")
        input->setValue(at(0));
    else if (type == "integer")
        input->setValue(int(at(0)));
    else if (type == "boolean")
        input->setValue(at(0) != 0.0f);
    else if (type == "vector2")
        input->setValue(mx::Vector2(at(0), at(1)));
    else if (type == "vector3")
        input->setValue(mx::Vector3(at(0), at(1), at(2)));
    else if (type == "color3")
        input->setValue(mx::Color3(at(0), at(1), at(2)));
    else if (type == "vector4")
        input->setValue(mx::Vector4(at(0), at(1), at(2), at(3)));
    else if (type == "color4")
        input->setValue(mx::Color4(at(0), at(1), at(2), at(3)));
}

}  // namespace

std::vector<MaterialInput> publicInputs(const mx::DocumentPtr &doc,
                                        const mx::NodePtr &surface)
{
    std::vector<MaterialInput> res;
    if (!doc || !surface)
        return res;
    // The graphs the rendered surface reaches, first sighting first.
    // Reachability is not a nicety: importLibrary puts every one of the
    // standard library's own node graphs in the document, so
    // getNodeGraphs() answers with hundreds of graphs that are nobody's
    // interface.
    std::vector<mx::NodeGraphPtr> graphs;
    std::set<std::string> seen;
    try {
        for (mx::Edge edge : surface->traverseGraph()) {
            mx::ElementPtr up = edge.getUpstreamElement();
            mx::ElementPtr parent = up ? up->getParent() : nullptr;
            mx::NodeGraphPtr graph = parent ? parent->asA<mx::NodeGraph>()
                                            : mx::NodeGraphPtr();
            if (graph && seen.insert(graph->getNamePath()).second)
                graphs.push_back(graph);
        }
    }
    catch (const std::exception &) {
        // A cycle stops the traversal; what it found before that still
        // stands, and the document is reported elsewhere.
    }
    std::set<std::string> names;
    for (const auto &graph : graphs) {
        // Which of the declared inputs the graph's own nodes name. One
        // it does not name is declared and drives nothing, and a
        // property for it would be a knob wired to nothing.
        std::set<std::string> used;
        for (mx::ElementPtr elem : graph->traverseTree()) {
            mx::InputPtr in = elem->asA<mx::Input>();
            if (in && in->hasInterfaceName())
                used.insert(in->getInterfaceName());
        }
        for (const mx::InputPtr &in : graph->getInputs()) {
            if (!used.count(in->getName()))
                continue;
            MaterialInput input;
            if (!valueFloats(in->getValue(), input.value))
                continue;
            input.type = in->getType();
            input.path = in->getNamePath();
            input.label = in->getAttribute("uiname");
            input.folder = in->getAttribute("uifolder");
            input.help = in->getAttribute("doc");
            // Two graphs may each declare a "scale". The plain name is
            // what a document means, so it stays the name of the first
            // one and the second is qualified by its graph.
            std::string name = identifier(in->getName());
            if (!names.insert(name).second) {
                name = identifier(graph->getName() + "_" + in->getName());
                if (!names.insert(name).second)
                    continue;
            }
            input.name = std::move(name);
            res.push_back(std::move(input));
        }
    }
    return res;
}

void applyInputs(const mx::DocumentPtr &doc,
                 const std::vector<RenderDebugConfig::UserParam> &params,
                 const std::string &surfaceName)
{
    if (params.empty() || !doc)
        return;
    std::vector<mx::NodePtr> shaders = surfaceShaders(doc);
    const int index = surfaceIndex(doc, surfaceName);
    if (index < 0)
        return;
    std::vector<MaterialInput> inputs =
        publicInputs(doc, shaders[std::size_t(index)]);
    for (const auto &input : inputs) {
        const std::string uniform = "u_" + input.name;
        for (const auto &param : params) {
            if (param.name != uniform)
                continue;
            mx::ElementPtr elem = doc->getDescendant(input.path);
            if (mx::InputPtr target = elem ? elem->asA<mx::Input>() : mx::InputPtr())
                setInputValue(target, input.type, param.values);
            break;
        }
    }
}

namespace
{

/// One input of the surface, read FLAT: what a consumer that samples
/// nothing per fragment can make of it. The walk follows the connection
/// kinds the interpreter evaluates -- an interface socket, a nodegraph
/// output, a node -- through the `dot` nodes and graph outputs the
/// OpenPBR translation leaves between the surface and what the document
/// wrote, and stops at a constant, at one image feeding the input
/// directly, or at anything else (a pattern graph), which it cannot read.
struct FlatInput {
    enum Kind { Unstated, Constant, Image, Other } kind = Unstated;
    std::vector<float> value;
    /// For Image: the file, resolved as inspect() resolves every image.
    std::string imagePath;
};

FlatInput flattenInput(const mx::DocumentPtr &doc, mx::InputPtr input)
{
    FlatInput out;
    // The implementation graphs entered on the way, innermost last: a
    // nodedef implemented as a nodegraph -- every shading-model
    // translation, most of the pattern library -- is walked by
    // descending into that graph with the calling node bound as its
    // interface, exactly as the path tracer's interpreter evaluates it
    // (CyclesMaterialX.cpp evalImplementation). An interface name met
    // inside resolves against the caller's input of that name.
    std::vector<mx::NodePtr> callers;
    for (int hops = 0; input && hops < 64; ++hops) {
        if (input->hasInterfaceName()) {
            const std::string name = input->getInterfaceName();
            if (!callers.empty()) {
                mx::NodePtr caller = callers.back();
                callers.pop_back();
                if (mx::InputPtr bound = caller->getInput(name)) {
                    input = bound;
                    continue;
                }
                // Unstated on the caller: its own nodedef's default.
                mx::NodeDefPtr def = caller->getNodeDef();
                mx::InputPtr defInput = def ? def->getActiveInput(name) : mx::InputPtr();
                if (defInput && valueFloats(defInput->getValue(), out.value))
                    out.kind = FlatInput::Constant;
                else
                    out.kind = FlatInput::Other;
                return out;
            }
            input = input->getInterfaceInput();
            continue;
        }
        mx::NodePtr node;
        std::string outputName;
        if (input->hasNodeGraphString()) {
            mx::OutputPtr output = input->getConnectedOutput();
            if (!output)
                break;
            node = output->getConnectedNode();
            outputName = output->getOutputString();
        }
        else if (input->hasNodeName()) {
            node = input->getConnectedNode();
            outputName = input->getOutputString();
        }
        else if (input->hasOutputString()) {
            break;
        }
        else {
            if (!input->hasValueString())
                return out;
            out.kind = valueFloats(input->getValue(), out.value)
                ? FlatInput::Constant : FlatInput::Other;
            return out;
        }
        // From the node reached to the next INPUT to follow: through a
        // pass-through node directly, through an implemented one by
        // entering its graph at the output that was named.
        input = mx::InputPtr();
        for (int inner = 0; node && inner < 64 && !input; ++inner) {
            const std::string &category = node->getCategory();
            if (category == "dot") {
                input = node->getInput("in");
                break;
            }
            if (category == "constant") {
                input = node->getInput("value");
                break;
            }
            if (category == "image" || category == "tiledimage") {
                mx::InputPtr file = node->getInput("file");
                if (file && file->hasValueString())
                    out.imagePath = resolveFile(doc, file->getValueString());
                out.kind = FlatInput::Image;
                return out;
            }
            mx::NodeDefPtr def = node->getNodeDef();
            mx::InterfaceElementPtr impl = def ? def->getImplementation()
                                              : mx::InterfaceElementPtr();
            mx::NodeGraphPtr graph = impl ? impl->asA<mx::NodeGraph>() : mx::NodeGraphPtr();
            if (!graph || callers.size() > 32)
                break;
            std::vector<mx::OutputPtr> outputs = graph->getOutputs();
            if (outputs.empty())
                break;
            mx::OutputPtr target = outputName.empty() ? outputs.front()
                                                      : graph->getOutput(outputName);
            if (!target)
                target = outputs.front();
            callers.push_back(node);
            if (target->hasInterfaceName()) {
                // The graph hands an interface socket straight through:
                // the caller's input of that name, without entering.
                mx::NodePtr caller = callers.back();
                callers.pop_back();
                input = caller->getInput(target->getInterfaceName());
                break;
            }
            outputName = target->getOutputString();
            node = target->getConnectedNode();
        }
        if (!input)
            break;
    }
    out.kind = FlatInput::Other;
    return out;
}

/// One input of the surface as floats: the constant it states, else the
/// nodedef's default, else `fallback`. Says whether it was a constant.
FlatInput surfaceInput(const mx::DocumentPtr &doc, const mx::NodePtr &surface,
                       const char *name, std::initializer_list<float> fallback)
{
    FlatInput flat = flattenInput(doc, surface->getInput(name));
    if (flat.kind == FlatInput::Constant)
        return flat;
    std::vector<float> value;
    mx::NodeDefPtr def = surface->getNodeDef();
    mx::InputPtr defInput = def ? def->getActiveInput(name) : mx::InputPtr();
    if (!defInput || !valueFloats(defInput->getValue(), value))
        value.assign(fallback);
    flat.value = std::move(value);
    return flat;
}

/// Fill DocumentInfo::transmission from the (translated) OpenPBR surface.
void readTransmission(const mx::DocumentPtr &doc, const mx::NodePtr &surface,
                      DocumentInfo &info)
{
    DocumentInfo::Transmission &t = info.transmission;
    FlatInput weight = surfaceInput(doc, surface, "transmission_weight", {0.0f});
    t.weight = weight.value.empty() ? 0.0f : weight.value[0];
    if (weight.kind != FlatInput::Constant && weight.kind != FlatInput::Unstated) {
        info.warnings.push_back(
            "transmission_weight is not a constant: the raster path cannot read "
            "a mapped weight flat, so the surface draws opaque there");
        return;
    }
    FlatInput color = surfaceInput(doc, surface, "transmission_color", {1.0f, 1.0f, 1.0f});
    // The flat reading feeds the FLAT consumers: the glass pass while
    // its per-fragment splice compiles, the viewer tier, the shadow
    // tint. The desktop glass stage reads the document's own graph per
    // fragment (docs/MaterialStorage.md sec 17.22), map and all.
    if (color.kind != FlatInput::Constant && color.kind != FlatInput::Unstated)
        info.warnings.push_back(
            "transmission_color is not a constant: the flat glass body (a pending "
            "compile, the viewer tier, the shadow tint) takes white");
    for (int c = 0; c < 3; ++c)
        t.color[c] = c < int(color.value.size()) ? color.value[c] : 1.0f;
    FlatInput depth = surfaceInput(doc, surface, "transmission_depth", {0.0f});
    t.depth = depth.value.empty() ? 0.0f : std::max(depth.value[0], 0.0f);
    FlatInput ior = surfaceInput(doc, surface, "specular_ior", {1.5f});
    t.ior = ior.value.empty() ? 1.5f : ior.value[0];
    FlatInput rough = surfaceInput(doc, surface, "specular_roughness", {0.3f});
    t.roughness = rough.value.empty() ? 0.3f : rough.value[0];
    if (rough.kind == FlatInput::Image)
        t.roughnessImage = rough.imagePath;
    t.glass = t.weight >= 0.5f;
}

}  // namespace

bool flatConstant(const mx::NodePtr &node, const char *name, std::vector<float> &out)
{
    if (!node)
        return false;
    FlatInput flat = flattenInput(node->getDocument(), node->getInput(name));
    if (flat.kind != FlatInput::Constant)
        return false;
    out = std::move(flat.value);
    return true;
}

DocumentInfo inspect(const std::string &xml, const std::string &sourcePath,
                     const std::string &surfaceName)
{
    DocumentInfo info;
    std::string error;
    mx::DocumentPtr doc = loadDocument(xml, sourcePath, error);
    if (!doc) {
        info.error = error;
        return info;
    }
    std::vector<mx::NodePtr> shaders = surfaceShaders(doc);
    if (shaders.empty()) {
        info.error = "the document describes no surface: it has neither a "
                     "material node nor a surface shader node";
        return info;
    }
    info.materials = surfaceNames(doc);
    const int index = surfaceIndex(doc, surfaceName);
    if (index < 0) {
        std::string known;
        for (const auto &name : info.materials)
            known += (known.empty() ? "" : ", ") + name;
        info.error = "the document states no surface '" + surfaceName
            + "'; it has " + known;
        return info;
    }
    info.material = info.materials[std::size_t(index)];
    info.surface = shaders[std::size_t(index)]->getCategory();
    // An image the document names but that is not there renders as the
    // node's default, which is a silently wrong material -- worth
    // saying out loud, but not worth refusing the document over.
    // The whole tree, not the top-level nodes: an image node almost
    // always sits inside a nodegraph.
    std::set<std::string> seen;
    std::set<std::string> seenResolved;
    for (mx::ElementPtr elem : doc->traverseTree()) {
        auto input = elem->asA<mx::Input>();
        if (!input || input->getType() != mx::FILENAME_TYPE_STRING)
            continue;
        const std::string value = input->getValueString();
        if (value.empty())
            continue;
        const std::string resolved = resolveFile(doc, value);
        if (!resolved.empty()) {
            // What a consumer that cannot open files itself has to be
            // handed. Deduplicated on the RESOLVED path: two nodes
            // naming one file through different relative spellings are
            // one image to load.
            if (seenResolved.insert(resolved).second)
                info.images.push_back(resolved);
            continue;
        }
        if (seen.insert(value).second)
            info.missingImages.push_back(value);
    }
    for (const auto &missing : info.missingImages)
        info.warnings.push_back("image not found: " + missing);
    // The interface is read from the document AS AUTHORED, not from
    // the OpenPBR translation of it: a graph interface survives the
    // translation untouched, so one enumeration answers for the
    // property editor, the path tracer and the generator alike.
    info.inputs = publicInputs(doc, shaders[std::size_t(index)]);
    info.valid = true;
    // The transmission is read off the OpenPBR surface AFTER the
    // translation, so every model answers in one vocabulary. Last,
    // because the translation rewrites the document. Its own
    // warnings are the generator's to report, not this inspection's.
    {
        std::string error;
        std::vector<std::string> warnings;
        if (mx::NodePtr surface = openPbrSurface(doc, surfaceName, error, warnings))
            readTransmission(doc, surface, info);
    }
    return info;
}

namespace {

/// The name a document means by one filename-valued input: the value
/// with the inherited fileprefix chain applied and nothing else. A
/// function of the text alone, which is what makes it the same on every
/// machine -- unlike a search-path resolution, which is an answer about
/// one disk.
std::string statedName(const mx::ElementPtr &elem, const mx::ValueElementPtr &value)
{
    try {
        // createStringResolver() carries the fileprefix inherited down
        // to this element; resolving with it applies that and leaves
        // the rest of the string alone.
        return value->getResolvedValueString(elem->createStringResolver());
    }
    catch (const std::exception &) {
        return value->getValueString();
    }
}

/// Parse without importing the data library and without validating.
/// Both questions below are about what the text SAYS, and the library
/// is 2.5MB of definitions that say nothing about that.
mx::DocumentPtr readOnly(const std::string &xml, const std::string &sourcePath)
{
    mx::DocumentPtr doc = mx::createDocument();
    try {
        mx::readFromXmlString(doc, xml);
    }
    catch (const std::exception &) {
        return {};
    }
    if (!sourcePath.empty())
        doc->setSourceUri(sourcePath);
    return doc;
}

}  // namespace

std::vector<ImageReference> imageReferences(const std::string &xml,
                                            const std::string &sourcePath)
{
    std::vector<ImageReference> refs;
    mx::DocumentPtr doc = readOnly(xml, sourcePath);
    if (!doc)
        return refs;
    std::set<std::string> seen;
    for (mx::ElementPtr elem : doc->traverseTree()) {
        auto input = elem->asA<mx::ValueElement>();
        if (!input || input->getType() != mx::FILENAME_TYPE_STRING)
            continue;
        if (input->getValueString().empty())
            continue;
        ImageReference ref;
        ref.name = statedName(elem, input);
        if (ref.name.empty() || !seen.insert(ref.name).second)
            continue;
        // Deduplicated by NAME. Two spellings of one file are two
        // references here and one image later: the deduplication that
        // matters for loading is on the resolved path, and that belongs
        // to inspect(), which is the question about this machine.
        ref.path = resolveFile(doc, ref.name);
        refs.push_back(std::move(ref));
    }
    return refs;
}

std::vector<Look> looks(const std::string &xml, const std::string &sourcePath)
{
    std::vector<Look> found;
    mx::DocumentPtr doc = readOnly(xml, sourcePath);
    if (!doc)
        return found;
    for (const mx::LookPtr &look : doc->getLooks()) {
        if (!look)
            continue;
        Look out;
        out.name = look->getName();
        for (const mx::MaterialAssignPtr &assign : look->getMaterialAssigns()) {
            if (!assign || assign->getMaterial().empty())
                continue;
            LookAssignment entry;
            entry.material = assign->getMaterial();
            entry.geom = assign->getActiveGeom();
            if (entry.geom.empty() && assign->hasCollectionString()) {
                // A collection is a named piece of geometry the document
                // states once and several assignments point at. Its
                // include geometry is the same kind of string the geom
                // attribute holds, so the consumer sees one shape either
                // way and knows which it was by the name kept here.
                entry.collection = assign->getCollectionString();
                if (mx::CollectionPtr collection = assign->getCollection())
                    entry.geom = collection->getActiveIncludeGeom();
            }
            out.assignments.push_back(std::move(entry));
        }
        found.push_back(std::move(out));
    }
    return found;
}

std::vector<MaterialInput> publicInputs(const std::string &xml, const std::string &surface)
{
    mx::DocumentPtr doc = mx::createDocument();
    try {
        mx::readFromXmlString(doc, xml);
    }
    catch (const std::exception &) {
        return {};
    }
    if (mx::ConstDocumentPtr library = dataLibrary())
        doc->setDataLibrary(library);
    std::vector<mx::NodePtr> shaders = surfaceShaders(doc);
    const int index = surfaceIndex(doc, surface);
    if (index < 0 || std::size_t(index) >= shaders.size())
        return {};
    return publicInputs(doc, shaders[std::size_t(index)]);
}

std::string applyInputsToDocument(const std::string &xml,
                                  const std::vector<RenderDebugConfig::UserParam> &params,
                                  const std::string &surface)
{
    if (params.empty())
        return xml;
    // Parsed with the library ATTACHED, not imported: an imported
    // library is written back out with the document.
    mx::DocumentPtr doc = mx::createDocument();
    try {
        mx::readFromXmlString(doc, xml);
    }
    catch (const std::exception &) {
        return xml;
    }
    if (mx::ConstDocumentPtr library = dataLibrary())
        doc->setDataLibrary(library);
    applyInputs(doc, params, surface);
    return mx::writeToXmlString(doc);
}

std::string substituteImages(const std::string &xml,
                             const std::vector<ImageReference> &files)
{
    if (files.empty())
        return xml;
    mx::DocumentPtr doc = readOnly(xml, {});
    if (!doc)
        return xml;

    std::map<std::string, std::string> byName;
    for (const auto &file : files) {
        if (!file.name.empty() && !file.path.empty())
            byName[file.name] = file.path;
    }
    bool changed = false;
    for (mx::ElementPtr elem : doc->traverseTree()) {
        auto input = elem->asA<mx::ValueElement>();
        if (!input || input->getType() != mx::FILENAME_TYPE_STRING)
            continue;
        if (input->getValueString().empty())
            continue;
        const std::string stated = statedName(elem, input);
        auto it = byName.find(stated);
        if (it == byName.end()) {
            // Not carried -- its content has not arrived, or this machine
            // never had the file. The prefixes are about to go for the
            // sake of the ones that ARE carried, so this one keeps what its
            // prefix meant by taking the stated name outright; otherwise a
            // reference that resolved before this call stops resolving
            // after it.
            if (stated != input->getValueString()) {
                input->setValueString(stated);
                changed = true;
            }
            continue;
        }
        input->setValueString(it->second);
        changed = true;
    }
    if (!changed)
        return xml;
    // The prefixes go with the names they qualified: what is left is
    // absolute, or the stated name with its prefix already applied, and
    // whoever resolves the result -- flattenFilenames, downstream --
    // would otherwise prepend the prefix to it again.
    for (mx::ElementPtr elem : doc->traverseTree()) {
        if (elem->hasAttribute(mx::Element::FILE_PREFIX_ATTRIBUTE))
            elem->removeAttribute(mx::Element::FILE_PREFIX_ATTRIBUTE);
    }
    try {
        return mx::writeToXmlString(doc);
    }
    catch (const std::exception &) {
        return xml;
    }
}

#endif  // HAVE_MATERIALX

}  // namespace Render::MaterialX

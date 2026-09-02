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

#include <cctype>
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

DocumentInfo inspect(const std::string &, const std::string &)
{
    DocumentInfo info;
    info.error = "MaterialX support is not built (BUILD_MATERIALX)";
    return info;
}

std::vector<ImageReference> imageReferences(const std::string &, const std::string &)
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

mx::NodePtr openPbrSurface(const mx::DocumentPtr &doc, std::string &error,
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
    mx::NodePtr surface = shaders.front();
    if (surface->getCategory() == "open_pbr_surface")
        return surface;

    // Every other shading model arrives through MaterialX's own
    // translation graphs. The library carries them one way only --
    // standard_surface translates TO the others, and UsdPreviewSurface
    // and the hair models translate to nothing -- so a document stating
    // one of those is reported rather than rendered wrong.
    const std::string original = surface->getCategory();
    try {
        mx::ShaderTranslatorPtr translator = mx::ShaderTranslator::create();
        translator->translateAllMaterials(doc, "open_pbr_surface");
    }
    catch (const std::exception &e) {
        error = "surface model '" + original + "' does not translate to OpenPBR: "
            + e.what();
        return mx::NodePtr();
    }
    shaders = surfaceShaders(doc);
    if (shaders.empty() || shaders.front()->getCategory() != "open_pbr_surface") {
        error = "surface model '" + original + "' did not translate to OpenPBR";
        return mx::NodePtr();
    }
    warnings.push_back("surface model '" + original
                       + "' translated to OpenPBR by MaterialX; a translation is "
                         "an approximation, not an identity");
    return shaders.front();
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
                 const std::vector<RenderDebugConfig::UserParam> &params)
{
    if (params.empty() || !doc)
        return;
    std::vector<mx::NodePtr> shaders = surfaceShaders(doc);
    if (shaders.empty())
        return;
    std::vector<MaterialInput> inputs = publicInputs(doc, shaders.front());
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

DocumentInfo inspect(const std::string &xml, const std::string &sourcePath)
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
    for (const auto &shader : shaders)
        info.materials.push_back(shader->getNamePath());
    info.surface = shaders.front()->getCategory();
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
    info.inputs = publicInputs(doc, shaders.front());
    info.valid = true;
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

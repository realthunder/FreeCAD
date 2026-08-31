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

#include <mutex>
#include <set>

#include <MaterialXCore/Material.h>
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
    for (mx::ElementPtr elem : doc->traverseTree()) {
        auto input = elem->asA<mx::Input>();
        if (!input || input->getType() != mx::FILENAME_TYPE_STRING)
            continue;
        const std::string value = input->getValueString();
        if (value.empty() || !resolveFile(doc, value).empty())
            continue;
        if (seen.insert(value).second)
            info.missingImages.push_back(value);
    }
    for (const auto &missing : info.missingImages)
        info.warnings.push_back("image not found: " + missing);
    info.valid = true;
    return info;
}

#endif  // HAVE_MATERIALX

}  // namespace Render::MaterialX

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

#include <MaterialXCore/Material.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>

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

DocumentInfo inspect(const std::string &)
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

mx::DocumentPtr loadDocument(const std::string &xml, std::string &error)
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
    std::string message;
    if (!doc->validate(&message)) {
        error = message;
        return {};
    }
    return doc;
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

DocumentInfo inspect(const std::string &xml)
{
    DocumentInfo info;
    std::string error;
    mx::DocumentPtr doc = loadDocument(xml, error);
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
    info.valid = true;
    return info;
}

#endif  // HAVE_MATERIALX

}  // namespace Render::MaterialX

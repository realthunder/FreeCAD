// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
#include <algorithm>
#include <sstream>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/FileBlobManager.h>
#include <App/MaterialXDocument.h>
#include <App/PropertyStandard.h>
#include <App/ShaderObject.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>

#include <QList>
#include <QString>
#include <QVariant>

#include "Materials.h"
#include "ModelUuids.h"
#include "PropertyMaterial.h"
#include "ShaderGraph.h"

using namespace Materials;

namespace
{
// A dynamic property on the program naming the graph (its manifest hash)
// it was materialized from: how the program is told apart from any other
// MATERIALX program bound to the object, and what an edit is measured
// against
const char* const Marker = "ShaderGraphOf";

std::string readText(const std::string& path)
{
    Base::ifstream in(Base::FileInfo(path), std::ios::in | std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The graph text a manifest names, out of the store; empty while any of
// it is not there
std::string graphText(App::FileBlobManager& manager, const App::MaterialXDocument& manifest)
{
    auto blob = manager.find(manifest.documentHash());
    return blob ? readText(blob->path()) : std::string();
}

bool targets(const App::ShaderBinding* binding, const App::DocumentObject* owner)
{
    const auto elements = binding->getElementListValue();
    return std::find(elements.begin(), elements.end(), owner) != elements.end();
}
}  // namespace

App::ShaderBinding* ShaderGraph::materialized(const App::DocumentObject* owner)
{
    if (!owner) {
        return nullptr;
    }
    for (auto* link : owner->getInList()) {
        auto* binding = dynamic_cast<App::ShaderBinding*>(link);
        if (binding && binding->scopeMode() == App::ShaderBinding::ScopeMode::Object
            && programOf(binding) && targets(binding, owner)) {
            return binding;
        }
    }
    return nullptr;
}

App::ShaderProgram* ShaderGraph::programOf(const App::ShaderBinding* binding)
{
    auto* shader = binding ? binding->resolveShader() : nullptr;
    if (!shader) {
        return nullptr;
    }
    for (auto* object : shader->Programs.getValues()) {
        auto* program = dynamic_cast<App::ShaderProgram*>(object);
        if (program && program->getPropertyByName(Marker)) {
            return program;
        }
    }
    return nullptr;
}

App::ShaderBinding* ShaderGraph::materialize(App::DocumentObject* owner, PropertyMaterial& card)
{
    auto* doc = owner ? owner->getDocument() : nullptr;
    if (!doc) {
        return nullptr;
    }
    if (auto* existing = materialized(owner)) {
        return existing;
    }
    const App::MaterialXDocument manifest = card.getValue().getMaterialXManifest();
    if (!manifest.isSet()) {
        return nullptr;
    }
    auto& manager = doc->getFileBlobManager();
    const std::string text = graphText(manager, manifest);
    if (text.empty()) {
        Base::Console().warning("%s: the shader graph's bytes are not in the store yet\n",
                                owner->getFullName().c_str());
        return nullptr;
    }
    // Every image, before anything is made: a program naming a file it
    // does not carry is worse than no program
    std::vector<std::pair<std::string, App::FileBlobHandle>> images;
    for (const auto& file : manifest.files) {
        if (file.name == manifest.document) {
            continue;
        }
        auto blob = manager.find(file.hash);
        if (!blob) {
            Base::Console().warning("%s: shader graph image '%s' is not in the store yet\n",
                                    owner->getFullName().c_str(),
                                    file.name.c_str());
            return nullptr;
        }
        images.emplace_back(file.name, std::move(blob));
    }

    const std::string base = owner->getNameInDocument();
    const std::string label = card.getValue().getName().toStdString();
    auto* program = static_cast<App::ShaderProgram*>(
        doc->addObject("App::ShaderProgram", (base + "_ShaderGraph").c_str()));
    program->Label.setValue(label + " shader graph");
    program->Stage.setValue("material");
    program->Dialect.setValue("MATERIALX");
    // Which surface of the graph the card wears; empty is its first,
    // which is what a graph with a single material has (sec 17.13)
    program->Surface.setValue(manifest.surface);
    // Stored first, set second (17.11): the sync on the text change finds
    // every name the graph refers to already carried and leaves it alone
    for (const auto& [name, blob] : images) {
        program->Images.setBlob(name.c_str(), blob);
    }
    program->FragmentProgram.setValue(text.c_str());
    auto* marker = program->addDynamicProperty(
        "App::PropertyString",
        Marker,
        "Shader Graph",
        "The graph this program was materialized from, as the hash of the card's manifest",
        0,
        true);
    static_cast<App::PropertyString*>(marker)->setValue(manifest.manifestHash());

    auto* shader = static_cast<App::Shader*>(
        doc->addObject("App::Shader", (base + "_Shader").c_str()));
    shader->Label.setValue(label + " shader");
    shader->Programs.setValues({program});
    shader->Demo.setValue("None");

    auto* binding = static_cast<App::ShaderBinding*>(
        doc->addObject("App::ShaderBinding", (base + "_Look").c_str()));
    binding->Label.setValue(label + " look");
    binding->Scope.setValue("Object");
    binding->getElementListProperty()->setValues({shader, owner});
    return binding;
}

bool ShaderGraph::edited(const App::DocumentObject* owner)
{
    auto* program = programOf(materialized(owner));
    if (!program) {
        return false;
    }
    auto* marker = dynamic_cast<App::PropertyString*>(program->getPropertyByName(Marker));
    auto& manager = program->getDocument()->getFileBlobManager();
    auto blob = marker ? manager.find(marker->getValue()) : App::FileBlobHandle();
    App::MaterialXDocument manifest;
    if (!blob || !App::MaterialXDocument::readFile(blob->path(), manifest)) {
        // Made from something this store no longer holds: whatever the
        // text is now, nothing can say it is unedited
        return true;
    }
    // Which surface is worn is part of what the card said, so changing
    // it alone is an edit: same bytes, different material (sec 17.13)
    if (manifest.surface != program->Surface.getValue()) {
        return true;
    }
    const std::string original = graphText(manager, manifest);
    return original.empty() || original != program->FragmentProgram.getValue();
}

std::shared_ptr<Material> ShaderGraph::cardFromEdit(const App::DocumentObject* owner,
                                                    const PropertyMaterial& card)
{
    auto* program = programOf(materialized(owner));
    if (!program || !program->getDocument()) {
        return nullptr;
    }
    const std::string text = program->FragmentProgram.getValue();
    if (text.empty()) {
        return nullptr;
    }
    const Material& worn = card.getValue();
    auto edited = std::make_shared<Material>(worn);
    edited->newUuid();
    edited->setParentUUID(worn.getUUID());
    if (!edited->hasAppearanceModel(ModelUUIDs::ModelUUID_Rendering_MaterialX)) {
        edited->addAppearance(ModelUUIDs::ModelUUID_Rendering_MaterialX);
    }
    QString graph = worn.getMaterialXShaderGraph();
    if (graph.isEmpty()) {
        graph = worn.getName() + QStringLiteral(".mtlx");
    }

    // The text as a file the library save can copy from. Not through the
    // store: the program's own text is already a blob there (the property
    // is a PropertyStringIncluded), and find() would hand that one back --
    // a blob the document holds in memory, with no file behind its path
    // until the document is saved. The transient directory is the right
    // place for a file that lives as long as the document does.
    auto& manager = program->getDocument()->getFileBlobManager();
    const std::string path = manager.uniquePath(graph.toStdString());
    {
        Base::ofstream to(Base::FileInfo(path), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!to) {
            Base::Console().error("%s: cannot write the shader graph to '%s'\n",
                                  owner->getFullName().c_str(),
                                  path.c_str());
            return nullptr;
        }
        to.write(text.data(), std::streamsize(text.size()));
    }

    auto names = std::make_shared<QList<QVariant>>();
    auto files = std::make_shared<QList<QVariant>>();
    std::vector<std::string> hashes;
    std::vector<std::string> paths;
    names->append(graph);
    files->append(QString::fromStdString(path));
    hashes.push_back(App::FileBlobManager::hashBytes(text));
    paths.push_back(path);
    for (const auto& image : program->Images.getValues()) {
        const std::string path = program->Images.filePath(image.name.c_str());
        if (path.empty()) {
            Base::Console().warning("%s: shader graph image '%s' has no bytes yet\n",
                                    owner->getFullName().c_str(),
                                    image.name.c_str());
            return nullptr;
        }
        names->append(QString::fromStdString(image.name));
        files->append(QString::fromStdString(path));
        hashes.push_back(image.hash);
        paths.push_back(path);
    }
    edited->setAppearanceValue(QStringLiteral("MaterialXShaderGraph"), graph);
    edited->setAppearanceValue(QStringLiteral("MaterialXSurface"),
                               QString::fromUtf8(program->Surface.getValue()));
    edited->setAppearanceValue(QStringLiteral("MaterialXNames"), names);
    edited->setAppearanceValue(QStringLiteral("MaterialXFiles"), files);
    edited->setMaterialXHashes(hashes);
    edited->setMaterialXPaths(paths);
    return edited;
}

bool ShaderGraph::revert(App::DocumentObject* owner)
{
    auto* binding = materialized(owner);
    if (!binding) {
        return false;
    }
    auto* doc = binding->getDocument();
    auto elements = binding->getElementListValue();
    std::vector<App::DocumentObject*> others;
    for (auto* element : elements) {
        if (element != owner && !element->isDerivedFrom<App::Shader>()) {
            others.push_back(element);
        }
    }
    if (!others.empty()) {
        // Given other targets since: they keep it, this one leaves
        elements.erase(std::remove(elements.begin(), elements.end(), owner), elements.end());
        binding->getElementListProperty()->setValues(elements);
        return true;
    }
    auto* shader = binding->resolveShader();
    std::vector<App::DocumentObject*> programs;
    if (shader) {
        programs = shader->Programs.getValues();
    }
    doc->removeObject(binding->getNameInDocument());
    if (shader) {
        doc->removeObject(shader->getNameInDocument());
    }
    for (auto* program : programs) {
        if (program && program->isDerivedFrom<App::ShaderProgram>()) {
            doc->removeObject(program->getNameInDocument());
        }
    }
    return true;
}

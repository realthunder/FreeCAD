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

#include <QList>
#include <QString>
#include <QVariant>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Mod/Material/App/Materials.h>
#include <Mod/Material/App/ModelUuids.h>
#include <Mod/Material/App/PropertyMaterial.h>

#include "ReaderLook.h"

using namespace Import;

namespace
{

/// Whether \a text answers to \a pattern, in which `*` stands for any run
/// of characters and `?` for one. The wildcards are MaterialX's, and the
/// match is the whole string: a look assigning `Pawn_*` means the pieces
/// whose names begin that way and not the ones that merely contain it.
bool globMatch(const char* pattern, const char* text)
{
    // Iterative backtracking: one star position remembered, which is all a
    // glob needs and costs nothing on the usual pattern with no star at all
    const char* star = nullptr;
    const char* retry = nullptr;
    while (*text) {
        if (*pattern == '?' || *pattern == *text) {
            ++pattern;
            ++text;
        }
        else if (*pattern == '*') {
            star = pattern++;
            retry = text;
        }
        else if (star) {
            pattern = star + 1;
            text = ++retry;
        }
        else {
            return false;
        }
    }
    while (*pattern == '*') {
        ++pattern;
    }
    return *pattern == 0;
}

/// The material properties of \a object -- the cards it can wear. Only
/// Part::Feature has one today, and this asks rather than naming the type
/// so that anything else growing one is dressed too.
std::vector<Materials::PropertyMaterial*> materialProperties(App::DocumentObject* object)
{
    std::vector<Materials::PropertyMaterial*> found;
    std::vector<App::Property*> properties;
    object->getPropertyList(properties);
    for (auto property : properties) {
        if (property->isDerivedFrom<Materials::PropertyMaterial>()) {
            found.push_back(static_cast<Materials::PropertyMaterial*>(property));
        }
    }
    return found;
}

}  // namespace

ReaderLook::ReaderLook(const Base::FileInfo& file)
    : _file {file}
{}

ReaderLook::~ReaderLook() = default;

void ReaderLook::setAssignments(std::vector<LookAssignment> assignments)
{
    _assignments = std::move(assignments);
    _cards.assign(_assignments.size(), nullptr);
}

void ReaderLook::setImages(std::vector<LookFile> images)
{
    _images = std::move(images);
}

void ReaderLook::setLookName(std::string name)
{
    _look = std::move(name);
}

bool ReaderLook::matches(const std::string& geom, const std::string& label)
{
    if (geom.empty() || label.empty()) {
        return false;
    }
    // A geometry string is a list: MaterialX separates its paths with
    // commas, and files in the wild use spaces as well
    std::size_t pos = 0;
    while (pos < geom.size()) {
        const std::size_t end = geom.find_first_of(", \t\n\r", pos);
        std::string path = geom.substr(pos, end == std::string::npos ? end : end - pos);
        pos = end == std::string::npos ? geom.size() : end + 1;
        if (path.empty()) {
            continue;
        }
        if (globMatch(path.c_str(), label.c_str())) {
            return true;
        }
        // A geometry path is a path: `/root/Bishop_B` is the mesh an
        // exporter wrote out of a scene tree, and the object made from it
        // is called by its last element. Try that too, but only that -- a
        // pattern is answered by a name, never by a name it contains.
        const std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < path.size()
            && globMatch(path.c_str() + slash + 1, label.c_str())) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Materials::Material> ReaderLook::cardFor(const LookAssignment& assignment)
{
    const QString graph = QString::fromStdString(_file.fileName());
    auto card = std::make_shared<Materials::Material>();
    card->setName(QString::fromStdString(assignment.material));
    card->addAppearance(Materials::ModelUUIDs::ModelUUID_Rendering_MaterialX);

    // The file set is the SAME for every card the look makes: one document
    // and its images, named by what the document calls them. The cards
    // differ in the surface alone, so the document's store keeps one copy
    // of each file however many pieces wear it (docs/MaterialStorage.md
    // sec 17.13)
    auto names = std::make_shared<QList<QVariant>>();
    auto files = std::make_shared<QList<QVariant>>();
    names->append(graph);
    files->append(QString::fromStdString(_file.filePath()));
    for (const auto& image : _images) {
        names->append(QString::fromStdString(image.name));
        files->append(QString::fromStdString(image.path));
    }
    card->setAppearanceValue(QStringLiteral("MaterialXShaderGraph"), graph);
    card->setAppearanceValue(QStringLiteral("MaterialXSurface"),
                             QString::fromStdString(assignment.material));
    card->setAppearanceValue(QStringLiteral("MaterialXNames"), names);
    card->setAppearanceValue(QStringLiteral("MaterialXFiles"), files);
    // Hashes the files where they are now: the card is content-identified,
    // and until this runs it has names and no identity
    card->resolveMaterialXFiles(QString());
    return card;
}

int ReaderLook::read(App::Document* doc)
{
    if (!doc) {
        return 0;
    }
    return read(doc->getObjects());
}

int ReaderLook::read(const std::vector<App::DocumentObject*>& objects)
{
    _unmatched.clear();
    if (_assignments.empty()) {
        return 0;
    }
    _cards.assign(_assignments.size(), nullptr);

    std::vector<bool> used(_assignments.size(), false);
    int dressed = 0;
    for (auto object : objects) {
        if (!object) {
            continue;
        }
        auto properties = materialProperties(object);
        if (properties.empty()) {
            continue;
        }
        // The first assignment that answers to the object's name wins,
        // which is the order the look states them in. The LABEL is what an
        // importer wrote the mesh's name into; the internal name answers
        // too, for a document whose labels were since edited.
        const std::string label = object->Label.getStrValue();
        for (std::size_t i = 0; i < _assignments.size(); ++i) {
            if (!matches(_assignments[i].geom, label)
                && !matches(_assignments[i].geom, object->getNameInDocument())) {
                continue;
            }
            if (!_cards[i]) {
                _cards[i] = cardFor(_assignments[i]);
            }
            for (auto property : properties) {
                property->setValue(*_cards[i]);
            }
            used[i] = true;
            ++dressed;
            break;
        }
    }
    for (std::size_t i = 0; i < _assignments.size(); ++i) {
        if (!used[i]) {
            _unmatched.push_back(_assignments[i].geom);
        }
    }
    if (!_unmatched.empty()) {
        Base::Console().warning("Look '%s': %d of %d assignments matched no object\n",
                                _look.empty() ? _file.fileName().c_str() : _look.c_str(),
                                static_cast<int>(_unmatched.size()),
                                static_cast<int>(_assignments.size()));
    }
    return dressed;
}

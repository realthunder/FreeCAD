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

#include <sstream>

#include <App/Document.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Gui/Renderer/MaterialXSupport.h>
#include <Mod/Import/App/ReaderLook.h>

#include "ReaderLookGui.h"

using namespace ImportGui;

namespace
{

std::string readFile(const Base::FileInfo& file)
{
    Base::ifstream str(file, std::ios::in | std::ios::binary);
    if (!str) {
        return {};
    }
    std::ostringstream buffer;
    buffer << str.rdbuf();
    return buffer.str();
}

}  // namespace

int ImportGui::readLook(const Base::FileInfo& file,
                        App::Document* doc,
                        const std::vector<App::DocumentObject*>& objects,
                        const std::string& look)
{
    if (!doc) {
        return -1;
    }
    if (!Render::MaterialX::available()) {
        Base::Console().error("%s: this build carries no MaterialX support\n",
                              file.fileName().c_str());
        return -1;
    }
    const std::string xml = readFile(file);
    if (xml.empty()) {
        Base::Console().error("Cannot read '%s'\n", file.filePath().c_str());
        return -1;
    }
    const auto found = Render::MaterialX::looks(xml, file.filePath());
    if (found.empty()) {
        Base::Console().error("%s states no look: there is nothing saying which material "
                              "each object wears\n",
                              file.fileName().c_str());
        return -1;
    }
    const Render::MaterialX::Look* chosen = &found.front();
    if (!look.empty()) {
        chosen = nullptr;
        for (const auto& candidate : found) {
            if (candidate.name == look) {
                chosen = &candidate;
                break;
            }
        }
        if (!chosen) {
            Base::Console().error("%s states no look called '%s'\n",
                                  file.fileName().c_str(),
                                  look.c_str());
            return -1;
        }
    }
    else if (found.size() > 1) {
        // Several looks are several dressings of one asset; without being
        // told which, the first is what a viewer shows too
        Base::Console().warning("%s states %d looks; reading '%s'\n",
                                file.fileName().c_str(),
                                static_cast<int>(found.size()),
                                chosen->name.c_str());
    }

    Import::ReaderLook reader(file);
    reader.setLookName(chosen->name);
    std::vector<Import::LookAssignment> assignments;
    assignments.reserve(chosen->assignments.size());
    for (const auto& assign : chosen->assignments) {
        assignments.push_back({assign.material, assign.geom});
    }
    reader.setAssignments(std::move(assignments));

    // What the document calls its images is the key the card, the manifest
    // and both renderers join on, so it comes from the document and nowhere
    // else (docs/MaterialStorage.md sec 17.12)
    std::vector<Import::LookFile> images;
    for (const auto& reference : Render::MaterialX::imageReferences(xml, file.filePath())) {
        if (reference.path.empty()) {
            Base::Console().warning("%s refers to '%s', which is not beside it\n",
                                    file.fileName().c_str(),
                                    reference.name.c_str());
            continue;
        }
        images.push_back({reference.name, reference.path});
    }
    reader.setImages(std::move(images));

    const int dressed = objects.empty() ? reader.read(doc) : reader.read(objects);
    Base::Console().message("Look '%s': %d object%s of %s\n",
                            chosen->name.c_str(),
                            dressed,
                            dressed == 1 ? "" : "s",
                            doc->getName());
    return dressed;
}

int ImportGui::readSidecarLook(const Base::FileInfo& file,
                               App::Document* doc,
                               const std::vector<App::DocumentObject*>& objects)
{
    Base::FileInfo sidecar(file.dirPath() + "/" + file.fileNamePure() + ".mtlx");
    // exists() first: FileInfo::isFile() answers TRUE for a name that is
    // not there at all (it only tests the kind of what it finds), and the
    // ordinary case here is that there is no sidecar
    if (!sidecar.exists() || !sidecar.isFile()) {
        return 0;
    }
    return readLook(sidecar, doc, objects);
}

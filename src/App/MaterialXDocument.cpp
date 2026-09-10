/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <fstream>
# include <sstream>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>

#include "FileSet.h"
#include "MaterialXDocument.h"

FC_LOG_LEVEL_INIT("App", true, 2, true)

using namespace App;

namespace {
const char *const manifestHeader = "FreeCAD MaterialX 1";
}

const MaterialXDocument::File *MaterialXDocument::find(const char *name) const
{
    if (!name || !name[0]) {
        return nullptr;
    }
    for (const auto &file : files) {
        if (file.name == name) {
            return &file;
        }
    }
    return nullptr;
}

std::string MaterialXDocument::documentHash() const
{
    const File *file = find(document.c_str());
    return file ? file->hash : std::string();
}

std::vector<std::string> MaterialXDocument::hashes() const
{
    std::vector<std::string> out;
    out.reserve(files.size());
    for (const auto &file : files) {
        if (!file.hash.empty()) {
            out.push_back(file.hash);
        }
    }
    return out;
}

std::string MaterialXDocument::write() const
{
    std::ostringstream out;
    out << manifestHeader << '\n';
    out << "document " << document << '\n';
    // Only when one is named: a manifest that wears the document's first
    // surface writes exactly the bytes it wrote before this line existed,
    // so no stored card's identity moved when it was added.
    if (!surface.empty()) {
        out << "surface " << surface << '\n';
    }
    for (const auto &file : files) {
        out << "file " << file.hash << ' ' << file.name << '\n';
    }
    return out.str();
}

bool MaterialXDocument::read(const std::string &text, MaterialXDocument &out)
{
    out = MaterialXDocument();
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != manifestHeader) {
        return false;
    }
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (line.compare(0, 9, "document ") == 0) {
            out.document = line.substr(9);
        }
        else if (line.compare(0, 8, "surface ") == 0) {
            out.surface = line.substr(8);
        }
        else if (line.compare(0, 5, "file ") == 0) {
            const std::size_t space = line.find(' ', 5);
            if (space == std::string::npos) {
                out = MaterialXDocument();
                return false;
            }
            File file;
            file.hash = line.substr(5, space - 5);
            file.name = line.substr(space + 1);
            if (file.name.empty()) {
                out = MaterialXDocument();
                return false;
            }
            out.files.push_back(std::move(file));
        }
        // Any other line: a key a later build added. Stepped over, so that
        // a manifest written by one still reads here.
    }
    return true;
}

bool MaterialXDocument::readFile(const std::string &path, MaterialXDocument &out)
{
    out = MaterialXDocument();
    Base::ifstream in(Base::FileInfo(path), std::ios::in | std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return read(text.str(), out);
}

std::string MaterialXDocument::manifestHash() const
{
    return FileBlobManager::hashBytes(write());
}

FileBlobHandle MaterialXDocument::store(FileBlobManager &manager) const
{
    const std::string text = write();
    if (auto existing = manager.find(FileBlobManager::hashBytes(text))) {
        return existing;
    }
    // Written where the store adopts from, then handed over: adoptFile()
    // hashes it and shares an existing blob when the same manifest is
    // already there.
    const std::string path = manager.uniquePath("materialx.manifest");
    {
        Base::ofstream to(Base::FileInfo(path), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!to) {
            FC_ERR("cannot write a MaterialX manifest to " << path);
            return {};
        }
        to.write(text.data(), std::streamsize(text.size()));
    }
    try {
        return manager.adoptFile(path.c_str(), "manifest");
    }
    catch (const Base::Exception &e) {
        FC_ERR("cannot store a MaterialX manifest: " << e.what());
        return {};
    }
}

MaterialXDocument MaterialXDocument::fromFileSet(const FileSet &files,
                                                 const std::string &document,
                                                 const std::string &surface)
{
    MaterialXDocument out;
    out.document = document;
    out.surface = surface;
    for (const auto &entry : files.entries()) {
        out.files.push_back({entry.name, entry.hash});
    }
    return out;
}

bool MaterialXDocument::operator==(const MaterialXDocument &other) const
{
    if (document != other.document || surface != other.surface
        || files.size() != other.files.size()) {
        return false;
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (files[i].name != other.files[i].name || files[i].hash != other.files[i].hash) {
            return false;
        }
    }
    return true;
}

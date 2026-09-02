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
#include <map>
#include <sstream>
#endif

#include <QString>
#include <QTextStream>

#include <App/AppearanceList.h>
#include <App/Document.h>
#include <App/FileBlobManager.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>

#include "MaterialClipboard.h"
#include "MaterialLoader.h"
#include "Materials.h"
#include "PropertyMaterial.h"

using namespace Materials;

namespace
{
const char* const Magic = "FreeCAD Material 1";

std::string readFile(const std::string& path)
{
    Base::ifstream in(Base::FileInfo(path), std::ios::in | std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string& path, const std::string& bytes)
{
    Base::ofstream out(Base::FileInfo(path), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(bytes.data(), std::streamsize(bytes.size()));
    return bool(out);
}

// One entry: a header line, the bytes, a newline
void put(std::string& out, const std::string& header, const std::string& bytes)
{
    out += header;
    out += ' ';
    out += std::to_string(bytes.size());
    out += '\n';
    out += bytes;
    out += '\n';
}

// A file the payload carries, by the hash the card or the look names
// it by. The bytes come from the store, or from the path a library
// card was hashed at.
void putFile(std::string& out,
             std::map<std::string, bool>& carried,
             const std::string& hash,
             const std::string& path,
             const App::FileBlobManager& manager)
{
    if (hash.empty() || carried.count(hash)) {
        return;
    }
    std::string source = path;
    if (source.empty() || !Base::FileInfo(source).exists()) {
        if (auto blob = manager.find(hash)) {
            source = blob->path();
        }
    }
    const std::string bytes = source.empty() ? std::string() : readFile(source);
    if (bytes.empty()) {
        Base::Console().warning("Copy Material: no bytes for file %s\n", hash.c_str());
        return;
    }
    carried[hash] = true;
    put(out, "file " + hash + " " + Base::FileInfo(source).extension(), bytes);
}

struct Entry
{
    std::string kind;
    std::vector<std::string> args;
    std::string bytes;
};

bool parse(const std::string& data, std::vector<Entry>& entries)
{
    const std::string magic = std::string(Magic) + "\n";
    if (data.compare(0, magic.size(), magic) != 0) {
        return false;
    }
    std::size_t pos = magic.size();
    while (pos < data.size()) {
        const std::size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) {
            return false;
        }
        std::istringstream header(data.substr(pos, eol - pos));
        Entry entry;
        std::vector<std::string> words;
        for (std::string word; header >> word;) {
            words.push_back(word);
        }
        if (words.size() < 2) {
            return false;
        }
        std::size_t size = 0;
        try {
            size = std::stoul(words.back());
        }
        catch (const std::exception&) {
            return false;
        }
        entry.kind = words.front();
        entry.args.assign(words.begin() + 1, words.end() - 1);
        pos = eol + 1;
        if (pos + size + 1 > data.size() || data[pos + size] != '\n') {
            return false;
        }
        entry.bytes = data.substr(pos, size);
        pos += size + 1;
        entries.push_back(std::move(entry));
    }
    return true;
}
}  // namespace

const char* Clipboard::mimeType()
{
    return "application/x-freecad-material";
}

bool Clipboard::isPayload(const std::string& data)
{
    std::vector<Entry> entries;
    return parse(data, entries);
}

std::string Clipboard::pack(const PropertyMaterial* card,
                            const App::PropertyAppearanceList* look,
                            const App::Document& doc)
{
    std::string out;
    std::map<std::string, bool> carried;
    const auto& manager = doc.getFileBlobManager();
    bool anything = false;

    if (card && !card->isUnresolved() && card->libraryStatus() != PropertyMaterial::LibraryStatus::NoCard) {
        const Material& worn = card->getValue();
        QString yaml;
        QTextStream stream(&yaml);
        worn.saveCanonical(stream);
        stream.flush();
        put(out, "uuid", worn.getUUID().toStdString());
        put(out, "name", worn.getName().toStdString());
        put(out, "card", yaml.toStdString());
        const auto& hashes = worn.getMaterialXHashes();
        const auto& paths = worn.getMaterialXPaths();
        for (std::size_t i = 0; i < hashes.size(); ++i) {
            putFile(out, carried, hashes[i], i < paths.size() ? paths[i] : std::string(), manager);
        }
        anything = true;
    }

    if (look && look->getSize()) {
        Base::StringWriter writer;
        writer.setForceXML(1);
        writer.setSchemaVersion(static_cast<int>(App::Document::getCurrentSchemaVersion()));
        look->Save(writer);
        put(out, "look", writer.getString());
        put(out, "custom", look->isFollowingMaterial() ? "0" : "1");
        for (const auto& hash : look->getList().getTextureHashes()) {
            putFile(out, carried, hash, std::string(), manager);
        }
        for (const auto& hash : look->getList().materialXHashes()) {
            putFile(out, carried, hash, std::string(), manager);
        }
        anything = true;
    }
    if (!anything) {
        return {};
    }
    return std::string(Magic) + "\n" + out;
}

bool Clipboard::apply(const std::string& data,
                      PropertyMaterial* card,
                      App::PropertyAppearanceList* look,
                      const std::vector<int>& faces,
                      App::Document& doc)
{
    std::vector<Entry> entries;
    if (!parse(data, entries)) {
        return false;
    }
    auto& manager = doc.getFileBlobManager();

    // The files first, into the target's store, so that everything below
    // that names one by hash finds it there -- the card's files through
    // the path each was written at, the look's textures through find().
    // The handles stay alive until the end: a store keeps a blob only
    // while something holds it, and the card and the look take hold of
    // theirs only when they are set.
    std::map<std::string, App::FileBlobHandle> blobOf;
    std::map<std::string, std::string> pathOf;
    for (const auto& entry : entries) {
        if (entry.kind != "file" || entry.args.empty()) {
            continue;
        }
        const std::string& hash = entry.args[0];
        const std::string ext = entry.args.size() > 1 ? entry.args[1] : std::string();
        const std::string path = manager.uniquePath("clipboard" + (ext.empty() ? "" : "." + ext));
        if (!writeFile(path, entry.bytes)) {
            Base::Console().error("Paste Material: cannot write '%s'\n", path.c_str());
            continue;
        }
        try {
            if (auto blob = manager.insertFile(path.c_str(), ext.empty() ? nullptr : ext.c_str())) {
                pathOf[hash] = blob->path();
                blobOf[hash] = std::move(blob);
                continue;
            }
        }
        catch (const Base::Exception& e) {
            Base::Console().error("Paste Material: cannot store '%s': %s\n", path.c_str(), e.what());
        }
        pathOf[hash] = path;
    }

    bool applied = false;
    std::string uuid;
    std::string name;
    std::string yaml;
    std::string xml;
    bool custom = false;
    for (const auto& entry : entries) {
        if (entry.kind == "uuid") {
            uuid = entry.bytes;
        }
        else if (entry.kind == "name") {
            name = entry.bytes;
        }
        else if (entry.kind == "card") {
            yaml = entry.bytes;
        }
        else if (entry.kind == "look") {
            xml = entry.bytes;
        }
        else if (entry.kind == "custom") {
            custom = entry.bytes == "1";
        }
    }

    if (card && !yaml.empty()) {
        const std::string path = manager.uniquePath("clipboard.FCMat");
        std::shared_ptr<Material> material;
        if (writeFile(path, yaml)) {
            material = MaterialLoader::getMaterialFromFile(QString::fromStdString(path));
        }
        if (material) {
            material->setUUID(QString::fromStdString(uuid));
            material->setName(QString::fromStdString(name));
            // The canonical form names the files by hash and no path; the
            // paths are where the store just put them, which is the route a
            // library card's files take into a document
            std::vector<std::string> paths;
            for (const auto& hash : material->getMaterialXHashes()) {
                auto found = pathOf.find(hash);
                paths.push_back(found == pathOf.end() ? std::string() : found->second);
            }
            material->setMaterialXPaths(paths);
            card->setValue(*material);
            applied = true;
        }
        else {
            Base::Console().error("Paste Material: the card cannot be read\n");
        }
    }

    if (look && !xml.empty() && (custom || !faces.empty())) {
        // Restored into a property of its own that answers for the
        // target's container, so the textures it names resolve against
        // the target's store, then handed over whole or by face
        App::PropertyAppearanceList pasted;
        pasted.setContainer(look->getContainer());
        std::istringstream in(xml);
        Base::XMLReader reader("clipboard", in);
        reader.DocumentSchema = static_cast<int>(App::Document::getCurrentSchemaVersion());
        pasted.Restore(reader);
        if (pasted.getSize()) {
            if (faces.empty()) {
                look->setList(pasted.getList());
            }
            else {
                const App::MaterialAppearance base = pasted.getBase();
                for (int face : faces) {
                    if (face >= 0 && face < look->getSize()) {
                        look->set1Value(face, base);
                    }
                }
            }
            applied = true;
        }
        pasted.setContainer(nullptr);
    }
    return applied;
}

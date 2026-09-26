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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cstring>
# include <map>
# include <mutex>
# include <sstream>
#endif

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>

#include <zipios++/zipios-config.h>
#include <zipios++/zipfile.h>

#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Uuid.h>

#include "FileHistory.h"
#include "Application.h"
#include "Document.h"
#include "FileBlobManager.h"
#include "TransactionLog.h"
#include "TransactionStore.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

namespace {

/// The registry: canonical path -> the history of that file. Main thread,
/// but guarded anyway: a history can die on whatever thread drops it last.
std::mutex& registryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::weak_ptr<FileHistory>>& registry()
{
    static std::map<std::string, std::weak_ptr<FileHistory>> map;
    return map;
}

} // namespace

FileHistory::FileHistory(Document& home)
    : _home(&home)
    , _dir(home.TransientDir.getStrValue())
{}

FileHistory::FileHistory(const std::string& dir)
    : _dir(dir)
{}

std::shared_ptr<FileHistory> FileHistory::create(Document& home)
{
    return std::shared_ptr<FileHistory>(new FileHistory(home));
}

namespace {

/// What openFile() needs of a file's Document.xml: the document's own
/// properties, before any object's.
struct FileFacts
{
    int schema {0};
    std::string label;
    std::string version;        ///< "<num> <save id>"
    std::string lastModified;
    std::string historyDb;
};

std::string unescapeXml(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        static const std::pair<const char*, char> named[] = {
            {"&quot;", '"'}, {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&apos;", '\''}};
        bool done = false;
        for (const auto& n : named) {
            const size_t len = std::strlen(n.first);
            if (s.compare(i, len, n.first) == 0) {
                out += n.second;
                i += len - 1;
                done = true;
                break;
            }
        }
        if (!done)
            out += s[i];
    }
    return out;
}

/// The value of attribute `attr` of the first element after `from` in
/// `xml`, before `limit`; empty when there is none.
std::string attributeAfter(const std::string& xml, size_t from, size_t limit, const char* attr)
{
    const std::string key = std::string(attr) + "=\"";
    const size_t a = xml.find(key, from);
    if (a == std::string::npos || a >= limit)
        return {};
    const size_t start = a + key.size();
    const size_t end = xml.find('"', start);
    if (end == std::string::npos)
        return {};
    return unescapeXml(xml.substr(start, end - start));
}

bool readFacts(const std::string& xml, FileFacts& facts)
{
    // The document's own properties come first; an object's Label must not
    // be taken for the document's.
    size_t limit = xml.find("<Objects");
    if (limit == std::string::npos)
        limit = xml.size();
    // The root is FCDocument in this fork, Document upstream.
    size_t doc = xml.find("<FCDocument ");
    if (doc == std::string::npos)
        doc = xml.find("<Document ");
    if (doc == std::string::npos || doc >= limit)
        return false;
    const std::string schema = attributeAfter(xml, doc, xml.find('>', doc), "SchemaVersion");
    facts.schema = schema.empty() ? 0 : std::atoi(schema.c_str());
    auto property = [&](const char* name) -> size_t {
        const std::string key = std::string("<Property name=\"") + name + "\"";
        const size_t at = xml.find(key);
        return at < limit ? at : std::string::npos;
    };
    auto value = [&](const char* name) -> std::string {
        const size_t at = property(name);
        if (at == std::string::npos)
            return {};
        const size_t end = xml.find("</Property>", at);
        return attributeAfter(xml, at + 1, end, "value");
    };
    facts.label = value("Label");
    facts.version = value("Version");
    facts.lastModified = value("LastModifiedDate");
    const size_t history = property("History");
    if (history != std::string::npos) {
        const size_t end = xml.find("</Property>", history);
        const size_t element = xml.find("<History", history);
        if (element != std::string::npos && element < end)
            facts.historyDb = attributeAfter(xml, element, xml.find('>', element), "db");
    }
    return true;
}

} // namespace

std::shared_ptr<FileHistory> FileHistory::openFile(const std::string& path, std::string* reason)
{
    auto fail = [reason](const std::string& why) {
        if (reason)
            *reason = why;
        return std::shared_ptr<FileHistory>();
    };
    if (auto open = find(path))
        return open;
    Base::FileInfo fi(path);
    if (!fi.exists() || fi.isDir())
        return fail("no such file");

    // Document.xml, and the other XML entries a version of it holds (split
    // object files, GuiDocument.xml): the file as found, for its version.
    TransactionLog::Entries entries;
    FileFacts facts;
    try {
        zipios::ZipFile zip(path);
        if (!zip.isValid())
            return fail("not a document archive");
        auto read = [&zip](const std::string& name, std::string& out) {
            std::unique_ptr<std::istream> in(zip.getInputStream(name));
            if (!in)
                return false;
            std::ostringstream buffer;
            buffer << in->rdbuf();
            out = buffer.str();
            return true;
        };
        std::string docXml;
        if (!read("Document.xml", docXml))
            return fail("no Document.xml");
        if (!readFacts(docXml, facts))
            return fail("Document.xml cannot be read");
        if (facts.historyDb.empty())
            return fail("the file carries no history");
        entries.emplace_back("Document.xml", std::move(docXml));
        const std::string prefix = FileBlobManager::archivePrefix();
        for (const auto& entry : zip.entries()) {
            const std::string name = entry->getName();
            if (name == "Document.xml" || name.compare(0, prefix.size(), prefix) == 0
                    || name.compare(0, 11, "thumbnails/") == 0 || entry->isDirectory())
                continue;
            std::string bytes;
            if (read(name, bytes))
                entries.emplace_back(name, std::move(bytes));
        }
    }
    catch (const std::exception& e) {
        return fail(std::string("cannot read the archive: ") + e.what());
    }

    // A directory named as a document's transient directory is, so that a
    // crash with only this history open is found by recovery too (sec 25).
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::fromStdString(path));
    std::ostringstream dir;
    dir << Application::getUserCachePath() << Application::getExecutableName() << "_Doc_"
        << Base::Uuid::createUuid() << "_" << hash.result().toHex().left(6).constData() << "_"
        << QCoreApplication::applicationPid();
    Base::FileInfo(dir.str()).createDirectories();
    std::shared_ptr<FileHistory> history(new FileHistory(dir.str()));
    if (!history->setPath(path))
        return find(path);
    history->_fileLabel = facts.label.empty() ? fi.fileNamePure() : facts.label;

    auto& blobs = history->blobs();
    if (!blobs.splitArchive(path))
        return fail("the archive's blobs cannot be read");
    struct EndRestore
    {
        FileBlobManager& blobs;
        ~EndRestore() { blobs.endRestore(); }
    } ending {blobs};
    FileBlobHandle db = blobs.find(facts.historyDb);
    if (!db)
        return fail("the history's database is not in the archive");

    // The guard of 16.4: the save that wrote the copy is the save the file
    // is from. When it is not, the copy's branches are kept closed and the
    // file as found starts a new main (16.6).
    bool matches = false;
    try {
        std::string saveId;
        {
            std::istringstream in(facts.version);
            int64_t num = 0;
            in >> num >> saveId;
        }
        auto copy = TransactionStore::openSQLite(db->path());
        const std::string id = copy->getMeta("save_id");
        matches = !id.empty() && id == saveId && copy->getMeta("save_date") == facts.lastModified;
    }
    catch (const Base::Exception& e) {
        return fail(std::string("the history's database cannot be read: ") + e.what());
    }
    auto& core = TransactionLogCore::of(*history);
    if (!core.adoptEmbedded(db->path()))
        return fail("the history cannot be adopted");
    if (!matches) {
        FC_WARN("the embedded history of " << path
                << " is not the file's (edited elsewhere?): kept as closed branches");
        core.closeAdopted();
    }
    history->_fileVersion = core.recordFile(path, entries, blobs.restoredEntries(), facts.schema);
    core.flush();
    return history;
}

FileHistory::~FileHistory()
{
    setPath(std::string());
    // What ~Document did for its own store: nothing deletes a file held
    // open on Windows, and a segment of the blob store may be; nor may the
    // store's worker write one meanwhile.
    try {
        // The log first: its worker writes into the store and the blob
        // segments until its queue is empty.
        _logCore.reset();
        if (_blobs)
            _blobs->shutdown();
        if (!_dir.empty())
            Base::FileInfo(_dir).deleteDirectoryRecursive();
    }
    catch (const Base::Exception& e) {
        FC_ERR("removing the history directory " << _dir << " failed: " << e.what());
    }
}

void FileHistory::releaseHome(const Document& doc)
{
    if (_home == &doc)
        _home = nullptr;
}

FileBlobManager& FileHistory::blobs()
{
    if (!_blobs)
        _blobs = std::make_unique<FileBlobManager>(this);
    return *_blobs;
}

std::string FileHistory::canonicalPath(const std::string& path)
{
    if (path.empty())
        return path;
    QFileInfo info(QString::fromUtf8(path.c_str()));
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        canonical = info.absoluteFilePath();
    return canonical.toUtf8().constData();
}

bool FileHistory::setPath(const std::string& path)
{
    const std::string key = canonicalPath(path);
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& map = registry();
    if (key == _path)
        return true;
    if (!key.empty()) {
        auto it = map.find(key);
        if (it != map.end()) {
            auto other = it->second.lock();
            if (other && other.get() != this) {
                FC_WARN("the file " << key << " has a history already; not registering another");
                return false;
            }
        }
    }
    if (!_path.empty()) {
        auto it = map.find(_path);
        if (it != map.end()) {
            auto other = it->second.lock();
            if (!other || other.get() == this)
                map.erase(it);
        }
    }
    _path = key;
    if (!key.empty()) {
        map[key] = weak_from_this();
    }
    return true;
}

std::shared_ptr<FileHistory> FileHistory::find(const std::string& path)
{
    const std::string key = canonicalPath(path);
    if (key.empty())
        return {};
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& map = registry();
    auto it = map.find(key);
    if (it == map.end())
        return {};
    auto history = it->second.lock();
    if (!history)
        map.erase(it);
    return history;
}

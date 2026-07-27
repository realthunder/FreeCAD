/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#include <algorithm>
#include <cstring>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Uuid.h>

#include "FileBlobManager.h"
#include "Document.h"
#include "PropertyFile.h"

FC_LOG_LEVEL_INIT("App", true, 2, true)

using namespace App;

// ---------------------------------------------------------------------------
// FileBlob
// ---------------------------------------------------------------------------

FileBlob::~FileBlob()
{
    if (_owner) {
        _owner->release(this);
    }
}

// ---------------------------------------------------------------------------
// FileBlobManager
// ---------------------------------------------------------------------------

FileBlobManager::FileBlobManager(Document* doc)
    : _doc(doc)
{}

FileBlobManager::~FileBlobManager()
{
    // Blobs can outlive the manager -- an undo snapshot or the clipboard may
    // still hold one -- and the save set holds strong references of its own.
    // Detach them all first so ~FileBlob does not call release() on a manager
    // whose members are already being destroyed. Nothing leaks: the document's
    // transient directory is removed wholesale when it closes.
    std::lock_guard<std::mutex> guard(_mutex);
    for (auto& entry : _blobs) {
        if (auto blob = entry.second.lock()) {
            blob->_owner = nullptr;
        }
    }
    for (auto& entry : _saveSet) {
        if (entry.second) {
            entry.second->_owner = nullptr;
        }
    }
    for (auto& entry : _restoreHold) {
        if (entry.second) {
            entry.second->_owner = nullptr;
        }
    }
    _restoreHold.clear();
    _saveSet.clear();
    _blobs.clear();
}

FileBlobManager& FileBlobManager::defaultManager()
{
    static FileBlobManager manager(nullptr);
    return manager;
}

std::string FileBlobManager::transientPath() const
{
    if (!_doc) {
        return Base::FileInfo::getTempPath();
    }
    return QDir::fromNativeSeparators(QString::fromUtf8(_doc->TransientDir.getValue()))
        .toUtf8()
        .constData();
}

void FileBlobManager::repath(const FileBlobHandle& blob, const std::string& path)
{
    if (!blob) {
        return;
    }
    std::lock_guard<std::mutex> guard(_mutex);
    blob->_path = path;
}

namespace
{
/// Base::FileInfo::size() is not implemented on this platform, so use Qt.
uint64_t fileSize(const char* path)
{
    return static_cast<uint64_t>(QFileInfo(QString::fromUtf8(path)).size());
}
}  // namespace

std::string FileBlobManager::hashFile(const char* path)
{
    QFile file(QString::fromUtf8(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result().toHex().constData();
}

const char* FileBlobManager::archivePrefix()
{
    return "blobs/";
}

void FileBlobManager::beginSave()
{
    // Declared before the lock, so it dies after the lock is released.
    // Dropping the last reference to a blob runs ~FileBlob, which calls
    // release() and takes this same mutex -- and the save set really can hold
    // the last reference, to content a property has since replaced. Doing
    // that under the lock deadlocks the thread against itself.
    std::unordered_map<std::string, FileBlobHandle> expiring;
    std::lock_guard<std::mutex> guard(_mutex);
    expiring.swap(_saveSet);
}

void FileBlobManager::noteReferenced(const FileBlobHandle& blob)
{
    if (!blob) {
        return;
    }
    // Replaced entry released after the lock, see beginSave().
    FileBlobHandle expiring;
    std::lock_guard<std::mutex> guard(_mutex);
    auto& slot = _saveSet[blob->hash()];
    expiring = std::move(slot);
    slot = blob;
}

void FileBlobManager::writeBlobs(Base::Writer& writer)
{
    // Shared entries are a schema 5 shape. Written for an older version, the
    // properties carry their own self-contained copies and these entries would
    // be dead weight the reader never asks for.
    if (writer.getSchemaVersion() < 5) {
        return;
    }

    std::vector<FileBlobHandle> pending;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        pending.reserve(_saveSet.size());
        for (const auto& entry : _saveSet) {
            pending.push_back(entry.second);
        }
    }
    // Hash order, so the same document always produces the same archive.
    std::sort(pending.begin(), pending.end(),
              [](const FileBlobHandle& a, const FileBlobHandle& b) {
                  return a->hash() < b->hash();
              });

    // Saving under a new name gives the document a new transient directory,
    // which leaves every stored path stale. The content is unchanged, so the
    // blob is still at its content-addressed location under the new root.
    // Repaired here rather than in the property, because the properties of the
    // view tier are written after this point and would be repaired too late.
    for (const auto& blob : pending) {
        if (Base::FileInfo(blob->path()).exists()) {
            continue;
        }
        Base::FileInfo moved(blobPath(blob->hash()));
        if (moved.exists()) {
            repath(blob, moved.filePath());
        }
    }

    // A directory writer needs the subdirectory to exist before an entry can
    // be opened inside it; it also lets an unchanged blob be skipped outright,
    // since content addressing makes an existing file proof of equality. That
    // is what keeps repeated autosaves of a document with a large embedded
    // file cheap.
    auto* fileWriter = dynamic_cast<Base::FileWriter*>(&writer);
    std::string dir;
    if (fileWriter) {
        dir = fileWriter->getDirName() + "/" + archivePrefix();
        Base::FileInfo(dir).createDirectory();
    }

    for (const auto& blob : pending) {
        if (fileWriter && Base::FileInfo(dir + blob->hash()).exists()) {
            continue;
        }
        Base::ifstream from(Base::FileInfo(blob->path()), std::ios::in | std::ios::binary);
        if (!from) {
            std::stringstream str;
            str << "FileBlobManager::writeBlobs(): file '" << blob->path()
                << "' in transient directory doesn't exist.";
            throw Base::FileSystemError(str.str());
        }
        writer.putNextEntry((std::string(archivePrefix()) + blob->hash()).c_str());
        writer.Stream() << from.rdbuf();
    }
}

void FileBlobManager::beginRestore(Base::XMLReader& reader)
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _pending.clear();
    }

    // An unpacked project keeps its content as ordinary files, so there are no
    // archive entries to claim -- take copies of what is there and be done.
    if (auto* source = reader.getReader()) {
        const std::string dir = source->getDirectory();
        if (!dir.empty()) {
            restoreFromDirectory(dir + "/" + archivePrefix());
            return;
        }
    }

    reader.setArchiveHandler([this](const std::string& name, Base::Reader& entry) {
        if (name.compare(0, std::strlen(archivePrefix()), archivePrefix()) != 0) {
            return false;
        }
        try {
            readBlobEntry(entry);
        }
        catch (const Base::Exception& e) {
            FC_ERR("Failed to read included file " << name << ": " << e.what());
        }
        return true;
    });
}

void FileBlobManager::readBlobEntry(Base::Reader& entry)
{
    const std::string staging = uniquePath("blob.part");
    {
        Base::ofstream to(Base::FileInfo(staging), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!to) {
            std::stringstream str;
            str << "FileBlobManager: cannot create " << staging;
            throw Base::FileSystemError(str.str());
        }
        entry >> to.rdbuf();
    }

    // adoptFile() hashes the content and relocates it, so the entry name is
    // never trusted: what the archive claims and what it holds are checked
    // against each other by construction. It also drops content that is
    // already stored, which is what makes reopening a document cheap.
    hold(adoptFile(staging.c_str()));
}

void FileBlobManager::restoreFromDirectory(const std::string& dir)
{
    QDir source(QString::fromUtf8(dir.c_str()));
    if (!source.exists()) {
        return;
    }
    for (const QFileInfo& info : source.entryInfoList(QDir::Files)) {
        try {
            // insertFile() copies: the files belong to the project directory
            // and must stay in it.
            hold(insertFile(info.absoluteFilePath().toUtf8().constData()));
        }
        catch (const Base::Exception& e) {
            FC_ERR("Failed to read included file " << info.fileName().toStdString() << ": "
                                                   << e.what());
        }
    }
}

void FileBlobManager::hold(FileBlobHandle blob)
{
    if (!blob) {
        return;
    }
    // Replaced entry released after the lock, see beginSave().
    FileBlobHandle expiring;
    std::lock_guard<std::mutex> guard(_mutex);
    auto& slot = _restoreHold[blob->hash()];
    expiring = std::move(slot);
    slot = std::move(blob);
}

void FileBlobManager::addPendingReferrer(const std::string& hash, PropertyFileIncluded* prop)
{
    if (hash.empty() || !prop) {
        return;
    }
    // Content read earlier in this restore, or left over from one before it,
    // can be handed over at once. Everything else waits for the drain.
    if (auto blob = find(hash)) {
        prop->assignRestoredBlob(blob);
        return;
    }
    std::lock_guard<std::mutex> guard(_mutex);
    _pending.emplace_back(hash, prop);
}

void FileBlobManager::removePendingReferrer(PropertyFileIncluded* prop)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                                  [prop](const auto& entry) { return entry.second == prop; }),
                   _pending.end());
}

void FileBlobManager::dispatchPending()
{
    std::vector<std::pair<std::string, PropertyFileIncluded*>> pending;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        pending.swap(_pending);
    }
    for (const auto& entry : pending) {
        if (auto blob = find(entry.first)) {
            entry.second->assignRestoredBlob(blob);
        }
        else {
            FC_WARN("Included file " << entry.first << " is missing from the document");
        }
    }
}

void FileBlobManager::endRestore()
{
    std::unordered_map<std::string, FileBlobHandle> hold;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _pending.clear();
        hold.swap(_restoreHold);
    }
    // Released outside the lock: the last reference to unclaimed content dies
    // here, and ~FileBlob calls back into release().
}

std::string FileBlobManager::blobDir() const
{
    const std::string root = transientPath() + "/blobs";
    Base::FileInfo(root).createDirectory();
    return root;
}

std::string FileBlobManager::blobPath(const std::string& hash) const
{
    return blobDir() + "/" + hash;
}

std::string FileBlobManager::uniquePath(const std::string& name) const
{
    const std::string dir = transientPath();
    Base::FileInfo fi(dir + "/" + name);
    while (fi.exists()) {
        Base::Uuid uuid;
        fi.setFile(dir + "/" + name + "." + uuid.getValue());
    }
    return fi.filePath();
}

FileBlobHandle FileBlobManager::make(const std::string& hash, const std::string& path,
                                     uint64_t size)
{
    // Not shared_ptr's make_shared: the constructor is private to keep blobs
    // creatable only through the manager that owns their lifetime.
    std::shared_ptr<FileBlob> blob(new FileBlob());
    blob->_owner = this;
    blob->_hash = hash;
    blob->_path = path;
    blob->_size = size;
    _blobs[hash] = blob;
    return blob;
}

FileBlobHandle FileBlobManager::find(const std::string& hash) const
{
    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(hash);
    if (it == _blobs.end()) {
        return {};
    }
    return it->second.lock();
}

FileBlobHandle FileBlobManager::insertFile(const char* srcPath)
{
    Base::FileInfo src(srcPath);
    if (!src.exists()) {
        std::stringstream str;
        str << "FileBlobManager: file " << srcPath << " does not exist.";
        throw Base::FileSystemError(str.str());
    }

    const std::string hash = hashFile(srcPath);
    if (hash.empty()) {
        std::stringstream str;
        str << "FileBlobManager: cannot read " << srcPath << " to hash it.";
        throw Base::FileSystemError(str.str());
    }

    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(hash);
    if (it != _blobs.end()) {
        if (auto existing = it->second.lock()) {
            // Content already stored: share it, whatever the caller calls it.
            return existing;
        }
    }

    const std::string dst = blobPath(hash);
    if (!src.copyTo(dst.c_str())) {
        std::stringstream str;
        str << "FileBlobManager: cannot copy " << srcPath << " to " << dst;
        throw Base::FileSystemError(str.str());
    }

    // Blobs are immutable; read-only makes accidental in-place edits fail loudly
    // instead of silently changing every referrer's content.
    Base::FileInfo fi(dst);
    fi.setPermissions(Base::FileInfo::ReadOnly);

    return make(hash, dst, fileSize(dst.c_str()));
}

FileBlobHandle FileBlobManager::adoptFile(const char* path)
{
    Base::FileInfo fi(path);
    if (!fi.exists()) {
        std::stringstream str;
        str << "FileBlobManager: cannot adopt missing file " << path;
        throw Base::FileSystemError(str.str());
    }

    const std::string hash = hashFile(path);
    if (hash.empty()) {
        std::stringstream str;
        str << "FileBlobManager: cannot read " << path << " to hash it.";
        throw Base::FileSystemError(str.str());
    }

    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(hash);
    if (it != _blobs.end()) {
        if (auto existing = it->second.lock()) {
            // Content already stored: drop the incoming duplicate.
            if (existing->path() != fi.filePath()) {
                fi.setPermissions(Base::FileInfo::ReadWrite);
                fi.deleteFile();
            }
            return existing;
        }
    }

    // An adopted file is a scratch file, or freshly restored content sitting
    // at a staging path. Move it to its content-addressed location.
    const std::string dst = blobPath(hash);
    if (fi.filePath() != dst) {
        fi.setPermissions(Base::FileInfo::ReadWrite);
        if (!fi.renameFile(dst.c_str())) {
            std::stringstream str;
            str << "FileBlobManager: cannot rename " << fi.filePath() << " to " << dst;
            throw Base::FileSystemError(str.str());
        }
        fi.setFile(dst);
    }

    fi.setPermissions(Base::FileInfo::ReadOnly);
    return make(hash, fi.filePath(), fileSize(fi.filePath().c_str()));
}

std::vector<FileBlobHandle> FileBlobManager::blobs() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    std::vector<FileBlobHandle> result;
    result.reserve(_blobs.size());
    for (const auto& entry : _blobs) {
        if (auto blob = entry.second.lock()) {
            result.push_back(std::move(blob));
        }
    }
    return result;
}

void FileBlobManager::release(FileBlob* blob)
{
    std::lock_guard<std::mutex> guard(_mutex);

    auto it = _blobs.find(blob->_hash);
    if (it != _blobs.end()) {
        if (auto live = it->second.lock()) {
            // A replacement blob already owns this key, and therefore this
            // path -- deleting the file would destroy its content. Note that
            // a weak_ptr reports expired as soon as the use count hits zero,
            // which is before ~FileBlob runs, so this is reachable.
            if (live.get() != blob) {
                return;
            }
        }
        else {
            _blobs.erase(it);
        }
    }

    if (blob->_path.empty()) {
        return;
    }
    Base::FileInfo fi(blob->_path);
    if (fi.exists()) {
        fi.setPermissions(Base::FileInfo::ReadWrite);
        fi.deleteFile();
    }
}

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

using namespace App;

TYPESYSTEM_SOURCE_ABSTRACT(App::FileBlobManager, Base::Persistence)

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

void FileBlobManager::beginSave()
{
    std::lock_guard<std::mutex> guard(_mutex);
    _saveSet.clear();
}

void FileBlobManager::noteReferenced(const FileBlobHandle& blob)
{
    if (!blob) {
        return;
    }
    std::lock_guard<std::mutex> guard(_mutex);
    _saveSet[blob->hash()] = blob;
}

void FileBlobManager::addFilesToWriter(Base::Writer& writer)
{
    std::vector<FileBlobHandle> pending;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        pending.reserve(_saveSet.size());
        for (const auto& entry : _saveSet) {
            pending.push_back(entry.second);
        }
    }
    // One entry per blob, named by hash: unique by construction, so the name
    // the writer hands back is the one asked for and the properties' stored
    // hashes stay valid.
    for (const auto& blob : pending) {
        writer.addFile(blob->hash().c_str(), this);
    }
}

void FileBlobManager::SaveDocFile(Base::Writer& writer) const
{
    const std::string hash = writer.getCurrentFileName();
    FileBlobHandle blob;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        auto it = _saveSet.find(hash);
        if (it != _saveSet.end()) {
            blob = it->second;
        }
    }
    if (!blob) {
        std::stringstream str;
        str << "FileBlobManager::SaveDocFile(): no blob for entry " << hash;
        throw Base::FileSystemError(str.str());
    }

    Base::ifstream from(Base::FileInfo(blob->path()), std::ios::in | std::ios::binary);
    if (!from) {
        std::stringstream str;
        str << "FileBlobManager::SaveDocFile(): file '" << blob->path()
            << "' in transient directory doesn't exist.";
        throw Base::FileSystemError(str.str());
    }
    writer.Stream() << from.rdbuf();
}

void FileBlobManager::requestRestore(const std::string& hash, Base::XMLReader& reader)
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        // One consumer per entry: several properties share the content, but
        // the archive holds it once and it is streamed in once.
        if (!_restoreSet.insert(hash).second) {
            return;
        }
    }
    reader.addFile(hash.c_str(), this);
}

void FileBlobManager::RestoreDocFile(Base::Reader& reader)
{
    const std::string hash = reader.getFileName();
    const std::string staging = uniquePath(hash + ".part");

    {
        Base::ofstream to(Base::FileInfo(staging), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!to) {
            std::stringstream str;
            str << "FileBlobManager::RestoreDocFile(): cannot create " << staging;
            throw Base::FileSystemError(str.str());
        }
        reader >> to.rdbuf();
    }

    // adoptFile() re-hashes and relocates. It also detects content that is
    // already stored, which is what makes a re-save of an unchanged document
    // cost nothing.
    FileBlobHandle blob = adoptFile(staging.c_str());

    // Hold it: the properties that refer to this content resolve their hashes
    // lazily, so until then nothing else owns the blob.
    std::lock_guard<std::mutex> guard(_mutex);
    _restoreHold[blob->hash()] = std::move(blob);
}

void FileBlobManager::releaseRestored(const std::string& hash)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _restoreHold.erase(hash);
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

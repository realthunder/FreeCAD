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
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Uuid.h>

#include "FileBlobManager.h"
#include "Document.h"

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

FileBlobManager::~FileBlobManager() = default;

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

std::string FileBlobManager::blobDir(const std::string& hash) const
{
    const std::string root = transientPath() + "/blobs";
    Base::FileInfo(root).createDirectory();
    const std::string dir = root + "/" + hash;
    Base::FileInfo(dir).createDirectory();
    return dir;
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

std::string FileBlobManager::key(const std::string& hash, const std::string& name)
{
    return hash + "/" + name;
}

FileBlobHandle FileBlobManager::make(const std::string& hash, const std::string& path,
                                     const std::string& name, uint64_t size)
{
    // Not shared_ptr's make_shared: the constructor is private to keep blobs
    // creatable only through the manager that owns their lifetime.
    std::shared_ptr<FileBlob> blob(new FileBlob());
    blob->_owner = this;
    blob->_hash = hash;
    blob->_path = path;
    blob->_baseName = name;
    blob->_size = size;
    _blobs[key(hash, name)] = blob;
    return blob;
}

FileBlobHandle FileBlobManager::find(const std::string& hash, const std::string& name) const
{
    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(key(hash, name));
    if (it == _blobs.end()) {
        return {};
    }
    return it->second.lock();
}

FileBlobHandle FileBlobManager::insertFile(const char* srcPath, const char* name)
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

    const std::string baseName = (name && name[0] != '\0') ? name : src.fileName();

    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(key(hash, baseName));
    if (it != _blobs.end()) {
        if (auto existing = it->second.lock()) {
            // Same content under the same name: share it outright.
            return existing;
        }
    }

    // Same content under a different name is a separate blob, stored as its
    // own copy. The name is caller-visible -- it becomes the archive entry
    // name and carries the extension consumers sniff -- so it cannot be
    // collapsed onto the first name that happened to arrive. De-duplicating
    // those bytes would need hard links, which are not portable enough to be
    // worth it for a case this rare.
    const std::string dst = blobDir(hash) + "/" + baseName;
    if (!src.copyTo(dst.c_str())) {
        std::stringstream str;
        str << "FileBlobManager: cannot copy " << srcPath << " to " << dst;
        throw Base::FileSystemError(str.str());
    }

    // Blobs are immutable; read-only makes accidental in-place edits fail loudly
    // instead of silently changing every referrer's content.
    Base::FileInfo fi(dst);
    fi.setPermissions(Base::FileInfo::ReadOnly);

    return make(hash, dst, baseName, fileSize(dst.c_str()));
}

FileBlobHandle FileBlobManager::adoptFile(const char* path, const char* name)
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

    const std::string baseName = (name && name[0] != '\0') ? name : fi.fileName();

    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(key(hash, baseName));
    if (it != _blobs.end()) {
        if (auto existing = it->second.lock()) {
            // Already stored under this name: drop the incoming duplicate.
            if (existing->path() != fi.filePath()) {
                fi.setPermissions(Base::FileInfo::ReadWrite);
                fi.deleteFile();
            }
            return existing;
        }
    }

    // An adopted file is a scratch file or freshly restored content sitting at
    // a staging path. Move it to its canonical location so it carries the name
    // callers expect rather than the temporary's.
    const std::string dst = blobDir(hash) + "/" + baseName;
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
    return make(hash, fi.filePath(), baseName, fileSize(fi.filePath().c_str()));
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

    auto it = _blobs.find(key(blob->_hash, blob->_baseName));
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

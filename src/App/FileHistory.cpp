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
# include <map>
# include <mutex>
#endif

#include <QFileInfo>

#include <Base/Console.h>
#include <Base/FileInfo.h>

#include "FileHistory.h"
#include "Document.h"
#include "FileBlobManager.h"

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

std::shared_ptr<FileHistory> FileHistory::create(Document& home)
{
    return std::shared_ptr<FileHistory>(new FileHistory(home));
}

FileHistory::~FileHistory()
{
    setPath(std::string());
    // What ~Document did for its own store: nothing deletes a file held
    // open on Windows, and a segment of the blob store may be; nor may the
    // store's worker write one meanwhile.
    try {
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

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
# include <algorithm>
#endif

#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Persistence.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include "FileSet.h"

using namespace App;

FileSet::Entry *FileSet::entry(const char *name)
{
    if (!name || !name[0]) {
        return nullptr;
    }
    for (auto &file : _files) {
        if (file.name == name) {
            return &file;
        }
    }
    return nullptr;
}

const FileSet::Entry *FileSet::find(const char *name) const
{
    return const_cast<FileSet*>(this)->entry(name);
}

std::string FileSet::filePath(const char *name) const
{
    const Entry *file = find(name);
    // A pending entry has a name and a hash and no content yet, which reads
    // here the same as no entry at all: neither can be opened.
    return (file && file->blob) ? file->blob->path() : std::string();
}

std::vector<std::string> FileSet::hashes() const
{
    std::vector<std::string> out;
    out.reserve(_files.size());
    for (const auto &file : _files) {
        out.push_back(file.hash);
    }
    return out;
}

std::vector<std::string> FileSet::pendingHashes() const
{
    std::vector<std::string> out;
    for (const auto &file : _files) {
        if (!file.hash.empty() && !file.blob) {
            out.push_back(file.hash);
        }
    }
    return out;
}

bool FileSet::holdsEveryNamedBlob() const
{
    for (const auto &file : _files) {
        if (!file.hash.empty() && !file.blob) {
            return false;
        }
    }
    return true;
}

void FileSet::setFile(FileBlobManager &manager, const char *name, const char *path,
                      const char *original)
{
    if (!name || !name[0]) {
        throw Base::ValueError("An included file needs a name");
    }
    FileBlobHandle blob;
    if (path && path[0]) {
        // insertFile() hashes the bytes and keeps whichever copy the store
        // already holds, so adding the same map under two names -- or in two
        // sets -- costs one file.
        blob = manager.insertFile(path, Base::FileInfo(name).extension().c_str());
    }
    setBlob(manager, name, blob, original ? original : path);
}

void FileSet::setBlob(FileBlobManager &manager, const char *name, const FileBlobHandle &handle,
                      const char *original)
{
    if (!name || !name[0]) {
        throw Base::ValueError("An included file needs a name");
    }
    FileBlobHandle blob = handle;
    if (blob && blob->owner() != &manager) {
        // Blobs never migrate between stores; taking one from another
        // document imports the content into this document's own.
        blob = manager.insertFile(blob->path().c_str(),
                                  Base::FileInfo(name).extension().c_str());
    }
    Entry *file = entry(name);
    if (!file) {
        _files.emplace_back();
        file = &_files.back();
        file->name = name;
    }
    file->blob = blob;
    file->hash = blob ? blob->hash() : std::string();
    if (original) {
        file->original = original;
    }
}

bool FileSet::remove(const char *name)
{
    if (!find(name)) {
        return false;
    }
    _files.erase(std::remove_if(_files.begin(), _files.end(),
                                [name](const Entry &file) { return file.name == name; }),
                 _files.end());
    return true;
}

void FileSet::assign(FileBlobManager &manager, std::vector<Entry> files)
{
    for (auto &file : files) {
        if (file.blob && file.blob->owner() != &manager) {
            file.blob = manager.insertFile(file.blob->path().c_str(),
                                           Base::FileInfo(file.name).extension().c_str());
        }
        if (file.blob) {
            file.hash = file.blob->hash();
        }
        else if (!file.hash.empty()) {
            // A copy taken while the content was still on its way -- an undo
            // snapshot, a paste -- names the content without holding it. The
            // store may have it by now; what it does not have, the caller
            // queues for.
            file.blob = manager.find(file.hash);
        }
    }
    _files = std::move(files);
}

bool FileSet::assignRestoredBlob(const FileBlobHandle &blob)
{
    if (blob) {
        for (auto &file : _files) {
            // By hash, not by position: the manager hands content over as
            // the archive yields it, in no order this set chose, and one
            // file may be known under two names.
            if (file.hash == blob->hash()) {
                file.blob = blob;
            }
        }
    }
    return holdsEveryNamedBlob();
}

BlobReferrer FileSet::referrerFor(const BlobReferrer &base, const Entry &file)
{
    BlobReferrer referrer = base;
    Base::FileInfo fi(file.name);
    const std::string stem = fi.fileNamePure();
    if (!stem.empty()) {
        referrer.name = referrer.name.empty() ? stem : referrer.name + "." + stem;
    }
    const std::string ext = fi.extension();
    referrer.ext = ext.empty() ? std::string() : "." + ext;
    return referrer;
}

void FileSet::collectBlobs(FileBlobManager &manager, const BlobReferrer &base) const
{
    for (const auto &file : _files) {
        if (file.blob) {
            manager.noteReferenced(file.blob, referrerFor(base, file));
        }
    }
}

void FileSet::save(Base::Writer &writer, const char *element, FileBlobManager *manager,
                   const BlobReferrer &base) const
{
    writer.Stream() << writer.ind() << '<' << element << " count=\"" << _files.size() << "\">\n";
    writer.incInd();
    const bool stored = writer.getSchemaVersion() >= 5;
    for (const auto &file : _files) {
        writer.Stream() << writer.ind() << "<File name=\"" << Base::Persistence::encodeAttribute(file.name) << "\"";
        if (stored) {
            // Noted again here for the reason the other referrers do it: it
            // costs nothing, and it keeps a set written through a path the
            // collect pass does not walk from losing its content.
            if (file.blob && manager) {
                manager->noteReferenced(file.blob, referrerFor(base, file));
            }
            writer.Stream() << " hash=\"" << Base::Persistence::encodeAttribute(file.hash) << "\"";
            if (!file.original.empty()) {
                writer.Stream() << " original=\"" << Base::Persistence::encodeAttribute(file.original) << "\"";
            }
        }
        writer.Stream() << "/>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</" << element << ">\n";
}

void FileSet::restore(Base::XMLReader &reader, const char *element)
{
    reader.readElement(element);
    const int count = reader.getAttributeAsInteger("count");
    std::vector<Entry> files;
    files.reserve(count);
    for (int i = 0; i < count; ++i) {
        reader.readElement("File");
        Entry file;
        file.name = reader.getAttribute("name");
        file.hash = reader.hasAttribute("hash") ? reader.getAttribute("hash") : "";
        file.original = reader.hasAttribute("original") ? reader.getAttribute("original") : "";
        files.push_back(std::move(file));
    }
    reader.readEndElement(element);
    _files = std::move(files);
}

unsigned int FileSet::getMemSize() const
{
    unsigned int mem = 0;
    for (const auto &file : _files) {
        mem += static_cast<unsigned int>(file.name.size() + file.original.size()
                                         + file.hash.size());
    }
    return mem;
}

bool FileSet::operator==(const FileSet &other) const
{
    if (_files.size() != other._files.size()) {
        return false;
    }
    for (std::size_t i = 0; i < _files.size(); ++i) {
        if (_files[i].name != other._files[i].name || _files[i].hash != other._files[i].hash) {
            return false;
        }
    }
    return true;
}

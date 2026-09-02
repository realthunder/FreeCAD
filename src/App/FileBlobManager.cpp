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

#include <atomic>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <numeric>
#include <sstream>
#include <tuple>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Base/Writer.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Uuid.h>

#include "FileBlobManager.h"
#include "Document.h"
#include "DocumentObject.h"
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

std::string FileBlobManager::relocatedPath(const FileBlobHandle& blob) const
{
    if (!blob) {
        return {};
    }
    // The transient directory is renamed with its contents, so the file is
    // still called what it was called; only the directory leading to it has
    // moved. Reading the name back off the stale path is what keeps this
    // independent of how the store names its files.
    return blobDir() + "/" + Base::FileInfo(blob->path()).fileName();
}

void FileBlobManager::relocate()
{
    for (const auto& blob : blobs()) {
        if (Base::FileInfo(blob->path()).exists()) {
            continue;
        }
        Base::FileInfo moved(relocatedPath(blob));
        if (moved.exists()) {
            repath(blob, moved.filePath());
        }
    }
}

namespace
{
uint64_t fileSize(const char* path)
{
    return Base::FileInfo(path).size();
}

/** Is \a name the name `stem` + `ext` would produce, suffix and all?
 *
 * A derived name carries a `-N` before its extension only to break a
 * collision. Telling a numbered variant of a name from a different name is
 * what lets the index pin a suffix: the name is still this referrer's, so it
 * is kept rather than reassigned by scan order.
 */
bool sameBaseName(const std::string& name, const std::string& stem, const std::string& ext)
{
    if (name.size() < stem.size() + ext.size()) {
        return false;
    }
    if (name.compare(0, stem.size(), stem) != 0) {
        return false;
    }
    if (name.compare(name.size() - ext.size(), ext.size(), ext) != 0) {
        return false;
    }
    const std::string middle = name.substr(stem.size(), name.size() - stem.size() - ext.size());
    if (middle.empty()) {
        return true;
    }
    if (middle.size() < 2 || middle[0] != '-') {
        return false;
    }
    return std::all_of(middle.begin() + 1, middle.end(), [](unsigned char chr) {
        return std::isdigit(chr) != 0;
    });
}

std::string numberedName(const std::string& stem, const std::string& ext, int number)
{
    return stem + "-" + std::to_string(number) + ext;
}

/** Is this safe to append to a derived name?
 *
 * The extension comes from the name a property was given, which comes from
 * Python and is not a file name the store chose -- so it is taken only when it
 * cannot turn a name into a path. Nothing is lost by dropping a strange one:
 * the extension is a courtesy to whatever opens the file, and identity is the
 * hash either way.
 */
bool isPlainExtension(const std::string& ext)
{
    return !ext.empty() && ext.size() <= 16
        && std::all_of(ext.begin(), ext.end(), [](unsigned char chr) {
               return std::isalnum(chr) != 0 || chr == '_' || chr == '-';
           });
}

/// Names a save wrote before the content index existed, which are the SHA-1
/// of what they hold and so identifiable as nothing else in the directory is.
bool isContentAddressedName(const std::string& name)
{
    return name.size() == 40
        && std::all_of(name.begin(), name.end(), [](unsigned char chr) {
               return std::isxdigit(chr) != 0;
           });
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

std::string FileBlobManager::hashBytes(const std::string& bytes)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())));
    return hash.result().toHex().constData();
}

const char* FileBlobManager::archivePrefix()
{
    return "blobs/";
}

const char* FileBlobManager::indexName()
{
    return "Content.xml";
}

BlobReferrer FileBlobManager::referrerOf(const Property* prop, const DocumentObject* object)
{
    BlobReferrer referrer;
    if (!prop) {
        return referrer;
    }

    // Asked of the interface, not of one concrete class: a property that
    // stores a file knows what it holds, and a list of concrete types here is
    // a list of what gets a useful name -- everything left off it silently
    // gets none. PropertyFileIncluded answers with the extension of the name
    // the user gave the file, so the derived name says what the file is
    // without inheriting a name that can change under it.
    if (auto referrerProp = dynamic_cast<const BlobReferrerProperty*>(prop)) {
        const std::string ext = referrerProp->blobExtension();
        if (isPlainExtension(ext)) {
            referrer.ext = "." + ext;
        }
    }

    if (!object) {
        object = Base::freecad_dynamic_cast<DocumentObject>(prop->getContainer());
    }
    if (!object || !object->isAttachedToDocument()) {
        // No object anchors a name -- a document-level property, or a view's
        // own property. Both still have one: the property names itself, and
        // getPersistentName() is what the container is called in its own
        // document across a save and a reload. That is not getFullName():
        // a view's carries an id from a process-wide counter, and the same
        // document saved id 5 in one session and 4 in the next, so a name
        // built from it would move the file on every save. A view's
        // persistent name is stored in the document and given back to it,
        // so View1.Render_PBREnvImageData stays that view's file however
        // many views the document has.
        if (prop->hasName()) {
            const std::string container =
                prop->getContainer() ? prop->getContainer()->getPersistentName()
                                     : std::string();
            referrer.name = container.empty()
                ? prop->getName()
                : container + "." + prop->getName();
        }
        return referrer;
    }

    referrer.id = object->getID();
    // getFileName() is Object.Property for an object's property and
    // Object.ViewObject.Property for a view provider's, so the marker that
    // keeps the two tiers from colliding on one name comes for free.
    referrer.name = prop->getFileName();
    return referrer;
}

void FileBlobManager::beginSave(Base::Writer& writer)
{
    // Declared before the lock, so it dies after the lock is released.
    // Dropping the last reference to a blob runs ~FileBlob, which calls
    // release() and takes this same mutex -- and the save set really can hold
    // the last reference, to content a property has since replaced. Doing
    // that under the lock deadlocks the thread against itself.
    std::unordered_map<std::string, FileBlobHandle> expiring;
    // Process-wide, so no two saves anywhere share a number -- see
    // saveGeneration().
    static std::atomic<uint64_t> saves {0};
    std::lock_guard<std::mutex> guard(_mutex);
    _generation = ++saves;
    expiring.swap(_saveSet);
    // Referrers are recomputed from scratch every save. Nothing is carried
    // forward, so a deleted object cannot leave a name behind it.
    _saveRefs.clear();
    _saveExts.clear();
    // Decided once, before a single referrer has been written. Below schema 5
    // the properties still carry their own copies; above ForceXML level 3 the
    // caller wants a document that carries its content inside the XML, which
    // is a different place to put the same table, not a reason to give up
    // sharing it.
    if (writer.getSchemaVersion() < 5) {
        _format = BlobFormat::None;
    }
    else if (writer.isForceXML() > 3) {
        _format = BlobFormat::InlineXml;
    }
    else {
        _format = BlobFormat::Entries;
    }
}

uint64_t FileBlobManager::saveGeneration() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _generation;
}

FileBlobManager::BlobFormat FileBlobManager::blobFormat() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _format;
}

bool FileBlobManager::hasInlineBlobs() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _format == BlobFormat::InlineXml && !_saveSet.empty();
}

void FileBlobManager::noteReferenced(const FileBlobHandle& blob, const BlobReferrer& referrer)
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

    // What the content is, kept apart from who refers to it: a referrer
    // that cannot name the file can still say what kind of file it is,
    // and a blob named by its hash has nowhere else to get that from.
    if (!referrer.ext.empty()) {
        auto& ext = _saveExts[blob->hash()];
        if (ext.empty()) {
            ext = referrer.ext;
        }
    }

    if (referrer.name.empty()) {
        // A caller that cannot say who is referring -- a property writing
        // itself out through a path the collect pass does not walk -- still
        // keeps the content in the save set. It just does not get to name it.
        return;
    }
    auto& referrers = _saveRefs[blob->hash()];
    // Shared content is one file with several referrers, and the same
    // property can be noted twice: once by the collect pass and once by its
    // own Save().
    if (std::none_of(referrers.begin(), referrers.end(), [&referrer](const BlobReferrer& other) {
            return other.id == referrer.id && other.name == referrer.name;
        })) {
        referrers.push_back(referrer);
    }
}

void FileBlobManager::dropReferenced(const FileBlobHandle& blob)
{
    if (!blob) {
        return;
    }
    // Released after the lock, as in beginSave(): this may hold the last
    // reference, and ~FileBlob takes the same mutex.
    FileBlobHandle expiring;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        auto it = _saveSet.find(blob->hash());
        if (it == _saveSet.end() || it->second != blob) {
            return;
        }
        expiring = std::move(it->second);
        _saveSet.erase(it);
        _saveRefs.erase(blob->hash());
    }
}

std::vector<FileBlobHandle> FileBlobManager::collected() const
{
    std::vector<FileBlobHandle> pending;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        pending.reserve(_saveSet.size());
        for (const auto& entry : _saveSet) {
            pending.push_back(entry.second);
        }
    }
    // Hash order, so the same document always produces the same file.
    std::sort(pending.begin(), pending.end(),
              [](const FileBlobHandle& a, const FileBlobHandle& b) {
                  return a->hash() < b->hash();
              });
    return pending;
}

std::vector<FileBlobManager::SaveEntry>
FileBlobManager::planSave(const std::map<std::string, BlobIndexEntry>& previous) const
{
    const std::vector<FileBlobHandle> pending = collected();

    std::unordered_map<std::string, std::vector<BlobReferrer>> refs;
    std::unordered_map<std::string, std::string> knownExts;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        refs = _saveRefs;
        knownExts = _saveExts;
    }

    std::vector<SaveEntry> entries;
    std::vector<std::string> stems;
    std::vector<std::string> exts;
    entries.reserve(pending.size());
    stems.reserve(pending.size());
    exts.reserve(pending.size());

    for (const auto& blob : pending) {
        std::vector<BlobReferrer> mine;
        auto found = refs.find(blob->hash());
        if (found != refs.end()) {
            mine = found->second;
        }
        // An object behind the referrer wins the name: an unanchored one
        // carries id 0 and would otherwise sort first and take the name of
        // content it merely shares -- Box.Shape is the better name for a
        // shape a view property happens to hold a copy of.
        std::sort(mine.begin(), mine.end(), [](const BlobReferrer& a, const BlobReferrer& b) {
            return std::make_tuple(a.id == 0, a.id, a.name)
                 < std::make_tuple(b.id == 0, b.id, b.name);
        });

        SaveEntry entry;
        entry.blob = blob;
        for (const auto& referrer : mine) {
            entry.referrers.push_back(std::to_string(referrer.id) + ":" + referrer.name);
        }

        // The name belongs to the *naming referrer* -- the live referrer with
        // the lowest object id -- and not to the referrer set. A rule that
        // kept a name while any referrer survived would let a blob shared by
        // Box and Cyl keep the name Box.Image.png after Box was deleted, and
        // internal names are reused, so a newly created Box would find its
        // rightful name squatted on. Re-deriving means the two generations
        // can never be live in the same save.
        std::string stem;
        std::string ext;
        if (!mine.empty()) {
            stem = mine.front().name;
            ext = mine.front().ext;
        }
        if (stem.empty()) {
            // Nothing can name it -- a view's own property, or a caller that
            // did not say who was referring. Content addressing is then all
            // there is, and a hash at least does not move while the content
            // does not. The extension stays: a name says nothing about the
            // content, but the file still has to be openable by whoever
            // reads it back, and a stored environment image that arrived
            // as a bare hash was taken for something Qt could read.
            stem = blob->hash();
            auto known = knownExts.find(blob->hash());
            ext = known != knownExts.end() ? known->second : std::string();
        }
        // *** The stem is Object.Property, and both halves are named by
        // whoever made them: an object's name only has to be a Python
        // identifier, which admits any length and every script name Windows
        // reads as a device, and a property's name admits the same. A
        // directory project writes this as a real file, so a name the file
        // system refuses loses the geometry with nothing but a line in the
        // report view (docs/SharedShapeStorage.md sec 12.1). A name that was
        // already fine is unchanged, so no existing project's files move.
        stem = Base::Tools::portableFileName(stem, 120 - ext.size());
        stems.push_back(std::move(stem));
        exts.push_back(std::move(ext));
        entries.push_back(std::move(entry));
    }

    // Assign in referrer order rather than in the content order collected()
    // hands back: which name a collision resolves to must not depend on some
    // other file's bytes having changed.
    std::vector<std::size_t> order(entries.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return std::tie(stems[a], exts[a], entries[a].blob->hash())
            < std::tie(stems[b], exts[b], entries[b].blob->hash());
    });

    std::set<std::string> taken;
    // A name the previous index gave to this exact content is kept as it
    // stands, suffix included. That is what pins a collision suffix: without
    // it the numbering follows scan order and can migrate between saves,
    // which is the one way a stable name still produces a spurious diff.
    for (std::size_t i : order) {
        for (const auto& prev : previous) {
            if (prev.second.hash != entries[i].blob->hash()) {
                continue;
            }
            if (!sameBaseName(prev.first, stems[i], exts[i])) {
                continue;
            }
            if (!taken.insert(prev.first).second) {
                continue;
            }
            entries[i].name = prev.first;
            break;
        }
    }
    for (std::size_t i : order) {
        if (!entries[i].name.empty()) {
            continue;
        }
        std::string candidate = stems[i] + exts[i];
        for (int number = 1; !taken.insert(candidate).second; ++number) {
            candidate = numberedName(stems[i], exts[i], number);
        }
        entries[i].name = std::move(candidate);
    }

    std::sort(entries.begin(), entries.end(), [](const SaveEntry& a, const SaveEntry& b) {
        return a.name < b.name;
    });
    return entries;
}

std::map<std::string, BlobIndexEntry> FileBlobManager::readIndex(const std::string& path)
{
    std::map<std::string, BlobIndexEntry> index;
    Base::FileInfo fi(path);
    if (!fi.exists()) {
        return index;
    }

    try {
        Base::ifstream from(fi, std::ios::in | std::ios::binary);
        if (!from) {
            return index;
        }
        Base::XMLReader reader(path.c_str(), from);
        if (!reader.isValid()) {
            return index;
        }
        // No element count: two branches that each add a file must merge
        // textually, and a count is exactly the line they would both edit.
        while (reader.readNextElement()) {
            if (std::strcmp(reader.localName(), "F") != 0) {
                continue;
            }
            if (!reader.hasAttribute("n") || !reader.hasAttribute("h")) {
                continue;
            }
            BlobIndexEntry entry;
            entry.hash = reader.getAttribute("h");
            if (reader.hasAttribute("r")) {
                std::istringstream tokens(reader.getAttribute("r"));
                std::string token;
                while (tokens >> token) {
                    entry.referrers.push_back(token);
                }
            }
            index[reader.getAttribute("n")] = std::move(entry);
        }
    }
    catch (const Base::Exception& e) {
        // An unreadable index is not an error: it only means this save cannot
        // keep the names the last one chose, and has to derive them again.
        FC_WARN("Failed to read " << path << ": " << e.what());
        index.clear();
    }
    return index;
}

void FileBlobManager::writeIndex(Base::Writer& writer, const std::vector<SaveEntry>& entries)
{
    writer.putNextEntry((std::string(archivePrefix()) + indexName()).c_str());
    std::ostream& str = writer.Stream();
    str << "<?xml version='1.0' encoding='utf-8'?>\n"
        << "<FileStore v=\"1\">\n";
    // One element per line and sorted by name, so a content edit is a one-line
    // diff and two branches touching different parts of a project merge.
    for (const auto& entry : entries) {
        str << "  <F n=\"" << Base::Persistence::encodeAttribute(entry.name) << "\" h=\""
            << entry.blob->hash() << "\" r=\"";
        for (std::size_t i = 0; i < entry.referrers.size(); ++i) {
            str << (i ? " " : "") << entry.referrers[i];
        }
        str << "\"/>\n";
    }
    str << "</FileStore>\n";
}

void FileBlobManager::prune(const std::string& dir,
                            const std::map<std::string, BlobIndexEntry>& previous,
                            const std::set<std::string>& kept)
{
    for (const auto& entry : previous) {
        if (kept.count(entry.first)) {
            continue;
        }
        Base::FileInfo fi(dir + entry.first);
        if (fi.exists()) {
            fi.deleteFile();
        }
    }

    // Files a save wrote before the index existed are named by their content
    // hash. They are ours by construction, and nothing else can identify them
    // once the names have moved -- so they are the one thing outside the
    // previous index that is removed. Without this an upgraded project keeps
    // every orphan it ever accumulated, and `git add -A` commits them all.
    QDir source(QString::fromUtf8(dir.c_str()));
    if (!source.exists()) {
        return;
    }
    for (const QFileInfo& info : source.entryInfoList(QDir::Files)) {
        const std::string name = info.fileName().toUtf8().constData();
        if (kept.count(name) || !isContentAddressedName(name)) {
            continue;
        }
        Base::FileInfo(dir + name).deleteFile();
    }
}

void FileBlobManager::writeBlobs(Base::Writer& writer)
{
    // Entries are one of the two places the content can go; writeInlineBlobs()
    // has already written it if the answer was the other one. Below schema 5
    // there is no table at all and the properties carry their own copies.
    if (blobFormat() != BlobFormat::Entries) {
        return;
    }

    // Saving under a new name gives the document a new transient directory,
    // which leaves every stored path stale. Repaired here rather than in the
    // property, because the properties of the view tier are written after this
    // point and would be repaired too late.
    relocate();

    auto* fileWriter = dynamic_cast<Base::FileWriter*>(&writer);
    std::string dir;
    std::map<std::string, BlobIndexEntry> previous;
    if (fileWriter) {
        dir = fileWriter->getDirName() + "/" + archivePrefix();
        // Read the index out of the directory being written, rather than
        // remembering the one this document last wrote: a save-as writes
        // somewhere else, where what is on disk is the only thing that says
        // what the names there mean. An archive has no previous state to
        // read, and rewrites every entry.
        previous = readIndex(dir + indexName());
    }

    const std::vector<SaveEntry> entries = planSave(previous);
    if (entries.empty() && previous.empty()) {
        // Nothing to write and nothing to take back: leave the directory
        // without a blobs/ in it at all.
        return;
    }

    // A directory writer needs the subdirectory to exist before an entry can
    // be opened inside it.
    if (fileWriter) {
        Base::FileInfo(dir).createDirectory();
    }

    // The index first, so that what is here can be known without reading the
    // content -- which is what a deferred read by name will need.
    writeIndex(writer, entries);

    // The entries are where a save of a large document spends its bytes -- a
    // building's worth of shapes is thousands of them -- so this is a phase
    // the progress indicator has to see. Ticked before the skip below, so a
    // save that rewrites nothing still walks the bar to the end.
    const std::size_t progressBase = writer.progressBase();
    std::set<std::string> kept;
    for (const auto& entry : entries) {
        writer.stepProgress(progressBase, entries.size());
        kept.insert(entry.name);
        if (fileWriter) {
            // Names are derived now, so a file existing proves nothing about
            // what is in it. Only the index does: it recorded what this name
            // held when it was last written, and equal hashes over an
            // existing file is the whole proof. That is what keeps repeated
            // saves of a document with a large embedded file cheap.
            auto found = previous.find(entry.name);
            if (found != previous.end() && found->second.hash == entry.blob->hash()
                && Base::FileInfo(dir + entry.name).exists()) {
                continue;
            }
        }
        Base::ifstream from(Base::FileInfo(entry.blob->path()), std::ios::in | std::ios::binary);
        if (!from) {
            std::stringstream str;
            str << "FileBlobManager::writeBlobs(): file '" << entry.blob->path()
                << "' in transient directory doesn't exist.";
            THROWM(Base::FileSystemError, str.str())
        }
        writer.putNextEntry((std::string(archivePrefix()) + entry.name).c_str());
        writer.Stream() << from.rdbuf();
    }

    if (fileWriter) {
        prune(dir, previous, kept);
    }
}

void FileBlobManager::writeInlineBlobs(Base::Writer& writer)
{
    if (blobFormat() != BlobFormat::InlineXml) {
        return;
    }

    std::vector<FileBlobHandle> pending = collected();
    if (pending.empty()) {
        return;
    }

    // Same reason as in writeBlobs(): a save under a new name has moved the
    // files, and the view tier is written after this point.
    relocate();

    writer.Stream() << writer.ind() << "<Blobs Count=\"" << pending.size() << "\">\n";
    writer.incInd();
    for (const auto& blob : pending) {
        if (!Base::FileInfo(blob->path()).exists()) {
            std::stringstream str;
            str << "FileBlobManager::writeInlineBlobs(): file '" << blob->path()
                << "' in transient directory doesn't exist.";
            THROWM(Base::FileSystemError, str.str())
        }
        writer.Stream() << writer.ind() << "<Blob hash=\"" << blob->hash() << "\" size=\""
                        << blob->size() << "\">\n";
        writer.insertBinFile(blob->path().c_str());
        writer.Stream() << writer.ind() << "</Blob>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Blobs>\n";
}

void FileBlobManager::restoreInlineBlobs(Base::XMLReader& reader)
{
    reader.readElement("Blobs");
    const int count = reader.getAttributeAsInteger("Count");
    for (int i = 0; i < count; ++i) {
        reader.readElement("Blob");
        const std::string hash = reader.getAttribute("hash");
        const std::string staging = uniquePath("blob.part");
        reader.readBase64(staging.c_str());
        reader.readEndElement("Blob");
        try {
            // Stored under the hash of what actually arrived, not the one the
            // file claims: the properties looking it up were written from the
            // same content, so agreeing with them is what matters.
            // The inline table carries a hash and a size, and nothing that
            // says what the content is -- so the store keeps it unnamed.
            FileBlobHandle blob = adoptFile(staging.c_str(), "");
            if (blob && blob->hash() != hash) {
                FC_WARN("Included file " << hash << " does not match its content");
            }
            hold(blob);
        }
        catch (const Base::Exception& e) {
            FC_ERR("Failed to read included file " << hash << ": " << e.what());
        }
    }
    reader.readEndElement("Blobs");
}

void FileBlobManager::beginRestore(Base::XMLReader& reader)
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _pending.clear();
        _restoreClosed = false;
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

    // The name predicate doubles as the ArchiveFilter: a random-access
    // reader asks it before paying to open an entry.
    auto wanted = [](const std::string& name) {
        return name.compare(0, std::strlen(archivePrefix()), archivePrefix()) == 0;
    };
    reader.setArchiveHandler([this, wanted](const std::string& name, Base::Reader& entry) {
        if (!wanted(name)) {
            return false;
        }
        try {
            readBlobEntry(name, entry);
        }
        catch (const Base::Exception& e) {
            FC_ERR("Failed to read included file " << name << ": " << e.what());
        }
        return true;
    }, wanted);
}

void FileBlobManager::readBlobEntry(const std::string& name, Base::Reader& entry)
{
    // The index sits among the content it describes and is claimed with it,
    // so that no other consumer is offered it. It says nothing a restore
    // needs -- identity is the hash in Document.xml -- and is read again from
    // the target directory when the next save needs to know the names there.
    if (name == std::string(archivePrefix()) + indexName()) {
        return;
    }

    const std::string staging = uniquePath("blob.part");
    {
        Base::ofstream to(Base::FileInfo(staging), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!to) {
            std::stringstream str;
            str << "FileBlobManager: cannot create " << staging;
            THROWM(Base::FileSystemError, str.str())
        }
        // ***Written, not extracted.*** `entry >> to.rdbuf()` is a formatted
        // extraction: its sentry skips leading whitespace, so content that
        // begins with any would arrive one or more bytes short. Content
        // addressing turns that from a quiet corruption into a blob whose
        // hash no longer matches the one the document refers to, and the
        // referrer is served nothing at all -- which is exactly what ASCII
        // BRep, whose first byte is a newline, hit.
        to << entry.rdbuf();
    }

    // adoptFile() hashes the content and relocates it, so the entry name is
    // never trusted for anything but the extension: what the archive claims
    // and what it holds are checked against each other by construction. It
    // also drops content that is already stored, which is what makes
    // reopening a document cheap.
    auto blob = adoptFile(staging.c_str(), Base::FileInfo(name).extension().c_str());
    FC_TRACE("blob entry " << name << " -> " << (blob ? blob->hash() : std::string("(none)")));
    hold(std::move(blob));
}

void FileBlobManager::restoreFromDirectory(const std::string& dir)
{
    QDir source(QString::fromUtf8(dir.c_str()));
    if (!source.exists()) {
        return;
    }
    for (const QFileInfo& info : source.entryInfoList(QDir::Files)) {
        if (info.fileName() == QString::fromUtf8(indexName())) {
            // Describes the content rather than being any; see readBlobEntry().
            continue;
        }
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

void FileBlobManager::addPendingReferrer(const std::string& hash, BlobReferrerProperty* prop)
{
    if (hash.empty() || !prop) {
        return;
    }
    // Content read earlier in this restore, or left over from one before it,
    // can be handed over at once. Everything else waits for the drain.
    if (auto blob = find(hash)) {
        FC_TRACE("referrer served at once from " << hash);
        prop->assignRestoredBlob(blob);
        return;
    }
    FC_TRACE("referrer queued for " << hash);
    std::lock_guard<std::mutex> guard(_mutex);
    if (_restoreClosed) {
        // Nothing will dispatch this: the hold is gone and dispatchPending()
        // has already run for the last time. A tier restoring this late has
        // to be restored inside the load instead -- Gui::Document does that
        // for a view provider whose record names a blob. Queuing it would
        // leave the property empty without a word, which is how this went
        // unnoticed once already.
        FC_WARN("Included file " << hash << " requested after the restore closed");
        return;
    }
    _pending.emplace_back(hash, prop);
}

void FileBlobManager::removePendingReferrer(BlobReferrerProperty* prop)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                                  [prop](const auto& entry) { return entry.second == prop; }),
                   _pending.end());
}

void FileBlobManager::dispatchPending()
{
    std::vector<std::pair<std::string, BlobReferrerProperty*>> pending;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        pending.swap(_pending);
    }
    // One absent blob is one defect in the document however many properties
    // name it, so it is counted rather than warned about per referrer. A
    // stock material card is the case that matters: it is deliberately not
    // written (docs/MaterialStorage.md sec 5), so if the installed library
    // can no longer produce its content every object sharing it lands here
    // at once -- 13642 of them in one IFC building, which is 13642 Console
    // dispatches into the report view saying the same sentence.
    // The archive is drained, so a blob still not found is not late, it is
    // absent -- which is what blobUnavailable() tells the property, and its
    // chance to stand in for the content rather than lose the value.
    std::map<std::string, std::pair<std::size_t, std::size_t>> missing;
    for (const auto& entry : pending) {
        if (auto blob = find(entry.first)) {
            entry.second->assignRestoredBlob(blob);
        }
        else {
            auto& counts = missing[entry.first];
            ++counts.first;
            if (entry.second->blobUnavailable()) {
                ++counts.second;
            }
        }
    }
    for (const auto& entry : missing) {
        const auto total = entry.second.first;
        const auto stood_in = entry.second.second;
        std::ostringstream str;
        str << "Included file " << entry.first << " is missing from the document";
        if (total > 1) {
            str << ", named by " << total << " properties";
        }
        if (stood_in == total) {
            str << (total > 1 ? "; all of them stood in for it" : "; it was stood in for");
        }
        else if (stood_in) {
            str << "; " << stood_in << " of them stood in for it";
        }
        FC_WARN(str.str());
    }
}

void FileBlobManager::endRestore()
{
    std::unordered_map<std::string, FileBlobHandle> hold;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _pending.clear();
        hold.swap(_restoreHold);
        // From here a referrer arriving is a bug in whatever produced it, not
        // something to wait for; addPendingReferrer() says so out loud.
        _restoreClosed = true;
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

std::string FileBlobManager::newBlobPath(const char* extension) const
{
    // A uuid, not the content hash: at insertFile() time there may be no
    // referrer to name the file after, the store is a cache nothing outside
    // may address by path, and a stored file is read-only and held by
    // absolute path -- so renaming one mid-session would be churn for
    // nothing. The extension is kept because the path is handed to whatever
    // consumes the content, and a file with no extension is a file some
    // viewers will not open.
    const std::string dir = blobDir();
    // Whatever the caller passed, it becomes part of a path here, and the
    // callers upstream of this take it from a name that came from Python.
    std::string ext = extension ? extension : "";
    if (!ext.empty() && ext[0] == '.') {
        ext.erase(ext.begin());
    }
    ext = isPlainExtension(ext) ? "." + ext : std::string();
    Base::FileInfo fi;
    do {
        Base::Uuid uuid;
        fi.setFile(dir + "/" + uuid.getValue() + ext);
    } while (fi.exists());
    return fi.filePath();
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

FileBlobHandle FileBlobManager::insertFile(const char* srcPath, const char* extension)
{
    Base::FileInfo src(srcPath);
    if (!src.exists()) {
        std::stringstream str;
        str << "FileBlobManager: file " << srcPath << " does not exist.";
        THROWM(Base::FileSystemError, str.str())
    }

    const std::string hash = hashFile(srcPath);
    if (hash.empty()) {
        std::stringstream str;
        str << "FileBlobManager: cannot read " << srcPath << " to hash it.";
        THROWM(Base::FileSystemError, str.str())
    }

    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(hash);
    if (it != _blobs.end()) {
        if (auto existing = it->second.lock()) {
            // Content already stored: share it, whatever the caller calls it.
            return existing;
        }
    }

    // The name the referrer will store this under decides the extension when
    // there is one: the source is often a scratch file whose name says less
    // about the content than the name the property gives it.
    std::string ext = extension ? extension : src.extension();
    if (!ext.empty() && ext[0] != '.') {
        ext.insert(ext.begin(), '.');
    }
    const std::string dst = newBlobPath(ext.empty() ? nullptr : ext.c_str());
    if (!src.copyTo(dst.c_str())) {
        std::stringstream str;
        str << "FileBlobManager: cannot copy " << srcPath << " to " << dst;
        THROWM(Base::FileSystemError, str.str())
    }

    // Blobs are immutable; read-only makes accidental in-place edits fail loudly
    // instead of silently changing every referrer's content.
    Base::FileInfo fi(dst);
    fi.setPermissions(Base::FileInfo::ReadOnly);

    return make(hash, dst, fileSize(dst.c_str()));
}

FileBlobHandle FileBlobManager::adoptFile(const char* path, const char* extension)
{
    Base::FileInfo fi(path);
    if (!fi.exists()) {
        std::stringstream str;
        str << "FileBlobManager: cannot adopt missing file " << path;
        THROWM(Base::FileSystemError, str.str())
    }

    const std::string hash = hashFile(path);
    if (hash.empty()) {
        std::stringstream str;
        str << "FileBlobManager: cannot read " << path << " to hash it.";
        THROWM(Base::FileSystemError, str.str())
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
    // at a staging path. Move it into the store. A scratch file is named
    // after what it holds, so its own extension is the right one to keep; a
    // staging path is not, and its caller passes what it knows instead --
    // which may be nothing, and an empty extension says exactly that.
    std::string ext = extension ? extension : fi.extension();
    if (!ext.empty() && ext[0] != '.') {
        ext.insert(ext.begin(), '.');
    }
    const std::string dst = newBlobPath(ext.empty() ? nullptr : ext.c_str());
    if (fi.filePath() != dst) {
        fi.setPermissions(Base::FileInfo::ReadWrite);
        if (!fi.renameFile(dst.c_str())) {
            std::stringstream str;
            str << "FileBlobManager: cannot rename " << fi.filePath() << " to " << dst;
            THROWM(Base::FileSystemError, str.str())
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

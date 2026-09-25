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
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <zlib.h>
#ifdef FC_HAVE_ZSTD
# include <zstd.h>
#endif

#include <cstdio>
#ifdef _WIN32
# include <io.h>
# include <windows.h>
#else
# include <fcntl.h>
# include <unistd.h>
#endif

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
#include "DocumentParams.h"
#include "PropertyFile.h"

FC_LOG_LEVEL_INIT("App", true, 2, true)

using namespace App;

// ---------------------------------------------------------------------------
// BlobArchive
// ---------------------------------------------------------------------------

namespace App
{

/// A zip member as an archive stores it: the compressed bytes, and what the
/// headers say about them.
struct BlobRawMember
{
    std::string name;
    int method {0};
    uint32_t crc {0};
    uint64_t size {0};
    std::string data;
};

/** A zip file read through its own central directory: a segment of the pack
 * store (docs/FileBlobsManager.md sec 15.7), or a document archive being split
 * into segments on open (sec 15.10).
 *
 * Restoring a blob as a file of its own costs a file create, and on a
 * filesystem watched by endpoint protection that is where a large document's
 * open goes: MiSTer.FCStd's 5401 entries took 51 s to write out on a managed
 * Windows laptop once every other round trip was gone (sec 14). So content
 * stays inside a few zip files until something needs a real file.
 *
 * One handle serves every read, since opening a file is itself a round trip
 * the monitor charges for. It is closed around anything that moves or deletes
 * the transient directory -- Windows refuses either while a file inside is
 * open -- and opened again by the next read.
 */
class BlobArchive
{
public:
    struct Entry
    {
        std::string name;
        uint64_t headerOffset {0};
        uint64_t compressedSize {0};
        uint64_t size {0};
        uint16_t method {0};
        uint32_t crc {0};
    };

    /// Index the central directory. Throws Base::FileException when the file
    /// is not a zip archive this can read. An \a owned file is the store's
    /// and is deleted with this object; a document being opened is not.
    BlobArchive(const std::string& path, bool owned);
    /// Deletes an owned file: nothing refers to its content any more.
    ~BlobArchive();

    BlobArchive(const BlobArchive&) = delete;
    BlobArchive& operator=(const BlobArchive&) = delete;

    const std::vector<Entry>& entries() const { return _entries; }
    std::string path() const;
    /// Follow the copy after the transient directory has moved.
    void setPath(const std::string& path);
    /// Content of one entry, decoded. False, and \a bytes empty, on failure.
    bool read(std::size_t index, std::string& bytes);
    /// One entry as stored, not decoded: what a raw copy writes.
    bool readRaw(std::size_t index, BlobRawMember& member);
    /// Let go of the file handle; the next read opens it again.
    void close();

private:
    bool open();
    bool readAt(uint64_t offset, char* data, std::size_t size);
    void index();

    mutable std::mutex _mutex;
    std::string _path;
    std::vector<Entry> _entries;
    Base::ifstream _file;
    bool _owned {true};
};

}  // namespace App

namespace
{
uint16_t le16(const unsigned char* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t le32(const unsigned char* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16)
        | (uint32_t(p[3]) << 24);
}

uint64_t le64(const unsigned char* p)
{
    return uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32);
}
}  // namespace

namespace
{
constexpr int zipStored = 0;
constexpr int zipDeflate = 8;
constexpr int zipZstd = 93;
/// The most members a segment holds: past it a zip needs zip64 records,
/// which a segment never writes.
constexpr std::size_t segmentMaxMembers = 65535;

void put16(std::string& out, uint32_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void put32(std::string& out, uint32_t value)
{
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}

/// The content hash a segment member is named by: its name up to the
/// extension.
std::string memberHash(const std::string& name)
{
    return name.substr(0, name.find('.'));
}

/// A file the store writes, flushed to the disk before it is closed.
class SegmentWriter
{
public:
    explicit SegmentWriter(const std::string& path)
        : _path(path)
    {
#ifdef _WIN32
        _file = _wfopen(Base::FileInfo(path).toStdWString().c_str(), L"wb");
#else
        _file = std::fopen(path.c_str(), "wb");
#endif
    }

    ~SegmentWriter()
    {
        if (_file) {
            std::fclose(_file);
            Base::FileInfo(_path).deleteFile();
        }
    }

    SegmentWriter(const SegmentWriter&) = delete;
    SegmentWriter& operator=(const SegmentWriter&) = delete;

    bool ok() const { return _file && _ok; }
    std::size_t count() const { return _count; }

    void add(const BlobRawMember& member)
    {
        if (!ok()) {
            return;
        }
        // No data descriptors, sizes in the local header: a segment whose
        // central directory is lost can still be rebuilt by a scan (15.7).
        // One fixed timestamp, 1980-01-01, so the same members make the same
        // bytes.
        const uint32_t version = member.method == zipZstd ? 63 : 20;
        std::string header;
        put32(header, 0x04034b50u);
        put16(header, version);
        put16(header, 0);
        put16(header, static_cast<uint32_t>(member.method));
        put16(header, 0);
        put16(header, 0x21);
        put32(header, member.crc);
        put32(header, static_cast<uint32_t>(member.data.size()));
        put32(header, static_cast<uint32_t>(member.size));
        put16(header, static_cast<uint32_t>(member.name.size()));
        put16(header, 0);
        header += member.name;

        put32(_directory, 0x02014b50u);
        put16(_directory, version);
        put16(_directory, version);
        put16(_directory, 0);
        put16(_directory, static_cast<uint32_t>(member.method));
        put16(_directory, 0);
        put16(_directory, 0x21);
        put32(_directory, member.crc);
        put32(_directory, static_cast<uint32_t>(member.data.size()));
        put32(_directory, static_cast<uint32_t>(member.size));
        put16(_directory, static_cast<uint32_t>(member.name.size()));
        put16(_directory, 0);
        put16(_directory, 0);
        put16(_directory, 0);
        put16(_directory, 0);
        put32(_directory, 0);
        put32(_directory, static_cast<uint32_t>(_offset));
        _directory += member.name;

        write(header);
        write(member.data);
        ++_count;
    }

    /// Central directory, then flush to the disk and close. False leaves
    /// nothing behind.
    bool finish()
    {
        if (!ok()) {
            return false;
        }
        const uint64_t start = _offset;
        write(_directory);
        std::string end;
        put32(end, 0x06054b50u);
        put16(end, 0);
        put16(end, 0);
        put16(end, static_cast<uint32_t>(_count));
        put16(end, static_cast<uint32_t>(_count));
        put32(end, static_cast<uint32_t>(_directory.size()));
        put32(end, static_cast<uint32_t>(start));
        put16(end, 0);
        write(end);
        if (_ok && std::fflush(_file) != 0) {
            _ok = false;
        }
#ifdef _WIN32
        if (_ok && _commit(_fileno(_file)) != 0) {
            _ok = false;
        }
#else
        if (_ok && ::fsync(fileno(_file)) != 0) {
            _ok = false;
        }
#endif
        const bool closed = std::fclose(_file) == 0;
        _file = nullptr;
        if (!_ok || !closed) {
            Base::FileInfo(_path).deleteFile();
            return false;
        }
        return true;
    }

private:
    void write(const std::string& bytes)
    {
        if (_ok && !bytes.empty()
            && std::fwrite(bytes.data(), 1, bytes.size(), _file) != bytes.size()) {
            _ok = false;
        }
        _offset += bytes.size();
    }

    std::string _path;
    std::FILE* _file {nullptr};
    std::string _directory;
    uint64_t _offset {0};
    std::size_t _count {0};
    bool _ok {true};
};

/** Make a new file's directory entry durable, where that is a separate step.
 *
 * On Windows the flush of the file itself covers it.
 */
void syncDirectoryOf(const std::string& path)
{
#ifndef _WIN32
    const std::string dir = Base::FileInfo(path).dirPath();
    const int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    (void)path;
#endif
}
}  // namespace

namespace App
{
/// Decode a member's bytes, whatever its method. False, \a bytes empty, when
/// the method is not one this build reads or the bytes are corrupt.
bool decodeMember(const BlobRawMember& member, std::string& bytes)
{
    bytes.clear();
    if (member.size > std::numeric_limits<std::size_t>::max() / 2) {
        return false;
    }
    if (member.method == zipStored) {
        if (member.data.size() != member.size) {
            return false;
        }
        bytes = member.data;
        return true;
    }
    if (member.method == zipDeflate) {
        if (member.size > std::numeric_limits<uInt>::max()
            || member.data.size() > std::numeric_limits<uInt>::max()) {
            return false;
        }
        bytes.resize(static_cast<std::size_t>(member.size));
        z_stream stream {};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            bytes.clear();
            return false;
        }
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(member.data.data()));
        stream.avail_in = static_cast<uInt>(member.data.size());
        stream.next_out = reinterpret_cast<Bytef*>(bytes.data());
        stream.avail_out = static_cast<uInt>(bytes.size());
        const int result = inflate(&stream, Z_FINISH);
        const bool done = result == Z_STREAM_END && stream.total_out == member.size;
        inflateEnd(&stream);
        if (!done) {
            bytes.clear();
        }
        return done;
    }
#ifdef FC_HAVE_ZSTD
    if (member.method == zipZstd) {
        bytes.resize(static_cast<std::size_t>(member.size));
        const size_t got =
            ZSTD_decompress(bytes.data(), bytes.size(), member.data.data(), member.data.size());
        if (ZSTD_isError(got) || got != member.size) {
            bytes.clear();
            return false;
        }
        return true;
    }
#endif
    return false;
}

/// Compress content into a member named \a name: zstd at level 3 where the
/// build has it (sec 15.7), else deflate.
std::shared_ptr<BlobRawMember> encodeMember(const std::string& bytes, const std::string& name)
{
    auto member = std::make_shared<BlobRawMember>();
    member->name = name;
    member->size = bytes.size();
    member->crc = static_cast<uint32_t>(
        crc32(0L, reinterpret_cast<const Bytef*>(bytes.data()), static_cast<uInt>(bytes.size())));
#ifdef FC_HAVE_ZSTD
    member->data.resize(ZSTD_compressBound(bytes.size()));
    const size_t n = ZSTD_compress(member->data.data(), member->data.size(), bytes.data(),
                                   bytes.size(), 3);
    if (!ZSTD_isError(n)) {
        member->data.resize(n);
        member->method = zipZstd;
        return member;
    }
#endif
    uLongf bound = compressBound(static_cast<uLong>(bytes.size()));
    member->data.resize(bound);
    z_stream stream {};
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = reinterpret_cast<Bytef*>(member->data.data());
    stream.avail_out = static_cast<uInt>(member->data.size());
    deflate(&stream, Z_FINISH);
    member->data.resize(stream.total_out);
    deflateEnd(&stream);
    member->method = zipDeflate;
    return member;
}
}  // namespace App

BlobArchive::BlobArchive(const std::string& path, bool owned)
    : _path(path)
    , _owned(owned)
{
    if (!open()) {
        throw Base::FileException("Cannot open archive copy", path.c_str());
    }
    index();
}

BlobArchive::~BlobArchive()
{
    close();
    if (!_owned) {
        return;
    }
    // Where it cannot be deleted -- on Windows, a file someone else has open
    // -- it is left, and the next open of the store deletes it (sec 15.7).
    Base::FileInfo fi(_path);
    if (fi.exists()) {
        fi.setPermissions(Base::FileInfo::ReadWrite);
        fi.deleteFile();
    }
}

void BlobArchive::index()
{
    // Read here rather than through zipios: all this needs is where each entry
    // is and how it is stored, and zipios' ZipFile does not read zip64, which
    // an archive past 4 GB has to be.
    _file.clear();
    _file.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(_file.tellg());
    const uint64_t tail = std::min<uint64_t>(fileSize, 22u + 65535u);
    if (tail < 22) {
        throw Base::FileException("Not a zip archive", _path.c_str());
    }
    std::vector<unsigned char> buf(static_cast<std::size_t>(tail));
    if (!readAt(fileSize - tail, reinterpret_cast<char*>(buf.data()), buf.size())) {
        throw Base::FileException("Cannot read the end of archive", _path.c_str());
    }
    // The last end-of-directory signature: the comment behind it may hold
    // anything, including something that looks like one.
    std::size_t eocd = std::string::npos;
    for (std::size_t i = buf.size() - 22 + 1; i-- > 0;) {
        if (le32(&buf[i]) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        throw Base::FileException("No zip central directory", _path.c_str());
    }
    uint64_t count = le16(&buf[eocd + 10]);
    uint64_t cdSize = le32(&buf[eocd + 12]);
    uint64_t cdOffset = le32(&buf[eocd + 16]);
    if (count == 0xFFFFu || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
        // zip64: the real numbers are in a record the locator points at, and
        // the locator sits right in front of the end-of-directory record.
        const uint64_t eocdAt = fileSize - tail + eocd;
        unsigned char locator[20];
        unsigned char record[56];
        if (eocdAt < sizeof(locator)
            || !readAt(eocdAt - sizeof(locator), reinterpret_cast<char*>(locator), sizeof(locator))
            || le32(locator) != 0x07064b50u
            || !readAt(le64(locator + 8), reinterpret_cast<char*>(record), sizeof(record))
            || le32(record) != 0x06064b50u) {
            throw Base::FileException("Unreadable zip64 directory", _path.c_str());
        }
        count = le64(record + 32);
        cdSize = le64(record + 40);
        cdOffset = le64(record + 48);
    }
    if (cdOffset > fileSize || cdSize > fileSize - cdOffset) {
        throw Base::FileException("Zip central directory out of range", _path.c_str());
    }

    std::vector<unsigned char> cd(static_cast<std::size_t>(cdSize));
    if (!cd.empty() && !readAt(cdOffset, reinterpret_cast<char*>(cd.data()), cd.size())) {
        throw Base::FileException("Cannot read zip central directory", _path.c_str());
    }
    _entries.reserve(static_cast<std::size_t>(std::min<uint64_t>(count, cd.size() / 46)));
    std::size_t pos = 0;
    while (pos + 46 <= cd.size() && le32(&cd[pos]) == 0x02014b50u) {
        const unsigned char* header = &cd[pos];
        const std::size_t nameLen = le16(header + 28);
        const std::size_t extraLen = le16(header + 30);
        const std::size_t commentLen = le16(header + 32);
        if (pos + 46 + nameLen + extraLen + commentLen > cd.size()) {
            break;
        }
        Entry entry;
        entry.method = le16(header + 10);
        entry.crc = le32(header + 16);
        entry.compressedSize = le32(header + 20);
        entry.size = le32(header + 24);
        entry.headerOffset = le32(header + 42);
        entry.name.assign(reinterpret_cast<const char*>(header + 46), nameLen);
        // zip64 extended information carries, in this order, whichever of
        // the three 32-bit fields above are saturated -- and only those.
        const unsigned char* extra = header + 46 + nameLen;
        const unsigned char* extraEnd = extra + extraLen;
        while (extra + 4 <= extraEnd) {
            const uint16_t id = le16(extra);
            const uint16_t len = le16(extra + 2);
            const unsigned char* field = extra + 4;
            if (field + len > extraEnd) {
                break;
            }
            if (id == 0x0001) {
                const unsigned char* value = field;
                const unsigned char* valueEnd = field + len;
                for (uint64_t* slot : {&entry.size, &entry.compressedSize, &entry.headerOffset}) {
                    if (*slot == 0xFFFFFFFFu && value + 8 <= valueEnd) {
                        *slot = le64(value);
                        value += 8;
                    }
                }
            }
            extra = field + len;
        }
        _entries.push_back(std::move(entry));
        pos += 46 + nameLen + extraLen + commentLen;
    }
}

std::string BlobArchive::path() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _path;
}

void BlobArchive::setPath(const std::string& path)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _file.close();
    _path = path;
}

void BlobArchive::close()
{
    std::lock_guard<std::mutex> guard(_mutex);
    _file.close();
    _file.clear();
}

bool BlobArchive::open()
{
    if (_file.is_open()) {
        return true;
    }
    _file.clear();
    _file.open(Base::FileInfo(_path), std::ios::in | std::ios::binary);
    return _file.is_open();
}

bool BlobArchive::readAt(uint64_t offset, char* data, std::size_t size)
{
    _file.clear();
    _file.seekg(static_cast<std::streamoff>(offset));
    _file.read(data, static_cast<std::streamsize>(size));
    return !_file.fail() && static_cast<std::size_t>(_file.gcount()) == size;
}

bool BlobArchive::readRaw(std::size_t index, BlobRawMember& member)
{
    member.data.clear();
    std::lock_guard<std::mutex> guard(_mutex);
    if (index >= _entries.size() || !open()) {
        return false;
    }
    const Entry& entry = _entries[index];
    unsigned char local[30];
    if (!readAt(entry.headerOffset, reinterpret_cast<char*>(local), sizeof(local))
        || le32(local) != 0x04034b50u) {
        return false;
    }
    // The local header's own name and extra lengths, which the format lets
    // differ from the central directory's.
    const uint64_t data = entry.headerOffset + sizeof(local) + le16(local + 26) + le16(local + 28);
    if (entry.compressedSize > std::numeric_limits<std::size_t>::max() / 2) {
        return false;
    }
    member.name = entry.name;
    member.method = entry.method;
    member.crc = entry.crc;
    member.size = entry.size;
    member.data.resize(static_cast<std::size_t>(entry.compressedSize));
    if (!member.data.empty() && !readAt(data, member.data.data(), member.data.size())) {
        member.data.clear();
        return false;
    }
    return true;
}

bool BlobArchive::read(std::size_t index, std::string& bytes)
{
    bytes.clear();
    BlobRawMember member;
    return readRaw(index, member) && decodeMember(member, bytes);
}

// ---------------------------------------------------------------------------
// FileBlob
// ---------------------------------------------------------------------------

FileBlob::~FileBlob()
{
    if (_owner) {
        _owner->release(this);
    }
}

const std::string& FileBlob::path() const
{
    if (!_materialized.load(std::memory_order_acquire) && _owner) {
        _owner->materialize(*this);
    }
    return _path;
}

bool FileBlob::inArchive() const
{
    return !_materialized.load(std::memory_order_acquire);
}

bool FileBlob::inPack() const
{
    return _packed;
}

bool FileBlob::hasExtension(const char* ext) const
{
    if (inArchive()) {
        return Base::FileInfo("blob." + _ext).hasExtension(ext);
    }
    return Base::FileInfo(_path).hasExtension(ext);
}

std::string FileBlob::extension() const
{
    if (inArchive()) {
        return _ext;
    }
    return Base::FileInfo(_path).extension();
}

namespace
{
std::mutex& sourceReaderMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, FileBlobManager::SourceReader>& sourceReaders()
{
    static std::map<std::string, FileBlobManager::SourceReader> readers;
    return readers;
}
}  // namespace

void FileBlobManager::registerSourceReader(const char* ext, SourceReader reader)
{
    std::lock_guard<std::mutex> guard(sourceReaderMutex());
    sourceReaders()[ext ? ext : ""] = reader;
}

std::vector<std::string> FileBlob::sources() const
{
    FileBlobManager::SourceReader reader = nullptr;
    {
        std::lock_guard<std::mutex> guard(sourceReaderMutex());
        auto it = sourceReaders().find(extension());
        if (it != sourceReaders().end()) {
            reader = it->second;
        }
    }
    std::string bytes;
    if (!reader || !read(bytes)) {
        return {};
    }
    return reader(bytes);
}

bool FileBlob::read(std::string& bytes) const
{
    bytes.clear();
    if (inArchive()) {
        return _owner && _owner->readPacked(*this, bytes);
    }
    Base::ifstream from(Base::FileInfo(_path), std::ios::in | std::ios::binary);
    if (!from) {
        return false;
    }
    bytes.reserve(static_cast<std::size_t>(_size));
    std::vector<char> chunk(64u * 1024u);
    while (from.read(chunk.data(), static_cast<std::streamsize>(chunk.size()))
           || from.gcount() > 0) {
        bytes.append(chunk.data(), static_cast<std::size_t>(from.gcount()));
    }
    return true;
}

// ---------------------------------------------------------------------------
// FileBlobManager
// ---------------------------------------------------------------------------

FileBlobManager::FileBlobManager(Document* doc)
    : _doc(doc)
{}

FileBlobManager::~FileBlobManager()
{
    stopWorker();
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
    _unflushed.clear();
    // The segment files go with their last reader; the transient directory
    // they are in goes with the document.
    _segments.clear();
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
    // Content still in the archive copy has no file to have moved, and asking
    // for its path would write one.
    if (!blob || blob->inArchive()) {
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
    // The archive copies moved with the directory as well, and like the files
    // they keep their names: only the directory leading to them is stale.
    for (const auto& archive : liveArchives()) {
        const std::string current = archive->path();
        if (Base::FileInfo(current).exists()) {
            continue;
        }
        Base::FileInfo moved(blobDir() + "/" + Base::FileInfo(current).fileName());
        if (moved.exists()) {
            archive->setPath(moved.filePath());
        }
    }
    for (const auto& blob : blobs()) {
        if (blob->inArchive()) {
            continue;
        }
        if (Base::FileInfo(blob->path()).exists()) {
            continue;
        }
        Base::FileInfo moved(relocatedPath(blob));
        if (moved.exists()) {
            repath(blob, moved.filePath());
        }
    }
}

std::vector<std::shared_ptr<BlobArchive>> FileBlobManager::liveArchives() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    std::vector<std::shared_ptr<BlobArchive>> live;
    for (const auto& archive : _archives) {
        if (auto held = archive.lock()) {
            live.push_back(std::move(held));
        }
    }
    return live;
}

void FileBlobManager::closeArchives()
{
    for (const auto& archive : liveArchives()) {
        archive->close();
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

namespace
{
thread_local BlobRecorder* currentRecorder = nullptr;
}

BlobRecorder::BlobRecorder()
    : _previous(currentRecorder)
{
    currentRecorder = this;
}

BlobRecorder::~BlobRecorder()
{
    currentRecorder = _previous;
}

BlobRecorder* BlobRecorder::current()
{
    return currentRecorder;
}

void BlobRecorder::add(const FileBlobHandle& blob)
{
    if (std::none_of(_blobs.begin(), _blobs.end(),
                     [&blob](const FileBlobHandle& held) { return held == blob; })) {
        _blobs.push_back(blob);
    }
}

void FileBlobManager::noteReferenced(const FileBlobHandle& blob, const BlobReferrer& referrer)
{
    if (!blob) {
        return;
    }
    // A capture, not a save: record what the value names and leave the
    // save set to the save it belongs to.
    if (auto recorder = BlobRecorder::current()) {
        recorder->add(blob);
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

std::vector<std::pair<std::string, FileBlobHandle>> FileBlobManager::collectedEntries() const
{
    std::vector<std::pair<std::string, FileBlobHandle>> out;
    for (auto& entry : planSave({})) {
        out.emplace_back(std::move(entry.name), std::move(entry.blob));
    }
    return out;
}

std::vector<std::pair<std::string, FileBlobHandle>> FileBlobManager::restoredEntries() const
{
    std::vector<FileBlobHandle> live = blobs();
    std::sort(live.begin(), live.end(), [](const FileBlobHandle& a, const FileBlobHandle& b) {
        return a->hash() < b->hash();
    });
    std::unordered_map<std::string, std::string> names;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        names = _restoreNames;
    }
    std::vector<std::pair<std::string, FileBlobHandle>> out;
    std::set<std::string> taken;
    for (auto& blob : live) {
        auto it = names.find(blob->hash());
        std::string name;
        if (it != names.end() && taken.insert(it->second).second) {
            name = it->second;
        }
        else {
            // Content no entry named, or one sharing a name with other
            // content (two copies of an archive disagreeing): the hash.
            const std::string ext = blob->extension();
            name = blob->hash() + (ext.empty() ? "" : "." + ext);
            taken.insert(name);
        }
        out.emplace_back(std::move(name), std::move(blob));
    }
    return out;
}

void FileBlobManager::nameRestored(const FileBlobHandle& blob, const std::string& name)
{
    if (!blob) {
        return;
    }
    const std::string prefix = archivePrefix();
    std::lock_guard<std::mutex> guard(_mutex);
    _restoreNames[blob->hash()] =
        name.compare(0, prefix.size(), prefix) == 0 ? name.substr(prefix.size()) : name;
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

    // No repack or merge while a save reads the segments (sec 15.8).
    struct Saving
    {
        std::atomic<int>& count;
        explicit Saving(std::atomic<int>& count)
            : count(count)
        {
            ++count;
        }
        ~Saving()
        {
            --count;
        }
    } saving(_saving);

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
        const std::string entryName = std::string(archivePrefix()) + entry.name;
        if (entry.blob->inPack()) {
            // The member as the pack store holds it, compressed once in its
            // life: an archive takes it raw (sec 15.7), and only a writer
            // that cannot -- a project directory -- is handed it decoded.
            auto member = readMember(*entry.blob);
            if (!member) {
                std::stringstream str;
                str << "FileBlobManager::writeBlobs(): content " << entry.blob->hash()
                    << " cannot be read back out of the document's blob store.";
                THROWM(Base::FileSystemError, str.str())
            }
            Base::Writer::RawEntry raw;
            raw.method = member->method;
            raw.crc = member->crc;
            raw.size = member->size;
            raw.data = member->data.data();
            raw.compressedSize = member->data.size();
            if (writer.putRawEntry(entryName.c_str(), raw)) {
                continue;
            }
            std::string bytes;
            if (!decodeMember(*member, bytes)) {
                std::stringstream str;
                str << "FileBlobManager::writeBlobs(): content " << entry.blob->hash()
                    << " cannot be decoded.";
                THROWM(Base::FileSystemError, str.str())
            }
            writer.putNextEntry(entryName.c_str());
            writer.Stream().write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            continue;
        }
        Base::ifstream from(Base::FileInfo(entry.blob->path()), std::ios::in | std::ios::binary);
        if (!from) {
            std::stringstream str;
            str << "FileBlobManager::writeBlobs(): file '" << entry.blob->path()
                << "' in transient directory doesn't exist.";
            THROWM(Base::FileSystemError, str.str())
        }
        writer.putNextEntry(entryName.c_str());
        writer.Stream() << from.rdbuf();
    }

    if (fileWriter) {
        prune(dir, previous, kept);
    }

    // A save is one of the batches new content is written in (sec 15.7).
    try {
        flush();
    }
    catch (const Base::Exception& e) {
        FC_ERR("Blob store: " << e.what());
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
        _restoreNames.clear();
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

    // A zip read through its central directory can be copied whole and its
    // content served from the copy (restoreFromArchive()). Gated on the index,
    // which every save that writes blobs writes first, so a file without one
    // costs no copy.
    std::string archive;
    if (DocumentParams::getArchiveBlobStore()) {
        auto* zip = dynamic_cast<Base::ZipFileReader*>(reader.getReader());
        if (zip && zip->hasEntry(std::string(archivePrefix()) + indexName())) {
            archive = zip->getFileName();
        }
    }

    // ***Served when the index entry is reached, not here.*** The copy could
    // be made now, but the content would then be in the store before
    // Document.xml is parsed -- and a shape property's Restore() ends by
    // asking for its shape, which a blob already present answers with a
    // parse. That is the lazy load undone: it moved 3.3 s of MiSTer's shape
    // parsing into the open. readFiles() drains the unregistered entries before
    // anything else, and the index is written ahead of the content it
    // describes, so reaching it is exactly the moment the content used to
    // arrive.
    //
    // Once the copy is serving, the predicate turns every later blob entry
    // away, so a random-access reader does not even open them. If the copy
    // cannot be made they are read one file each, as before.
    auto served = std::make_shared<bool>(false);
    // The name predicate doubles as the ArchiveFilter: a random-access
    // reader asks it before paying to open an entry.
    auto wanted = [served](const std::string& name) {
        return !*served
            && name.compare(0, std::strlen(archivePrefix()), archivePrefix()) == 0;
    };
    reader.setArchiveHandler(
        [this, wanted, served, archive](const std::string& name, Base::Reader& entry) {
            if (!wanted(name)) {
                return false;
            }
            if (!archive.empty() && name == std::string(archivePrefix()) + indexName()) {
                *served = restoreFromArchive(archive);
                return true;
            }
            try {
                readBlobEntry(name, entry);
            }
            catch (const Base::Exception& e) {
                FC_ERR("Failed to read included file " << name << ": " << e.what());
            }
            return true;
        },
        wanted);
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

    const std::string ext = Base::FileInfo(name).extension();

    // ***Read, not extracted.*** sgetn() takes the bytes as they are, where
    // an `entry >> ...` extraction is formatted and its sentry skips leading
    // whitespace: content beginning with any would arrive short, and ASCII
    // BRep begins with a newline. Content addressing turned that from a quiet
    // corruption into a blob whose hash no longer matched what the document
    // referred to (sec 13.7 of docs/FileBlobsManager.md).
    //
    // Held in memory rather than staged in a file, because the bytes are what
    // decides whether anything has to be written at all: an entry the store
    // already holds now costs no file operation, and a new one costs exactly
    // the write. Staging cost a second file create, a read back to hash it, a
    // rename and two permission changes for every entry -- on Windows 61 ms
    // of the 85 an entry took, all of it filesystem round trips.
    constexpr std::size_t inMemoryCap = 16u * 1024u * 1024u;
    std::streambuf* source = entry.rdbuf();
    std::vector<char> chunk(64u * 1024u);
    std::string bytes;
    std::streamsize got = 0;
    while (bytes.size() <= inMemoryCap
           && (got = source->sgetn(chunk.data(),
                                   static_cast<std::streamsize>(chunk.size()))) > 0) {
        bytes.append(chunk.data(), static_cast<std::size_t>(got));
    }

    if (bytes.size() <= inMemoryCap) {
        auto blob = adoptBytes(bytes, ext.c_str());
        FC_TRACE("blob entry " << name << " -> " << (blob ? blob->hash() : std::string("(none)")));
        nameRestored(blob, name);
        hold(std::move(blob));
        return;
    }

    // An included file may be any size, so one too large to hold takes the
    // old path: what was read goes out first and the rest streams after it,
    // then adoptFile() hashes the file and moves it into place. The entry name
    // is still never trusted for anything but the extension -- what the
    // archive claims and what it holds are checked against each other by the
    // hash either way.
    const std::string staging = uniquePath("blob.part");
    {
        Base::ofstream to(Base::FileInfo(staging), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!to) {
            std::stringstream str;
            str << "FileBlobManager: cannot create " << staging;
            THROWM(Base::FileSystemError, str.str())
        }
        to.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        to << source;
    }
    auto blob = adoptFile(staging.c_str(), ext.c_str());
    FC_TRACE("blob entry " << name << " -> " << (blob ? blob->hash() : std::string("(none)")));
    nameRestored(blob, name);
    hold(std::move(blob));
}

bool FileBlobManager::restoreFromArchive(const std::string& source)
{
    FC_TIME_INIT(tSplit);

    // Read in place and split into segments (sec 15.10), never served from
    // the file itself: the next save of the document replaces it while its
    // content is still referred to.
    std::shared_ptr<BlobArchive> archive;
    try {
        archive = std::make_shared<BlobArchive>(source, false);
    }
    catch (const Base::Exception& e) {
        FC_WARN("Cannot read included files out of " << source << " (" << e.what()
                                                     << "), reading them one at a time");
        return false;
    }

    const std::string prefix = archivePrefix();
    const std::string index = prefix + indexName();
    std::size_t served = 0;
    std::string bytes;
    const auto& entries = archive->entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        // The index describes the content rather than being any; see
        // readBlobEntry().
        if (entry.name.compare(0, prefix.size(), prefix) != 0 || entry.name == index) {
            continue;
        }
        // Every member is decoded and hashed now: identity is what the archive
        // holds, never what an entry's name claims, and MiSTer's 87 MB of it
        // costs a fraction of a second. The member itself is copied raw.
        auto member = std::make_shared<BlobRawMember>();
        if (!archive->readRaw(i, *member) || !decodeMember(*member, bytes)) {
            FC_ERR("Failed to read included file " << entry.name << " from " << source);
            continue;
        }
        const std::string hash = hashBytes(bytes);
        std::string ext = Base::FileInfo(entry.name).extension();
        if (!isPlainExtension(ext)) {
            ext.clear();
        }
        member->name = hash + (ext.empty() ? "" : "." + ext);
        FileBlobHandle blob = adoptMember(hash, bytes, std::move(member), ext.c_str());
        nameRestored(blob, entry.name);
        hold(std::move(blob));
        ++served;
    }
    archive->close();
    archive.reset();

    // The last batch; earlier ones were written as they filled a segment.
    try {
        flush();
    }
    catch (const Base::Exception& e) {
        FC_ERR("Blob store: " << e.what());
    }
    FC_DURATION_DECL_INIT(dSplit);
    FC_DURATION_PLUS(dSplit, tSplit);
    FC_LOG("blob store " << source << ": " << served << " entries split into the pack store, "
                         << dSplit.count() << "s");
    return true;
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
            FileBlobHandle blob = insertFile(info.absoluteFilePath().toUtf8().constData());
            nameRestored(blob, info.fileName().toUtf8().constData());
            hold(std::move(blob));
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
    {
        std::unordered_map<std::string, FileBlobHandle> hold;
        {
            std::lock_guard<std::mutex> guard(_mutex);
            _pending.clear();
            hold.swap(_restoreHold);
            _restoreNames.clear();
            // From here a referrer arriving is a bug in whatever produced it,
            // not something to wait for; addPendingReferrer() says so out loud.
            _restoreClosed = true;
        }
        // Released outside the lock: the last reference to unclaimed content
        // dies here, and ~FileBlob calls back into release().
    }
    // Content a forward-only read handed over entry by entry is still in
    // memory; the restore is its batch.
    try {
        flush();
    }
    catch (const Base::Exception& e) {
        FC_ERR("Blob store: " << e.what());
    }
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

namespace
{
/// An extension as a blob keeps it: no dot, and only a plain one.
std::string plainExtension(const char* extension)
{
    std::string ext = extension ? extension : "";
    if (!ext.empty() && ext[0] == '.') {
        ext.erase(ext.begin());
    }
    return isPlainExtension(ext) ? ext : std::string();
}

bool readWholeFile(const Base::FileInfo& fi, std::string& bytes)
{
    Base::ifstream from(fi, std::ios::in | std::ios::binary);
    if (!from) {
        return false;
    }
    std::ostringstream buffer;
    buffer << from.rdbuf();
    bytes = buffer.str();
    return true;
}
}  // namespace

FileBlobHandle FileBlobManager::insertFile(const char* srcPath, const char* extension)
{
    Base::FileInfo src(srcPath);
    if (!src.exists()) {
        std::stringstream str;
        str << "FileBlobManager: file " << srcPath << " does not exist.";
        THROWM(Base::FileSystemError, str.str())
    }

    // The name the referrer will store this under decides the extension when
    // there is one: the source is often a scratch file whose name says less
    // about the content than the name the property gives it.
    std::string ext = extension ? extension : src.extension();
    if (!ext.empty() && ext[0] != '.') {
        ext.insert(ext.begin(), '.');
    }

    // Into the pack store without a file: a consumer that wants one asks
    // for its path.
    if (packStore() && src.size() <= memberCap()) {
        std::string bytes;
        if (!readWholeFile(src, bytes)) {
            std::stringstream str;
            str << "FileBlobManager: cannot read " << srcPath;
            THROWM(Base::FileSystemError, str.str())
        }
        return adoptBytes(bytes, ext.c_str());
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

    // A scratch file is named after what it holds, so its own extension is
    // the right one to keep; a staging path is not, and its caller passes
    // what it knows instead -- which may be nothing, and an empty extension
    // says exactly that.
    std::string ext = extension ? extension : fi.extension();
    if (!ext.empty() && ext[0] != '.') {
        ext.insert(ext.begin(), '.');
    }

    if (packStore() && fi.size() <= memberCap()) {
        // Into the pack store, so a save copies it compressed; and the file,
        // which exists already, becomes the blob's own -- a caller adopting
        // one usually wants a path next, and writing it again would cost the
        // create the store saves everywhere else.
        std::string bytes;
        if (!readWholeFile(fi, bytes)) {
            std::stringstream str;
            str << "FileBlobManager: cannot read " << path;
            THROWM(Base::FileSystemError, str.str())
        }
        FileBlobHandle blob = adoptBytes(bytes, ext.c_str());
        std::lock_guard<std::mutex> once(_materializeMutex);
        if (!blob->inArchive()) {
            if (blob->_path != fi.filePath()) {
                fi.setPermissions(Base::FileInfo::ReadWrite);
                fi.deleteFile();
            }
            return blob;
        }
        const std::string dst = newBlobPath(ext.empty() ? nullptr : ext.c_str());
        fi.setPermissions(Base::FileInfo::ReadWrite);
        if (fi.renameFile(dst.c_str())) {
            Base::FileInfo(dst).setPermissions(Base::FileInfo::ReadOnly);
            {
                std::lock_guard<std::mutex> guard(_mutex);
                blob->_path = dst;
            }
            blob->_materialized.store(true, std::memory_order_release);
        }
        else {
            fi.deleteFile();
        }
        return blob;
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
            // Content already stored: drop the incoming duplicate. _path and
            // not path(), which for a packed blob writes its file and takes
            // this same lock to do it.
            if (existing->_path != fi.filePath()) {
                fi.setPermissions(Base::FileInfo::ReadWrite);
                fi.deleteFile();
            }
            return existing;
        }
    }

    // Move it into the store.
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

FileBlobHandle FileBlobManager::adoptBytes(const std::string& bytes, const char* extension)
{
    return adoptMember(hashBytes(bytes), bytes, nullptr, extension);
}

FileBlobHandle FileBlobManager::adoptMember(const std::string& hash,
                                            const std::string& bytes,
                                            std::shared_ptr<BlobRawMember> member,
                                            const char* extension)
{
    const std::string ext = plainExtension(extension);
    const bool pack = packStore() && bytes.size() <= memberCap();

    // What the store has already costs nothing: a live blob is shared, and
    // content still on disk or in memory from an earlier life is taken back
    // without writing it again -- a shape changed and changed back, or an
    // undo reaching past the content's release.
    auto known = [&]() -> FileBlobHandle {
        auto it = _blobs.find(hash);
        if (it != _blobs.end()) {
            if (auto existing = it->second.lock()) {
                return existing;
            }
        }
        auto on = _where.find(hash);
        if (on != _where.end()) {
            return makePacked(hash, bytes.size(), ext, nullptr, on->second);
        }
        auto held = _unflushed.find(hash);
        if (held != _unflushed.end()) {
            return makePacked(hash, bytes.size(), ext, held->second, 0);
        }
        return {};
    };

    if (!pack) {
        std::lock_guard<std::mutex> guard(_mutex);
        if (auto blob = known()) {
            return blob;
        }
        // Written straight to its place in the store: there is nothing to
        // adopt from, so there is no staging path to move and no reason to
        // hash a file that was just written from bytes already hashed here.
        return make(hash, writeNewFile(bytes, ext.c_str()), bytes.size());
    }

    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (auto blob = known()) {
            return blob;
        }
    }
    // Compressed outside the lock, once in the content's life (sec 15.7).
    if (!member) {
        member = encodeMember(bytes, hash + (ext.empty() ? "" : "." + ext));
    }
    FileBlobHandle blob;
    bool full = false;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        blob = known();
        if (!blob) {
            blob = makePacked(hash, bytes.size(), ext, std::move(member), 0);
        }
        full = _unflushedBytes >= segmentCap();
    }
    if (full) {
        // A segment's worth gathered: that is a batch too.
        try {
            flush();
        }
        catch (const Base::Exception& e) {
            FC_ERR("Blob store: " << e.what());
        }
    }
    return blob;
}

std::string FileBlobManager::writeNewFile(const std::string& bytes, const char* extension) const
{
    const std::string dst = newBlobPath(extension && *extension ? extension : nullptr);
    {
        Base::ofstream to(Base::FileInfo(dst), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!to) {
            std::stringstream str;
            str << "FileBlobManager: cannot create " << dst;
            THROWM(Base::FileSystemError, str.str())
        }
        if (!bytes.empty()) {
            to.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        if (!to) {
            std::stringstream str;
            str << "FileBlobManager: cannot write " << dst;
            THROWM(Base::FileSystemError, str.str())
        }
    }

    Base::FileInfo fi(dst);
    fi.setPermissions(Base::FileInfo::ReadOnly);
    return fi.filePath();
}

void FileBlobManager::materialize(const FileBlob& blob)
{
    // Its own lock, so two threads asking for the same path write one file;
    // not the store's, which reading the content takes.
    std::lock_guard<std::mutex> once(_materializeMutex);
    if (!blob.inArchive()) {
        return;
    }
    std::string bytes;
    if (!readPacked(blob, bytes)) {
        FC_ERR("Included file " << blob._hash << " cannot be read back out of the blob store");
        return;
    }
    std::string path;
    try {
        path = writeNewFile(bytes, blob._ext.c_str());
    }
    catch (const Base::Exception& e) {
        FC_ERR("Included file " << blob._hash << ": " << e.what());
        return;
    }
    {
        std::lock_guard<std::mutex> guard(_mutex);
        blob._path = path;
    }
    blob._materialized.store(true, std::memory_order_release);
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

    // Content that died before a batch wrote it never reaches the disk;
    // content in a segment is dead space the worker may reclaim.
    if (blob->_packed && blob->_segment == 0) {
        auto held = _unflushed.find(blob->_hash);
        if (held != _unflushed.end()) {
            _unflushedBytes -= held->second->data.size();
            _unflushed.erase(held);
        }
    }
    else if (blob->_packed) {
        scheduleMaintenance();
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

// ---------------------------------------------------------------------------
// The pack store (docs/FileBlobsManager.md sec 15.7-15.10)
// ---------------------------------------------------------------------------

/** One segment: a zip file `seg-<N>.<g>` in the blob directory.
 *
 * Its members are named by content hash, so its central directory is the
 * index (sec 15.8), read into byHash when a generation is installed. A
 * generation is immutable once named; the next one replaces it whole.
 */
struct FileBlobManager::Segment
{
    int number {0};
    int generation {0};
    std::shared_ptr<BlobArchive> file;
    /// hash -> entry index in file
    std::unordered_map<std::string, std::size_t> byHash;
};

bool FileBlobManager::packStore() const
{
    return _doc && DocumentParams::getArchiveBlobStore();
}

uint64_t FileBlobManager::segmentCap() const
{
    return static_cast<uint64_t>(std::max<long>(DocumentParams::getBlobSegmentSize(), 4)) * 1024u;
}

uint64_t FileBlobManager::memberCap() const
{
    return segmentCap() / 4;
}

FileBlobHandle FileBlobManager::makePacked(const std::string& hash,
                                           uint64_t size,
                                           const std::string& ext,
                                           std::shared_ptr<const BlobRawMember> member,
                                           int segment)
{
    FileBlobHandle blob = make(hash, std::string(), size);
    blob->_ext = ext;
    blob->_packed = true;
    blob->_segment = segment;
    blob->_materialized.store(false, std::memory_order_release);
    if (segment == 0 && member && !_unflushed.count(hash)) {
        _unflushedBytes += member->data.size();
        _unflushed.emplace(hash, std::move(member));
    }
    return blob;
}

int FileBlobManager::resolveSegment(int number) const
{
    for (auto it = _redirect.find(number); it != _redirect.end(); it = _redirect.find(number)) {
        number = it->second;
    }
    return number;
}

bool FileBlobManager::isLive(const std::string& hash) const
{
    auto it = _blobs.find(hash);
    return it != _blobs.end() && !it->second.expired();
}

int FileBlobManager::newSegmentNumber()
{
    if (_nextSegment == 0) {
        // Numbers are never reused in a store (sec 15.10), and a directory
        // may hold segments this manager did not write.
        int highest = 0;
        QDir dir(QString::fromUtf8(blobDir().c_str()));
        for (const QString& name : dir.entryList({QStringLiteral("seg-*")}, QDir::Files)) {
            const QString number = name.mid(4).section(QLatin1Char('.'), 0, 0);
            highest = std::max(highest, number.toInt());
        }
        _nextSegment = highest + 1;
    }
    return _nextSegment++;
}

std::shared_ptr<const BlobRawMember> FileBlobManager::readMember(const FileBlob& blob) const
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::shared_ptr<BlobArchive> file;
        std::size_t index = 0;
        {
            std::lock_guard<std::mutex> guard(_mutex);
            if (!blob._packed) {
                return {};
            }
            if (blob._segment == 0) {
                auto held = _unflushed.find(blob._hash);
                return held != _unflushed.end() ? held->second : nullptr;
            }
            auto seg = _segments.find(resolveSegment(blob._segment));
            if (seg == _segments.end()) {
                return {};
            }
            auto hit = seg->second->byHash.find(blob._hash);
            if (hit == seg->second->byHash.end()) {
                return {};
            }
            file = seg->second->file;
            index = hit->second;
        }
        // Read with no lock held: the generation is immutable, and a rewrite
        // that retires it meanwhile leaves this reader its file (sec 15.7).
        auto member = std::make_shared<BlobRawMember>();
        if (file->readRaw(index, *member)) {
            return member;
        }
        // The transient directory may have moved under the segment.
        if (attempt == 0 && !Base::FileInfo(file->path()).exists()) {
            const_cast<FileBlobManager*>(this)->relocate();
            continue;
        }
        break;
    }
    return {};
}

bool FileBlobManager::readPacked(const FileBlob& blob, std::string& bytes) const
{
    auto member = readMember(blob);
    return member && decodeMember(*member, bytes);
}

void FileBlobManager::writeGeneration(int number,
                                      const std::vector<std::pair<int, std::string>>& copies,
                                      const std::vector<std::shared_ptr<const BlobRawMember>>& added)
{
    // The sources as they are now. Only a writer changes them, and the
    // caller holds _writeMutex.
    std::map<int, std::shared_ptr<Segment>> sources;
    int generation = 1;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        for (const auto& copy : copies) {
            auto it = _segments.find(copy.first);
            if (it != _segments.end()) {
                sources.emplace(copy.first, it->second);
            }
        }
        auto own = _segments.find(number);
        if (own != _segments.end()) {
            sources.emplace(number, own->second);
            generation = own->second->generation + 1;
        }
    }

    // Written straight to its name, never renamed into place (sec 15.11):
    // on the monitored laptop renaming a zip under about 10 MB costs 3-11 s,
    // and writing the same bytes to their final name costs nothing. The
    // name is new -- a generation is never reused -- so a rename bought no
    // atomicity: no reader opens a generation before it is installed below,
    // which is after the file is complete and flushed. A crash mid-write
    // leaves a file whose central directory does not check out, which is
    // debris as a `.tmp` was.
    const std::string path =
        blobDir() + "/seg-" + std::to_string(number) + "." + std::to_string(generation);
    {
        SegmentWriter out(path);
        BlobRawMember member;
        for (const auto& copy : copies) {
            auto seg = sources.find(copy.first);
            if (seg == sources.end()) {
                continue;
            }
            auto hit = seg->second->byHash.find(copy.second);
            // Copied raw, never decoded or compressed again.
            if (hit == seg->second->byHash.end()
                || !seg->second->file->readRaw(hit->second, member)) {
                FC_ERR("Blob store: member " << copy.second << " cannot be read out of "
                                             << seg->second->file->path());
                continue;
            }
            out.add(member);
        }
        for (const auto& add : added) {
            out.add(*add);
        }
        if (!out.finish()) {
            THROWM(Base::FileSystemError, "Blob store: cannot write " + path)
        }
    }
    syncDirectoryOf(path);
    auto file = std::make_shared<BlobArchive>(path, true);
    auto segment = std::make_shared<Segment>();
    segment->number = number;
    segment->generation = generation;
    segment->file = file;
    for (std::size_t i = 0; i < file->entries().size(); ++i) {
        segment->byHash.emplace(memberHash(file->entries()[i].name), i);
    }
    file->close();

    // Strong references taken below die after the lock is let go: ~FileBlob
    // takes it.
    std::vector<FileBlobHandle> holding;
    // Content taken back to memory: live again after the rewrite was planned
    // without it.
    std::vector<std::pair<std::string, std::shared_ptr<BlobRawMember>>> rescued;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        for (const auto& add : added) {
            const std::string hash = memberHash(add->name);
            _where[hash] = number;
            auto held = _unflushed.find(hash);
            if (held != _unflushed.end() && held->second == add) {
                _unflushedBytes -= add->data.size();
                _unflushed.erase(held);
            }
            auto it = _blobs.find(hash);
            if (it != _blobs.end()) {
                if (auto blob = it->second.lock()) {
                    if (blob->_packed && blob->_segment == 0) {
                        blob->_segment = number;
                    }
                    holding.push_back(std::move(blob));
                }
            }
        }
        for (const auto& source : sources) {
            for (const auto& member : source.second->byHash) {
                const std::string& hash = member.first;
                if (segment->byHash.count(hash)) {
                    _where[hash] = number;
                    continue;
                }
                auto on = _where.find(hash);
                if (on == _where.end() || on->second != source.first) {
                    continue;
                }
                _where.erase(on);
                auto it = _blobs.find(hash);
                FileBlobHandle blob = it != _blobs.end() ? it->second.lock() : nullptr;
                if (!blob || !blob->_packed || resolveSegment(blob->_segment) != source.first) {
                    continue;
                }
                // Taken back after the plan was made: back to memory, for
                // the next batch.
                auto copy = std::make_shared<BlobRawMember>();
                if (source.second->file->readRaw(member.second, *copy)) {
                    blob->_segment = 0;
                    _unflushedBytes += copy->data.size();
                    _unflushed[hash] = copy;
                }
                holding.push_back(std::move(blob));
            }
        }
        for (const auto& source : sources) {
            if (source.first != number) {
                // Merged: the handles naming it now read the new segment.
                _segments.erase(source.first);
                _redirect[source.first] = number;
                if (_appendSegment == source.first) {
                    _appendSegment = 0;
                }
            }
        }
        _segments[number] = segment;
        _archives.erase(std::remove_if(_archives.begin(), _archives.end(),
                                       [](const auto& held) { return held.expired(); }),
                        _archives.end());
        _archives.push_back(file);
    }
    // The retired generations go with `sources`, each file deleted when its
    // last reader lets go.
}

void FileBlobManager::flush()
{
    // The batch only: the dead dropped from the segment written to is all
    // the repack a caller's thread pays; the rest is the worker's.
    std::lock_guard<std::mutex> writing(_writeMutex);
    flushLocked();
}

void FileBlobManager::flushLocked()
{
    const uint64_t cap = segmentCap();
    for (;;) {
        std::vector<std::shared_ptr<const BlobRawMember>> batch;
        std::vector<std::pair<int, std::string>> keep;
        int number = 0;
        {
            std::lock_guard<std::mutex> guard(_mutex);
            // Dead before it was written: gone, and never on disk.
            std::vector<std::shared_ptr<const BlobRawMember>> live;
            for (auto it = _unflushed.begin(); it != _unflushed.end();) {
                if (!isLive(it->first)) {
                    _unflushedBytes -= it->second->data.size();
                    it = _unflushed.erase(it);
                    continue;
                }
                live.push_back(it->second);
                ++it;
            }
            if (live.empty()) {
                return;
            }
            // Name order, so the same content makes the same segment.
            std::sort(live.begin(), live.end(), [](const auto& a, const auto& b) {
                return a->name < b->name;
            });

            // The segment being written to, while it has room: its live
            // members carried into the next generation, its dead ones
            // dropped on the way (repack rides the rewrite, sec 15.7).
            uint64_t used = 0;
            auto seg = _segments.find(_appendSegment);
            if (seg != _segments.end()) {
                const auto& entries = seg->second->file->entries();
                for (const auto& member : seg->second->byHash) {
                    auto on = _where.find(member.first);
                    if (isLive(member.first) && on != _where.end() && on->second == _appendSegment) {
                        keep.emplace_back(_appendSegment, member.first);
                        used += entries[member.second].compressedSize;
                    }
                }
                if (used + live.front()->data.size() > cap
                    || keep.size() >= segmentMaxMembers) {
                    keep.clear();
                    used = 0;
                    _appendSegment = 0;
                }
            }
            else {
                _appendSegment = 0;
            }
            number = _appendSegment ? _appendSegment : newSegmentNumber();
            for (const auto& member : live) {
                if ((!batch.empty() && used + member->data.size() > cap)
                    || keep.size() + batch.size() >= segmentMaxMembers) {
                    break;
                }
                batch.push_back(member);
                used += member->data.size();
            }
            std::sort(keep.begin(), keep.end());
        }
        writeGeneration(number, keep, batch);
        std::lock_guard<std::mutex> guard(_mutex);
        _appendSegment = number;
    }
}

void FileBlobManager::maintain()
{
    const uint64_t cap = segmentCap();
    struct Plan
    {
        int number {0};
        std::vector<std::pair<int, std::string>> keep;
        uint64_t live {0};
        uint64_t total {0};
    };
    std::vector<Plan> plans;
    int append = 0;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        append = _appendSegment;
        for (const auto& seg : _segments) {
            Plan plan;
            plan.number = seg.first;
            const auto& entries = seg.second->file->entries();
            for (const auto& member : seg.second->byHash) {
                const uint64_t size = entries[member.second].compressedSize;
                plan.total += size;
                auto on = _where.find(member.first);
                if (isLive(member.first) && on != _where.end() && on->second == seg.first) {
                    plan.keep.emplace_back(seg.first, member.first);
                    plan.live += size;
                }
            }
            std::sort(plan.keep.begin(), plan.keep.end());
            plans.push_back(std::move(plan));
        }
    }

    std::vector<Plan*> small;
    for (auto& plan : plans) {
        if (plan.keep.empty()) {
            // Nothing live: deleted, not rewritten.
            std::shared_ptr<Segment> gone;
            std::lock_guard<std::mutex> guard(_mutex);
            auto it = _segments.find(plan.number);
            if (it == _segments.end()) {
                continue;
            }
            // Something taken back since the plan was made keeps it.
            bool taken = false;
            for (const auto& member : it->second->byHash) {
                auto on = _where.find(member.first);
                if (on != _where.end() && on->second == plan.number && isLive(member.first)) {
                    taken = true;
                    break;
                }
            }
            if (taken) {
                continue;
            }
            for (const auto& member : it->second->byHash) {
                auto on = _where.find(member.first);
                if (on != _where.end() && on->second == plan.number) {
                    _where.erase(on);
                }
            }
            gone = std::move(it->second);
            _segments.erase(it);
            if (_appendSegment == plan.number) {
                _appendSegment = 0;
            }
            continue;
        }
        if (plan.number == append) {
            continue;
        }
        // A segment nothing is written to shrinks once half of it is dead.
        if (plan.live * 2 < plan.total) {
            writeGeneration(plan.number, plan.keep, {});
        }
        if (plan.live < cap / 4) {
            small.push_back(&plan);
        }
    }

    // Several small segments are merged into one (sec 15.8): a new number,
    // higher than theirs, and the handles naming them redirected.
    std::size_t start = 0;
    while (start + 1 < small.size()) {
        std::vector<std::pair<int, std::string>> keep;
        uint64_t live = 0;
        std::size_t end = start;
        while (end < small.size() && live + small[end]->live <= cap
               && keep.size() + small[end]->keep.size() < segmentMaxMembers) {
            live += small[end]->live;
            keep.insert(keep.end(), small[end]->keep.begin(), small[end]->keep.end());
            ++end;
        }
        if (end - start >= 2) {
            int number = 0;
            {
                std::lock_guard<std::mutex> guard(_mutex);
                number = newSegmentNumber();
            }
            writeGeneration(number, keep, {});
            start = end;
        }
        else {
            start = std::max(end, start + 1);
        }
    }
}

std::unique_lock<std::mutex> FileBlobManager::holdWrites()
{
    return std::unique_lock<std::mutex>(_writeMutex);
}

void FileBlobManager::shutdown()
{
    stopWorker();
    closeArchives();
}

void FileBlobManager::scheduleMaintenance()
{
    std::lock_guard<std::mutex> guard(_workerMutex);
    if (_workerStop) {
        return;
    }
    _maintenanceDue = true;
    if (!_worker.joinable()) {
        _worker = std::thread([this]() { workerLoop(); });
    }
    _wake.notify_one();
}

void FileBlobManager::workerLoop()
{
    std::unique_lock<std::mutex> lock(_workerMutex);
    for (;;) {
        _wake.wait(lock, [this]() { return _workerStop || _maintenanceDue; });
        // Settle first: deleting a thousand objects is a thousand releases,
        // and one pass after them. A save reading the segments is waited
        // out the same way.
        while (!_workerStop && (_maintenanceDue || _saving.load() > 0)) {
            _maintenanceDue = false;
            _wake.wait_for(lock, std::chrono::milliseconds(200), [this]() { return _workerStop; });
        }
        if (_workerStop) {
            return;
        }
        lock.unlock();
        try {
            std::lock_guard<std::mutex> writing(_writeMutex);
            maintain();
        }
        catch (const Base::Exception& e) {
            FC_ERR("Blob store: " << e.what());
        }
        catch (const std::exception& e) {
            FC_ERR("Blob store: " << e.what());
        }
        lock.lock();
    }
}

void FileBlobManager::stopWorker()
{
    {
        std::lock_guard<std::mutex> guard(_workerMutex);
        _workerStop = true;
    }
    _wake.notify_all();
    if (_worker.joinable() && _worker.get_id() != std::this_thread::get_id()) {
        _worker.join();
    }
}

std::size_t FileBlobManager::recoverStore()
{
    std::lock_guard<std::mutex> writing(_writeMutex);
    const std::string dir = blobDir();
    QDir qdir(QString::fromUtf8(dir.c_str()));
    if (!qdir.exists()) {
        return 0;
    }
    // Segments by number, generations newest first.
    std::map<int, std::vector<int>> generations;
    std::vector<std::string> loose;
    for (const QString& name : qdir.entryList(QDir::Files)) {
        if (name.startsWith(QStringLiteral("seg-"))) {
            bool okNumber = false;
            bool okGeneration = false;
            const int number = name.mid(4).section(QLatin1Char('.'), 0, 0).toInt(&okNumber);
            const int generation = name.section(QLatin1Char('.'), 1, 1).toInt(&okGeneration);
            if (okNumber && okGeneration && number > 0 && generation > 0) {
                generations[number].push_back(generation);
                continue;
            }
            // A name no writer of this store makes: a `.tmp` of an older
            // build, or debris.
            Base::FileInfo(dir + "/" + name.toStdString()).deleteFile();
            continue;
        }
        if (name == QString::fromUtf8(indexName())) {
            continue;
        }
        loose.push_back(dir + "/" + name.toStdString());
    }

    std::size_t found = 0;
    std::vector<std::shared_ptr<Segment>> kept;
    for (auto& entry : generations) {
        auto& gens = entry.second;
        std::sort(gens.rbegin(), gens.rend());
        std::shared_ptr<Segment> segment;
        for (int generation : gens) {
            const std::string path = dir + "/seg-" + std::to_string(entry.first) + "."
                + std::to_string(generation);
            if (segment) {
                // Superseded: a newer generation is complete, and holds every
                // member of this one that was live when it was written.
                Base::FileInfo(path).deleteFile();
                continue;
            }
            try {
                auto file = std::make_shared<BlobArchive>(path, true);
                segment = std::make_shared<Segment>();
                segment->number = entry.first;
                segment->generation = generation;
                segment->file = file;
                for (std::size_t i = 0; i < file->entries().size(); ++i) {
                    segment->byHash.emplace(memberHash(file->entries()[i].name), i);
                }
                file->close();
            }
            catch (const Base::Exception& e) {
                // Cut short by the crash: its directory is not there, or does
                // not check out (sec 15.11). Debris, as a `.tmp` was.
                FC_WARN("Blob store: " << path << " is incomplete, deleted: " << e.what());
                Base::FileInfo(path).deleteFile();
            }
        }
        if (segment) {
            kept.push_back(segment);
        }
    }

    // Hashed outside the lock: loose files are the large ones.
    std::vector<std::pair<std::string, std::pair<std::string, uint64_t>>> hashed;
    for (const auto& path : loose) {
        Base::FileInfo fi(path);
        const std::string hash = hashFile(path.c_str());
        if (hash.empty()) {
            continue;
        }
        hashed.emplace_back(hash, std::make_pair(path, static_cast<uint64_t>(fi.size())));
    }

    std::lock_guard<std::mutex> guard(_mutex);
    int highest = 0;
    for (const auto& segment : kept) {
        highest = std::max(highest, segment->number);
        for (const auto& member : segment->byHash) {
            auto on = _where.find(member.first);
            if (on == _where.end()) {
                ++found;
                _where.emplace(member.first, segment->number);
            }
            else if (on->second < segment->number) {
                on->second = segment->number;   // the newest segment wins
            }
        }
        _segments[segment->number] = segment;
        _archives.push_back(segment->file);
    }
    _nextSegment = std::max(_nextSegment, highest + 1);
    for (auto& entry : hashed) {
        if (_where.count(entry.first) || _recoveredLoose.count(entry.first)) {
            Base::FileInfo(entry.second.first).deleteFile();   // a copy
            continue;
        }
        ++found;
        _recoveredLoose.emplace(entry.first, entry.second);
    }
    return found;
}

FileBlobHandle FileBlobManager::recovered(const std::string& hash)
{
    std::lock_guard<std::mutex> guard(_mutex);
    auto it = _blobs.find(hash);
    if (it != _blobs.end()) {
        if (auto live = it->second.lock()) {
            return live;
        }
    }
    auto on = _where.find(hash);
    if (on != _where.end()) {
        auto seg = _segments.find(resolveSegment(on->second));
        if (seg != _segments.end()) {
            auto member = seg->second->byHash.find(hash);
            if (member != seg->second->byHash.end()) {
                const auto& entry = seg->second->file->entries()[member->second];
                const auto dot = entry.name.find('.');
                const std::string ext =
                    dot == std::string::npos ? std::string() : entry.name.substr(dot + 1);
                return makePacked(hash, entry.size, ext, nullptr, seg->first);
            }
        }
    }
    auto file = _recoveredLoose.find(hash);
    if (file != _recoveredLoose.end()) {
        FileBlobHandle blob = make(hash, file->second.first, file->second.second);
        _recoveredLoose.erase(file);
        return blob;
    }
    return {};
}

void FileBlobManager::endRecovery()
{
    std::unordered_map<std::string, std::pair<std::string, uint64_t>> left;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        left.swap(_recoveredLoose);
    }
    for (const auto& entry : left) {
        Base::FileInfo(entry.second.first).deleteFile();
    }
    // A segment whose members nothing took is dead space: the worker's
    // first pass deletes it, or rewrites it when some of it is live.
    if (!_segments.empty()) {
        scheduleMaintenance();
    }
}

void FileBlobManager::makeDurable(const std::vector<FileBlobHandle>& blobs)
{
    bool pending = false;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        for (const auto& blob : blobs) {
            if (blob && blob->_owner == this && blob->_packed && blob->_segment == 0) {
                pending = true;
                break;
            }
        }
    }
    if (!pending) {
        return;
    }
    std::lock_guard<std::mutex> writing(_writeMutex);
    flushLocked();
}

// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************************************
 *                                                                                                 *
 *   Copyright (c) 2022 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>                       *
 *   Copyright (c) 2023 FreeCAD Project Association                                                *
 *                                                                                                 *
 *   This file is part of FreeCAD.                                                                 *
 *                                                                                                 *
 *   FreeCAD is free software: you can redistribute it and/or modify it under the terms of the     *
 *   GNU Lesser General Public License as published by the Free Software Foundation, either        *
 *   version 2.1 of the License, or (at your option) any later version.                            *
 *                                                                                                 *
 *   FreeCAD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;          *
 *   without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.     *
 *   See the GNU Lesser General Public License for more details.                                   *
 *                                                                                                 *
 *   You should have received a copy of the GNU Lesser General Public License along with           *
 *   FreeCAD. If not, see <https://www.gnu.org/licenses/>.                                         *
 *                                                                                                 *
 **************************************************************************************************/

#ifndef APP_STRING_ID_H
#define APP_STRING_ID_H

#include <FCConfig.h>

#include <bitset>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QByteArray>
#include <QVector>

#include <Base/Bitmask.h>
#include <Base/Handle.h>
#include <Base/Persistence.h>
#include <CXX/Objects.hxx>
#include <utility>

namespace Data
{
class MappedName;
}

namespace App
{

class StringHasher;
class StringID;
class StringIDRef;
using StringHasherRef = Base::Reference<StringHasher>;

/** Class to store a string
 *
 * The main purpose of this class is to provide an efficient storage of the
 * mapped geometry element name (i.e. the new Topological Naming), but it can
 * also be used as a general purpose string table.
 *
 * The StringID is to be stored in a string table (StringHasher), and be
 * referred to by an integer ID. The stored data can be optionally divided into
 * two parts, prefix and postfix. This is because a new mapped name is often
 * created by adding some common postfix to an existing name, so data sharing
 * can be improved using the following techniques:
 *
 *      a) reference count (through QByteArray) the main data part,
 *
 *      b) (recursively) encode prefix and/or postfix as an integer (in the
 *         format of #<hex>, e.g. #1b) that references another StringID,
 *
 *      c) Check index based name in prefix, e.g. Edge1, Vertex2, and encode
 *         only the text part as StringID. The index is stored separately in
 *         reference class StringIDRef to maximize data sharing.
 */
class AppExport StringID: public Base::BaseClass, public Base::Handled
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();// NOLINT

public:
    /// Flag of the stored string data
    enum class Flag
    {
        /// No flag
        None = 0,
        /// The stored data is binary
        Binary = 1 << 0,
        /// The stored data is the sha1 hash of the original content
        Hashed = 1 << 1,
        /** Postfix is encoded as #<hex>, e.g. #1b, where the hex integer part
         * refers to another StringID.
         */
        PostfixEncoded = 1 << 2,
        /// The data is split as prefix and postfix
        Postfixed = 1 << 3,
        /// The prefix data is split as text + index
        Indexed = 1 << 4,
        /** The prefix data is encoded as #<hex>, e.g. #1b, where the hex
         * integer part refers to another StringID.
         */
        PrefixID = 1 << 5,
        /** The prefix split as text + index, where the text is encoded
         * using another StringID.
         */
        PrefixIDIndex = 1 << 6,
        /// The string ID is persistent regardless of internal mark
        Persistent = 1 << 7,
        /// Internal marked used to check if the string ID is used
        Marked = 1 << 8,
    };
    using Flags = Base::Flags<Flag>;

    /** Constructor
     * @param id: integer ID of this StringID
     * @param data: input data
     * @param flags: flags describes the data
     *
     * User code is not supposed to create StringID directly, but through StringHasher::getID()
     */
    StringID(long id, QByteArray data, const Flags& flags = Flag::None)
        : _id(id),
          _data(std::move(data)),
          _flags(flags)
    {}

    /// Constructs an empty StringID
    StringID()
        : _id(0),
          _flags(Flag::None)
    {}

    StringID(long id, const QByteArray &data, bool binary, bool hashed);
    StringID(const StringID& other) = delete;
    StringID(StringID&& other) noexcept = delete;
    StringID& operator=(const StringID& rhs) = delete;
    StringID& operator=(StringID&& rhs) noexcept = delete;

    ~StringID() override;

    /// Returns the ID of this StringID
    long value() const
    {
        return _id;
    }

    /// Returns all related StringIDs that used to encode this StringID
    const QVector<StringIDRef>& relatedIDs() const
    {
        return _sids;
    }

    /// @name Flag accessors
    //@{
    inline bool isBinary() const;
    inline bool isHashed() const;
    inline bool isPostfixed() const;
    inline bool isPostfixEncoded() const;
    inline bool isIndexed() const;
    inline bool isPrefixID() const;
    inline bool isPrefixIDIndex() const;
    inline bool isMarked() const;
    inline bool isPersistent() const;
    //@}

    /// Checks if this StringID is from the input hasher
    bool isFromSameHasher(const StringHasherRef& hasher) const
    {
        return this->_hasher == hasher;
    }

    /// Checks if this StringID is `hasher`'s; a pointer compare, safe on any thread
    bool isFromHasher(const StringHasher* hasher) const
    {
        return hasher && this->_hasher == hasher;
    }

    /// Returns the owner hasher
    StringHasherRef getHasher() const
    {
        return {_hasher};
    }

    /// Returns the data (prefix)
    const QByteArray& data() const
    {
        return _data;
    }

    /// Returns the postfix
    QByteArray postfix() const
    {
        return _postfix;
    }

    /// Sets the postfix
    void setPostfix(QByteArray postfix)
    {
        _postfix = std::move(postfix);
    }

    PyObject *getPyObject() override;
    /// Returns a Python tuple containing both the text and index
    PyObject* getPyObjectWithIndex(int index);

    /** Convert to string representation of this StringID
     * @param index: optional index
     *
     * The format is #<id>. And if index is non zero, then #<id>:<index>. Both
     * <id> and <index> are in hex format.
     */
    std::string toString(int index) const;

    /// Light weight structure of holding a string ID and associated index
    struct IndexID {
        long id;
        int index;

        explicit operator bool() const
        {
            return id > 0;
        }

        friend std::ostream& operator<<(std::ostream& stream, const IndexID& indexID)
        {
            stream << indexID.id;
            if (indexID.index != 0) {
                stream << ':' << indexID.index;
            }
            return stream;
        }
    };

    /** Parse string to get ID and index
     * @param name: input string
     * @param eof: Whether to check the end of string. If true, then the input
     *             string must contain only the string representation of this
     *             StringID
     * @param size: input string size, or -1 if the input string is zero terminated.
     * @return Return the integer ID and index.
     *
     * The input string is expected to be in the format of #<id> or with index
     * #<id>:<index>, where both id and index are in hex digits.
     */
    static IndexID fromString(const char* name, bool eof = true, int size = -1);

    /** Parse string to get ID and index
     * @param bytes: input data
     * @param eof: Whether to check the end of string. If true, then the input
     *             string must contain only the string representation of this
     *             StringID
     *
     * The input string is expected to be in the format of #<id> or with index
     * #<id>:<index>, where both id and index are in hex digits.
     */
    static IndexID fromString(const QByteArray& bytes, bool eof = true)
    {
        return fromString(bytes.constData(), eof, bytes.size());
    }

    /** Get the text content of this StringID
     * @param index: optional index
     * @return Return the text content of this StringID. If the data is binary,
     *         then output in base64 encoded string.
     */
    std::string dataToText(int index = 0) const;

    /** Get the content of this StringID as QByteArray
     * @param index: optional index.
     */
    QByteArray dataToBytes(int index = 0) const
    {
        QByteArray res(_data);
        if (index != 0) {
            res += QByteArray::number(index);
        }
        if (_postfix.size() != 0) {
            res += _postfix;
        }
        return res;
    }

    /// Mark this StringID as used
    void mark() const;

    /// Mark the StringID as persistent regardless of usage mark
    inline void setPersistent(bool enable);

    bool operator<(const StringID &other) const
    {
        return compare(other) < 0;
    }

    /** Compare StringID
     * @param other: the other StringID for comparison
     * @return Returns -1 if less than the other StringID, 1 if greater, or 0 if equal
     */
    int compare(const StringID& other) const
    {
        if (_hasher < other._hasher) {
            return -1;
        }
        if (_hasher > other._hasher) {
            return 1;
        }
        if (_id < other._id) {
            return -1;
        }
        if (_id > other._id) {
            return 1;
        }
        return 0;
    }

    friend class StringHasher;

private:
    long _id;
    QByteArray _data;
    QByteArray _postfix;
    StringHasher* _hasher = nullptr;
    mutable Flags _flags;
    mutable QVector<StringIDRef> _sids;
};

//////////////////////////////////////////////////////////////////////////

/** Counted reference to a StringID instance
 */
class StringIDRef
{
public:
    /// Standard construction from a heap-allocated StringID. This reference-counting class manages
    /// the lifetime of the StringID, ensuring it is deallocated when its reference count goes to
    /// zero.
    /// \param stringID A pointer to a StringID allocated with "new"
    /// \param index (optional) An index value to store along with the StringID. Defaults to zero.
    StringIDRef()
        : _sid(nullptr),
          _index(0)
    {}

    /// Standard construction from a heap-allocated StringID. This reference-counting class manages
    /// the lifetime of the StringID, ensuring it is deallocated when its reference count goes to
    /// zero.
    /// \param stringID A pointer to a StringID allocated with "new"
    /// \param index (optional) An index value to store along with the StringID. Defaults to zero.
    StringIDRef(StringID* stringID, int index = 0)
        : _sid(stringID),
          _index(index)
    {
        if (_sid) {
            _sid->ref();
        }
    }

    /// Copy construction results in an incremented reference count for the stored StringID
    StringIDRef(const StringIDRef& other)
        : _sid(other._sid),
          _index(other._index)
    {
        if (_sid) {
            _sid->ref();
        }
    }

    /// Move construction does NOT increase the reference count of the StringID (instead, it
    /// invalidates the pointer in the moved object).
    StringIDRef(StringIDRef&& other) noexcept
        : _sid(other._sid)
        , _index(other._index)
    {
        other._sid = nullptr;
    }

    StringIDRef(const StringIDRef & other, int index)
        : _sid(other._sid)
        , _index(index)
    {
        if (_sid) {
            _sid->ref();
        }
    }

    ~StringIDRef()
    {
        if (_sid) {
            _sid->unref();
        }
    }

    void reset(const StringIDRef& stringID = StringIDRef())
    {
        *this = stringID;
    }

    void reset(const StringIDRef& stringID, int index)
    {
        *this = stringID;
        this->_index = index;
    }

    void swap(StringIDRef& stringID)
    {
        if (*this != stringID) {
            auto tmp = stringID;
            stringID = *this;
            *this = tmp;
        }
    }

    StringIDRef& operator=(StringID* stringID)
    {
        if (_sid == stringID) {
            return *this;
        }
        if (_sid) {
            _sid->unref();
        }
        _sid = stringID;
        if (_sid) {
            _sid->ref();
        }
        this->_index = 0;
        return *this;
    }

    StringIDRef& operator=(const StringIDRef& stringID)
    {
        if (&stringID == this) {
            return *this;
        }
        if (_sid != stringID._sid) {
            if (_sid) {
                _sid->unref();
            }
            _sid = stringID._sid;
            if (_sid) {
                _sid->ref();
            }
        }
        this->_index = stringID._index;
        return *this;
    }

    StringIDRef& operator=(StringIDRef&& stringID) noexcept
    {
        if (_sid != stringID._sid) {
            if (_sid) {
                _sid->unref();
            }
            _sid = stringID._sid;
            stringID._sid = nullptr;
        }
        this->_index = stringID._index;
        return *this;
    }

    bool operator<(const StringIDRef& stringID) const
    {
        if (!stringID._sid) {
            return false;
        }
        if (!_sid) {
            return true;
        }
        int res = _sid->compare(*stringID._sid);
        if (res < 0) {
            return true;
        }
        if (res > 0) {
            return false;
        }
        return _index < stringID._index;
    }

    bool operator==(const StringIDRef& stringID) const
    {
        if (_sid && stringID._sid) {
            return _sid->compare(*stringID._sid) == 0 && _index == stringID._index;
        }
        return _sid == stringID._sid;
    }

    bool operator!=(const StringIDRef& stringID) const
    {
        return !(*this == stringID);
    }

    explicit operator bool() const
    {
        return _sid != nullptr;
    }

    int getRefCount() const
    {
        if (_sid) {
            return _sid->getRefCount();
        }
        return 0;
    }

    std::string toString() const
    {
        if (_sid) {
            return _sid->toString(_index);
        }
        return {};
    }

    std::string dataToText() const
    {
        if (_sid) {
            return _sid->dataToText(_index);
        }
        return {};
    }

    /// Get a reference to the data: only makes sense if index and postfix are both empty, but
    /// calling code is responsible for ensuring that.
    const char* constData() const
    {
        if (_sid) {
            assert(_index == 0);
            assert(_sid->postfix().isEmpty());
            return _sid->data().constData();
        }
        return "";
    }

    const StringID& deref() const
    {
        return *_sid;
    }

    long value() const
    {
        if (_sid) {
            return _sid->value();
        }
        return 0;
    }

    QVector<StringIDRef> relatedIDs() const
    {
        if (_sid) {
            return _sid->relatedIDs();
        }
        return {};
    }

    bool isBinary() const
    {
        if (_sid) {
            return _sid->isBinary();
        }
        return false;
    }

    bool isHashed() const
    {
        if (_sid) {
            return _sid->isHashed();
        }
        return false;
    }

    void toBytes(QByteArray& bytes) const
    {
        // TODO: return the QByteArray instead of passing in by reference
        if (_sid) {
            bytes = _sid->dataToBytes(_index);
        }
    }

    PyObject* getPyObject()
    {
        if (_sid) {
            return _sid->getPyObjectWithIndex(_index);
        }
        Py_INCREF(Py_None);
        return Py_None;
    }

    void mark() const
    {
        if (_sid) {
            _sid->mark();
        }
    }

    bool isMarked() const
    {
        return _sid && _sid->isMarked();// NOLINT
    }

    bool isFromSameHasher(const StringHasherRef& hasher) const
    {
        return _sid && _sid->isFromSameHasher(hasher);// NOLINT
    }

    /// Whether the string is `hasher`'s; a pointer compare, safe on any thread.
    bool isFromHasher(const StringHasher* hasher) const
    {
        return _sid && _sid->isFromHasher(hasher);// NOLINT
    }

    StringHasherRef getHasher() const
    {
        if (_sid) {
            return _sid->getHasher();
        }
        return {};
    }

    void setPersistent(bool enable)
    {
        if (_sid) {
            _sid->setPersistent(enable);
        }
    }

    /// Used predominantly by the unit test code to verify that index is set correctly. In general
    /// user code should not need to call this function.
    int getIndex() const
    {
        return _index;
    }

    friend class StringHasher;

private:
    StringID* _sid;
    int _index;
};

/// \brief A bidirectional map  of strings and their integer identifier.
///
/// Maps an arbitrary text string to a unique integer ID, maintaining a reference-counted shared
/// pointer for each. This permits elimination of unused strings based on their reference
/// count. If a duplicate string is added, no additional copy is made, and a new reference to the
/// original storage is returned (incrementing the reference counter of the instance).
///
/// If the string is longer than a given threshold, instead of storing the string, its SHA1 hash is
/// stored (and the original string discarded). This allows an upper threshold on the length of a
/// stored string, while still effectively guaranteeing uniqueness in the table.
class AppExport StringHasher: public Base::Persistence, public Base::Handled {

    TYPESYSTEM_HEADER_WITH_OVERRIDE();// NOLINT

public:
    StringHasher();
    ~StringHasher() override;

    StringHasher(const StringHasher&) = delete;
    StringHasher(StringHasher&&) noexcept = delete;
    StringHasher& operator=(StringHasher& other) = delete;
    StringHasher& operator=(StringHasher&& other) noexcept = delete;

    unsigned int getMemSize() const override;
    void Save(Base::Writer& /*writer*/) const override;
    void Restore(Base::XMLReader& /*reader*/) override;
    void SaveDocFile(Base::Writer& /*writer*/) const override;
    void RestoreDocFile(Base::Reader& /*reader*/) override;
    void setPersistenceFileName(const char* name) const;
    const std::string& getPersistenceFileName() const;

    /** Maps an arbitrary string to an integer
     *
     * @param text: input string.
     * @param len: length of the string: optional if the string is null-terminated.
     * @param hashable: whether hashing the string is permitted.
     * @return A shared pointer to the internally-stored StringID.
     *
     * Maps an arbitrary text string to a unique integer ID, returning a reference-counted shared
     * pointer to the StringID. This permits elimination of unused strings based on their reference
     * count. If a duplicate string is added, no additional copy is made, and a new reference to the
     * original storage is returned (incrementing the reference counter of the instance).
     *
     * If \c hashable is true and the string is longer than the threshold setting of this
     * StringHasher, only the SHA1 hash of the string is stored: the original content of the string
     * is discarded. If \c hashable is false, the string is copied and stored inside a StringID
     * instance.
     *
     * The purpose of this function is to provide a short form of a stable string identification.
     */
    StringIDRef getID(const char *text, int len=-1, bool hashable=false);

    /// Options for string string data
    enum class Option
    {
        /// No option is set
        None = 0,

        /// The input data is binary
        Binary = 1 << 0,

        /// Hashing is permitted for this input data. If the data length is longer than the
        /// threshold setting of the StringHasher, it will be sha1 hashed before storing, and the
        /// original content of the string is discarded.
        Hashable = 1 << 1,

        /// Do not copy the data: assume it is constant and exists for the lifetime of this hasher.
        /// If this option is not set, the data will be copied before storing.
        NoCopy = 1 << 2,
    };
    using Options = Base::Flags<Option>;

    /** Map text or binary data to an integer
     *
     * @param data: input data.
     * @param options: options describing how to store the data.
     * @return A shared pointer to the internally stored StringID.
     *
     * \sa getID (const char*, int, bool);
     */
    StringIDRef getID(const QByteArray& data, Options options = Option::None);

    /** Map geometry element name to an integer */
    StringIDRef getID(const Data::MappedName& name, const QVector<StringIDRef>& sids);

    /** Obtain the reference counted StringID object from numerical id
     *
     * @param id: string ID
     * @param index: optional index of the string ID
     * @return Return a shared pointer to the internally stored StringID.
     *
     * This function exists because the stored string may be one way hashed,
     * and the original text is not persistent. The caller use this function to
     * retrieve the reference count ID object after restore
     */
    StringIDRef getID(long id, int index = 0) const;

    /** Obtain the reference counted StringID object from numerical id and index
     *
     * @param id: string ID with index
     * @return Return a shared pointer to the internally stored StringID.
     */
    StringIDRef getID(const StringID::IndexID& id) const
    {
        return getID(id.id, id.index);
    }

    std::map<long,StringIDRef> getIDMap() const;

    /// Clear all string hashes
    void clear();

    /// Size of the hash table
    size_t size() const;

    /// Return the number of hashes that are used by others
    size_t count() const;

    struct StorageSizes {
        size_t referenced_size = 0;
        size_t total_size = 0;
        size_t shared_size = 0;
        size_t total_shared_size = 0;
    };
    /// Return the storage size
    StorageSizes getStorageSize() const;

    PyObject *getPyObject(void) override;

    /** Enable/disable saving all string ID
     *
     * If saveAll is true, then compact() does nothing even when called explicitly, and a save
     * keeps every string (docs/TransactionLog.md sec 27.51). Setting it to false compacts
     * nothing by itself: the next save does.
     */
    void setSaveAll(bool enable);
    bool getSaveAll() const;

    /** Set threshold of string hashing
     *
     * For hashable strings that are longer than this threshold, the string will
     * be replaced by its sha1 hash.
     */
    void setThreshold(int threshold);
    int getThreshold() const;

    /** Clear internal marks
     *
     * The internal marks on internally stored StringID instances are used to
     * check if the StringID is used.
     */
    void clearMarks() const;

    /** Compact string storage by eliminating unused strings from the table.
     *
     * @param keep: ids to keep although nothing in memory holds them -- what
     *              the file's retained history uses (docs/TransactionLog.md
     *              sec 27.50 item 4). What a kept string is built from stays
     *              with it.
     * @return the ids dropped.
     */
    std::vector<long> compact(const std::function<bool(long)>& keep = {});

    /** The whole table as the file's own member (docs/TransactionLog.md sec
     * 27.50 item 1): every id, whatever is marked, in the format
     * restoreTable() reads.
     */
    std::string saveTable() const;
    /** The SHA-1 (lowercase hex) of what saveTable() writes, cached until the
     * entries change: what a save names the member by, and a snapshot for
     * the log names the same table by without writing it.
     */
    const std::string& contentHash() const;
    /** Replace the table with the one `stream` holds (saveTable()'s format,
     * or a count and the entries as SaveDocFile() wrote before it). Throws on
     * a table that does not parse.
     */
    void restoreTable(std::istream& stream);

    /** One string as the transaction log keeps it (docs/TransactionLog.md
     * sec 27.50 item 2): its id, its flags, the ids (and indices) it refers
     * to, and its two byte parts as they are held.
     */
    struct Row
    {
        long id {0};
        int flags {0};
        std::vector<std::pair<long, int>> sids;
        QByteArray data;
        QByteArray postfix;
    };
    /// Every string whose id is above `after` and that `want` accepts (all,
    /// when it is empty), in id order.
    std::vector<Row> rows(long after = 0, const std::function<bool(long)>& want = {}) const;
    /** Take in `rows`, in id order, each under its own id. A row whose id is
     * held already is skipped, and counted in `conflicts` when the string
     * there is another; one that refers to an id held neither here nor
     * before it in `rows` is skipped as well. Returns how many came in.
     */
    std::size_t insertRows(const std::vector<Row>& rows, std::size_t* conflicts = nullptr);
    /// Whether `id` is held.
    bool hasID(long id) const;
    /// The ids a save marked, with the persistent ones, in order: what a
    /// version uses (docs/TransactionLog.md sec 27.50 item 4).
    std::vector<long> markedIDs() const;
    /// The last id handed out or read; none is handed out again.
    long lastID() const;

    /** Write the elements naming the table as the member `file`, with content
     * `hash` and `count` strings (docs/TransactionLog.md sec 27.50 item 1),
     * where Save() writes the entries; a version the log keeps names the
     * same and has no member, the log's table standing for it (item 2).
     * `used`, sorted, is what the save marked, written as ranges: the ids the
     * document as written uses (item 4).
     */
    void saveReference(Base::Writer& writer, const std::string& file, const std::string& hash,
                       std::size_t count, const std::vector<long>& used) const;
    /** The ids the save that wrote `xml`, a document's XML, marked -- the
     * `used` ranges of its table element (sec 27.50 item 4). False when it
     * names none.
     */
    static bool parseUsed(const std::string& xml, std::vector<std::pair<long, long>>& ranges);
    /// The member the last Restore() was referred to, and its content hash;
    /// both empty when the entries were inline.
    const std::string& tableFile() const
    {
        return _tableFile;
    }
    const std::string& tableHash() const
    {
        return _tableHash;
    }

    /** Take in `other`'s table (docs/TransactionLog.md sec 27.40 item 2):
     * each id it has that this one has not, under the same id. False, with
     * nothing changed, when an id means something else here. `aliased`
     * gets how many strings came in that this table already had under
     * another id (sec 27.41 Q2).
     */
    bool merge(const StringHasher& other, std::size_t* aliased = nullptr);

    /// What the calls of one import have found or taken in, by the other
    /// table's string, so each is visited once (null: it cannot be had).
    using ImportMemo = std::unordered_map<const StringID*, StringIDRef>;

    /** Find or take in `foreign`, a string of another table, by content
     * (docs/TransactionLog.md sec 27.76 item 1, 27.77). What it is built
     * from comes first, the same way, and every id its text names -- a
     * prefix reference, a `#` inside a postfix, the names of a combo string
     * -- is rewritten to this table's, the string it names taken in as
     * well. The index of `foreign` is kept; of two equal strings here the
     * lower id is taken. Null when `foreign` belongs to no table or names an
     * id its own table does not have.
     */
    StringIDRef importID(const StringIDRef& foreign, ImportMemo* memo = nullptr);
    /// importID() taking nothing in: null when this table has not got it.
    StringIDRef lookupID(const StringIDRef& foreign, ImportMemo* memo = nullptr) const;
    /** `text` with every `#id` of `from`'s rewritten to this table's, each
     * string it names taken in and added to `sids` when given. False, with
     * `out` undefined, when one cannot be had.
     */
    bool importText(const QByteArray& text, const StringHasher& from, QByteArray& out,
                    QVector<StringIDRef>* sids, ImportMemo& memo);
    /** `name`, a name of `from`'s table, as this table would have it (sec
     * 27.76 item 2): its text rewritten, and `sids` -- the ids it holds, of
     * any table -- replaced by this table's, with every string the new text
     * names. False when its text names an id that cannot be had.
     */
    bool importName(const Data::MappedName& name, const StringHasher& from,
                    Data::MappedName& out, QVector<StringIDRef>& sids, ImportMemo& memo);

    class HashMap;
    friend class StringID;

protected:
    StringID* insert(const StringIDRef& sid);
    StringIDRef importOne(const StringIDRef& foreign, ImportMemo& memo, bool take);
    StringIDRef importNew(const StringID& foreign, const StringHasher& from, ImportMemo& memo,
                          bool take);
    bool rewriteIds(const QByteArray& text, const StringHasher& from, QByteArray& out,
                    QVector<StringIDRef>* sids, ImportMemo& memo, bool take);
    void saveStream(std::ostream& stream, bool all = false) const;
    void restoreStream(std::istream& stream, std::size_t count);
    void restoreStreamNew(std::istream& stream, std::size_t count);

private:
    std::unique_ptr<HashMap> _hashes;///< Bidirectional map of StringID and its index (a long int).
    mutable std::string _filename;
    std::string _tableFile;
    std::string _tableHash;
    mutable std::string _contentHash;
    mutable uint64_t _hashRevision {0};
};
/** The ids an element map written on this thread uses (docs/TransactionLog.md
 * sec 27.49, 27.50 item 4). While one lives, an element map lists after each
 * name every id of `hasher` the name refers to -- not the ones the last save
 * marked, which another thread may be marking -- and each goes into `ids`:
 * what a value captured for the log refers to, by the capture's own walk.
 */
class AppExport StringIDCollector
{
public:
    explicit StringIDCollector(const StringHasher* hasher);
    ~StringIDCollector();
    StringIDCollector(const StringIDCollector&) = delete;
    StringIDCollector& operator=(const StringIDCollector&) = delete;

    /// Whether an element map writes `sid`: one of the innermost collector's
    /// hasher (noted), or with none on this thread, one a save marked.
    static bool take(const StringIDRef& sid);
    /// The ids noted, sorted, each once.
    std::vector<long> sortedIds() const;

    const StringHasher* hasher;
    std::vector<long> ids;

private:
    StringIDCollector* _outer;
};

}// namespace App

ENABLE_BITMASK_OPERATORS(App::StringID::Flag)
ENABLE_BITMASK_OPERATORS(App::StringHasher::Option)

namespace App
{
inline StringID::StringID(long id, const QByteArray &data, bool binary, bool hashed)
    : _id(id) ,_data(data)
{
    if(binary) {
        _flags.setFlag(Flag::Binary);
    }
    if(hashed) {
        _flags.setFlag(Flag::Hashed);
    }
}

inline bool StringID::isBinary() const
{
    return _flags.testFlag(Flag::Binary);
}
inline bool StringID::isHashed() const
{
    return _flags.testFlag(Flag::Hashed);
}
inline bool StringID::isPostfixed() const
{
    return _flags.testFlag(Flag::Postfixed);
}
inline bool StringID::isPostfixEncoded() const
{
    return _flags.testFlag(Flag::PostfixEncoded);
}
inline bool StringID::isIndexed() const
{
    return _flags.testFlag(Flag::Indexed);
}
inline bool StringID::isPrefixID() const
{
    return _flags.testFlag(Flag::PrefixID);
}
inline bool StringID::isPrefixIDIndex() const
{
    return _flags.testFlag(Flag::PrefixIDIndex);
}
inline bool StringID::isMarked() const
{
    return _flags.testFlag(Flag::Marked);
}
inline bool StringID::isPersistent() const
{
    return _flags.testFlag(Flag::Persistent);
}
inline void StringID::setPersistent(bool enable)
{
    _flags.setFlag(Flag::Persistent, enable);
}
}// namespace App

#endif// APP_STRING_ID_H

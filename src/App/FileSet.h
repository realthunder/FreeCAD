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

#ifndef APP_FILESET_H
#define APP_FILESET_H

#include <string>
#include <vector>

#include "FileBlobManager.h"

namespace Base {
class Writer;
class XMLReader;
}

namespace App
{

/** A named set of files, each a shared, content-addressed blob.
 *
 * The VALUE behind App::PropertyFileIncludedList, on its own so that
 * things which are not properties can carry one: a material card's
 * MaterialX document and its maps, an appearance wearing that card, and
 * the shader program's Images property (docs/MaterialStorage.md sec 17.3).
 * Each entry is held under the NAME the referring content uses to ask for
 * it, which is the whole point of the type: a document states its maps as
 * filenames, and it travels only if something can answer "the file this
 * document calls brass_color.jpg is here". filePath() is that answer, a
 * real path in the transient directory, so every consumer downstream goes
 * on opening files.
 *
 * A value has no container to find the owning document from, so every
 * mutator that needs a store takes the FileBlobManager EXPLICITLY. Falling
 * back to the process-wide store here would silently strand content
 * outside the document that is about to save it.
 *
 * What stays with the property: the manager lookup, the pending-content
 * queue, the change signalling and the Python conversion. What is here:
 * the entries, the naming of their archive entries, their XML form, and
 * equality -- on name and content, never on provenance.
 */
class AppExport FileSet
{
public:
    /** One file of the set.
     *
     * hash is kept beside the handle rather than read off it because an
     * entry restored from a document has a hash before it has content --
     * that is what it is waiting for, and what identifies it when the
     * content arrives.
     */
    struct Entry
    {
        /// What the referring content calls this file. The key; unique
        /// within the set, and never empty.
        std::string name;
        /// Where the bytes came from, provenance only. Never resolved
        /// against.
        std::string original;
        /// Content hash, known from the moment the entry exists.
        std::string hash;
        /// The content. Null while a restore is still pending, and for an
        /// entry whose content the archive did not hold.
        FileBlobHandle blob;
    };

    const std::vector<Entry> &entries() const { return _files; }
    int size() const { return static_cast<int>(_files.size()); }
    bool empty() const { return _files.empty(); }

    /// The entry called \a name, or null.
    const Entry *find(const char *name) const;
    /// Non-const half of find().
    Entry *entry(const char *name);
    /** Absolute path of the file called \a name.
     *
     * Empty when there is no such file and when its content has not
     * arrived, which a consumer must treat alike: both mean it cannot be
     * opened.
     */
    std::string filePath(const char *name) const;
    /// The content hashes, in entry order. What identity is computed over.
    std::vector<std::string> hashes() const;
    /// Hashes of the entries whose content has not arrived.
    std::vector<std::string> pendingHashes() const;
    /// True when every entry that names content holds it.
    bool holdsEveryNamedBlob() const;

    /** Add the file at \a path under \a name, replacing any file of that
     * name. The bytes are copied into \a manager, which hashes them and
     * keeps whichever copy is already there. \a original defaults to
     * \a path.
     */
    void setFile(FileBlobManager &manager, const char *name, const char *path,
                 const char *original = nullptr);
    /** Add \a blob under \a name, replacing any file of that name. Content
     * from another document's store is imported into \a manager, because
     * blobs do not migrate.
     */
    void setBlob(FileBlobManager &manager, const char *name, const FileBlobHandle &blob,
                 const char *original = nullptr);
    /// Drop the file called \a name. Answers whether there was one.
    bool remove(const char *name);
    void clear() { _files.clear(); }
    /** Replace the whole set. Foreign blobs are imported into \a manager,
     * hashes refreshed off the handles, and an entry that has a hash but
     * no content -- a copy taken while a restore was pending -- is given
     * the content if \a manager already holds it.
     */
    void assign(FileBlobManager &manager, std::vector<Entry> files);

    /** Take the content restored for one of the entries. The blob says
     * which by its hash, and every entry naming that hash takes it -- one
     * file may be known under two names. Answers holdsEveryNamedBlob().
     */
    bool assignRestoredBlob(const FileBlobHandle &blob);

    /** The referrer one entry is noted under: \a base, which names the
     * property, with the entry's stem appended and its extension taken.
     * One referrer per entry, or every map of a material would land in
     * the archive under one name; an unpacked project should say which
     * map a file is.
     */
    static BlobReferrer referrerFor(const BlobReferrer &base, const Entry &file);
    /// Note every file of the set for the save in progress.
    void collectBlobs(FileBlobManager &manager, const BlobReferrer &base) const;

    /** Write the set as \a element.
     *
     * At schema 5 and above each entry carries its hash and, when known,
     * its provenance, and the content is noted with \a manager under
     * \a base so a property written through a path the collect pass did
     * not walk still gets its archive entries. Below schema 5 there is no
     * store and no per-entry spelling to fall back on: the names alone
     * are written, so what was lost can be said.
     */
    void save(Base::Writer &writer, const char *element, FileBlobManager *manager,
              const BlobReferrer &base) const;
    /** Read the set written as \a element. Every entry comes back with its
     * name and hash and NO content; the caller queues the hashes with the
     * manager that will serve them.
     */
    void restore(Base::XMLReader &reader, const char *element);

    unsigned int getMemSize() const;

    /// The name and the content, not the provenance: two sets holding the
    /// same bytes under the same names are the same value however
    /// differently the files were once reached.
    bool operator==(const FileSet &other) const;
    bool operator!=(const FileSet &other) const { return !operator==(other); }

private:
    std::vector<Entry> _files;
};

} // namespace App

#endif // APP_FILESET_H

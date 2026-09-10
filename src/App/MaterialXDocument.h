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

#ifndef APP_MATERIALXDOCUMENT_H
#define APP_MATERIALXDOCUMENT_H

#include <string>
#include <vector>

#include "FileBlobManager.h"

namespace App
{

class FileSet;

/** A MaterialX document and the files it refers to, as a MANIFEST.
 *
 * The payload a material card carries and an appearance wears
 * (docs/MaterialStorage.md sec 17.4): one document plus its image maps,
 * every one of them a content-addressed blob in the document's store.
 * This type is the tree object over them -- git's tree, USD's layer
 * stack, glTF's external buffers are the same shape: a small text that
 * names each child by the NAME the document refers to it by and by its
 * CONTENT HASH. The manifest is itself stored as a blob, and the hash of
 * that blob is the ONE string an App::MaterialAppearance carries
 * (MaterialAppearance::materialx), exactly as a texture slot carries a
 * map's hash. Identity therefore is computed over the hashes (sec 17.6):
 * change a map and the manifest changes and so does its hash, and the
 * card's content hash after it.
 *
 * Nothing here parses MaterialX. The names are whatever the document
 * SAYS (Render::MaterialX::imageReferences() decides that, Gui-side, at
 * authoring or import time), and the Materials module can read a
 * manifest without a MaterialX library at all, which the layering needs
 * (sec 17.9).
 *
 * The text form, one entry per line, ASCII:
 *
 *     FreeCAD MaterialX 1
 *     document <stated name of the document entry>
 *     surface <name of the surface the document is shaded by>
 *     file <hash> <stated name>
 *     file <hash> <stated name>
 *
 * The hash comes first because a name may contain spaces; a name may not
 * contain a line break. The document is one of the files. The surface
 * line is written only when a surface is named, and a reader that does
 * not know it steps over it and renders the document's first surface,
 * which is what every build before it did.
 */
class AppExport MaterialXDocument
{
public:
    struct File
    {
        /// What the document calls this file, or for the document itself
        /// what the card calls it. The key; unique, never empty.
        std::string name;
        /// Content hash of the blob holding the bytes.
        std::string hash;
    };

    /// Stated name of the entry that IS the document. Empty = no document.
    std::string document;
    /// Which surface of the document is worn: the name of one of its
    /// `surfacematerial` nodes, or of a bare surface shader node where
    /// it states no material (docs/MaterialStorage.md sec 17.13). Empty
    /// means the first surface the document states.
    ///
    /// Part of the manifest, so part of its hash and of the card's
    /// identity after it: an asset's whole material set is usually ONE
    /// document, and the fifteen materials of MaterialX's chess set are
    /// fifteen cards over one shared set of files, told apart by this
    /// and nothing else.
    std::string surface;
    /// Every file, the document among them, in the order they were stated.
    std::vector<File> files;

    bool isSet() const { return !document.empty(); }
    const File *find(const char *name) const;
    /// The document entry's own hash, or empty when it is not listed.
    std::string documentHash() const;
    /// Every content hash, in file order. What a holder must keep alive.
    std::vector<std::string> hashes() const;

    /// The manifest text, exactly the bytes whose hash is the identity.
    std::string write() const;
    /// Parse the text form. False, and \a out cleared, when it is not one.
    static bool read(const std::string &text, MaterialXDocument &out);
    /// Parse the text form out of a file. False when it cannot be read.
    static bool readFile(const std::string &path, MaterialXDocument &out);

    /** Hash of write(): the identity, and the value an appearance carries.
     *
     * Pure function of the names and hashes, so it is the same on every
     * machine and needs no store -- a material card computes it at load.
     */
    std::string manifestHash() const;

    /** Write the manifest into \a manager as a blob and hand back its
     * handle, whose hash() is manifestHash(). Content already stored
     * comes back as the same blob.
     */
    FileBlobHandle store(FileBlobManager &manager) const;

    /// Over a set of stored files, naming \a document as the document
    /// and \a surface as the surface it is shaded by (empty = the first).
    static MaterialXDocument fromFileSet(const FileSet &files,
                                         const std::string &document,
                                         const std::string &surface = {});

    bool operator==(const MaterialXDocument &other) const;
    bool operator!=(const MaterialXDocument &other) const { return !operator==(other); }
};

} // namespace App

#endif // APP_MATERIALXDOCUMENT_H

// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef IMPORT_READER_LOOK_H
#define IMPORT_READER_LOOK_H

#include <memory>
#include <string>
#include <vector>

#include <Mod/Import/ImportGlobal.h>
#include <Base/FileInfo.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Materials
{
class Material;
}

namespace Import
{

/// One assignment of a MaterialX `<look>`: a surface of the document, and
/// the geometry that wears it.
struct LookAssignment
{
    /// Namepath of the assigned `surfacematerial` node, as the look writes
    /// it. It goes straight into the card's Surface, which resolves a name
    /// by that spelling first (docs/MaterialStorage.md sec 17.13).
    std::string material;
    /// The geometry it is assigned to: a MaterialX geometry name path, or
    /// several separated by commas or spaces, wildcards and all.
    std::string geom;
};

/// One file the shader graph names, and where its bytes are on this
/// machine. `name` is what the DOCUMENT calls the file, which is the key
/// the card, the manifest and both renderers join on; `path` is this
/// box's answer and travels no further than the card's first save.
struct LookFile
{
    std::string name;
    std::string path;
};

/** Reading a MaterialX `<look>` onto the objects of a document.
 *
 * A material document says how a surface looks; a look says WHO looks that
 * way. MaterialX's chess set is one `.mtlx` with fifteen `surfacematerial`
 * nodes and one `<look>` assigning each to a mesh of `chess_set.glb` by
 * name -- so importing the glb gives fifteen objects with the right shapes
 * and no materials, and this is the other half (docs/MaterialStorage.md
 * sec 17.13 item 2).
 *
 * Each assignment becomes ONE material card over the shared file set,
 * differing only in the surface it names: that is what item 1 built the
 * surface name for, and it is why fifteen cards cost one copy of the
 * document and one of each image in the document's blob store, which is
 * content-addressed.
 *
 * What this class does NOT do is parse MaterialX. The looks and the image
 * names come from `Render::MaterialX` (the renderer owns every question
 * about what a document says, sec 17.9), and are handed in as plain data:
 * so the reader is a FreeCAD question -- which object is called what, and
 * which property wears a card -- and is testable without the library.
 */
class ImportExport ReaderLook
{
public:
    /// \a file is the document the look was read from: what its card calls
    /// the shader graph, and where the relative image names resolved.
    explicit ReaderLook(const Base::FileInfo& file);
    ~ReaderLook();

    /// The look's assignments, in document order. An object wears the FIRST
    /// one whose geometry matches it, which is the order a look is read in.
    void setAssignments(std::vector<LookAssignment> assignments);
    /// Every file the document names, the document itself excluded.
    void setImages(std::vector<LookFile> images);
    /// The look's own name. Reporting only.
    void setLookName(std::string name);

    /// Dress every object of \a doc that a card can be put on. The number
    /// of objects that came to wear one.
    int read(App::Document* doc);
    /// The same over the objects given, which is what a caller with a
    /// selection or a fresh import has in hand.
    int read(const std::vector<App::DocumentObject*>& objects);

    /// Geometry names the look assigns that nothing in the document
    /// answered to. What a caller reports: the usual cause is that the
    /// mesh was imported under other names.
    const std::vector<std::string>& unmatched() const
    {
        return _unmatched;
    }

    /// Whether \a geom, a MaterialX geometry string, names \a label.
    static bool matches(const std::string& geom, const std::string& label);

private:
    /// The card for \a assignment, made once and shared by every object the
    /// assignment matches. Null when the look names a file that is not here.
    std::shared_ptr<Materials::Material> cardFor(const LookAssignment& assignment);

private:
    Base::FileInfo _file;
    std::string _look;
    std::vector<LookAssignment> _assignments;
    std::vector<LookFile> _images;
    std::vector<std::shared_ptr<Materials::Material>> _cards;
    std::vector<std::string> _unmatched;
};

}  // namespace Import

#endif  // IMPORT_READER_LOOK_H

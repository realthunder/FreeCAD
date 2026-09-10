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

#ifndef IMPORTGUI_READER_LOOK_GUI_H
#define IMPORTGUI_READER_LOOK_GUI_H

#include <string>
#include <vector>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Base
{
class FileInfo;
}

namespace ImportGui
{

/** Read the `<look>` of a MaterialX document onto a document's objects.
 *
 * The Gui half of docs/MaterialStorage.md sec 17.13 item 2, and thin on
 * purpose: it reads the file, asks Render::MaterialX what the document
 * says -- which is the one place that answers questions about MaterialX --
 * and hands the answer as plain data to Import::ReaderLook, which does the
 * FreeCAD work. It lives here because the renderer library is what carries
 * MaterialX, and the App tier does not link it.
 *
 * \a objects empty means every object of \a doc, which is what importing a
 * `.mtlx` on its own does; an import that has just made objects passes
 * those, so a second asset in the same document is not re-dressed.
 * \a look names which look to read, empty meaning the document's first.
 *
 * Returns how many objects came to wear a card, or -1 when the file could
 * not be read or states no look.
 */
int readLook(const Base::FileInfo& file,
             App::Document* doc,
             const std::vector<App::DocumentObject*>& objects = {},
             const std::string& look = {});

/** The look beside \a file, read onto \a objects.
 *
 * A mesh bundle carrying its materials in a MaterialX document beside it
 * is the arrangement the sec 17.13 ruling describes: `asset.glb` with
 * `asset.mtlx` next to it. Exactly that name -- a directory's other
 * documents are not this file's materials. Returns as readLook() does, or
 * 0 when there is no such file, which is the ordinary case.
 */
int readSidecarLook(const Base::FileInfo& file,
                    App::Document* doc,
                    const std::vector<App::DocumentObject*>& objects);

}  // namespace ImportGui

#endif  // IMPORTGUI_READER_LOOK_GUI_H

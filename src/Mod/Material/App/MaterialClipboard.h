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

#ifndef MATERIAL_MATERIALCLIPBOARD_H
#define MATERIAL_MATERIALCLIPBOARD_H

#include <string>
#include <vector>

#include <Mod/Material/MaterialGlobal.h>

namespace App
{
class Document;
class PropertyAppearanceList;
}  // namespace App

namespace Materials
{
class PropertyMaterial;

/** Copy Material / Paste Material: a material as a clipboard payload.
 *
 * docs/MaterialStorage.md 17.12, item 4. What an object HAS is the card
 * (`ShapeMaterial`: uuid, name, content, its shader graph files) and the
 * look (`ShapeAppearance`: base, per-face overrides, the follow flag),
 * and the payload carries both, self-contained: the card's canonical
 * YAML, the look as the XML the document writes, and the bytes of every
 * file either refers to by hash. That is what makes a paste work across
 * documents and across FreeCAD instances -- a target that lacks the
 * bytes stores them the way an import does.
 *
 * Paste sets the card (under FollowMaterial the look comes with it) and
 * then, when the source look was custom, the custom base and per-face
 * overrides; given face indices, the source's base lands as an override
 * on those faces and nothing else changes.
 *
 * The container is a line-oriented byte format, ASCII headers and raw
 * bytes: `FreeCAD Material 1`, then entries of `<kind> [args] <size>` on
 * a line followed by that many bytes.
 */
namespace Clipboard
{
/// The mime type the payload travels under
MaterialsExport const char* mimeType();
/// The payload for what \a card and \a look hold, either of which may be
/// null. Files are read through \a doc's store. Empty when there is
/// nothing to carry.
MaterialsExport std::string pack(const PropertyMaterial* card,
                                 const App::PropertyAppearanceList* look,
                                 const App::Document& doc);
/// Whether \a data is a payload pack() wrote
MaterialsExport bool isPayload(const std::string& data);
/** Apply \a data to \a card and \a look (either may be null), storing the
 * files it carries in \a doc. \a faces non-empty restricts the look to
 * per-face overrides on those faces. False when \a data is not a payload
 * or nothing in it applied.
 */
MaterialsExport bool apply(const std::string& data,
                           PropertyMaterial* card,
                           App::PropertyAppearanceList* look,
                           const std::vector<int>& faces,
                           App::Document& doc);
}  // namespace Clipboard

}  // namespace Materials

#endif  // MATERIAL_MATERIALCLIPBOARD_H

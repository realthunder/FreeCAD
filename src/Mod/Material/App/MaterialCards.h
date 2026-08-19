// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#pragma once

#include <memory>
#include <string>

#include <QString>

#include <Mod/Material/MaterialGlobal.h>

namespace Materials
{

class Material;

/** The parsed cards behind stored content, one instance per distinct card.
 *
 * Storage de-duplication buys nothing at runtime on its own: five hundred
 * steel brackets referring to one stored card still parse it five hundred
 * times unless something remembers. This is that something -- the
 * `hash -> shared_ptr<const Material>` cache docs/MaterialStorage.md sec 3
 * calls for, and the place the preset index (sec 5) hangs off.
 *
 * Cards are handed out `const`. That is what makes copy-on-write honest: no
 * referrer can reach into a card another one is using, so an edit has to
 * allocate, which in turn is what lets a property copy be a refcount
 * increment.
 *
 * Entries are weak: a card stays only while something refers to it, so a
 * document that closes takes its cards with it.
 */
class MaterialsExport MaterialCards
{
public:
    /** The card for stored content, parsing the file only if it has to.
     *
     * \a hash identifies the content and \a path is where it currently sits.
     * \a uuid and \a name are the provenance the referring property carries,
     * and they are stamped onto the card -- so they are part of the cache key
     * too: two documents naming the same content differently want the name
     * each of them stored, and that is the only case where identical content
     * costs two instances.
     *
     * Returns null when the file cannot be read or parsed. That is not the
     * same as "no material" and the caller must not treat it as one: see
     * PropertyMaterial::Restore, which keeps the assignment visible instead.
     */
    static std::shared_ptr<const Material> load(const std::string& hash,
                                                const QString& path,
                                                const QString& uuid,
                                                const QString& name);

    /// The cached card for content and provenance, or null. Never parses.
    static std::shared_ptr<const Material> find(const std::string& hash,
                                                const QString& uuid,
                                                const QString& name);

    /** Take a card that is already in memory as the value for stored content.
     *
     * Shares the instance outright when the provenance matches -- which for a
     * stock card it usually does -- and otherwise stamps a copy, since the
     * name belongs to the referrer and not to the content.
     */
    static std::shared_ptr<const Material> adopt(const std::string& hash,
                                                 const QString& uuid,
                                                 const QString& name,
                                                 const std::shared_ptr<const Material>& card);

    /** The installed card whose canonical content is this hash, or null.
     *
     * Stock cards ship with FreeCAD, so a document using one need not carry a
     * copy: this index is what lets a save leave the content out and a
     * restore find it again. Matching is by content, so a library card edited
     * under the same uuid does not masquerade as the card the document was
     * saved with -- that document carries its own copy, and this returns
     * nothing for it (docs/MaterialStorage.md sec 5, sec 6).
     *
     * Built on first use and thrown away by MaterialManager::refresh().
     */
    static std::shared_ptr<const Material> preset(const std::string& hash);

    /// Drop the preset index. The libraries changed under it.
    static void clearPresets();

    /** Offer a card the cache did not have to parse.
     *
     * The preset index uses this: a stock card is already in memory, so a
     * document blob whose hash matches one resolves to the instance the
     * library is already holding rather than to a second copy of it.
     */
    static void insert(const std::string& hash,
                       const QString& uuid,
                       const QString& name,
                       const std::shared_ptr<const Material>& card);

    /// Live entries. For tests and diagnostics.
    static std::size_t size();
};

}  // namespace Materials

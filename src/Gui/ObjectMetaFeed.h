/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef GUI_OBJECTMETAFEED_H
#define GUI_OBJECTMETAFEED_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <FCGlobal.h>

namespace Render
{
class Renderer;
}

namespace Gui
{

/** The labels a remote viewer names objects by.
 *
 * A published object entry carries its document and internal name — its
 * identity, which is fixed and which the publish path reads off the
 * render cache for nothing. What it does not carry is what a human calls
 * the object: a `Label` is presentation, it describes no geometry, and
 * it changes only when somebody renames something. So it does not belong
 * on the publish path, where it used to cost a document lookup per draw
 * per publish for a table only a remote viewer reads
 * (docs/ThinClient.md §4.1).
 *
 * This is where it belongs instead. Both publishers use it — the
 * view-less serve source and a desktop view whose backend is serving —
 * so neither owns the observers.
 *
 * **What it sends is a delta.** The renderer holds the resident table;
 * this side keeps only a journal of which objects changed, so a rename
 * in an 18000-object document sends one entry rather than 18000. The
 * journal matters more for creation than for renames: `feed()` runs per
 * published frame, so a live import announcing 17800 new objects would
 * otherwise rebuild and re-send a growing table on every frame of it.
 * A renderer that has never been fed, or one so far behind that the
 * journal no longer reaches it, gets the whole table once instead.
 *
 * Nothing is resolved until a renderer asks: an event costs two string
 * copies, and a process with nothing serving never walks a document.
 *
 * ⚠️ Every name and label here is UTF-8 that may hold anything a Python
 * identifier may, internal names included. The table is keyed by
 * document name and then object name for that reason: there is no
 * separator byte a joined key could rely on.
 */
class GuiExport ObjectMetaFeed
{
public:
    static ObjectMetaFeed& instance();

    /// Bring \a renderer up to date: nothing if it already is, a delta
    /// if the journal reaches back far enough, the whole table if not.
    void feed(Render::Renderer* renderer);

    /// Drop what is remembered about \a renderer, which is going away.
    /// Without it a later renderer allocated at the same address could
    /// be taken for one that had already been fed.
    void forget(const Render::Renderer* renderer);

private:
    ObjectMetaFeed();

    /// One object whose label may have changed, by name. Names rather
    /// than pointers: a removal has to survive the object it names, and
    /// an internal name never changes.
    struct Change
    {
        std::string doc;
        std::string obj;
        bool removed = false;
    };

    /// Note that \a doc / \a obj needs re-sending. Called from the
    /// signal handlers, and deliberately does no lookup of its own.
    void note(const char* doc, const char* obj, bool removed);

    /// Forget the journal and make every renderer take a full table
    /// next time. What a document closing does, and what an overlong
    /// journal does rather than grow without bound.
    void dropHistory();

    /// Bumped once per journalled change; a renderer records the value
    /// it was last brought up to.
    uint64_t version = 0;
    /// The version the first surviving journal entry carries. A
    /// renderer older than this cannot be caught up by delta.
    uint64_t journalBase = 0;
    std::vector<Change> journal;
    std::unordered_map<const Render::Renderer*, uint64_t> fed;
};

}  // namespace Gui

#endif  // GUI_OBJECTMETAFEED_H

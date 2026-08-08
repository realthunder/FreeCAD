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

#ifndef GUI_DRAINCURSOR_H
#define GUI_DRAINCURSOR_H

#include <cstddef>
#include <string>
#include <vector>

#include <App/Document.h>
#include <App/DocumentObject.h>

namespace Gui
{

/** A cursor over the objects a document had when a drain started.
 *
 * The deferred view-provider drain (docs/DocumentLoad.md §13) runs in
 * budgeted slices across event-loop turns, which is the same thing as
 * saying the document is live between them: the user, a macro, or a
 * restore handler may add or delete objects while the drain is only half
 * done. An index into the document's own object array cannot survive
 * that. A deletion shifts every later element down one, the cursor steps
 * over an object, and that object is never handed to the phase that owed
 * it work -- left with no view provider (phase one) or with Gui::isRestoring
 * still set and no mode switch (phase three), which reads to the user as an
 * object that loaded but will not show.
 *
 * The cursor therefore records the object NAMES once, when the drain
 * starts, and resolves each one on arrival:
 *
 * - an object deleted meanwhile resolves to nothing and is skipped, and
 *   the objects after it keep their turn;
 * - an object added after the snapshot is deliberately absent. It never
 *   went through the load's parked path, so it gets its view provider from
 *   slotNewObject() at creation like any other live addition -- handing it
 *   to the drain as well would restore it twice;
 * - a name reused by a new object resolves to that object, which is a live
 *   object needing exactly the work the phase does (phase three does
 *   nothing to a view provider that is not mid-restore).
 *
 * Names, not pointers: same rule as everything else the progressive load
 * parks (docs/ProgressiveLoading.md §7).
 */
class DrainCursor
{
public:
    /// Record \a doc's objects as the set this drain owes work to.
    void snapshot(const App::Document* doc)
    {
        _names.clear();
        _pos = 0;
        _ready = true;
        if (!doc) {
            return;
        }
        const auto& objs = doc->getObjects();
        _names.reserve(objs.size());
        for (auto obj : objs) {
            if (const char* name = obj->getNameInDocument()) {
                _names.emplace_back(name);
            }
        }
    }

    /// Forget the snapshot; the next drain takes its own.
    void clear()
    {
        _names.clear();
        _names.shrink_to_fit();
        _pos = 0;
        _ready = false;
    }

    /// Whether a snapshot has been taken. An empty document snapshots
    /// empty, which is ready and done at once.
    bool ready() const
    {
        return _ready;
    }

    /// Whether every recorded object has been handed out.
    bool done() const
    {
        return _pos >= _names.size();
    }

    /// Walk the same snapshot again, for the phase that follows.
    void rewind()
    {
        _pos = 0;
    }

    /// How far the walk got, and how far it has to go. For the progress
    /// line only -- position() counts names consumed, including the ones
    /// that resolved to nothing.
    std::size_t position() const
    {
        return _pos;
    }
    std::size_t size() const
    {
        return _names.size();
    }

    /** The next recorded object that is still in \a doc.
     *
     * Null when the snapshot is spent, so a slice loop reads
     * `while (auto obj = cursor.next(doc))`. Objects deleted since the
     * snapshot are stepped over silently -- that is the point.
     */
    App::DocumentObject* next(const App::Document* doc)
    {
        while (_pos < _names.size()) {
            App::DocumentObject* obj = doc ? doc->getObject(_names[_pos].c_str()) : nullptr;
            ++_pos;
            if (obj) {
                return obj;
            }
        }
        return nullptr;
    }

private:
    std::vector<std::string> _names;
    std::size_t _pos {0};
    bool _ready {false};
};

}  // namespace Gui

#endif  // GUI_DRAINCURSOR_H

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

#ifndef GUI_VIEWVISIBILITY_H
#define GUI_VIEWVISIBILITY_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <FCGlobal.h>

#include "Inventor/SoFCVisibilityElement.h"
#include "Renderer/Renderer.h"

namespace App {
class Document;
}

namespace Gui {

/** One view's own object visibility (docs/CoinRetirement.md 5.18).
 *
 * The parsed table the backend resolves draws against, the same table
 * as SoFCVisibilityElement carries it for the Coin traversals, and the
 * objects the table makes this view count: every object its entries END
 * at (SoFCVisibilityElement::countOverride -- the only switches that read
 * the element, in any view) and every object it SHOWS (SoFCSwitch::
 * setPerViewShown and a forced ViewProvider update, so a hidden one is
 * tessellated and captured). Held by a desktop view (View3DInventorViewer)
 * and by a served client's view (MirrorViewer) alike; the counts are
 * released when the table drops them and with the holder.
 */
class GuiExport ViewVisibility
{
public:
    ViewVisibility() = default;
    ~ViewVisibility();
    ViewVisibility(const ViewVisibility &) = delete;
    ViewVisibility &operator=(const ViewVisibility &) = delete;

    /// Replace the table. False when nothing changed (no entries before
    /// or after); otherwise the holder has to tell whatever caches what
    /// it answered -- its selection root, its backend.
    bool set(Render::VisibilityOverrideTable &&table);
    /// Drop the table and release every count.
    void clear();

    /// The table, or null when it has no entries.
    const Render::VisibilityOverrideTable *table() const;
    /// The table as SoFCVisibilityElement carries it, or null.
    const SoFCVisibilityElement::Table *elementTable() const;

private:
    Render::VisibilityOverrideTable entries;
    uint32_t serial = 0;
    SoFCVisibilityElement::Table element;
    std::set<std::pair<std::string, std::string>> shown;
    std::set<std::pair<std::string, std::string>> overridden;
};

/// Resolve one per-view override KEY -- the form ObjectDisplayModes and
/// ObjectVisibilities share -- into {doc, obj} steps. A key with no dot
/// is BARE (the object wherever it appears in the view; "Doc#Obj" for an
/// object of another document shown through a link); one with a dot is
/// a subname PATH from a top-level object of \a doc, naming one
/// occurrence. False when the key does not resolve -- its object was
/// deleted -- which callers treat as inert, not as an error.
GuiExport bool parseOverrideKey(const std::string &key,
                                App::Document *doc,
                                std::vector<Render::ObjectRef> &path,
                                bool &rooted);

/// Parse an ObjectVisibilities map ("1" shown, "0" hidden) into a
/// visibility table. Bare entries count only while \a perView
/// (PerViewVisibilities) is on; path entries always count.
GuiExport Render::VisibilityOverrideTable parseObjectVisibilities(
        const std::map<std::string, std::string> &values,
        App::Document *doc,
        bool perView);

} // namespace Gui

#endif // GUI_VIEWVISIBILITY_H

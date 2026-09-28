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

#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <FCGlobal.h>

#include "Inventor/SoFCVisibilityElement.h"
#include "Renderer/Renderer.h"

class SoFCRenderCacheManager;

namespace App {
class Document;
class DocumentObject;
}

namespace Gui {

/// One visibility entry as a view states it, before it is resolved
/// against the scene: a BARE entry is \c obj wherever its own root is
/// drawn; a PATH entry (\c rooted) is the one occurrence \c subname
/// names under the top-level object \c obj. Names, not nodes, because
/// the scene an entry resolves to changes under it -- an object moved
/// into a group, a link relinked -- and the view's map outlives that.
struct VisibilityEntry {
    std::string doc;        ///< document internal name of \c obj
    std::string obj;        ///< object internal name
    std::string subname;    ///< from \c obj, dot terminated; empty when bare
    bool rooted = true;
    bool visible = false;
};

/** One view's own object visibility (docs/CoinRetirement.md 5.18, 5.23).
 *
 * The entries resolved into NODE keys (SoFCVisibilityElement::Entry):
 * a path entry into the selection roots of the occurrence's node path
 * (ViewProvider::getDetailPath with append, from the top-level object),
 * a bare one into the object's own root. That one table is what the
 * view's Coin traversals read (the element) and what the draws are
 * resolved against (drawSet(), the per-objectKey answer the backend
 * filters by), with one matcher, so the two cannot disagree. Held by a
 * desktop view (View3DInventorViewer) and by a served client's view
 * (MirrorViewer) alike.
 *
 * The table also makes this view count, in the process-wide sets every
 * view shares: every root an entry ENDS at (SoFCVisibilityElement::
 * countOverride -- the only switches that read the element, in any
 * view) and every object it SHOWS (SoFCSwitch::setPerViewShown and a
 * forced ViewProvider update, so a hidden one is tessellated and
 * captured). Released when the table drops them and with the holder.
 *
 * Two sources feed the one table. The PERSISTED entries are the view's
 * own map (set()); the TRANSIENT ones are what an edit session hides in
 * this view while it runs (setTransient(): the occurrence being edited,
 * whose geometry the session draws itself). The transient entries come
 * first, so an edit hide beats a persisted show of the same path. They
 * are never written anywhere: a save during the edit sees only the map.
 *
 * Node keys follow the scene, not the names: after a change to the
 * document's STRUCTURE -- an object added or removed, a link property
 * changed (a group's members, a link's target) -- every table with
 * entries is resolved again once the event loop is back, and a holder
 * whose keys changed is told (setOnChanged()). Not per recompute: a
 * recompute moves no node.
 */
class GuiExport ViewVisibility
{
public:
    ViewVisibility();
    ~ViewVisibility();
    ViewVisibility(const ViewVisibility &) = delete;
    ViewVisibility &operator=(const ViewVisibility &) = delete;

    /// Replace the persisted entries. False when the table came out the
    /// same; otherwise the holder has to tell whatever caches what it
    /// answered -- its selection root, its backend.
    bool set(std::vector<VisibilityEntry> &&entries);
    /// Replace the transient entries; false as set().
    bool setTransient(std::vector<VisibilityEntry> &&entries);
    /// Drop both sources and release every count.
    void clear();

    /// Called when a deferred resolution -- after a structure change --
    /// changed the table, with the same duty as a true from set().
    void setOnChanged(std::function<void()> callback);

    /// The table as SoFCVisibilityElement carries it, or null when it has
    /// no entries.
    const SoFCVisibilityElement::Table *elementTable() const;

    /// The table's answer per draw of the scene \a feed captures -- the
    /// render-cache manager feeding the backend the set is for -- or null
    /// when the table has no entries. Resolved again when the table or
    /// the feed's draw identities changed since the last call; the set's
    /// version moves only when its content does.
    const Render::VisibilitySet *drawSet(SoFCRenderCacheManager *feed);

    /// The scene's structure changed where no App signal says so -- a
    /// container rebuilt its 3D children (Document::handleChildren3D), as
    /// a load does once the children's view providers exist: every table
    /// with entries is resolved again once the event loop is back.
    static void sceneChanged() { scheduleResolve(); }

    /// What keeping the tables resolved has cost, process-wide, since the
    /// last reset (docs/CoinRetirement.md 5.24): the deferred passes after
    /// structure changes, the direct rebuilds of set()/setTransient(), and
    /// the per-draw rescans of drawSet(). Two clock reads per rebuild or
    /// rescan, nothing per frame. FreeCADGui.viewVisibilityStats().
    struct Stats {
        uint64_t triggers = 0;      ///< structure changes heard (scheduleResolve)
        uint64_t scheduled = 0;     ///< of them, the ones that queued a pass
        uint64_t passes = 0;        ///< deferred passes run
        uint64_t passTables = 0;    ///< tables rebuilt by them
        uint64_t passEntries = 0;   ///< entries they resolved
        uint64_t passResolved = 0;  ///< of those, the ones that resolved
        uint64_t passChanged = 0;   ///< tables whose keys came out different
        uint64_t passNs = 0;
        uint64_t sets = 0;          ///< set()/setTransient() rebuilds
        uint64_t setEntries = 0;
        uint64_t setResolved = 0;
        uint64_t setNs = 0;
        uint64_t draws = 0;         ///< drawSet() rescans
        uint64_t drawKeys = 0;      ///< draw keys they scanned
        uint64_t drawNs = 0;
    };
    static Stats &stats();

private:
    /// Resolve both sources and recount; false when the table came out
    /// the same.
    bool rebuild();
    /// rebuild() for set()/setTransient(), counted.
    bool rebuildSet();
    static void scheduleResolve();

    std::vector<VisibilityEntry> persisted;
    std::vector<VisibilityEntry> transient;
    /// The resolved entries in source order, what rebuild() compares.
    std::vector<SoFCVisibilityElement::Entry> resolved;
    SoFCVisibilityElement::Table element;
    uint32_t serial = 0;
    std::set<uint32_t> overridden;
    std::set<std::pair<std::string, std::string>> shown;
    std::function<void()> onChanged;

    Render::VisibilitySet draws;
    const SoFCRenderCacheManager *drawFeed = nullptr;
    uint64_t drawInfoSerial = 0;
    uint32_t drawTableVersion = 0;
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

/// Resolve the occurrence \a subname names under \a root -- the object
/// path of a subname, its element name ignored -- into {doc, obj} steps
/// from \a root down; a null or empty \a subname is \a root itself.
/// False when a step does not resolve.
GuiExport bool resolveObjectPath(App::DocumentObject *root,
                                 const char *subname,
                                 std::vector<Render::ObjectRef> &path);

/// Parse an ObjectVisibilities map ("1" shown, "0" hidden) into entries.
/// Bare entries count only while \a perView (PerViewVisibilities) is
/// on; path entries always count.
GuiExport std::vector<VisibilityEntry> parseObjectVisibilities(
        const std::map<std::string, std::string> &values,
        App::Document *doc,
        bool perView);

} // namespace Gui

#endif // GUI_VIEWVISIBILITY_H

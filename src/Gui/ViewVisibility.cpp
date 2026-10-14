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

#include "PreCompiled.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <tuple>
#include <unordered_map>

#include <QCoreApplication>
#include <QTimer>

#include <Inventor/SoPath.h>
#include <Inventor/details/SoDetail.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyElementAppearance.h>
#include <App/PropertyLinks.h>
#include <Base/Console.h>

#include "Application.h"
#include "OwnNodesPath.h"
#include "RenderParams.h"
#include "SoFCUnifiedSelection.h"
#include "View3DInventor.h"
#include "ViewProviderDocumentObject.h"
#include "ViewVisibility.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "Inventor/SoFCSwitch.h"
#include "Renderer/MeshSource.h"

using namespace Gui;

namespace {

/// The ViewProvider of \a doc#\a obj, if it has one.
ViewProviderDocumentObject *viewProviderOf(const std::string &docName,
                                           const std::string &objName)
{
    if (!Application::Instance)
        return nullptr;
    auto doc = App::GetApplication().getDocument(docName.c_str());
    auto obj = doc ? doc->getObject(objName.c_str()) : nullptr;
    return Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
            Application::Instance->getViewProvider(obj));
}

/// The display-mode switch of \a doc#\a obj, if it is an SoFCSwitch.
SoFCSwitch *switchOf(const std::string &docName, const std::string &objName)
{
    auto vp = viewProviderOf(docName, objName);
    SoSwitch *sw = vp ? vp->getModeSwitch() : nullptr;
    return (sw && sw->isOfType(SoFCSwitch::getClassTypeId()))
        ? static_cast<SoFCSwitch *>(sw) : nullptr;
}

/// Evict released per-view-shown entries until \a deficit bytes are
/// covered (Render::MeshSourceRegistry's shown evictor). Each candidate's
/// bytes belong to the entry it draws under -- the innermost object on
/// its chain that has one, and a candidate whose innermost entry some
/// view still counts is not evictable at all. Ranked by size times time
/// since release: the big and the long unwanted go first, a quick
/// hide-show keeps what it is toggling.
size_t evictShown(const std::vector<Render::MeshSourceRegistry::ShownCandidate> &candidates,
                  size_t deficit)
{
    if (!Application::Instance)
        return 0;
    struct Entry {
        size_t bytes = 0;
        double age = 0.0;
        const Render::ObjectRef *ref = nullptr;
    };
    std::map<SoFCSwitch *, Entry> entries;
    for (const auto &cand : candidates) {
        for (auto it = cand.path.rbegin(); it != cand.path.rend(); ++it) {
            SoFCSwitch *sw = switchOf(it->doc, it->obj);
            if (!sw || !SoFCSwitch::isPerViewShown(sw))
                continue;
            const double age = SoFCSwitch::perViewShownReleasedAge(sw);
            if (age >= 0.0) {
                auto &entry = entries[sw];
                entry.bytes += cand.bytes;
                entry.age = age;
                entry.ref = &*it;
            }
            break;
        }
    }
    std::vector<std::pair<double, std::pair<SoFCSwitch *, const Entry *>>> ranked;
    for (const auto &[sw, entry] : entries) {
        if (entry.bytes)
            ranked.push_back({double(entry.bytes) * entry.age, {sw, &entry}});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    size_t freed = 0;
    for (const auto &item : ranked) {
        if (freed >= deficit)
            break;
        const Entry &entry = *item.second.second;
        if (!SoFCSwitch::evictPerViewShown(item.second.first))
            continue;
        freed += entry.bytes;
        if (RenderParams::getLevelDebug())
            Base::Console().Message(
                    "render levels: evict released per-view-shown %s#%s "
                    "(%.1fKB, released %.1fs ago)\n",
                    entry.ref->doc.c_str(), entry.ref->obj.c_str(),
                    double(entry.bytes) / 1024.0, entry.age);
    }
    return freed;
}

/// Released per-view-shown entries go first under memory pressure:
/// the evictor is registered once, the first time any view shows an
/// object on its own.
void registerShownEvictor()
{
    static bool registered;
    if (registered)
        return;
    registered = true;
    Render::MeshSourceRegistry::instance().setShownEvictor(
            &evictShown, &SoFCSwitch::releasedPerViewShownCount);
}

/// Count or release the root \a id as one some view has an entry ending
/// at. Its switch is touched when that changes whether ANY view has one:
/// the caches above it (shared by every view) were built without reading
/// the element, or will stop reading it. A root deleted meanwhile has no
/// caches left to tell.
void countOverride(uint32_t id, bool add)
{
    if (!SoFCVisibilityElement::countOverride(id, add))
        return;
    SoFCSelectionRoot *root = SoFCSelectionRoot::getRootById(id);
    if (!root)
        return;
    // The root's own switch, the one the element is read by.
    for (int i = 0, n = root->getNumChildren(); i < n; ++i) {
        SoNode *child = root->getChild(i);
        if (child->isOfType(SoFCSwitch::getClassTypeId())) {
            child->touch();
            return;
        }
    }
    root->touch();
}

/// Add \a obj to \a deps, and what it links to, link by link.
void addDependency(const App::DocumentObject *obj,
                   std::vector<const App::DocumentObject *> &deps)
{
    while (obj && std::find(deps.begin(), deps.end(), obj) == deps.end()) {
        deps.push_back(obj);
        auto linked = obj->getLinkedObject(false);
        obj = linked != obj ? linked : nullptr;
    }
}

/// The key of an object the entry's own is seen through, and that object.
struct MemberKey {
    std::vector<uint32_t> key;
    std::pair<std::string, std::string> leaf;
};

bool resolveEntry(const VisibilityEntry &entry,
                  std::vector<uint32_t> &key,
                  std::pair<std::string, std::string> &leaf,
                  std::vector<const App::DocumentObject *> &deps,
                  std::vector<std::pair<std::string, std::string>> &steps,
                  std::vector<MemberKey> &members,
                  bool &seenThrough,
                  bool &ownKey,
                  int depth = 0);

/// The keys of what \a owner, the object \a entry ends at, is seen through
/// (SeenThrough): the children of a plain group, drawn beside the group and
/// not under it, and so on down a group in a group. Each is named as the
/// owner is, one step up -- 'Part.Box.' for a child of 'Part.Group.', the
/// object itself for a child of a top-level group -- and has the entry's
/// value. What is on their paths is added to \a deps; the owner is there
/// already, and a change of what it holds resolves the entry again.
/// \a ownKey comes back false where a show of the owner is not one of its
/// own nodes: those of a plain group, which hold every child once more.
void resolveMembers(const VisibilityEntry &entry,
                    const App::DocumentObject *owner,
                    std::vector<const App::DocumentObject *> &deps,
                    std::vector<MemberKey> &members,
                    bool &seenThrough,
                    bool &ownKey,
                    int depth)
{
    // A group holding itself, some way down
    constexpr int maxDepth = 16;
    if (!owner || depth >= maxDepth)
        return;
    // More are hidden with an object than are shown with it, never fewer
    auto list = SeenThrough::of(owner, SeenThrough::For::Hide);
    if (list.empty())
        return;
    seenThrough = true;
    if (entry.visible) {
        list = SeenThrough::of(owner, SeenThrough::For::Show);
        auto self = std::find(list.begin(), list.end(), owner);
        ownKey = self != list.end();
        if (ownKey)
            list.erase(self);
    }

    const bool bare = !entry.rooted || entry.subname.empty();
    std::string parent;
    if (!bare && entry.subname.size() > 1) {
        const auto pos = entry.subname.rfind('.', entry.subname.size() - 2);
        if (pos != std::string::npos)
            parent = entry.subname.substr(0, pos + 1);
    }
    for (auto member : list) {
        VisibilityEntry sub;
        sub.visible = entry.visible;
        if (bare) {
            sub.rooted = false;
            sub.doc = member->getDocument()->getName();
            sub.obj = member->getNameInDocument();
        }
        else {
            sub.rooted = true;
            sub.doc = entry.doc;
            sub.obj = entry.obj;
            sub.subname = parent + member->getNameInDocument() + ".";
        }
        MemberKey found;
        std::vector<const App::DocumentObject *> subDeps;
        // Those of the owner, which is one step from where the member is
        std::vector<std::pair<std::string, std::string>> subSteps;
        bool nested = false;
        bool nestedOwn = true;
        if (resolveEntry(sub, found.key, found.leaf, subDeps, subSteps, members, nested,
                         nestedOwn, depth + 1)
                && nestedOwn)
            members.push_back(std::move(found));
        for (const auto *dep : subDeps) {
            if (std::find(deps.begin(), deps.end(), dep) == deps.end())
                deps.push_back(dep);
        }
    }
}

/// Resolve \a entry into its node key, and the object it ends at in
/// \a leaf. False when it does not resolve -- its object gone, its path
/// no longer through the scene -- which leaves it inert until the next
/// resolution. \a deps gets the objects the resolution went through,
/// resolved or not: a change to any of them may move the key. \a members
/// gets the keys of what the object is seen through, \a seenThrough
/// whether it is seen through anything, whichever value the entry has, and
/// \a ownKey whether the entry's own key counts (resolveMembers()).
bool resolveEntry(const VisibilityEntry &entry,
                  std::vector<uint32_t> &key,
                  std::pair<std::string, std::string> &leaf,
                  std::vector<const App::DocumentObject *> &deps,
                  std::vector<std::pair<std::string, std::string>> &steps,
                  std::vector<MemberKey> &members,
                  bool &seenThrough,
                  bool &ownKey,
                  int depth)
{
    key.clear();
    deps.clear();
    steps.clear();
    if (!Application::Instance)
        return false;
    auto doc = App::GetApplication().getDocument(entry.doc.c_str());
    auto obj = doc ? doc->getObject(entry.obj.c_str()) : nullptr;
    addDependency(obj, deps);
    const bool bare = !entry.rooted || entry.subname.empty();
    // The path first, for its dependencies: an entry that fails further on
    // must still be tried again when a step of it changes.
    std::vector<Render::ObjectRef> refs;
    bool pathOk = true;
    if (!bare) {
        pathOk = resolveObjectPath(obj, entry.subname.c_str(), refs);
        // The steps it got through, each with what it links to: a Link's
        // target is where the children it shows are grouped.
        for (size_t i = 1; i < refs.size(); ++i) {
            auto stepDoc = App::GetApplication().getDocument(refs[i].doc.c_str());
            addDependency(stepDoc ? stepDoc->getObject(refs[i].obj.c_str()) : nullptr, deps);
        }
    }
    auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
            Application::Instance->getViewProvider(obj));
    if (!vp)
        return false;
    if (bare) {
        // The object's own root: wherever it is drawn -- which a Link to
        // the object is not, having a root of its own.
        SoNode *root = vp->getRoot();
        if (!root || !root->isOfType(SoFCSelectionRoot::getClassTypeId()))
            return false;
        key.push_back(static_cast<SoFCSelectionRoot *>(root)->getSelNodeId());
        leaf = {entry.doc, entry.obj};
        resolveMembers(entry, obj, deps, members, seenThrough, ownKey, depth);
        return true;
    }
    if (!pathOk)
        return false;
    // The occurrence's node path from the top-level object's REAL root
    // (append), and of it the selection roots: what a traversal's stack
    // holds when it reaches that object's switch, and what the render
    // cache composes the draw's key of.
    // The object's own nodes, wherever it is seen through others: the key
    // is resolved again when the structure changes, not when something is
    // shown or hidden.
    OwnNodesPath ownNodes;
    CoinPtr<SoPath> path(new SoPath(10));
    SoDetail *det = nullptr;
    const bool ok = vp->getDetailPath(entry.subname.c_str(),
                                      static_cast<SoFullPath *>(path.get()), true, det);
    delete det;
    if (!ok)
        return false;
    auto full = static_cast<SoFullPath *>(path.get());
    for (int i = 0, n = full->getLength(); i < n; ++i) {
        SoNode *node = full->getNode(i);
        if (node->isOfType(SoFCSelectionRoot::getClassTypeId()))
            key.push_back(static_cast<SoFCSelectionRoot *>(node)->getSelNodeId());
    }
    if (key.empty())
        return false;
    leaf = {refs.back().doc, refs.back().obj};
    for (size_t i = 0; i + 1 < refs.size(); ++i)
        steps.emplace_back(refs[i].doc, refs[i].obj);
    auto leafDoc = App::GetApplication().getDocument(leaf.first.c_str());
    resolveMembers(entry, leafDoc ? leafDoc->getObject(leaf.second.c_str()) : nullptr,
                   deps, members, seenThrough, ownKey, depth);
    return true;
}

/// A table's content in one canonical order (by the root its entries end
/// at, each list as the table keeps it), what two tables must share to
/// answer alike.
using TableContent = std::vector<std::pair<uint32_t, std::vector<std::pair<std::vector<uint32_t>, int8_t>>>>;

struct Interned {
    uint64_t id = 0;
    int refs = 0;
};

/// The content of every live table, and the identity it is known by
/// (SoFCVisibilityElement::Table::identity). An identity lives while some
/// table holds it; one released for good is never handed out again, so a
/// cache recorded against it can match nothing later.
std::map<TableContent, Interned> &identities()
{
    static auto *map = new std::map<TableContent, Interned>;
    return *map;
}

std::unordered_map<uint64_t, std::map<TableContent, Interned>::iterator> &identityIndex()
{
    static auto *map = new std::unordered_map<uint64_t, std::map<TableContent, Interned>::iterator>;
    return *map;
}

uint64_t acquireIdentity(const SoFCVisibilityElement::Table &table)
{
    static uint64_t lastId;
    TableContent content;
    content.reserve(table.byEnd.size());
    for (const auto &[end, list] : table.byEnd) {
        std::vector<std::pair<std::vector<uint32_t>, int8_t>> entries;
        entries.reserve(list.size());
        for (const auto &entry : list)
            entries.emplace_back(entry.key, entry.visibility);
        content.emplace_back(end, std::move(entries));
    }
    std::sort(content.begin(), content.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    auto res = identities().emplace(std::move(content), Interned());
    auto &interned = res.first->second;
    if (res.second) {
        interned.id = ++lastId;
        identityIndex()[interned.id] = res.first;
    }
    ++interned.refs;
    return interned.id;
}

void releaseIdentity(uint64_t id)
{
    auto it = identityIndex().find(id);
    if (it == identityIndex().end())
        return;
    if (--it->second->second.refs > 0)
        return;
    identities().erase(it->second);
    identityIndex().erase(it);
}

/// Every table with entries, for the resolution after a structure change.
/// Never destroyed: a holder can outlive static destruction's turn for it
/// (a served mirror owned by a singleton).
std::set<ViewVisibility *> &instances()
{
    static auto *set = new std::set<ViewVisibility *>;
    return *set;
}

bool ResolvePending = false;
/// What the structure changes since the last pass touched: the objects
/// whose entries must resolve again, whether the unresolved ones must try
/// again, and whether every entry must.
std::set<const App::DocumentObject *> PendingObjects;
bool PendingUnresolved = false;
bool PendingAll = false;

/// How many entries, over every table, show an object that is seen through
/// others. While there is none, what a group remembers as hidden by hand
/// moves no key, and a group writes that map every time one of its
/// children is shown or hidden: a pass for each would be a pass for nothing.
int ShowsSeenThrough = 0;

/// Count or release \a key's object as shown by one view.
void countShown(const std::pair<std::string, std::string> &key, bool enable)
{
    auto vp = viewProviderOf(key.first, key.second);
    if (!vp)
        return;
    vp->forceUpdate(enable);
    SoSwitch *sw = vp->getModeSwitch();
    if (sw && sw->isOfType(SoFCSwitch::getClassTypeId()))
        SoFCSwitch::setPerViewShown(static_cast<SoFCSwitch *>(sw), enable);
    if (enable)
        registerShownEvictor();
}

} // namespace

ViewVisibility::ViewVisibility() = default;

ViewVisibility::~ViewVisibility()
{
    instances().erase(this);
    clear();
}

ViewVisibility::Stats &ViewVisibility::stats()
{
    static Stats s;
    return s;
}

namespace {
/// For the counters.
uint64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

bool ViewVisibility::set(std::vector<VisibilityEntry> &&entries)
{
    return rebuildSet(persisted, persistedRes, std::move(entries));
}

bool ViewVisibility::setTransient(std::vector<VisibilityEntry> &&entries)
{
    return rebuildSet(transient, transientRes, std::move(entries));
}

bool ViewVisibility::rebuildSet(std::vector<VisibilityEntry> &source,
                                std::vector<Resolution> &results,
                                std::vector<VisibilityEntry> &&entries)
{
    const uint64_t t = nowNs();
    size_t count = 0;
    const bool changed = rebuild(source, results, std::move(entries), count);
    auto &st = stats();
    ++st.sets;
    st.setEntries += persisted.size() + transient.size();
    st.setResolves += count;
    st.setResolved += resolved.size();
    st.setNs += nowNs() - t;
    return changed;
}

void ViewVisibility::setOnChanged(std::function<void()> callback)
{
    onChanged = std::move(callback);
}

void ViewVisibility::scheduleResolve()
{
    // Once the event loop is back: a structure change reaches the scene
    // graph through the view providers, which hear of it on the same
    // signal and may rebuild their nodes later still.
    ++stats().triggers;
    if (ResolvePending)
        return;
    if (instances().empty() || !QCoreApplication::instance()) {
        // No table to keep: one set later resolves in full. Kept, the
        // marks would make the first pass after it resolve what they
        // named then.
        PendingObjects.clear();
        PendingUnresolved = PendingAll = false;
        return;
    }
    ++stats().scheduled;
    ResolvePending = true;
    QTimer::singleShot(0, QCoreApplication::instance(), []() {
        ResolvePending = false;
        const auto dirty = std::move(PendingObjects);
        PendingObjects.clear();
        const bool unresolved = PendingUnresolved;
        const bool all = PendingAll;
        PendingUnresolved = PendingAll = false;
        // Copied: a holder told of a change may set its tables again.
        std::vector<ViewVisibility *> list(instances().begin(), instances().end());
        auto &st = stats();
        ++st.passes;
        for (ViewVisibility *vis : list) {
            if (!instances().count(vis))
                continue;
            const uint64_t t = nowNs();
            const std::size_t builds = ViewProvider::VisualBuildCount;
            size_t count = 0;
            const bool changed = vis->refresh(dirty, unresolved, all, count);
            st.passNs += nowNs() - t;
            st.passBuilds += ViewProvider::VisualBuildCount - builds;
            ++st.passTables;
            st.passEntries += vis->persisted.size() + vis->transient.size();
            st.passResolves += count;
            st.passResolved += vis->resolved.size();
            if (changed) {
                ++st.passChanged;
                if (vis->onChanged)
                    vis->onChanged();
            }
        }
    });
}

void ViewVisibility::sceneChanged(const App::DocumentObject *container)
{
    if (container)
        PendingObjects.insert(container);
    else
        PendingAll = true;
    PendingUnresolved = true;
    scheduleResolve();
}

void ViewVisibility::resolveInto(const VisibilityEntry &entry, Resolution &res)
{
    std::vector<MemberKey> members;
    res.seenThrough = false;
    res.own = true;
    res.ok = resolveEntry(entry, res.entry.key, res.leaf, res.deps, res.steps, members,
                          res.seenThrough, res.own);
    res.entry.visibility = entry.visible ? 1 : 0;
    res.members.clear();
    if (!res.ok)
        return;
    res.members.reserve(members.size());
    for (auto &member : members) {
        Resolution::Member m;
        m.entry.key = std::move(member.key);
        m.entry.visibility = res.entry.visibility;
        m.leaf = std::move(member.leaf);
        res.members.push_back(std::move(m));
    }
}

void ViewVisibility::connectSignals()
{
    // What makes a change a STRUCTURE change: an object coming or going,
    // and a link property -- a group's members, a link's target -- which
    // is how an object moves in the scene. Each names the object it
    // touched: only the entries whose resolution went through it can
    // have moved. A new object can be on no path resolved so far, but may
    // be what an unresolved entry names, or bring the view provider one
    // was waiting for; so may a container's rebuilt children. Connected
    // once, and only ever acted on while some table has entries.
    static bool connected;
    if (!connected) {
        connected = true;
        auto &app = App::GetApplication();
        app.signalNewObject.connect([](const App::DocumentObject &) {
            PendingUnresolved = true;
            scheduleResolve();
        });
        app.signalDeletedObject.connect([](const App::DocumentObject &obj) {
            PendingObjects.insert(&obj);
            scheduleResolve();
        });
        app.signalFinishRestoreDocument.connect([](const App::Document &) {
            PendingAll = true;
            scheduleResolve();
        });
        app.signalChangedObject.connect(
                [](const App::DocumentObject &obj, const App::Property &prop) {
                    // A link, or what a group remembers as hidden by hand:
                    // no link, and what a view's show of the group leaves
                    // out (SeenThrough)
                    if (!prop.isDerivedFrom(App::PropertyLinkBase::getClassTypeId())
                            && (!ShowsSeenThrough || !SeenThrough::dependsOn(prop)))
                        return;
                    // The look of an object is a link by its class (it may
                    // name a material) and moves nothing in the scene. It is
                    // written for every container as a reopened document's
                    // view providers come up, one after the other over some
                    // turns of the event loop: each write falling behind the
                    // pass was a second pass over every entry under it.
                    if (prop.isDerivedFrom(App::PropertyElementAppearance::getClassTypeId()))
                        return;
                    // An unresolved entry is tried again here too when it
                    // got through this object: its dependencies are the
                    // steps it did resolve, where a change can complete it.
                    PendingObjects.insert(&obj);
                    scheduleResolve();
                });
    }
}

bool ViewVisibility::rebuild(std::vector<VisibilityEntry> &source,
                             std::vector<Resolution> &results,
                             std::vector<VisibilityEntry> &&entries,
                             size_t &count)
{
    connectSignals();
    // An entry the source already holds keeps its resolution, whatever
    // its value: the path is the same, and the structure changes since it
    // was resolved have marked it for the pass already queued, which
    // resolves it again here as it would have there. Only the new entries
    // are resolved -- a map built one entry at a time, or a served
    // client's whole map sent again for one toggle, costs the entries
    // that changed, not the map.
    using Identity = std::tuple<const std::string &, const std::string &,
                                const std::string &, bool>;
    auto identity = [](const VisibilityEntry &e) {
        return Identity(e.doc, e.obj, e.subname, e.rooted);
    };
    std::map<Identity, size_t> known;
    for (size_t i = 0; i < source.size(); ++i)
        known.emplace(identity(source[i]), i);
    std::vector<Resolution> now(entries.size());
    count = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto it = known.find(identity(entries[i]));
        // What an object is seen through is not the same for a show as for
        // a hide: such an entry is resolved again when its value changes
        if (it == known.end()
                || (results[it->second].seenThrough
                    && source[it->second].visible != entries[i].visible)) {
            resolveInto(entries[i], now[i]);
            ++count;
            continue;
        }
        now[i] = results[it->second];
        now[i].entry.visibility = entries[i].visible ? 1 : 0;
    }
    // After the lookups: the identities point into the old source.
    known.clear();
    source = std::move(entries);
    results = std::move(now);
    return commit();
}

bool ViewVisibility::refresh(const std::set<const App::DocumentObject *> &dirty,
                             bool unresolved, bool all, size_t &count)
{
    count = 0;
    auto stale = [&](const Resolution &res) {
        if (all || (!res.ok && unresolved))
            return true;
        for (const auto *obj : res.deps) {
            if (dirty.count(obj))
                return true;
        }
        return false;
    };
    for (auto [source, results] : {std::make_pair(&transient, &transientRes),
                                   std::make_pair(&persisted, &persistedRes)}) {
        for (size_t i = 0; i < source->size(); ++i) {
            if (!stale((*results)[i]))
                continue;
            resolveInto((*source)[i], (*results)[i]);
            ++count;
        }
    }
    return count ? commit() : false;
}

bool ViewVisibility::commit()
{
    std::vector<SoFCVisibilityElement::Entry> now;
    std::set<std::pair<std::string, std::string>> nowShown;
    now.reserve(transientRes.size() + persistedRes.size());
    int showing = 0;
    for (const auto *results : {&transientRes, &persistedRes}) {
        for (const auto &res : *results) {
            if (res.ok && res.seenThrough && res.entry.visibility)
                ++showing;
        }
    }
    ShowsSeenThrough += showing - showsSeenThrough;
    showsSeenThrough = showing;
    for (const auto *results : {&transientRes, &persistedRes}) {
        for (const auto &res : *results) {
            if (!res.ok)
                continue;
            if (res.entry.visibility) {
                // What it is reached through: a body hidden in the document
                // kept the capture from the feature this view shows in it,
                // and an edit showed no base feature with its body hidden
                // (docs/HandsOnQueue.md entry 108). Also where its own key
                // does not count: what it is seen through is reached the
                // same way.
                for (const auto &step : res.steps)
                    nowShown.insert(step);
            }
            if (!res.own)
                continue;
            if (res.entry.visibility)
                nowShown.insert(res.leaf);
            now.push_back(res.entry);
        }
    }
    // What the entries' objects are seen through, behind every entry given
    // by name: the group hidden and one child of it shown shows the child,
    // an equal key keeping this order in the table.
    for (const auto *results : {&transientRes, &persistedRes}) {
        for (const auto &res : *results) {
            if (!res.ok)
                continue;
            for (const auto &member : res.members) {
                if (member.entry.visibility)
                    nowShown.insert(member.leaf);
                now.push_back(member.entry);
            }
        }
    }
    if (persisted.empty() && transient.empty())
        instances().erase(this);
    else
        instances().insert(this);

    const bool same = now.size() == resolved.size()
        && std::equal(now.begin(), now.end(), resolved.begin(),
                      [](const auto &a, const auto &b) {
                          return a.visibility == b.visibility && a.key == b.key;
                      })
        && nowShown == shown;
    if (same)
        return false;

    resolved = std::move(now);
    element.byEnd.clear();
    for (const auto &e : resolved)
        element.byEnd[e.key.back()].push_back(e);
    // Longest key first; an equal key keeps the source order, so the
    // edit's hide stays ahead of a persisted show of the same path.
    for (auto &item : element.byEnd) {
        std::stable_sort(item.second.begin(), item.second.end(),
                         [](const auto &a, const auto &b) {
                             return a.key.size() > b.key.size();
                         });
    }
    element.version = ++serial;
    const uint64_t oldIdentity = element.identity;
    element.identity = element.empty() ? 0 : acquireIdentity(element);
    releaseIdentity(oldIdentity);

    std::set<uint32_t> nowOverridden;
    for (const auto &item : element.byEnd)
        nowOverridden.insert(item.first);
    // Counted in before the old ones go, so a root this change keeps is
    // not touched on the way through.
    for (uint32_t id : nowOverridden) {
        if (!overridden.count(id))
            countOverride(id, true);
    }
    for (uint32_t id : overridden) {
        if (!nowOverridden.count(id))
            countOverride(id, false);
    }
    overridden = std::move(nowOverridden);
    for (const auto &key : nowShown) {
        if (!shown.count(key))
            countShown(key, true);
    }
    for (const auto &key : shown) {
        if (!nowShown.count(key))
            countShown(key, false);
    }
    shown = std::move(nowShown);
    return true;
}

void ViewVisibility::clear()
{
    ShowsSeenThrough -= showsSeenThrough;
    showsSeenThrough = 0;
    for (const auto &key : shown)
        countShown(key, false);
    shown.clear();
    for (uint32_t id : overridden)
        countOverride(id, false);
    overridden.clear();
    persisted.clear();
    transient.clear();
    persistedRes.clear();
    transientRes.clear();
    resolved.clear();
    element.byEnd.clear();
    element.version = ++serial;
    releaseIdentity(element.identity);
    element.identity = 0;
    instances().erase(this);
}

const SoFCVisibilityElement::Table *ViewVisibility::elementTable() const
{
    return element.empty() ? nullptr : &element;
}

const Render::VisibilitySet *ViewVisibility::drawSet(SoFCRenderCacheManager *feed)
{
    if (element.empty() || !feed)
        return nullptr;
    uint64_t infoSerial = 0;
    const Render::ObjectInfoMap &info = feed->getObjectInfo(infoSerial);
    if (feed == drawFeed && infoSerial == drawInfoSerial
            && element.version == drawTableVersion)
        return &draws;
    drawFeed = feed;
    drawInfoSerial = infoSerial;
    drawTableVersion = element.version;
    // Every key of the scene through the one matcher the traversals use,
    // each root on its chain that an entry ends at: the backend then only
    // looks a draw up. A key no entry reaches is simply absent.
    const uint64_t t0 = nowNs();
    std::unordered_map<uint64_t, uint8_t> keys;
    for (const auto &item : info) {
        if (const uint8_t flags = element.resolveDraw(item.second.nodes))
            keys.emplace(item.first, flags);
    }
    auto &st = stats();
    ++st.draws;
    st.drawKeys += info.size();
    st.drawNs += nowNs() - t0;
    if (keys != draws.keys) {
        draws.keys = std::move(keys);
        ++draws.version;
    }
    return &draws;
}

bool Gui::parseOverrideKey(const std::string &key,
                           App::Document *doc,
                           std::vector<Render::ObjectRef> &path,
                           bool &rooted)
{
    path.clear();
    if (key.find('.') == std::string::npos) {
        rooted = false;
        auto sep = key.find('#');
        if (sep != std::string::npos)
            path.push_back({key.substr(0, sep), key.substr(sep + 1)});
        else
            path.push_back({doc->getName(), key});
        return true;
    }
    // Path form: one occurrence, from a top-level object of doc.
    rooted = true;
    const auto dot = key.find('.');
    App::DocumentObject *root = doc->getObject(key.substr(0, dot).c_str());
    std::string subname = key.substr(dot + 1);
    if (!subname.empty() && subname.back() != '.')
        subname += '.';
    return resolveObjectPath(root, subname.c_str(), path);
}

bool Gui::resolveObjectPath(App::DocumentObject *root,
                            const char *subname,
                            std::vector<Render::ObjectRef> &path)
{
    path.clear();
    if (!root || !root->isAttachedToDocument())
        return false;
    path.push_back({root->getDocument()->getName(), root->getNameInDocument()});
    // Token by token, so every step carries its true document --
    // getSubObject follows links across documents the same way the scene
    // graph does. Only dot-terminated tokens are objects; what follows
    // the last dot is an element name.
    App::DocumentObject *cur = root;
    const char *tok = subname;
    for (const char *dot = tok ? strchr(tok, '.') : nullptr; dot;
         tok = dot + 1, dot = strchr(tok, '.')) {
        if (dot == tok)
            continue;
        cur = cur->getSubObject(std::string(tok, dot + 1).c_str());
        if (!cur || !cur->isAttachedToDocument())
            return false;
        path.push_back({cur->getDocument()->getName(), cur->getNameInDocument()});
    }
    return true;
}

std::vector<VisibilityEntry> Gui::parseObjectVisibilities(
        const std::map<std::string, std::string> &values,
        App::Document *doc,
        bool perView)
{
    std::vector<VisibilityEntry> entries;
    if (!doc)
        return entries;
    for (const auto &kv : values) {
        const std::string &key = kv.first;
        if (key.empty() || kv.second.empty())
            continue;
        VisibilityEntry entry;
        const auto dot = key.find('.');
        if (dot == std::string::npos) {
            // Bare: "Obj", or "Doc#Obj" for an object of another document
            // shown through a link.
            if (!perView)
                continue;
            entry.rooted = false;
            const auto sep = key.find('#');
            entry.doc = sep == std::string::npos ? doc->getName() : key.substr(0, sep);
            entry.obj = sep == std::string::npos ? key : key.substr(sep + 1);
        }
        else {
            // Path: one occurrence, from a top-level object of doc.
            entry.doc = doc->getName();
            entry.obj = key.substr(0, dot);
            entry.subname = key.substr(dot + 1);
            if (!entry.subname.empty() && entry.subname.back() != '.')
                entry.subname += '.';
        }
        entry.visible = View3DInventor::visibilityValue(kv.second);
        entries.push_back(std::move(entry));
    }
    return entries;
}

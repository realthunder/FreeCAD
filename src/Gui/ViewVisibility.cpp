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
#include <cstring>

#include <QCoreApplication>
#include <QTimer>

#include <Inventor/SoPath.h>
#include <Inventor/details/SoDetail.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <Base/Console.h>

#include "Application.h"
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

/// Resolve \a entry into its node key, and the object it ends at in
/// \a leaf. False when it does not resolve -- its object gone, its path
/// no longer through the scene -- which leaves it inert until the next
/// resolution.
bool resolveEntry(const VisibilityEntry &entry,
                  std::vector<uint32_t> &key,
                  std::pair<std::string, std::string> &leaf)
{
    key.clear();
    if (!Application::Instance)
        return false;
    auto doc = App::GetApplication().getDocument(entry.doc.c_str());
    auto obj = doc ? doc->getObject(entry.obj.c_str()) : nullptr;
    auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
            Application::Instance->getViewProvider(obj));
    if (!vp)
        return false;
    if (!entry.rooted || entry.subname.empty()) {
        // The object's own root: wherever it is drawn -- which a Link to
        // the object is not, having a root of its own.
        SoNode *root = vp->getRoot();
        if (!root || !root->isOfType(SoFCSelectionRoot::getClassTypeId()))
            return false;
        key.push_back(static_cast<SoFCSelectionRoot *>(root)->getSelNodeId());
        leaf = {entry.doc, entry.obj};
        return true;
    }
    std::vector<Render::ObjectRef> refs;
    if (!resolveObjectPath(obj, entry.subname.c_str(), refs))
        return false;
    // The occurrence's node path from the top-level object's REAL root
    // (append), and of it the selection roots: what a traversal's stack
    // holds when it reaches that object's switch, and what the render
    // cache composes the draw's key of.
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
    return true;
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

bool ViewVisibility::set(std::vector<VisibilityEntry> &&entries)
{
    persisted = std::move(entries);
    return rebuild();
}

bool ViewVisibility::setTransient(std::vector<VisibilityEntry> &&entries)
{
    transient = std::move(entries);
    return rebuild();
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
    if (ResolvePending || instances().empty() || !QCoreApplication::instance())
        return;
    ResolvePending = true;
    QTimer::singleShot(0, QCoreApplication::instance(), []() {
        ResolvePending = false;
        // Copied: a holder told of a change may set its tables again.
        std::vector<ViewVisibility *> list(instances().begin(), instances().end());
        for (ViewVisibility *vis : list) {
            if (!instances().count(vis))
                continue;
            if (vis->rebuild() && vis->onChanged)
                vis->onChanged();
        }
    });
}

bool ViewVisibility::rebuild()
{
    // What makes a change a STRUCTURE change: an object coming or going,
    // and a link property -- a group's members, a link's target -- which
    // is how an object moves in the scene. Connected once, and only ever
    // acted on while some table has entries.
    static bool connected;
    if (!connected) {
        connected = true;
        auto &app = App::GetApplication();
        app.signalNewObject.connect([](const App::DocumentObject &) { scheduleResolve(); });
        app.signalDeletedObject.connect([](const App::DocumentObject &) { scheduleResolve(); });
        app.signalFinishRestoreDocument.connect([](const App::Document &) { scheduleResolve(); });
        app.signalChangedObject.connect(
                [](const App::DocumentObject &, const App::Property &prop) {
                    if (prop.isDerivedFrom(App::PropertyLinkBase::getClassTypeId()))
                        scheduleResolve();
                });
    }

    std::vector<SoFCVisibilityElement::Entry> now;
    std::set<std::pair<std::string, std::string>> nowShown;
    now.reserve(transient.size() + persisted.size());
    std::pair<std::string, std::string> leaf;
    for (const auto *source : {&transient, &persisted}) {
        for (const auto &entry : *source) {
            SoFCVisibilityElement::Entry e;
            if (!resolveEntry(entry, e.key, leaf))
                continue;
            e.visibility = entry.visible ? 1 : 0;
            if (entry.visible)
                nowShown.insert(leaf);
            now.push_back(std::move(e));
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
    for (const auto &key : shown)
        countShown(key, false);
    shown.clear();
    for (uint32_t id : overridden)
        countOverride(id, false);
    overridden.clear();
    persisted.clear();
    transient.clear();
    resolved.clear();
    element.byEnd.clear();
    element.version = ++serial;
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
    std::unordered_map<uint64_t, uint8_t> keys;
    for (const auto &item : info) {
        if (const uint8_t flags = element.resolveDraw(item.second.nodes))
            keys.emplace(item.first, flags);
    }
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

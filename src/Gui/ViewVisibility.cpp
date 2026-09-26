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
#include <sstream>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>

#include "Application.h"
#include "RenderParams.h"
#include "View3DInventor.h"
#include "ViewProviderDocumentObject.h"
#include "ViewVisibility.h"
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

/// Count or release \a key's object as one some view has an entry for.
/// Its switch is touched when that changes whether ANY view has one: the
/// caches above it (shared by every view) were built without reading the
/// element, or will stop reading it.
void countOverride(const std::pair<std::string, std::string> &key, bool add)
{
    if (!SoFCVisibilityElement::countOverride(key.first.c_str(), key.second.c_str(), add))
        return;
    auto vp = viewProviderOf(key.first, key.second);
    if (SoSwitch *sw = vp ? vp->getModeSwitch() : nullptr)
        sw->touch();
}

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

ViewVisibility::~ViewVisibility()
{
    clear();
}

bool ViewVisibility::set(Render::VisibilityOverrideTable &&table)
{
    if (table.entries.empty() && entries.entries.empty())
        return false;
    table.version = ++serial;
    entries = std::move(table);
    element.update(this->table());

    std::set<std::pair<std::string, std::string>> nowShown;
    std::set<std::pair<std::string, std::string>> nowOverridden;
    for (const auto &ov : entries.entries) {
        if (ov.path.empty())
            continue;
        nowOverridden.emplace(ov.path.back().doc, ov.path.back().obj);
        if (ov.visible)
            nowShown.emplace(ov.path.back().doc, ov.path.back().obj);
    }
    // Counted in before the old ones go, so an object this change keeps
    // is not touched on the way through.
    for (const auto &key : nowOverridden) {
        if (!overridden.count(key))
            countOverride(key, true);
    }
    for (const auto &key : overridden) {
        if (!nowOverridden.count(key))
            countOverride(key, false);
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
    for (const auto &key : overridden)
        countOverride(key, false);
    overridden.clear();
    entries.entries.clear();
    element.update(nullptr);
}

const Render::VisibilityOverrideTable *ViewVisibility::table() const
{
    return entries.entries.empty() ? nullptr : &entries;
}

const SoFCVisibilityElement::Table *ViewVisibility::elementTable() const
{
    return element.table ? &element : nullptr;
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
    // Path form: one occurrence, resolved token by token so every
    // element carries its true document -- getSubObject follows links
    // across documents the same way the scene graph does.
    rooted = true;
    std::istringstream iss(key);
    std::string tok;
    App::DocumentObject *cur = nullptr;
    while (std::getline(iss, tok, '.')) {
        if (tok.empty())
            continue;
        if (!cur)
            cur = doc->getObject(tok.c_str());
        else
            cur = cur->getSubObject((tok + ".").c_str());
        if (!cur || !cur->isAttachedToDocument())
            return false;
        path.push_back({cur->getDocument()->getName(),
                        cur->getNameInDocument()});
    }
    return !path.empty();
}

Render::VisibilityOverrideTable Gui::parseObjectVisibilities(
        const std::map<std::string, std::string> &values,
        App::Document *doc,
        bool perView)
{
    Render::VisibilityOverrideTable table;
    if (!doc)
        return table;
    for (const auto &kv : values) {
        if (kv.first.empty() || kv.second.empty())
            continue;
        Render::VisibilityOverride ov;
        if (!parseOverrideKey(kv.first, doc, ov.path, ov.rooted))
            continue;
        if (!ov.rooted && !perView)
            continue;
        ov.visible = View3DInventor::visibilityValue(kv.second);
        table.entries.push_back(std::move(ov));
    }
    return table;
}

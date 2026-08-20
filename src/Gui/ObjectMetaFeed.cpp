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

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Base/Console.h>

#include "ObjectMetaFeed.h"
#include "Renderer/Renderer.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;

namespace
{
/// How long the journal may get before a full table is cheaper than
/// replaying it. Also the bound on what a process with nothing serving
/// accumulates: nobody is asking, so the entries are dropped rather
/// than kept forever.
constexpr std::size_t kMaxJournal = 4096;
}  // namespace

ObjectMetaFeed& ObjectMetaFeed::instance()
{
    static ObjectMetaFeed feed;
    return feed;
}

ObjectMetaFeed::ObjectMetaFeed()
{
    // The events that can change what an object should be called, and
    // nothing else -- a moved, recomputed or recoloured object keeps its
    // name. Application-wide rather than per document: an external link
    // puts another document's objects on a served wire.
    //
    // Each one only writes down a name. Reading the label is the
    // renderer's business, and only when one asks.
    auto changed = [this](const App::DocumentObject& obj) {
        if (obj.getDocument() && obj.getNameInDocument()) {
            note(obj.getDocument()->getName(), obj.getNameInDocument(), false);
        }
    };
    auto removed = [this](const App::DocumentObject& obj) {
        if (obj.getDocument() && obj.getNameInDocument()) {
            note(obj.getDocument()->getName(), obj.getNameInDocument(), true);
        }
    };

    // Connected for the life of the process; the feed is a singleton and
    // outlives every renderer it serves.
    static fastsignals::scoped_connection relabel =
        App::GetApplication().signalRelabelObject.connect(changed);
    static fastsignals::scoped_connection created =
        App::GetApplication().signalNewObject.connect(changed);
    static fastsignals::scoped_connection deleted =
        App::GetApplication().signalDeletedObject.connect(removed);
    // A closing document takes an unbounded number of objects with it,
    // and its own signal arrives whether or not each one was announced.
    // Replaying that is exactly the case a full table is cheaper than.
    static fastsignals::scoped_connection closed =
        App::GetApplication().signalDeleteDocument.connect([this](const App::Document&) {
            dropHistory();
        });
}

void ObjectMetaFeed::note(const char* doc, const char* obj, bool removed)
{
    if (!doc || !obj) {
        return;
    }
    ++version;
    if (journal.size() >= kMaxJournal) {
        // Replaying this many is worse than sending the table, and
        // nothing may grow without bound in a process nobody serves.
        dropHistory();
        return;
    }
    journal.push_back({doc, obj, removed});
}

void ObjectMetaFeed::dropHistory()
{
    journal.clear();
    journal.shrink_to_fit();
    // Everything before now is unreachable by delta; the next feed of
    // any renderer sends the whole table.
    journalBase = ++version;
}

void ObjectMetaFeed::feed(Render::Renderer* renderer)
{
    if (!renderer) {
        return;
    }
    auto it = fed.find(renderer);
    if (it != fed.end() && it->second == version) {
        return;
    }

    // A renderer that has seen a version at or after the journal's base
    // can be caught up by replaying it; anything else takes the table.
    const bool full = it == fed.end() || it->second < journalBase;
    if (full) {
        Render::ObjectMetaMap meta;
        std::size_t objects = 0;
        for (auto doc : App::GetApplication().getDocuments()) {
            if (!doc->getName()) {
                continue;
            }
            auto& byObject = meta[doc->getName()];
            for (auto obj : doc->getObjects()) {
                if (!obj->getNameInDocument()) {
                    continue;
                }
                Render::ObjectMeta& m = byObject[obj->getNameInDocument()];
                m.label = obj->Label.getValue();
                m.type = obj->getTypeId().getName();
                ++objects;
            }
        }
        FC_LOG("object meta: full table, " << objects << " objects in " << meta.size()
                                           << " document(s)");
        renderer->setObjectMeta(std::move(meta));
        fed[renderer] = version;
        return;
    }

    // The delta: every name journalled since this renderer last looked,
    // resolved now. An object journalled as changed that has since gone
    // is a removal -- the journal records what happened, the document
    // says what is true.
    Render::ObjectMetaMap changed;
    std::vector<std::pair<std::string, std::string>> gone;
    uint64_t at = journalBase;
    for (const auto& entry : journal) {
        ++at;
        if (at <= it->second) {
            continue;  // this renderer already has it
        }
        App::DocumentObject* obj = nullptr;
        if (!entry.removed) {
            if (auto doc = App::GetApplication().getDocument(entry.doc.c_str())) {
                obj = doc->getObject(entry.obj.c_str());
            }
        }
        if (obj) {
            Render::ObjectMeta& m = changed[entry.doc][entry.obj];
            m.label = obj->Label.getValue();
            m.type = obj->getTypeId().getName();
        }
        else {
            gone.emplace_back(entry.doc, entry.obj);
        }
    }
    // Objects, not documents: the number that says whether this is a
    // delta at all is how many entries went out against how many the
    // document holds.
    std::size_t objects = 0;
    for (const auto& doc : changed) {
        objects += doc.second.size();
    }
    FC_LOG("object meta: delta, " << objects << " object(s) in " << changed.size()
                                  << " document(s), " << gone.size() << " removed");
    renderer->updateObjectMeta(std::move(changed), gone);
    fed[renderer] = version;
}

void ObjectMetaFeed::forget(const Render::Renderer* renderer)
{
    fed.erase(renderer);
}

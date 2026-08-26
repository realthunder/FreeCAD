/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef _PreComp_
#include <QMetaObject>
#include <QPointer>
#include <map>
#include <set>
#include <vector>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Renderer/Page2D.h>
#include <Gui/Renderer/Page2DWire.h>
#include <Gui/Renderer/SceneServer.h>
#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawSVGTemplate.h>
#include <Mod/TechDraw/App/DrawView.h>
#include <Mod/TechDraw/App/DrawViewPart.h>

#include "PageServe.h"

#include "PageFeed.h"
#include "QGSPage.h"
#include "Rez.h"
#include "ViewProviderPage.h"

using namespace TechDrawGui;

/// The template raster scale a served page uses: the viewer's zoom is
/// unknown here, so the sheet rasterizes at a fixed 2x of the page's
/// Rez resolution -- the same bound the interactive path caps at. A
/// viewer-driven re-raster is doc sec 24.6 future work.
static const float kServeTemplateScale = 2.0f;

class PageServe::Private
{
public:
    TechDraw::DrawPage* page = nullptr;
    std::string group;
    std::string label;
    Render::Page2D page2d;
    QTimer timer;

    // The damage state, mirroring QGVPage::drawVgPreview.
    struct Track
    {
        uint32_t layer = 0;
        float fedX = 0.0f;
        float fedY = 0.0f;
        fastsignals::scoped_connection repaint;
    };
    std::map<std::string, Track> tracks;
    std::set<std::string> dirty;
    size_t structure = 0;
    size_t templateStamp = 0;
    std::vector<fastsignals::scoped_connection> connections;

    // The wire state: current entries by item id (a std::map, so the
    // entry list comes out ordered by objectKey -- what the diff and
    // the splice both assume), the last published list, and the small
    // tables.
    std::map<uint64_t, Render::SceneSnapshot::ObjectEntry> itemEntries;
    std::vector<Render::SceneSnapshot::ObjectEntry> published;
    std::map<uint64_t, Render::PageSnapshot::Image> imageTable;
    std::map<std::string, Render::PageSnapshot::Font> fontTable;
    bool everPublished = false;
    bool tablesDirty = false;
    float publishedW = 0.0f;
    float publishedH = 0.0f;
};

namespace
{
/// The served pages. Function-local so teardown happens at exit, not
/// at static-destruction time in another translation unit.
std::map<TechDraw::DrawPage*, std::unique_ptr<PageServe>>& servedPages()
{
    static std::map<TechDraw::DrawPage*, std::unique_ptr<PageServe>> pages;
    return pages;
}

/// One page item as a content-addressed chunk plus its root entry.
Render::SceneSnapshot::ObjectEntry
entryForItem(const Render::Page2D& page2d, uint64_t id,
             std::vector<uint8_t>& chunk)
{
    Render::SceneSnapshot::ObjectEntry entry;
    entry.objectKey = id;
    Render::Page2D::Kind kind;
    uint32_t layer = 0;
    const std::vector<uint8_t>* ops = nullptr;
    if (!page2d.itemInfo(id, kind, layer, &ops))
        return entry; // empty key = caller skips
    Render::encodePageItemChunk(kind, layer, *ops, chunk);
    entry.key = Render::sha1Hex(chunk.data(), chunk.size());
    entry.size = (uint32_t)chunk.size();
    float box[4];
    if (Render::Page2D::opsBounds(*ops, box)) {
        entry.bbox[0] = box[0];
        entry.bbox[1] = box[1];
        entry.bbox[3] = box[2];
        entry.bbox[4] = box[3];
    }
    return entry;
}
} // namespace

PageServe::PageServe(TechDraw::DrawPage* page)
    : d(new Private)
{
    d->page = page;
    App::Document* doc = page->getDocument();
    d->group = std::string(doc->getName()) + ":"
        + page->getNameInDocument();
    const char* label = page->Label.getValue();
    d->label = label ? label : page->getNameInDocument();

    // Coalesce: an HLR completion or a recompute damages many views;
    // the publish runs once the batch has been applied -- and, for the
    // capture tier, after ViewProviderDrawingView::onGuiRepaint has
    // refreshed the Qt layout this feed photographs (the queued hop is
    // what orders the two).
    d->timer.setSingleShot(true);
    d->timer.setInterval(0);
    connect(&d->timer, &QTimer::timeout, this,
            [this]() { publishNow(); });

    // Structure and property damage from the App side. X/Y moves purge
    // their touch and signal nothing -- publishNow()'s compare catches
    // them when anything else schedules; a bare move still arrives via
    // signalGuiPaint (requestPaint fires on repaint-worthy changes).
    d->connections.emplace_back(doc->signalChangedObject.connect(
        [this](const App::DocumentObject& obj, const App::Property&) {
            const char* name = obj.getNameInDocument();
            if (!name)
                return;
            if (&obj == d->page
                || &obj == d->page->Template.getValue())
                schedulePublish();
            else if (d->tracks.count(name)) {
                d->dirty.insert(name);
                schedulePublish();
            }
        }));
    d->connections.emplace_back(doc->signalDeletedObject.connect(
        [this](const App::DocumentObject& obj) {
            if (&obj == d->page) {
                // Nothing may follow: this source is gone.
                unserve(d->page);
                return;
            }
            schedulePublish();
        }));
    d->connections.emplace_back(
        App::GetApplication().signalDeleteDocument.connect(
            [this](const App::Document& doc) {
                if (&doc == d->page->getDocument())
                    unserve(d->page);
            }));

    // The server-side hooks: a fresh viewer's hello pings the work
    // notifier, and without a republish an idle page would still be
    // served its last payload -- the notifier just keeps the stream
    // warm. Called on a server thread; marshal.
    auto& server = Render::SceneStreamServer::instance();
    QPointer<PageServe> self(this);
    server.setWorkNotifier([self]() {
        QMetaObject::invokeMethod(qApp, [self]() {
            if (self)
                self->schedulePublish();
        }, Qt::QueuedConnection);
    }, d->group);
    server.setDocumentInfo(d->group, d->label);

    schedulePublish();
}

PageServe::~PageServe()
{
    // Hand the group back before anything of this source goes away --
    // clears the publisher claim and handler slots so no server thread
    // dispatches into a dying object.
    Render::SceneStreamServer::instance().releaseGroup(d->group);
}

const std::string& PageServe::group() const
{
    return d->group;
}

void PageServe::schedulePublish()
{
    if (!d->timer.isActive())
        d->timer.start();
}

PageServe* PageServe::serve(TechDraw::DrawPage* page, int port)
{
    if (!page || !page->getNameInDocument() || !page->getDocument())
        return nullptr;
    auto& pages = servedPages();
    auto it = pages.find(page);
    auto& server = Render::SceneStreamServer::instance();
    if (it == pages.end()) {
        std::unique_ptr<PageServe> source(new PageServe(page));
        it = pages.emplace(page, std::move(source)).first;
    }
    if (port > 0 && !server.running() && !server.start(port)) {
        Base::Console().Error(
            "PageServe: scene server failed to start on port %d\n", port);
        pages.erase(it);
        return nullptr;
    }
    return it->second.get();
}

void PageServe::unserve(TechDraw::DrawPage* page)
{
    servedPages().erase(page);
}

bool PageServe::serving(TechDraw::DrawPage* page)
{
    return servedPages().count(page) != 0;
}

bool PageServe::publishNow()
{
    TechDraw::DrawPage* page = d->page;
    if (!page || !page->getNameInDocument())
        return false;

    // The annotation tier converts laid-out Qt scene items; populate
    // the ViewProviderPage's QGSPage without any widget when a view
    // has no item yet -- the renderPageVg recipe.
    QGSPage* qgs = nullptr;
    if (Gui::Document* gdoc =
            Gui::Application::Instance->getDocument(page->getDocument())) {
        auto vpp = dynamic_cast<ViewProviderPage*>(
            gdoc->getViewProvider(page));
        qgs = vpp ? vpp->getQGSPage() : nullptr;
        if (qgs) {
            bool missing = false;
            for (App::DocumentObject* obj : page->getAllViews()) {
                if (!qgs->findQViewForDocObj(obj)) {
                    missing = true;
                    break;
                }
            }
            if (missing) {
                qgs->addChildrenToPage();
                qgs->redrawAllViews();
            }
            qgs->setViewParents();
        }
    }

    // Structure pass, as the interactive preview does it: on a change
    // the retained page resets wholesale; the journal's `cleared` then
    // makes the wire pass below rebuild every entry, and the list diff
    // still keeps the wire delta minimal.
    size_t structure = 0;
    for (App::DocumentObject* obj : page->getAllViews()) {
        if (const char* name = obj->getNameInDocument())
            structure = structure * 31 + std::hash<std::string> {}(name);
    }
    if (structure != d->structure) {
        d->structure = structure;
        d->page2d.clear();
        d->templateStamp = 0;
        d->tracks.clear();
        d->dirty.clear();
        uint32_t layer = 1; // layer 0 is the template's
        for (App::DocumentObject* obj : page->getAllViews()) {
            auto dv = dynamic_cast<TechDraw::DrawView*>(obj);
            if (dv && dv->getNameInDocument()) {
                std::string name = dv->getNameInDocument();
                Private::Track& track = d->tracks[name];
                track.layer = layer;
                track.repaint = dv->signalGuiPaint.connect(
                    [this, name](const TechDraw::DrawView*) {
                        d->dirty.insert(name);
                        schedulePublish();
                    });
                d->dirty.insert(name);
            }
            ++layer;
        }
    }
    else {
        for (auto& v : d->tracks) {
            if (d->dirty.count(v.first))
                continue;
            auto dv = dynamic_cast<TechDraw::DrawView*>(
                page->getDocument()->getObject(v.first.c_str()));
            if (dv
                && ((float)dv->X.getValue() != v.second.fedX
                    || (float)dv->Y.getValue() != v.second.fedY))
                d->dirty.insert(v.first);
        }
    }

    if (!d->dirty.empty()) {
        std::set<std::string> dirty;
        dirty.swap(d->dirty);
        for (const std::string& name : dirty) {
            auto it = d->tracks.find(name);
            if (it == d->tracks.end())
                continue;
            auto dv = dynamic_cast<TechDraw::DrawView*>(
                page->getDocument()->getObject(name.c_str()));
            if (!dv)
                continue;
            QGIView* qgiv = qgs ? qgs->findQViewForDocObj(dv) : nullptr;
            if (auto dvp = dynamic_cast<TechDraw::DrawViewPart*>(dv)) {
                PageFeed::feedViewPart(dvp, d->page2d, PageFeed::Style(),
                                       it->second.layer);
                if (qgiv)
                    PageFeed::feedViewDecorations(qgiv, d->page2d,
                                                  it->second.layer);
            }
            else if (qgiv) {
                PageFeed::feedViewCapture(qgiv, d->page2d,
                                          it->second.layer);
            }
            it->second.fedX = (float)dv->X.getValue();
            it->second.fedY = (float)dv->Y.getValue();
        }
    }

    // The template, at the fixed serve scale; the stamp folds the
    // template identity and its editable texts.
    {
        size_t stamp = std::hash<float> {}(kServeTemplateScale);
        if (App::DocumentObject* tmpl = page->Template.getValue()) {
            if (const char* tname = tmpl->getNameInDocument())
                stamp = stamp * 31 + std::hash<std::string> {}(tname);
            if (auto svgt = dynamic_cast<TechDraw::DrawSVGTemplate*>(tmpl)) {
                for (const auto& kv : svgt->EditableTexts.getValues())
                    stamp = stamp * 31 + std::hash<std::string> {}(kv.second);
            }
        }
        if (stamp != d->templateStamp) {
            d->templateStamp = stamp;
            PageFeed::feedTemplate(page, d->page2d, kServeTemplateScale);
        }
    }

    // -- the wire pass: journal -> entries -> diff -> chunks -> payload.

    Render::Page2D::Changes ch;
    d->page2d.takeChanges(ch);
    if (ch.cleared) {
        // The journal after a clear names everything re-fed since, so
        // the caches rebuild from it; entries not re-fed are exactly
        // the ones the diff should retire.
        d->itemEntries.clear();
        d->imageTable.clear();
        d->tablesDirty = true;
    }

    // Chunks encoded this pass, so an expired retain re-publishes
    // without a second encode.
    std::map<std::string, std::vector<uint8_t>> freshChunks;

    for (uint64_t id : ch.itemsRemoved)
        d->itemEntries.erase(id);
    for (uint64_t id : ch.items) {
        std::vector<uint8_t> chunk;
        Render::SceneSnapshot::ObjectEntry entry =
            entryForItem(d->page2d, id, chunk);
        if (entry.key.empty())
            continue;
        freshChunks.emplace(entry.key, std::move(chunk));
        d->itemEntries[id] = std::move(entry);
    }

    for (uint64_t id : ch.imagesRemoved) {
        if (d->imageTable.erase(id))
            d->tablesDirty = true;
    }
    for (uint64_t id : ch.images) {
        uint16_t w = 0, h = 0;
        bool repeat = false;
        const std::vector<uint8_t>* pixels = nullptr;
        if (!d->page2d.imageInfo(id, w, h, repeat, &pixels) || !pixels)
            continue;
        Render::PageSnapshot::Image img;
        img.id = id;
        img.width = w;
        img.height = h;
        img.repeat = repeat;
        img.key = Render::sha1Hex(pixels->data(), pixels->size());
        img.size = (uint32_t)pixels->size();
        auto it = d->imageTable.find(id);
        if (it == d->imageTable.end() || it->second.key != img.key
            || it->second.repeat != img.repeat) {
            d->tablesDirty = true;
            freshChunks.emplace(img.key, *pixels);
            d->imageTable[id] = std::move(img);
        }
    }

    // Fonts are process-global and immutable per name.
    Render::Page2D::forEachFont([&](const std::string& name,
                                    const uint8_t* data, uint32_t size) {
        if (d->fontTable.count(name))
            return;
        Render::PageSnapshot::Font font;
        font.name = name;
        font.key = Render::sha1Hex(data, size);
        font.size = size;
        freshChunks.emplace(font.key,
                            std::vector<uint8_t>(data, data + size));
        d->fontTable[name] = std::move(font);
        d->tablesDirty = true;
    });

    std::vector<Render::SceneSnapshot::ObjectEntry> entries;
    entries.reserve(d->itemEntries.size());
    for (const auto& kv : d->itemEntries)
        entries.push_back(kv.second);

    std::vector<Render::SceneSnapshot::ObjectEntry> changed;
    std::vector<uint64_t> removed;
    Render::diffObjectLists(d->published, entries, changed, removed);

    const float pageW = (float)Rez::guiX(page->getPageWidth());
    const float pageH = (float)Rez::guiX(page->getPageHeight());
    if (d->everPublished && changed.empty() && removed.empty()
        && !d->tablesDirty && pageW == d->publishedW
        && pageH == d->publishedH)
        return false;

    auto& server = Render::SceneStreamServer::instance();
    const uint64_t version = server.beginPublish(this, d->group);
    if (!version)
        return false;

    Render::PageSnapshot snap;
    snap.manifestVersion = version;
    snap.sessionId = server.sessionId(d->group);
    snap.pageWidth = pageW;
    snap.pageHeight = pageH;
    snap.fonts.reserve(d->fontTable.size());
    for (const auto& kv : d->fontTable)
        snap.fonts.push_back(kv.second);
    snap.images.reserve(d->imageTable.size());
    for (const auto& kv : d->imageTable)
        snap.images.push_back(kv.second);
    snap.entries = entries;

    // Every chunk this publish names must be retained or re-published
    // -- every publish, every key; a missed retain 404s two publishes
    // later (the server's two-generation sweep).
    auto need = [&](const std::string& key,
                    const std::function<std::vector<uint8_t>()>& bytes) {
        if (key.empty() || server.retainBlob(key, nullptr, d->group))
            return;
        auto it = freshChunks.find(key);
        if (it != freshChunks.end())
            server.publishBlob(key, std::move(it->second), d->group);
        else
            server.publishBlob(key, bytes(), d->group);
    };
    for (const auto& kv : d->itemEntries)
        need(kv.second.key, [&]() {
            std::vector<uint8_t> chunk;
            entryForItem(d->page2d, kv.first, chunk);
            return chunk;
        });
    for (const auto& kv : d->imageTable)
        need(kv.second.key, [&]() {
            uint16_t w, h;
            bool repeat;
            const std::vector<uint8_t>* pixels = nullptr;
            d->page2d.imageInfo(kv.first, w, h, repeat, &pixels);
            return pixels ? *pixels : std::vector<uint8_t>();
        });
    for (const auto& kv : d->fontTable)
        need(kv.second.key, [&]() {
            std::vector<uint8_t> bytes;
            Render::Page2D::forEachFont(
                [&](const std::string& name, const uint8_t* data,
                    uint32_t size) {
                    if (name == kv.first)
                        bytes.assign(data, data + size);
                });
            return bytes;
        });

    Render::SceneStreamServer::ScenePublish pub;
    pub.version = version;
    Render::SceneSnapshot::RootSpans spans;
    if (!Render::savePageSnapshot(pub.payload, snap, spans)) {
        // The claim stays ours; the next schedule retries.
        return false;
    }
    pub.spans = spans;
    pub.changed = std::move(changed);
    pub.removed = std::move(removed);
    pub.objects = entries.size();
    server.publish(std::move(pub), d->group);

    d->published = std::move(entries);
    d->everPublished = true;
    d->tablesDirty = false;
    d->publishedW = pageW;
    d->publishedH = pageH;
    return true;
}

#include <Mod/TechDraw/Gui/moc_PageServe.cpp>

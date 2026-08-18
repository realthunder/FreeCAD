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

#ifndef _PreComp_
#include <Inventor/SbBox3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <QApplication>
#include <QColor>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <vector>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>

#include "SceneServeSource.h"

#include "Document.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneServer.h"
#include "RenderParams.h"
#include "ObjectMetaFeed.h"
#include "SceneControl.h"
#include "Selection.h"
#include "SoFCUnifiedSelection.h"
#include "ViewProviderDocumentObject.h"

using namespace Gui;

namespace
{
/// The viewport a synthesized camera is built for. Only the framing a
/// joining viewer adopts before it frames the scene itself, so this has
/// to be sane rather than right.
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

/// The background viewers draw behind the scene. A real viewer builds
/// this from its own Coin background node, but that node is itself only
/// a mirror of these preferences (View3DSettings.cpp) — and the
/// background is a property of how the document is presented, not of a
/// window. Same keys, same defaults, so both publishers describe it
/// identically.
Render::Background backgroundFromPreferences()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View");

    // Stored 0xRRGGBBAA; the packed form the renderer wants is the same
    // with an opaque alpha.
    auto opaque = [](unsigned long packed) {
        return uint32_t((packed & 0xffffff00UL) | 0xffUL);
    };

    Render::Background bg;
    if (hGrp->GetBool("Gradient", true))
        bg.type = Render::Background::LinearGradient;
    else if (hGrp->GetBool("RadialGradient", false))
        bg.type = Render::Background::RadialGradient;
    else {
        bg.type = Render::Background::Flat;
        bg.fromColor = opaque(hGrp->GetUnsigned("BackgroundColor", 3940932863UL));
        return bg;
    }

    bg.fromColor = opaque(hGrp->GetUnsigned("BackgroundColor2", 859006463UL));
    bg.toColor = opaque(hGrp->GetUnsigned("BackgroundColor3", 2880160255UL));
    bg.hasMid = hGrp->GetBool("UseBackgroundColorMid", false);
    if (bg.hasMid)
        bg.midColor = opaque(hGrp->GetUnsigned("BackgroundColor4", 1869583359UL));
    return bg;
}
/*!
 * Where the Render_* overrides live with no 3D view to hold them
 * (docs/HeadlessServe.md §3.3), and the reason it is a subclass: a
 * property edit has to reach the publisher.
 *
 * In a window an edit is followed by a redraw, and the redraw is what
 * re-reads the configs and republishes. Here there are no frames, so a
 * change that nothing listens for is a change a viewer never sees — the
 * edit lands, the snapshot on the wire keeps the old value, and the
 * viewer's own UI silently disagrees with what it is drawing.
 */
class ServeRenderProperties : public App::PropertyContainer
{
public:
    std::function<void()> changed;

protected:
    void onChanged(const App::Property *prop) override
    {
        App::PropertyContainer::onChanged(prop);
        if (changed)
            changed();
    }
};

}  // namespace

class SceneServeSource::Private
{
public:
    Document *doc = nullptr;
    /// The server group this source publishes into: the document's
    /// name (docs/MultiDocServe.md §3). Fixed at construction -- a
    /// document rename mid-serve keeps the stream's identity.
    std::string groupName;
    SoFCUnifiedSelection *root = nullptr;
    SoOrthographicCamera *camera = nullptr;
    /*!
     * Where the Render_* overrides live with no 3D view to hold them
     * (docs/HeadlessServe.md §3.3).
     *
     * A viewer hangs them off its View3DInventor, but everything that
     * reads them — the whole translate*Config family — only ever calls
     * getPropertyByName. They never needed a view, they needed a
     * property container, and App::PropertyContainer implements dynamic
     * properties itself. This is what keeps the control channel's
     * "view3d" subject answerable headlessly: a viewer's edit still
     * lands somewhere, and the next publish reads it back.
     */
    ServeRenderProperties renderProps;
    std::unique_ptr<Render::Renderer> renderer;
    QTimer timer;
    std::vector<boost::signals2::scoped_connection> connections;

    ~Private()
    {
        // The label feed remembers renderers by address; this one is
        // about to stop being one.
        if (renderer)
            ObjectMetaFeed::instance().forget(renderer.get());
        // Detach the backend before the graph goes: the cache manager
        // lives in the root and would otherwise push into a renderer
        // that is already being destroyed.
        if (root) {
            root->setExternalRenderer(nullptr);
            root->unref();
        }
        if (camera)
            camera->unref();
    }

    /// Hang every view provider of the document under the root, the way
    /// View3DInventorViewer::addViewProvider does for a real view.
    void attachViewProviders()
    {
        if (!doc || !root)
            return;
        for (auto *vp : doc->getViewProvidersOfType(
                 ViewProviderDocumentObject::getClassTypeId())) {
            auto *vpd = static_cast<ViewProviderDocumentObject *>(vp);
            SoSeparator *vproot = vpd->getRoot();
            if (!vproot || doc->isClaimed3D(vpd) || !vpd->canAddToSceneGraph())
                continue;
            // Only the physical objects. A real viewer puts the rest in
            // its nonObjectGroup, which is view furniture — the same
            // reason the overlays are not published either.
            if (!vpd->isPartOfPhysicalObject())
                continue;
            if (root->findChild(vproot) < 0)
                root->addChild(vproot);
        }
    }

    /// Frame the synthetic camera on the scene, and hand back the
    /// matrices the snapshot carries. There is no real camera to report,
    /// so this is only the framing a joining viewer adopts before its
    /// own fitAll — it has to be sane, not right. It is a real node
    /// rather than bare matrices because ray picking needs the camera
    /// traversed as a child.
    void cameraMatrices(float *viewMatrix, float *projMatrix)
    {
        SbViewportRegion viewport{short(kDefaultWidth), short(kDefaultHeight)};
        // A fixed isometric-ish orientation, then let Coin size it to
        // whatever the scene turned out to be.
        SbRotation tilt(SbVec3f(1.0f, 0.0f, 0.0f), float(M_PI) / 3.0f);
        SbRotation spin(SbVec3f(0.0f, 0.0f, 1.0f), float(M_PI) / 4.0f);
        camera->orientation.setValue(spin * tilt);
        if (root)
            camera->viewAll(root, viewport);

        SbMatrix viewMat;
        SbMatrix projMat;
        camera->getViewVolume(viewport.getViewportAspectRatio())
            .getMatrices(viewMat, projMat);
        std::memcpy(viewMatrix, viewMat.getValue(), 16 * sizeof(float));
        std::memcpy(projMatrix, projMat.getValue(), 16 * sizeof(float));
    }

    /// The scene as a pickable graph: the camera has to be a traversed
    /// child for setRay picking (the getPointOnRay pattern), so pick a
    /// temporary root of camera + scene rather than the scene itself.
    CoinPtr<SoSeparator> pickRoot()
    {
        CoinPtr<SoSeparator> pickroot(new SoSeparator, true);
        pickroot->addChild(camera);
        pickroot->addChild(root);
        return pickroot;
    }
};

SceneServeSource::SceneServeSource(Document *doc)
    : pimpl(new Private)
{
    pimpl->doc = doc;

    const std::string &type = RenderParams::getType();
    if (type.empty() || type == "Default") {
        Base::Console().Warning(
            "SceneServeSource: no render engine configured; nothing to "
            "publish through\n");
        return;
    }
    pimpl->renderer = Render::RendererFactory::create(type, nullptr, true);
    if (!pimpl->renderer) {
        Base::Console().Warning(
            "SceneServeSource: render engine '%s' cannot publish without a "
            "graphics device\n", type.c_str());
        return;
    }
    // Publishes land in this document's own group on the server, so a
    // second served document is a second group -- not a second claimant
    // on a single stream.
    if (doc && doc->getDocument())
        pimpl->groupName = doc->getDocument()->getName();
    pimpl->renderer->setPublishGroup(pimpl->groupName);

    pimpl->root = new SoFCUnifiedSelection();
    pimpl->root->ref();
    pimpl->root->applySettings();
    pimpl->root->setDocument(doc);
    pimpl->camera = new SoOrthographicCamera();
    pimpl->camera->ref();
    // The source's own container stands in for the view, so the Render_*
    // overrides work exactly as they do in a window -- including that
    // editing one shows up, which without a frame loop takes an explicit
    // republish.
    initRenderProperties(&pimpl->renderProps);
    pimpl->renderProps.changed = [this]() { schedulePublish(); };
    pimpl->root->setViewObject(&pimpl->renderProps);
    pimpl->root->setExternalRenderer(pimpl->renderer.get(),
                                     &pimpl->renderProps);
    pimpl->attachViewProviders();

    // Coalesce: a recompute or a load changes many objects, and each one
    // would otherwise be a full traversal. Zero-timer, so the publish
    // happens once the current batch of changes has been applied.
    pimpl->timer.setSingleShot(true);
    pimpl->timer.setInterval(0);
    connect(&pimpl->timer, &QTimer::timeout,
            this, &SceneServeSource::onPublishTimeout);

    if (doc) {
        pimpl->connections.emplace_back(doc->signalNewObject.connect(
            [this](const ViewProviderDocumentObject &) {
                pimpl->attachViewProviders();
                schedulePublish();
            }));
        pimpl->connections.emplace_back(doc->signalDeletedObject.connect(
            [this](const ViewProviderDocumentObject &vp) {
                // A real view drops the subtree in removeViewProvider();
                // with no view, the root's own child ref would keep the
                // deleted object's geometry in every later publish -- a
                // ghost no viewer can get rid of.
                if (SoSeparator *vproot = vp.getRoot(); vproot && pimpl->root) {
                    int index = pimpl->root->findChild(vproot);
                    if (index >= 0)
                        pimpl->root->removeChild(index);
                }
                schedulePublish();
            }));
        pimpl->connections.emplace_back(doc->signalChangedObject.connect(
            [this](const ViewProviderDocumentObject &, const App::Property &) {
                schedulePublish();
            }));
        // The source must not outlive its document: the pick handler,
        // renderProperties() and the level-source override lookup all
        // dereference it, so a click from a still-connected viewer after
        // the close would land on a freed document. Erasing from inside
        // the signal is safe -- signals2 keeps the invoked slot alive
        // through the call -- and it runs before the Gui::Document
        // itself is destroyed. Nothing may follow the unserve() call:
        // this source is gone when it returns.
        pimpl->connections.emplace_back(doc->signalDeleteDocument.connect(
            [this](const Document &) { unserve(pimpl->doc); }));
    }

    // A rename changes no geometry and no key, so nothing else in this
    // source would ask for a publish over it -- and then the new label
    // would sit in ObjectMetaFeed until something moved. What to send is
    // the feed's business; that there is something to send is this
    // source's.
    pimpl->connections.emplace_back(
        App::GetApplication().signalRelabelObject.connect(
            [this](const App::DocumentObject &) { schedulePublish(); }));

    installHandlers();
    schedulePublish();
}

SceneServeSource::~SceneServeSource()
{
    // Hand the group back before anything of this source goes away:
    // clears its publisher claim and handler slots and purges its
    // queued level jobs, so no server thread dispatches into a source
    // being destroyed. The group node itself stays (SceneServer.h,
    // releaseGroup).
    if (isValid())
        Render::SceneStreamServer::instance().releaseGroup(
                pimpl->groupName);
}

void SceneServeSource::installHandlers()
{
    // What a viewer installed in setRendererType(), minus the viewer
    // (docs/HeadlessServe.md §2e). All three run on the GUI thread: the
    // server calls them from its own, and the marshal is what makes
    // touching the document safe.
    auto &server = Render::SceneStreamServer::instance();
    QPointer<SceneServeSource> self(this);

    // Remote-viewer click selection: a viewer's click arrives as a world
    // ray, picked against this source's graph and synthetic camera.
    server.setPickHandler([self](const Render::ScenePickRequest &req) {
        Render::ScenePickRequest r = req;
        QMetaObject::invokeMethod(qApp, [self, r]() {
            if (self)
                self->pickAndSelect(SbVec3f(r.origin[0], r.origin[1], r.origin[2]),
                                    SbVec3f(r.dir[0], r.dir[1], r.dir[2]),
                                    r.modifiers & 1);
        }, Qt::QueuedConnection);
    }, pimpl->groupName);

    // The semantic control channel (docs/ThinClient.md §4.2) needs
    // nothing from a view and never did — it works on the document.
    // Installed on this document's group, bound to this document: a
    // remote edit of "view3d" must land on the container the publish
    // reads (docs/MultiDocServe.md §5).
    installSceneControlHandler(pimpl->groupName);

    // A finished level-generation job is announced by the next publish,
    // and this source publishes only when something asks it to. Without
    // this an idle backend would sit on finished work forever — the same
    // reason a viewer scheduled a redraw here.
    server.setWorkNotifier([self]() {
        QMetaObject::invokeMethod(qApp, [self]() {
            if (self)
                self->schedulePublish();
        }, Qt::QueuedConnection);
    }, pimpl->groupName);

    // With the handlers in place the document is servable: put it on
    // the wire — joinable by name, listed in the `docs` push every
    // connected viewer's menu redraws from (docs/MultiDocServe.md §4).
    const char *label = pimpl->doc && pimpl->doc->getDocument()
        ? pimpl->doc->getDocument()->Label.getValue() : "";
    server.setDocumentInfo(pimpl->groupName, label ? label : "");
}

void SceneServeSource::pickAndSelect(const SbVec3f &origin, const SbVec3f &dir,
                                     bool ctrl)
{
    if (!isValid())
        return;

    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View");
    SbViewportRegion viewport{short(kDefaultWidth), short(kDefaultHeight)};

    SoRayPickAction rp(viewport);
    rp.setRay(origin, dir);
    rp.setRadius(hGrp->GetFloat("PickRadius", 5.0f));
    auto pickroot = pimpl->pickRoot();
    rp.apply(pickroot);

    SoPickedPoint *pp = rp.getPickedPoint();
    ViewProviderDocumentObject *vpd = nullptr;
    std::string subname;
    if (pp && pimpl->doc) {
        vpd = pimpl->doc->getViewProviderByPathFromHead(
            static_cast<SoFullPath *>(pp->getPath()));
        if (vpd && (!vpd->getObject() || !vpd->getObject()->isAttachedToDocument()
                    || !vpd->getElementPicked(pp, subname)))
            vpd = nullptr;
    }
    if (!vpd) {
        if (!ctrl)
            Gui::Selection().clearSelection();
        return;
    }

    const char *docname = vpd->getObject()->getDocument()->getName();
    const char *objname = vpd->getObject()->getNameInDocument();
    const auto &pt = pp->getPoint();
    SelectionNoTopParentCheck guard;
    if (ctrl) {
        if (Gui::Selection().isSelected(docname, objname, subname.c_str(),
                                        ResolveMode::NoResolve))
            Gui::Selection().rmvSelection(docname, objname, subname.c_str());
        else
            Gui::Selection().addSelection(docname, objname, subname.c_str(),
                                          pt[0], pt[1], pt[2]);
    }
    else {
        Gui::Selection().clearSelection();
        Gui::Selection().addSelection(docname, objname, subname.c_str(),
                                      pt[0], pt[1], pt[2]);
    }
    // A selection changes the feeds, and nothing else will ask.
    schedulePublish();
}

namespace
{
/// The served documents. A function-local static so the sources are torn
/// down at exit rather than at static-destruction time in some other
/// translation unit, which would race the Coin graph they hold.
std::map<Document *, std::unique_ptr<SceneServeSource>> &servedDocuments()
{
    static std::map<Document *, std::unique_ptr<SceneServeSource>> sources;
    return sources;
}

/// Serve order, oldest first: the first-served document is the server's
/// default group -- what an unadorned viewer is joined to -- so "the"
/// render properties (renderProperties with no document) must be that
/// one's, not whichever map entry has the lowest address.
std::vector<Document *> &serveOrder()
{
    static std::vector<Document *> order;
    return order;
}
}  // namespace

SceneServeSource *SceneServeSource::serve(Document *doc, int port)
{
    if (!doc)
        return nullptr;

    auto &sources = servedDocuments();
    auto it = sources.find(doc);
    if (it != sources.end()) {
        // Already served — but the listener may have been stopped
        // since (SceneStreamServer::stop, the share UI's teardown), and
        // a re-serve with a port is how it comes back.
        auto &server = Render::SceneStreamServer::instance();
        if (port > 0 && !server.running() && !server.start(port)) {
            Base::Console().Error(
                "SceneServeSource: scene server failed to start on port %d\n",
                port);
            return nullptr;
        }
        return it->second.get();
    }

    // The source before the listener: constructing it claims the
    // document's group and installs its handlers, so the first-served
    // document is the default group an unadorned viewer joins -- and a
    // source that fails to construct leaves no orphaned listener
    // behind, which used to flip the process-wide coarse-tessellation
    // gate with no publisher anywhere. A port of 0 means the caller
    // arranged the server some other way -- FC_BGFX_SERVE_SCENE, or an
    // already running server serving another document.
    auto source = std::make_unique<SceneServeSource>(doc);
    if (!source->isValid())
        return nullptr;

    auto &server = Render::SceneStreamServer::instance();
    if (port > 0 && !server.running() && !server.start(port)) {
        Base::Console().Error(
            "SceneServeSource: scene server failed to start on port %d\n",
            port);
        return nullptr;
    }

    auto *raw = source.get();
    sources[doc] = std::move(source);
    serveOrder().push_back(doc);
    return raw;
}

void SceneServeSource::unserve(Document *doc)
{
    auto &order = serveOrder();
    order.erase(std::remove(order.begin(), order.end(), doc), order.end());
    servedDocuments().erase(doc);
}

App::PropertyContainer *SceneServeSource::ownRenderProperties() const
{
    return &pimpl->renderProps;
}

App::PropertyContainer *SceneServeSource::renderProperties(
        App::Document *doc)
{
    if (doc) {
        auto *src = sourceFor(doc);
        return src ? src->ownRenderProperties() : nullptr;
    }
    // No document named: the first-served source, which is the same
    // document the server's default group serves to an unadorned
    // viewer.
    auto &order = serveOrder();
    if (order.empty())
        return nullptr;
    auto it = servedDocuments().find(order.front());
    return it == servedDocuments().end()
        ? nullptr : it->second->ownRenderProperties();
}

SceneServeSource *SceneServeSource::sourceFor(App::Document *doc)
{
    if (!doc)
        return nullptr;
    for (auto &entry : servedDocuments()) {
        if (entry.first->getDocument() == doc)
            return entry.second.get();
    }
    return nullptr;
}

bool SceneServeSource::serving(App::Document *doc)
{
    return sourceFor(doc) != nullptr;
}

bool SceneServeSource::isValid() const
{
    return pimpl->renderer && pimpl->root;
}

Document *SceneServeSource::document() const
{
    return pimpl->doc;
}

void SceneServeSource::schedulePublish()
{
    if (isValid() && !pimpl->timer.isActive())
        pimpl->timer.start();
}

void SceneServeSource::onPublishTimeout()
{
    publishNow();
}

bool SceneServeSource::publishNow()
{
    if (!isValid())
        return false;

    auto *manager = pimpl->root->getRenderManager();
    if (!manager)
        return false;

    // The two feeds a viewer supplies from outside its scene graph.
    pimpl->renderer->setBackground(backgroundFromPreferences());
    applySectionHatchTexture(*manager, &pimpl->renderProps);

    // What the objects on this wire are called. A no-op unless something
    // was renamed, added or removed since the last publish.
    ObjectMetaFeed::instance().feed(pimpl->renderer.get());

    // Build the caches without a frame. This is the whole of what a
    // drawing viewer's render path did for the feed.
    SbViewportRegion viewport{short(kDefaultWidth), short(kDefaultHeight)};
    manager->traverse(pimpl->root, viewport);

    float viewMatrix[16];
    float projMatrix[16];
    pimpl->cameraMatrices(viewMatrix, projMatrix);

    return pimpl->renderer->publish(QColor(0x33, 0x33, 0x33), viewMatrix,
                                    projMatrix, kDefaultWidth,
                                    kDefaultHeight);
}

#include "moc_SceneServeSource.cpp"

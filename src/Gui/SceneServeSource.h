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

#ifndef GUI_SCENESERVESOURCE_H
#define GUI_SCENESERVESOURCE_H

#include <cstdint>
#include <memory>

#include <QObject>
#include <QTimer>

#include <Inventor/SbBox.h>
#include <Inventor/SbViewportRegion.h>

#include <FCGlobal.h>

class SoFCRenderCacheManager;

namespace App
{
class PropertyContainer;
}

namespace Render
{
class Renderer;
}

namespace Gui
{

class Document;
class SoFCUnifiedSelection;
class ViewerContext;
class MirrorViewer;

/*!
 * Publishes a document to streaming viewers with no 3D view behind it
 * (docs/HeadlessServe.md §3).
 *
 * A serving process has had no reason to *draw* since stage 1 — its
 * viewers render the scene themselves, off their own clock, from the
 * published snapshot. What it still had was a `View3DInventor`: a GL
 * widget, a live context, bgfx initialized on it, and under Xvfb a
 * software rasterizer loaded for a window nobody watches. This is what
 * replaces it.
 *
 * The reason that is possible at all is that the feed was never really
 * produced by drawing. `SoFCRenderCacheManager` rebuilds its caches by
 * traversing with an `SoCallbackAction` and pushes the result to the
 * backend; drawing is merely what used to call it. So the source keeps
 * the three things that are genuinely document state — the scene graph
 * root, the cache manager, and a renderer to publish through — and drops
 * the one thing that was only ever presentation.
 *
 * What it does *not* have, and what therefore has to be supplied rather
 * than observed: a camera. Viewers navigate with their own, so the one
 * published is only the framing a joining viewer adopts before it frames
 * the scene itself. It is synthesized from the scene bounds.
 */
class GuiExport SceneServeSource : public QObject
{
    Q_OBJECT

public:
    /*!
     * Serve \a doc. The renderer is created in publish-only mode, so no
     * graphics device is created and none is required; if the backend
     * declines that mode the source is inert and isValid() is false.
     */
    explicit SceneServeSource(Document *doc);
    ~SceneServeSource() override;

    /*!
     * Serve \a doc, creating the source on first call and returning the
     * existing one after that — one source per document, which is also
     * all the stream server can arbitrate (docs/HeadlessServe.md §5).
     * The returned source is owned here and lives until the document
     * closes. Null if no publish-only renderer could be made.
     */
    static SceneServeSource *serve(Document *doc, int port = 0);
    /// Stop serving \a doc, if it was.
    static void unserve(Document *doc);

    /*!
     * The render-property container of a serving source, for callers
     * that would otherwise reach for the 3D view's — the control
     * channel's "view3d" subject above all (docs/HeadlessServe.md §3.3).
     * With \a doc, the container of that document's source, null when
     * it is not served; without, the first-served source's — the same
     * document an unadorned viewer is joined to — and null when
     * nothing is served at all.
     */
    static App::PropertyContainer *renderProperties(
            App::Document *doc = nullptr);

    /// The source serving \a doc, or null.
    static SceneServeSource *sourceFor(App::Document *doc);
    /// Whether \a doc is being served by a source. This — not whether
    /// the stream server's listener is up — is what document-scoped
    /// gates ask (docs/MultiDocServe.md §5): a listener can be running
    /// with no publisher behind it, and serving one document says
    /// nothing about another.
    static bool serving(App::Document *doc);

    /// This source's own render-property container.
    App::PropertyContainer *ownRenderProperties() const;

    /// False when no publish-only renderer could be created — the
    /// backend does not support publishing without a device, or none is
    /// configured. Nothing else on the source does anything then.
    bool isValid() const;

    /// The document being served.
    Document *document() const;

    /*!
     * Ask for a publish. Coalesced onto the event loop, so the many
     * changes of a recompute or a load cost one traversal rather than
     * one each. This is the source's equivalent of scheduleRedraw() —
     * and the only trigger it has, since no frame is ever drawn.
     */
    void schedulePublish();

    /// Traverse and publish now, on the calling thread. Returns false if
    /// nothing was published (invalid source, or the feeds had not
    /// changed).
    bool publishNow();

    /*!
     * Select what a world ray hits, as a remote viewer's click asks
     * (docs/ThinClient.md sec 8.3). Resolved through \a client's mirror
     * viewer -- its own camera and canvas, so its pick radius in pixels
     * means what it means on the desktop -- and against this source's
     * synthetic camera when that client has stated no camera, or is not
     * named at all.
     *
     * \a flags is what the click MEANT, as the client resolved it against
     * its own selection: a set operation, a scope and the element kind its
     * pick filter admits (Render::ScenePickRequest::modifiers, and
     * docs/ThinClient.md sec 8.5). The grammar stays on the client, where
     * the state it depends on is; this side supplies the vocabulary.
     *
     * The pick commits into the room selection, which is what every
     * viewer and every panel already reads. GUI thread only.
     */
    void pickAndSelect(const SbVec3f &origin, const SbVec3f &dir,
                       uint32_t flags, uint64_t client = 0);

    /*!
     * The view  client is looking through -- its mirror viewer
     * (docs/ThinClient.md sec 8.3) -- or null when it has stated no
     * camera. This is the type an edit mode is given, and it is what an
     * edit entered from a browser has to be bound to: a served document
     * has no 3D view, so there is nothing else for setEdit to find. GUI
     * thread only.
     */
    ViewerContext *viewerFor(uint64_t client) const;

    /*!
     * The same view as the mirror it is, for the few things that are the
     * mirror's own rather than any view's -- its on-view parameter set
     * (docs/ThinClient.md sec 8.7). GUI thread only.
     */
    MirrorViewer *mirrorViewerFor(uint64_t client) const;

private Q_SLOTS:
    void onPublishTimeout();

private:
    /// Pick, control channel and work notifier — the three a viewer used
    /// to install (docs/HeadlessServe.md §2e).
    void installHandlers();

    class Private;
    std::unique_ptr<Private> pimpl;
};

}  // namespace Gui

#endif  // GUI_SCENESERVESOURCE_H

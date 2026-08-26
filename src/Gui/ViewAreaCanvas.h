/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
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

#ifndef GUI_VIEWAREACANVAS_H
#define GUI_VIEWAREACANVAS_H

#include <memory>
#include <vector>

#include <QOpenGLWidget>
#include <QPointer>

#include "Renderer/Renderer.h"

namespace Gui {

class MDIView;
class ViewArea;
class ViewAreaCell;
class View3DInventorViewer;

/** The one canvas a ViewArea draws its 3D cells into.
 *
 * The desktop half of the split-view render tier (docs/SplitViews.md
 * sec 13). Without it every 3D cell is a View3DInventor with a GL
 * surface of its own, and every one of those carries its OWN backend
 * instance -- N copies of the GPU scene caches, N context dances and N
 * blits per wall frame. With it the container hosts a single
 * QOpenGLWidget, the cells become sub-views (banks) of ONE backend, and
 * the whole layout is drawn by one Renderer::renderSubViews call: the
 * same architecture as the browser tier.
 *
 * The child View3DInventors stay -- hidden, but resized to their cell
 * rects -- so activation, camera persistence, message routing and all
 * the input math keep working unchanged; the canvas forwards input to
 * them by cell rect. Cells the canvas cannot claim (page views, and
 * anything not sharing the resident scene) keep their own widget
 * composition, and the canvas simply leaves their rects alone.
 *
 * Gated by ViewParams::getUnifiedCanvas(), default off.
 */
class GuiExport ViewAreaCanvas : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit ViewAreaCanvas(ViewArea *area);
    ~ViewAreaCanvas() override;

    /// Whether a unified canvas is wanted at all: the preference is on
    /// and the render engine is the path that draws 3D views (the
    /// canvas has nothing to composite without a backend).
    static bool wanted();

    /** Re-resolve the claimed cells, size their hidden children, and
     * release what is no longer claimed. Idempotent and cheap -- call
     * it after every layout change, every resize and every activation
     * change.
     */
    void sync();
    /** Hand every claimed cell back to its own widget composition.
     *
     * \a restoreBackends gives each viewer a backend of its own again,
     * which is what a pref change wants and a teardown does not: the
     * container is on its way out and the two backends would be created
     * only to be destroyed with the widgets a moment later.
     */
    void releaseAll(bool restoreBackends = true);
    /// Stop drawing \a cell -- what a cell whose content is being
    /// swapped out needs before its child view is taken away.
    void releaseCell(ViewAreaCell *cell);
    /// Whether \a cell is drawn by this canvas rather than by itself.
    bool claims(const ViewAreaCell *cell) const;
    /// How many cells the canvas currently draws.
    int claimCount() const { return int(_cells.size()); }

protected:
    void paintGL() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// One claimed cell: the tile, and the stable sub-view id its bank
    /// is keyed by. Never 0 -- that is the backend's implicit
    /// full-canvas sub-view.
    struct Claim {
        QPointer<ViewAreaCell> cell;
        /// The child the claim was made against: a cell whose content
        /// is replaced (setCellView) must be released and re-claimed,
        /// or the newcomer would never be hidden or adopted.
        QPointer<MDIView> child;
        int id = 0;
    };

    static View3DInventorViewer *viewerOf(const ViewAreaCell *cell);
    /// Whether this cell's content can be drawn as a sub-view of the
    /// shared backend: a 3D view of the canvas's own document, so that
    /// it shows the resident scene the canvas feeds.
    bool claimable(const ViewAreaCell *cell) const;
    void claim(ViewAreaCell *cell, int id);
    void release(ViewAreaCell *cell, bool restoreBackend = true);
    /// Point the shared backend's scene feed at \a cell's viewer, and
    /// away from whoever fed it before. Exactly one cell may feed.
    void setFeeder(ViewAreaCell *cell);
    /// The claimed cell under \a globalPos, or null.
    ViewAreaCell *cellAt(const QPoint &globalPos) const;
    /// Re-send \a event to \a cell's hidden child, at the coordinates
    /// the child would have seen had it been the widget on screen.
    bool forwardEvent(ViewAreaCell *cell, QEvent *event);
    /// One pass of sync(); call sync(), which guards re-entrancy.
    void syncOnce();
    /// The sub-view id \a cell is claimed under, or 0 if it is not
    /// claimed. Cells feed their chrome under this id.
    int claimId(const ViewAreaCell *cell) const;
    /// The cell rect in this canvas's device pixels, top-left origin.
    QRect cellRect(const ViewAreaCell *cell) const;

    ViewArea *_area;
    std::shared_ptr<Render::Renderer> _renderer;
    std::vector<Claim> _cells;
    QPointer<ViewAreaCell> _feeder;
    int _nextId = 1;
    bool _painting = false;
    /// Set while an event is on its way to a hidden child. The child is
    /// still a CHILD of the cell, so anything it leaves unaccepted
    /// climbs straight back into the cell -- and into this filter,
    /// which would forward it again, and again.
    bool _forwarding = false;
    /// Set while sync() is mutating the claim list. A claim's side
    /// effects can deliver events that call sync() again, and a nested
    /// run would claim a cell whose entry the outer one has not
    /// recorded yet -- twice.
    bool _syncing = false;
    bool _syncAgain = false;
};

} // namespace Gui

#endif // GUI_VIEWAREACANVAS_H

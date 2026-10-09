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

#ifndef GUI_VIEWAREA_H
#define GUI_VIEWAREA_H

#include <functional>
#include <string>
#include <vector>
#include <QPointer>
#include <QSplitter>
#include "MDIView.h"

namespace Gui {

class ViewArea;
class ViewAreaCanvas;
class ViewAreaCell;
class ViewAreaZone;
class ViewAreaMenuButton;
class ViewAreaHighlight;

/** The splitter used inside a ViewArea.
 *
 * Adds public splitter dragging (to put the border of a corner-drag
 * split where it was released), handles that show a drag as frames and
 * move on release, and a context menu on them. Its border is thinner
 * than a splitter's elsewhere in the program.
 */
class GuiExport ViewAreaSplitter : public QSplitter
{
    Q_OBJECT

public:
    ViewAreaSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);

    static constexpr int HandleWidth = 3;

    void dragSplitter(int pos, int index) { moveSplitter(pos, index); }

    /// What applyLayout's parser asked setSizes for. A never-shown
    /// splitter neither answers sizes() with those values nor honors
    /// them at realization (the missing space is handed out EQUALLY,
    /// skewing every ratio toward even) -- so the root-adoption step
    /// reads THIS, and resizeEvent re-applies it, scaled, at the first
    /// real geometry.
    QList<int> initialSizes;

protected:
    void resizeEvent(QResizeEvent *) override;

protected:
    QSplitterHandle *createHandle() override;
};

/** One tile of a ViewArea.
 *
 * A cell is a thin frame hosting exactly one embedded MDIView (its
 * "child view") -- a View3DInventor, later a TechDraw page or any other
 * MDIView type. The cell owns the widget Qt-wise; document attachment
 * stays with the child view itself, so per-view persistence, message
 * handling and selection keep working unchanged (see docs/SplitViews.md
 * sec 5.2).
 */
class GuiExport ViewAreaCell : public QWidget
{
    Q_OBJECT

public:
    explicit ViewAreaCell(ViewArea *area);
    ~ViewAreaCell() override;

    ViewArea *area() const { return _area; }
    MDIView *childView() const { return _child; }

    /// Reparent \a view into this cell. The cell must be empty.
    void hostView(MDIView *view);
    /// Detach the child view from the cell without deleting it.
    MDIView *releaseView();
    /// The per-cell menu (docs/SplitViews.md sec 5.4/5.5): cell
    /// management plus the content selector. Opened by the corner
    /// button; \a globalPos anchors it.
    void showCellMenu(const QPoint &globalPos);
    /// Reveal (or hide again) the two corner action zones without the
    /// cursor being on them. Hovering the menu button asks for this:
    /// the button is the discoverable chrome, so it is where the
    /// invisible zones get pointed out (docs/SplitViews.md sec 5.4).
    void showZoneHint(bool on);
    /// Repaint the active-cell border. It lives on a raised child
    /// widget, so update() on the cell does not reach it.
    void updateHighlight();
    /** The least a border drag or a shrinking window leaves a cell: the
     * minimum cell size (View/OpenView/MinimumCellSize), in place of the
     * 400 x 300 every MDIView asks for as a tab -- and never more than
     * that, so that a large setting refuses splits without pushing the
     * main window off the screen.
     */
    QSize minimumSizeHint() const override;

    /** Put the menu button and the two corner zones in their places:
     * the corners, except that a zone steps aside from a scroll bar of
     * the hosted view it would lie on.
     */
    void placeChrome();

protected:
    void paintEvent(QPaintEvent *) override;
    void childEvent(QChildEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void enterEvent(QEnterEvent *) override;
    /// Watches the hosted view's scroll bars come and go (placeChrome).
    bool eventFilter(QObject *, QEvent *) override;

private:
    ViewArea *_area;
    QPointer<MDIView> _child;
    ViewAreaZone *_zoneTopRight;
    ViewAreaZone *_zoneBottomLeft;
    ViewAreaMenuButton *_menuButton;
    ViewAreaHighlight *_highlight;
    /// Activation order stamp (ViewArea::_mruCounter at last
    /// activation); the placement policy's reuse step picks the
    /// highest among matching cells.
    int _mruStamp = 0;

    friend class ViewArea;
};

/** A Blender-style corner action zone.
 *
 * Every cell carries one in its top right and bottom left corner.
 * Dragging from it INTO the cell splits it along the dominant drag
 * axis, then keeps adjusting the new border until release; dragging
 * ACROSS the cell border into an adjacent sibling cell arms a join --
 * the doomed neighbor gets a stop sign, releasing the left button
 * commits; dragging back, Escape or any other mouse button cancels
 * (docs/SplitViews.md sec 5.4).
 */
class GuiExport ViewAreaZone : public QWidget
{
    Q_OBJECT

public:
    enum Corner { TopRight, BottomLeft };
    ViewAreaZone(ViewAreaCell *cell, Corner corner);

    static constexpr int Size = 14;

    /// Paint the grip although the cursor is elsewhere, dimmer than a
    /// real hover. Driven by the cell's menu button (showZoneHint).
    void setHint(bool on);
    /// Give a drag under way up: nothing is split or joined.
    void cancelDrag();

protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    /// On the application while a drag is under way: Escape cancels it.
    bool eventFilter(QObject *, QEvent *) override;

private:
    void armJoin(ViewAreaCell *target, Qt::Orientation axis, bool after);
    void disarmJoin();
    void armSplit(const QPoint &global, const QPoint &delta);
    void disarmSplit();
    void endDrag();

    ViewAreaCell *_cell;
    Corner _corner;
    bool _hover = false;
    bool _hint = false;
    bool _dragging = false;
    QPoint _pressGlobal;
    // armed split: nothing is split until the release; the frames show
    // the two cells the one will become, the border under the cursor
    bool _splitArmed = false;
    bool _splitRefused = false;
    Qt::Orientation _splitOrientation = Qt::Horizontal;
    int _splitAt = 0;  // the border, from the cell's leading edge
    // armed join
    QPointer<ViewAreaCell> _joinTarget;
};

/** A Blender-style tiled container of embedded views.
 *
 * One ViewArea occupies one MDI tab and hosts a binary splitter tree
 * whose leaves are ViewAreaCell tiles, each embedding a whole child
 * MDIView. The container owns layout and split/join only; activation is
 * forwarded so that MainWindow::activeWindow() always resolves to the
 * focused child view, keeping the existing command layer working on the
 * focused tile. Design: docs/SplitViews.md.
 */
class GuiExport ViewArea : public MDIView
{
    Q_OBJECT

    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewArea);

public:
    ViewArea(Gui::Document* pcDocument, QWidget* parent,
             Qt::WindowFlags wflags=Qt::WindowFlags());
    ~ViewArea() override;

    const char *getName() const override;

    /// The enclosing ViewArea of a widget, or null if not embedded in one.
    static ViewArea *areaOf(const QWidget *w);
    /** Replace an MDI-hosted view's sub window with a new ViewArea
     * containing the view as its first cell. Returns null if \a view is
     * not currently hosted in the MDI area.
     */
    static ViewArea *wrap(MDIView *view);

    ViewAreaCell *activeCell() const { return _activeCell; }
    /** Hint the corner zones (ViewAreaCell::showZoneHint) of the cells
     * \a handle borders, clearing every other cell's hint; \a on false
     * clears them all. A hovered splitter handle asks for this, so a
     * border points at the gesture that makes and removes it
     * (docs/SplitViews.md sec 18).
     */
    void showZoneHintAt(const QSplitterHandle *handle, bool on);
    ViewAreaCell *cellOf(const MDIView *view) const;
    std::vector<ViewAreaCell*> cells() const;
    int cellCount() const;

    /** Split \a cell along \a orientation (Qt::Horizontal = side by
     * side). The new cell hosts \a newChild if given, else a clone of
     * the cell's current child view (camera copied). \a share, when
     * not negative, is what \a cell keeps along \a orientation, in
     * pixels; the default halves it. Returns the new cell, or null if
     * the content cannot be cloned or a cell would go under the minimum
     * cell size (canSplitCell; said in the report view).
     */
    ViewAreaCell *splitCell(ViewAreaCell *cell, Qt::Orientation orientation,
                            MDIView *newChild = nullptr, int share = -1);
    /** Close \a cell: its child view goes through its normal close path
     * (which may refuse), the tile collapses into its neighbors. When
     * the last cell closes the whole container closes.
     */
    bool closeCell(ViewAreaCell *cell);
    /** Take \a view away without asking it: what it shows was deleted
     * or hidden, so there is no close to refuse. The view leaves its
     * cell now and is deleted when control is back in the event loop
     * (the caller may be inside one of the view's own handlers); the
     * cell collapses now -- except the last one, which stays, empty:
     * closing the container would close its document. What
     * MainWindow::removeWindow does for a view that sits in a cell.
     * False if \a view is not hosted here.
     */
    bool removeView(MDIView *view);

    /** One frame of a drag's preview: the place a cell will have once
     * the drag is released, in this container's coordinates.
     */
    struct DragFrame {
        enum Kind {
            Kept,     ///< a cell that stays, at the size it will have
            Fresh,    ///< the cell a split makes
            Going,    ///< the neighbor a join closes
            Refused,  ///< a split that would leave a cell too small
        };
        QRect rect;
        Kind kind = Kept;
        /// For Going: the way the join swallows it
        Qt::Orientation axis = Qt::Horizontal;
        bool after = true;
    };
    /** Show what a drag under way will do when it is released, as
     * translucent frames over the cells it changes -- the way an overlay
     * dock widget shows its drag. \a operation names it ("split",
     * "join", "resize"); the layout itself is not touched until the
     * release, so a 3D view is not resized at every mouse move. An
     * empty list hides the frames (docs/SplitViews.md sec 5.4).
     */
    void showDragFrames(const char *operation, const QList<DragFrame> &frames);
    void hideDragFrames() { showDragFrames("", {}); }
    /** The frames of a border drag: the handle before widget \a index of
     * \a splitter moved to \a pos. EVERY cell whose geometry the move
     * changes gets one -- the cells of a nested splitter on either
     * side, and the ones a neighbor at its minimum pushes on.
     */
    QList<DragFrame> resizeFrames(const QSplitter *splitter, int index, int pos) const;
    /** The frames of a split of \a cell along \a orientation with the
     * new border at \a global (clamped so that neither half goes under
     * the minimum cell size); one Refused frame when the cell cannot be
     * split. \a at receives the clamped border position, measured from
     * the cell's leading edge along \a orientation.
     */
    QList<DragFrame> splitFrames(ViewAreaCell *cell, Qt::Orientation orientation,
                                 const QPoint &global, int *at = nullptr) const;
    /** Whether \a cell can be split along \a orientation without a cell
     * -- either half, the new one included -- going under the minimum
     * cell size (View/OpenView/MinimumCellSize). With \a report, a
     * refusal is said in the report view, not more than once in a while.
     * A cell that has no geometry yet (a layout being restored) can
     * always be split.
     */
    bool canSplitCell(const ViewAreaCell *cell, Qt::Orientation orientation,
                      bool report = false) const;
    /** Say that \a cell is not split along \a orientation for the
     * minimum cell size, as an error. With \a always, at once -- a
     * corner drag says it each time its cursor turns to the forbidden
     * one; without, not more than once in five seconds.
     */
    void reportRefusedSplit(const ViewAreaCell *cell, Qt::Orientation orientation,
                            bool always) const;

    /** Replace \a cell's content with \a view -- the Blender "switch
     * the area's editor" operation. The old child goes through its
     * normal close path (which may refuse); \a view may currently be
     * hosted in the MDI area (its tab is taken over) or be parentless.
     * Returns false if the old child refused to close or \a view is
     * embedded elsewhere.
     */
    bool setCellView(ViewAreaCell *cell, MDIView *view);

    /** The most-recently-active cell whose child view satisfies
     * \a pred, or null -- the placement policy's reuse candidate
     * (docs/ViewPlacement.md sec 3.2). Cells never activated rank
     * lowest, in tree order.
     */
    ViewAreaCell *lastUsedCell(
            const std::function<bool(MDIView*)> &pred) const;

    /** Make the cell hosting \a view the active tile and activate its
     * window; false if \a view is not hosted here. The placement
     * policy's activation step -- splitCell deliberately keeps the
     * ORIGINAL cell active for gesture splits.
     */
    bool activateCellOf(MDIView *view);

    /** The adjacent sibling cell a join from \a cell along \a axis can
     * consume (Blender's aligned-edge rule: same parent splitter, leaf
     * only). \a after selects the right/bottom neighbor. Null when the
     * join is not legal.
     */
    ViewAreaCell *joinTargetFor(ViewAreaCell *cell, Qt::Orientation axis,
                                bool after) const;

    /** Blender's maximize-area: temporarily give \a cell the whole
     * container; calling again (or with the maximized cell) restores
     * the layout. Split/close/replace operations restore first.
     */
    void toggleMaximizeCell(ViewAreaCell *cell);
    ViewAreaCell *maximizedCell() const { return _maximizedCell; }
    /** Maximize \a cell once the container has its first real
     * geometry. Restoring a saved maximize cannot toggle right away:
     * toggleMaximizeCell records the pre-maximize splitter state to
     * put back later, and recording before layout captures degenerate
     * sizes -- un-maximizing then lost the saved proportions.
     */
    void setPendingMaximize(ViewAreaCell *cell);
    /// The pre-maximize sizes of \a sp while a cell is maximized;
    /// empty when not maximized or \a sp is not recorded. What lets
    /// layoutString persist the underlying proportions rather than the
    /// degenerate hidden-sibling ones.
    QList<int> preMaximizeSizes(const QSplitter *sp) const;

    /** Serialize the splitter tree for GuiDocument.xml: leaves through
     * \a leafToken (empty result drops the leaf), groups as
     * orientation + permille sizes. Example: "H{330,670|L0,V{500,500|L1,O:Page}}".
     */
    std::string layoutString(
            const std::function<std::string(MDIView*)> &leafToken) const;
    /** Rebuild the tree of a FRESH container (single empty cell) from a
     * layoutString. Leaves resolve through \a tokenToView (null skips
     * the leaf); single-child groups flatten. Returns false when
     * nothing could be resolved (the container is left with one empty
     * cell).
     */
    bool applyLayout(const std::string &layout,
            const std::function<MDIView*(const std::string&)> &tokenToView);
    /** Detach \a view from wherever it is hosted -- a cell (the cell
     * stays, empty), an MDI tab (taken over), or nowhere -- so it can
     * be hosted in a cell.
     */
    static void detachViewForHosting(MDIView *view);

    /** Bring the unified canvas (docs/SplitViews.md sec 13) in line with
     * the current layout, or tear it down when it is not wanted. One
     * canvas draws every 3D cell as a sub-view of a single backend
     * instead of composing each cell's own GL widget; the pref is
     * View/UnifiedCanvas, default off. Idempotent -- called after every
     * layout, activation and size change.
     */
    void syncCanvas();
    /// The unified canvas, or null when the container composes widgets.
    ViewAreaCanvas *canvas() const { return _canvas; }
    /// Make \a cell the active tile. Public for the canvas: its cells'
    /// child views are hidden, so activation can no longer ride the
    /// child's focus event.
    void setCanvasActiveCell(ViewAreaCell *cell);

    /// The focused child view; what activation resolves to.
    MDIView *activeSubView() override;

    bool onMsg(const char* pMsg, const char** ppReturn) override;
    bool onHasMsg(const char* pMsg) const override;
    bool canClose() override;
    void deleteSelf() override;
    void viewAll() override;
    void onUpdate() override;
    bool containsViewProvider(const ViewProvider*) const override;
    void print() override;
    void printPdf() override;
    void printPreview() override;
    void print(QPrinter*) override;
    QStringList undoActions() const override;
    QStringList redoActions() const override;
    void setOverrideCursor(const QCursor&) override;
    void restoreOverrideCursor() override;

protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void setActiveCell(ViewAreaCell *cell, bool activateWindow = true);
    void onFocusChanged(QWidget *old, QWidget *now);
    /// Qt-level destruction of a hosted child view (e.g. document close).
    void childViewGone(ViewAreaCell *cell);
    MDIView *cloneChildFor(ViewAreaCell *cell);
    void collapseCell(ViewAreaCell *cell);

private:
    /// Take an MDI-hosted view out of its QMdiSubWindow, keeping it alive.
    static void stealFromMdiArea(MDIView *view);
    /// Fire a queued setPendingMaximize once geometry is real.
    void armPendingMaximize();

    /// What toggleMaximizeCell must put back: the splitter's full
    /// state, plus its plain sizes so layoutString can persist the
    /// UNDERLYING proportions while a cell is maximized (saveState is
    /// an opaque blob; sizes are readable).
    struct MaximizeState {
        QPointer<QSplitter> splitter;
        QByteArray state;
        QList<int> sizes;
    };

    ViewAreaSplitter *_rootSplitter;
    ViewAreaCanvas *_canvas = nullptr;
    QPointer<ViewAreaCell> _activeCell;
    QPointer<ViewAreaCell> _maximizedCell;
    QPointer<ViewAreaCell> _pendingMaximize;
    std::vector<MaximizeState> _maximizeRestore;
    QPointer<QWidget> _dragFrames;
    bool _closing = false;
    int _mruCounter = 0;

    friend class ViewAreaCell;
};

} // namespace Gui

#endif // GUI_VIEWAREA_H

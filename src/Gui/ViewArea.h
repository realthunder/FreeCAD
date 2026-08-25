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

#include <vector>
#include <QPointer>
#include "MDIView.h"

class QSplitter;

namespace Gui {

class ViewArea;

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

protected:
    void paintEvent(QPaintEvent *) override;

private:
    ViewArea *_area;
    QPointer<MDIView> _child;

    friend class ViewArea;
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
    ViewAreaCell *cellOf(const MDIView *view) const;
    std::vector<ViewAreaCell*> cells() const;
    int cellCount() const;

    /** Split \a cell along \a orientation (Qt::Horizontal = side by
     * side). The new cell hosts \a newChild if given, else a clone of
     * the cell's current child view (camera copied). Returns the new
     * cell, or null if the content cannot be cloned.
     */
    ViewAreaCell *splitCell(ViewAreaCell *cell, Qt::Orientation orientation,
                            MDIView *newChild = nullptr);
    /** Close \a cell: its child view goes through its normal close path
     * (which may refuse), the tile collapses into its neighbors. When
     * the last cell closes the whole container closes.
     */
    bool closeCell(ViewAreaCell *cell);

    /** Replace \a cell's content with \a view -- the Blender "switch
     * the area's editor" operation. The old child goes through its
     * normal close path (which may refuse); \a view may currently be
     * hosted in the MDI area (its tab is taken over) or be parentless.
     * Returns false if the old child refused to close or \a view is
     * embedded elsewhere.
     */
    bool setCellView(ViewAreaCell *cell, MDIView *view);

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
    void setActiveCell(ViewAreaCell *cell, bool activateWindow = true);
    void onFocusChanged(QWidget *old, QWidget *now);
    /// Qt-level destruction of a hosted child view (e.g. document close).
    void childViewGone(ViewAreaCell *cell);
    MDIView *cloneChildFor(ViewAreaCell *cell);
    void collapseCell(ViewAreaCell *cell);

private:
    /// Take an MDI-hosted view out of its QMdiSubWindow, keeping it alive.
    static void stealFromMdiArea(MDIView *view);

    QSplitter *_rootSplitter;
    QPointer<ViewAreaCell> _activeCell;
    bool _closing = false;

    friend class ViewAreaCell;
};

} // namespace Gui

#endif // GUI_VIEWAREA_H

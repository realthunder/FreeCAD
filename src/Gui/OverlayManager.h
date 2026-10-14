/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef FC_OVERLAYMANAGER_H
#define FC_OVERLAYMANAGER_H

#include <QBrush>
#include <QObject>
#include <QRegion>
#include <FCGlobal.h>

class QAction;
class QDockWidget;
class QPainter;
class QPointF;
class QTextBlock;
class QString;
class QPoint;
class QWidget;

namespace Gui {

class OverlayTabWidget;

/// Class that manages overlay dockable widgets
class GuiExport OverlayManager : public QObject {
    Q_OBJECT
public:
    OverlayManager();
    ~OverlayManager() override;

    /// restore states
    void restore();
    /// save states
    void save();
    /// Retranslate text on language change
    void retranslate();
    /// Reload icon images
    void refreshIcons();

    /// Reload mode
    enum class ReloadMode {
        /// Reload is pending
        ReloadPending = 0,
        /// Reload is paused
        ReloadPause = 1,
        /// Resume state reload
        ReloadResume = 2,
    };
    /** Set reload mode
     *
     * An internal timer is used to batch handle all relevant parameter
     * changes. The function is provided to let other code temporarily disable
     * the timer before changing the parameter, and then resume it after done.
     */
    void reload(ReloadMode mode = ReloadMode::ReloadPending);

    /** Refresh overlay internal layout
     * @param widget: optional source widget that triggers the refresh
     * @param refreshStyle: whether to reload stylesheet
     */
    void refresh(QWidget *widget=nullptr, bool refreshStyle=false);

    /// Setup title bar for a QDockWidget
    /// Give \a dock the overlay title bar. \a content is what the dock is
    /// about to hold, when it is not set yet: the dock that holds the task
    /// view carries one button more, the switch between task panels in the
    /// combo view and in their views (docs/TaskPanelPerView.md sec 5.5).
    void setupTitleBar(QDockWidget *, QWidget *content = nullptr);

    /// Overlay mode
    enum class OverlayMode {
        /// Toggle the focused widget between normal and overlay on top of MDI client area
        ToggleActive,
        /// Toggle overlay dock widget background between opaque and transparent
        ToggleTransparent,
        /// Make the focused widget switch to overlay on top of MDI client area
        EnableActive,
        /// Make the focused widget switch back to normal dockable widget
        DisableActive,
        /// Make all docked widget switch to overlay
        EnableAll,
        /// Make all docked widget switch back to normal
        DisableAll,
        /// Toggle all docked widget between normal and overlay
        ToggleAll,
        /// Set all overlay dock widget to transparent background
        TransparentAll,
        /// Set all overlay dock widget to opaque background
        TransparentNone,
        /// Toggle all overlay dock widget background between opaque and transparent
        ToggleTransparentAll,
        /// Toggle show/hide of the left docked widgets
        ToggleLeft,
        /// Toggle show/hide of the right docked widgets
        ToggleRight,
        /// Toggle show/hide of the top docked widgets
        ToggleTop,
        /// Toggle show/hide of the bottom docked widgets
        ToggleBottom,
    };
    /// Set overlay mode
    void setOverlayMode(OverlayMode mode);

    /// Enable/disable mouse transparent mode
    void setMouseTransparent(bool enabled);
    /// Report if mouse transparent mode is active
    bool isMouseTransparent() const;

    /// Check if the cursor is within an overlay docked widget
    bool isUnderOverlay() const;

    /** The parts of \a widget that overlaid docks stand over, in its own
     * coordinates.
     *
     * For what is laid over a view and must not stand under a dock, nor
     * over one: a task panel in overlay mode
     * (docs/TaskPanelPerView.md sec 5.3). Only docks that are shown --
     * not one that is hidden to its hint, or by the user -- and each of
     * those whether it is see-through at the moment or has the pointer
     * or the focus and is opaque. Empty when no dock is overlaid, and for
     * a widget none of them reaches.
     */
    QRegion occupied(const QWidget *widget) const;

    /** The brush an item of an overlaid view is backed with
     * (docs/HandsOnQueue.md entry 69): of the tree view and of the property
     * view, whose text has to read over the 3D view while the panel itself
     * is transparent.
     *
     * \a own and \a alpha are what a style sheet set on the view (its
     * overlayItemBackground and overlayAlpha properties): no brush and -1
     * where it set nothing, and then the settings answer -- the 3D view's
     * background colour as the theme has it, the first colour of a
     * gradient, at View/DockOverlayAlpha; or, with
     * View/DockOverlayThemeItemColor off, the tree view's own item
     * background colour as it is stored.
     */
    static QBrush overlayItemBackground(const QBrush &own = QBrush(), int alpha = -1);
    /// View/DockOverlayAlpha, or \a alpha where a style sheet set one
    static int overlayAlpha(int alpha = -1);
    /** Paint \a ground behind the text of one block of a text view, line by
     * line and as wide as each line's text; \a pos is where the block's
     * layout stands in what \a painter paints on. For a text view in a
     * see-through overlay -- the Python console, the report view -- whose
     * text has to read over the 3D view (docs/HandsOnQueue.md entry 98).
     */
    static void paintTextGround(QPainter &painter, const QTextBlock &block,
                                const QPointF &pos, const QBrush &ground);
    /** The ground for paintTextGround(), by one setting
     * (docs/HandsOnQueue.md entry 111, as ruled): with
     * View/DockOverlayThemeItemColor on, "Item background follows the
     * theme", the ground the tree gives its items (overlayItemBackground());
     * with it off the text view's own background -- \a own where the view
     * has a colour of its own (the Python console's, Editor/Background), or
     * else the base colour of the application's palette, which is what the
     * view stands on when it is docked -- at the overlay's opacity.
     *
     * A theme may keep a text of another kind in its text views than in its
     * items: Classic has a black text in the console, for the white page a
     * docked one has, and gives the tree a light text on the dark of its 3D
     * view. Black on that dark was the "scrambled" text of the entry; the
     * setting off puts it back on its own white.
     */
    static QBrush overlayTextGround(const QColor &own = QColor());
    /** The brush an item of an overlaid tree is backed with while the pointer
     * is on it (docs/HandsOnQueue.md entry 87): the theme's colour for that at
     * the opacity of the item backgrounds, as wide as those. No brush under
     * a theme that has no such colour.
     */
    static QBrush overlayHoverBackground(int alpha = -1);

    /** The parts of \a widget that are laid over: by the overlaid docks,
     * as occupied() answers, and by whatever else registered itself with
     * addCover() -- a task panel shown over its view.
     *
     * For what gets out of the way of all of them: the navigation cube
     * (docs/HandsOnQueue.md entry 76).
     */
    QRegion covered(const QWidget *widget) const;
    /// \a cover is laid over views while it is shown: covered() counts it,
    /// and coverChanged() tells when it moves, shows or hides. Forgotten
    /// when it is destroyed.
    void addCover(QWidget *cover);

    bool isChangingMode() const
    {
        return _changingOverlayMode;
    }

    /// Initialize a newly created dock widget
    void initDockWidget(QDockWidget *);
    /// Prepare a dock widget for overlay display
    void setupDockWidget(QDockWidget *, int dockArea = Qt::NoDockWidgetArea);
    /// Switch a dock widget back to normal display
    void unsetupDockWidget(QDockWidget *);

    /** Mouse event handler for dragging a dock widget
     * @param pos: mouse cursor position
     * @param widget: dragging widget, can either be a QDockWidget if it is
     *                floating, or a OverlayTabWidget if docked in overlay mode
     * @param offset: offset from the mouse cursor to the widget origin
     * @param size: widget size before dragging start
     * @param drop: whether to drop after drag to the position
     */
    void dragDockWidget(const QPoint &pos,
                        QWidget *widget,
                        const QPoint &offset,
                        const QSize &size,
                        bool drop = false);

    /// Float an overlay docked widget
    void floatDockWidget(QDockWidget *);

    /// Return the last widget whose mouse event got intercepted by the overlay manager for mouse pass through
    QWidget *getLastMouseInterceptWidget() const;

    /// Return the stylesheet for overlay widgets
    const QString &getStyleSheet() const;

    /// Check whether to hide tab in overlay dock widget
    bool getHideTab() const;

    /// Helper function to set focus when switching active sub window
    static void setFocusView();

    /// Return the singleton instance of the overlay manager
    static OverlayManager * instance();
    /// Destroy the overlay manager
    static void destruct();

    class Private;

Q_SIGNALS:
    /// The overlaid docks were laid out again: occupied() may answer
    /// differently now
    void layoutChanged();
    /// covered() may answer differently now
    void coverChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

    /// Register a named docked widget with an overlay tab widget
    void registerDockWidget(const QString &name, OverlayTabWidget *);
    /// Unregister a named docked widget with an overlay tab widget
    void unregisterDockWidget(const QString &name, OverlayTabWidget *);

private:
    void onToggleDockWidget(bool checked);
    void onDockVisibleChange(bool visible);
    void onDockWidgetTitleChange(const QString &);
    void onTaskViewUpdate();
    void onFocusChanged(QWidget *, QWidget *);
    void onAction();

private Q_SLOTS:
    void raiseAll();

private:
    friend class Private;
    friend class OverlayTabWidget;
    bool _changingOverlayMode = false;
    Private * d;
};

} // namespace Gui

#endif // FC_OVERLAYMANAGER_H

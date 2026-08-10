/***************************************************************************
 *   Copyright (c) 2005 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_TOOLBARMANAGER_H
#define GUI_TOOLBARMANAGER_H

#include <boost_signals2.hpp>
#include <string>
#include <QStringList>
#include <QPointer>
#include <QTimer>
#include <QToolBar>

#include <FCGlobal.h>
#include <Base/Parameter.h>

class QAction;
class QMenu;
class QToolBar;
class QMouseEvent;

namespace Gui {

class ToolBarArea;
class Workbench;

class GuiExport ToolBarItem
{
public:
    /** Manages the default visibility status of a toolbar item, as well as the default status
     * of the toggleViewAction usable by the contextual menu to enable and disable its visibility
    */
    enum class DefaultVisibility {
        Visible,     // toolbar is hidden by default, visibility toggle action is enabled
        Hidden,      // toolbar hidden by default, visibility toggle action is enabled
        Unavailable, // toolbar visibility is managed independently by client code and defaults to
                     // hidden, visibility toggle action is disabled by default (it is unavailable
                     // to the UI). Upon being forced to be available, these toolbars default to
                     // visible.
    };

    ToolBarItem();
    explicit ToolBarItem(ToolBarItem* item, DefaultVisibility visibilityPolicy = DefaultVisibility::Visible);
    ~ToolBarItem();

    void setCommand(const std::string&);
    const std::string &command() const;

    void setID(const QString&);
    const QString & id() const;

    bool hasItems() const;
    ToolBarItem* findItem(const std::string&);
    ToolBarItem* copy() const;
    uint count() const;

    void appendItem(ToolBarItem* item);
    bool insertItem(ToolBarItem*, ToolBarItem* item);
    void removeItem(ToolBarItem* item);
    void clear();

    ToolBarItem& operator << (ToolBarItem* item);
    ToolBarItem& operator << (const std::string& command);
    QList<ToolBarItem*> getItems() const;

    DefaultVisibility visibilityPolicy;

private:
    std::string _name;
    QString _id;
    QList<ToolBarItem*> _items;
};


class ToolBarGrip: public QWidget
{
    Q_OBJECT
public:
    ToolBarGrip(QToolBar *);

    QAction *action()
    {
        return _action;
    }

protected:
    void paintEvent(QPaintEvent*);
    void mouseMoveEvent(QMouseEvent *);
    void mousePressEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);

private:
    QPointer<QAction> _action;
};


/**
 * The ToolBarManager class is responsible for the creation of toolbars and appending them
 * to the main window.
 * @see ToolBoxManager
 * @see MenuManager
 * @author Werner Mayer
 */
class GuiExport ToolBarManager: public QObject
{
    Q_OBJECT
public:

    enum class State {
        ForceHidden,    // Forces a toolbar to hide and hides the toggle action
        ForceAvailable, // Forces a toolbar toggle action to show, visibility depends on user config
        RestoreDefault, // Restores a toolbar toggle action default, visibility as user config
        SaveState,      // Saves the state of the toolbars
    };

    /// The one and only instance.
    static ToolBarManager* getInstance();
    static void destruct();
    /** Sets up the toolbars of a given workbench. */
    void setup(ToolBarItem*);
    void saveState() const;
    void restoreState();
    void setDefaultMovable(bool enable);
    bool isDefaultMovable() const;

    /*! Whether the toolbars parked in the menu bar and status bar areas are
     * locked as a group, independently of the per-toolbar locks. They are the
     * ones a stray drag is most likely to pull out of place, and with a custom
     * title bar they share a row with the window drag area.
     */
    bool areTitleToolBarsLocked() const;
    void setTitleToolBarsLocked(bool locked);
    void retranslate();
    static void checkToolBar();

    void setToolBarVisibility(bool show, const QList<QString>& names);
    void setToolBarVisible(QToolBar *toolbar, bool show);

    void setState(const QList<QString>& names, State state);

    void removeToolBar(const QString &);

    static bool isCustomToolBarName(const char *name);
    static QString generateToolBarID(const char *groupName, const char *name);

    static QSize actionsIconSize(const QList<QAction*> &actions, QWidget *widget=nullptr);
    void checkToolBarIconSize(QAction *action);
    void checkToolBarIconSize(QToolBar *tb);

    int toolBarIconSize(QWidget *widget=nullptr) const;
    void setupToolBarIconSize();

    void addToolBarToMainWindow(QToolBar *);
    ToolBarArea *getToolBarArea(QToolBar *);
    void setToolBarMovable(QToolBar *);

    /*! Put the two menu-bar toolbar areas wherever the title bar currently is:
     * the menu bar's corners with a native title bar, the title bar's own left
     * and right areas with a custom one. Called once during setup and again
     * whenever MainWindow switches between them.
     */
    void relocateMenuBarAreas();

    /*! Park the two menu-bar toolbar areas on the main window itself. Whichever
     * title bar is about to be torn down owns them, and a child of a deleted
     * widget is deleted with it -- so this has to run first, with
     * relocateMenuBarAreas() putting them back afterwards.
     */
    void detachMenuBarAreas();

    /*! Move the first row of top-docked toolbars into the title bar's left
     * area, or, with \a enable false, move whatever is parked in the two areas
     * back to the top dock.
     *
     * Which toolbars make up "the first row" is asked of the layout rather than
     * named: the set differs per workbench, so a fixed list would be right in
     * the one workbench it was written for and wrong in the rest. Filling only
     * happens when both areas are empty -- a row the user arranged is theirs.
     */
    void setTitleBarToolBars(bool enable);

protected Q_SLOTS:
    void onToggleToolBar(bool);
    void onMovableChanged(bool);
    void onTimer();

protected:
    void setup(ToolBarItem*, QToolBar*) const;
    /** Returns a list of all currently existing toolbars. */
    std::map<QString, QPointer<QToolBar>> toolBars();

    ToolBarItem::DefaultVisibility getToolbarPolicy(const QToolBar *) const;

    QAction* findAction(const QList<QAction*>&, const QString&) const;
    QToolBar *createToolBar(const QString &name);
    void connectToolBar(QToolBar *);
    void getGlobalToolBarNames();
    bool eventFilter(QObject *, QEvent *);

    void setTitleToolBarsMovable(bool movable);

    /*! Where a floating toolbar has to be dropped to land in one of the two
     * menu-bar areas, in global coordinates. Empty when there is nowhere to
     * drop it.
     */
    QRect menuBarDropRect() const;

    /*! The visible toolbars in the top dock area's first row, left to right,
     * i.e. the row directly under the menu bar. Empty when there are none.
     */
    std::vector<QToolBar*> firstToolBarRow();

    bool addToolBarToArea(QObject *, QMouseEvent*);
    bool showContextMenu(QObject *);
    void onToggleStatusBarWidget(QWidget *, bool);
    static void setToolBarIconSize(QToolBar *tb);

    ToolBarManager();
    ~ToolBarManager();

private:
    QTimer timer;
    QTimer timerChild;
    QTimer timerResize;
    QTimer menuBarTimer;
    QStringList toolbarNames;
    static ToolBarManager* _instance;
    ParameterGrp::handle hPref;
    ParameterGrp::handle hMovable;
    ParameterGrp::handle hMainWindow;
    ParameterGrp::handle hGlobal;
    ParameterGrp::handle hStatusBar;
    ParameterGrp::handle hMenuBarLeft;
    ParameterGrp::handle hMenuBarRight;
    ParameterGrp::handle hGeneral;
    boost::signals2::scoped_connection connParam;
    bool restored = false;
    bool migrating = false;
    bool adding = false;
    Qt::ToolBarArea defaultArea;
    Qt::ToolBarArea globalArea;
    std::set<QString> globalToolBarNames;
    std::map<QToolBar*, QPointer<QToolBar>> connectedToolBars;
    ToolBarArea *statusBarArea = nullptr;
    ToolBarArea *menuBarLeftArea = nullptr;
    ToolBarArea *menuBarRightArea = nullptr;

    std::map<QToolBar*, QPointer<QToolBar>> resizingToolBars;

    int _toolBarIconSize;
    int _workbenchTabIconSize;
    int _workbenchComboIconSize;
    int _statusBarIconSize;
    int _menuBarIconSize;
};

} // namespace Gui


#endif // GUI_TOOLBARMANAGER_H

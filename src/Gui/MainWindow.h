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


#ifndef GUI_MAINWINDOW_H
#define GUI_MAINWINDOW_H

#include <list>

#include <QEvent>
#include <QMdiArea>

#include "InputHint.h"

#include <customtitlebarkit/CustomTitleBarWindow.h>

#include "Window.h"

class QMimeData;
class QUrl;
class QMdiSubWindow;

namespace App {
class Document;
}

namespace Gui {

class BaseView;
class CommandManager;
class Document;
class MacroManager;
class MDIView;

namespace DockWnd {
    class HelpView;
} //namespace DockWnd

class GuiExport UrlHandler : public QObject
{
    Q_OBJECT

public:
    explicit UrlHandler(QObject* parent = nullptr)
        : QObject(parent){
    }
    ~UrlHandler() override = default;
    virtual void openUrl(App::Document*, const QUrl&) {
    }
};

/**
 * Identifies which side of the status bar an item belongs to.
 * Left items are non-permanent (a status message may temporarily cover them);
 * Right items are permanent and never obscured.
 */
enum class StatusBarSlot
{
    Left,
    Right,
};

/**
 * Metadata describing a status-bar item registered through
 * MainWindow::addStatusBarItem(). The caller states intent -- slot, order, a
 * stable id and a human title -- and MainWindow owns the layout, the ordering,
 * the persistence of the user's show/hide choice and the context-menu entry.
 */
struct StatusBarItemSpec
{
    QByteArray id;    ///< Stable identifier, used for removal and persistence.
    QString title;    ///< Label shown in the status bar's context menu.
    StatusBarSlot slot = StatusBarSlot::Right;
    int order = 0;    ///< Sort key within the slot; lower sits closer to the centre.
    bool persistentVisibility = true;  ///< Remember the show/hide choice across sessions.
    int stretch = 0;  ///< Layout stretch factor.
};

/**
 * The MainWindow class provides a main window with menu bar, toolbars, dockable windows,
 * a status bar and mainly a workspace for the MDI windows.
 * @author Werner Mayer
 */
class GuiExport MainWindow : public CustomTitleBarWindow
{
    Q_OBJECT
    Q_PROPERTY(QString overrideIcons READ overrideIcons WRITE setOverrideIcons)

public:
    /// Whether the window is drawing its own title bar rather than the platform's.
    bool isCustomTitleBar() const
    {
        return mode() == Mode::Custom;
    }

    /*! Switch the title bar between the platform's and our own, on the running
     * window. Moves the menu bar and the two menu-bar toolbar areas to whichever
     * of the two is now hosting them, and remembers the choice in
     * MainWindow/CustomTitleBar.
     */
    void setCustomTitleBar(bool enable);

    /*! Whether the custom title bar's menu bar is folded behind the logo button
     * rather than laid out inline. Folding gives the row back to the toolbars
     * at the cost of a hover; MainWindow/FoldTitleBarMenu, on by default.
     */
    bool foldTitleBarMenu() const;
    void setFoldTitleBarMenu(bool enable);

    /*! Whether the workbench toolbar belongs in the custom title bar rather
     * than under the menu bar. MainWindow/TitleBarToolBars, on by default, and
     * meaningless while the platform's title bar is in charge.
     */
    bool titleBarToolBars() const;

    /*! Put the keyboard on the menu bar, unfolding it first if it is folded
     * behind the title bar logo: the first menu is highlighted, and the arrow
     * keys walk the row from there without opening anything. This is what
     * Std_ShowMenuBar and the Alt key both come down to. Returns false if
     * there is no menu bar to show.
     */
    bool activateMenuBar();

    /*! Put the window chrome where MainWindow/CustomTitleBar and
     * MainWindow/TitleBarToolBars now say it goes. A preference pack -- which
     * is what a theme is -- carries both, so this is how a theme decides which
     * title bar the window wears and what lives in it.
     */
    void applyTitleBarParams();

    enum ConfirmSaveResult {
        Cancel = 0,
        Save,
        SaveAll,
        Discard,
        DiscardAll
    };
    /**
     * Constructs an empty main window. For default \a parent is 0, as there usually is
     * no toplevel window there.
     */
    explicit MainWindow(QWidget * parent = nullptr, Qt::WindowFlags f = Qt::Window);
    /** Destroys the object and frees any allocated resources. */
    ~MainWindow() override;
    /**
     * Filters events if this object has been installed as an event filter for the watched object.
     */
    bool eventFilter(QObject* o, QEvent* e) override;
    /**
     * Adds an MDI window \a view to the main window's workspace and adds a new tab
     * to the tab bar.
     */
    void addWindow(MDIView* view);
    /**
     * Removes an MDI window from the main window's workspace and its associated tab without
     * deleting the widget. If the main windows does not have such a window nothing happens.
     */
    void removeWindow(MDIView* view, bool close=true);
    /**
     * Returns a list of all MDI windows in the worpspace.
     */
    QList<QWidget*> windows(QMdiArea::WindowOrder order = QMdiArea::CreationOrder) const;
    /**
     * Returns the internal QMdiArea instance.
     */
    QMdiArea *getMdiArea() const;
    /**
     * Can be called after the caption of an MDIView has changed to update the tab's caption.
     */
    void tabChanged(MDIView* view);
    /**
     * Returns the active MDI window or 0 if there is none.
     */
    MDIView* activeWindow() const;
    /**
     * Sets the active window to \a view.
     */
    void setActiveWindow(MDIView* view);
    /**
     * MRU: Appends \a file to the list of recent files.
     */
    void appendRecentFile(const QString& filename);
    /**
     * MRU: Appends \a macro to the list of recent macros.
     */
    void appendRecentMacro(const QString& filename);
    /**
     * Returns true that the context menu contains the 'Customize...' menu item.
     */
    QMenu * createPopupMenu() override;

    QString overrideIcons() const;
    void setOverrideIcons(const QString &);

    QString overrideExtraIcons() const;
    void setOverrideExtraIcons(const QString &);

    /** @name Splasher and access methods */
    //@{
    /** Gets the one and only instance. */
    static MainWindow* getInstance();
    /** Starts the splasher at startup. */
    void startSplasher();
    /** Stops the splasher after startup. */
    void stopSplasher();
    /* The image of the About dialog, it might be empty. */
    QPixmap aboutImage() const;
    /* The image of the splash screen of the application. */
    QPixmap splashImage() const;
    /** Shows the online documentation. */
    void showDocumentation(const QString& help);
    //@}

    /** @name Layout Methods
     */
    //@{
    /// Loads the main window settings.
    void loadWindowSettings();
    /// Saves the main window settings.
    void saveWindowSettings(bool canDelay = false);
    //@}

    /** @name Menu
     */
    //@{
    /// Set menu for dock windows.
    void setDockWindowMenu(QMenu*);
    /// Set menu for toolbars.
    void setToolBarMenu(QMenu*);
    /// Set menu for sub-windows
    void setWindowsMenu(QMenu*);
    //@}

    /** @name MIME data handling
     */
    //@{
    /** Create mime data from selected objects */
    QMimeData * createMimeDataFromSelection () const;
    /** Check if mime data contains object data */
    bool canInsertFromMimeData (const QMimeData * source) const;
    /** Insert the objects into the active document. If no document exists
     * one gets created.
     */
    void insertFromMimeData (const QMimeData * source);
    /**
     * Load files from the given URLs into the given document. If the document is 0
     * one gets created automatically if needed.
     *
     * If a url handler is registered that supports its scheme it will be delegated
     * to this handler. This mechanism allows to change the default behaviour.
     */
    void loadUrls(App::Document*, const QList<QUrl>&);
    /**
     * Sets the \a handler for the given \a scheme.
     * If setUrlHandler() is used to set a new handler for a scheme which already has a handler,
     * the existing handler is simply replaced with the new one. Since MainWindow does not take
     * ownership of handlers, no objects are deleted when a handler is replaced.
     */
    void setUrlHandler(const QString &scheme, UrlHandler* handler);
    /**
     * Removes a previously set URL handler for the specified \a scheme.
     */
    void unsetUrlHandler(const QString &scheme);
    //@}

    void updateActions(bool delay = false);

    enum StatusType {None, Err, Wrn, Pane, Msg, Log, Tmp, Critical};
    void showStatus(int type, const QString & message);

    void showHints(const std::list<InputHint>& hints = {});
    void hideHints();

    /** @name Status bar items
     *
     * A workbench does not touch the QStatusBar layout: it registers a widget
     * here, and this window decides where the widget sits, in what order,
     * whether the user's show/hide choice survives a restart, and how it
     * appears in the status bar's context menu. Draft, BIM and Tux are written
     * against this API and create their widgets through
     * UiLoader().createWidget("Gui::ToolBar").
     */
    //@{
    /// Registers and places \a widget in the status bar according to \a spec.
    void addStatusBarItem(QWidget* widget, const StatusBarItemSpec& spec);
    /// Removes a registered item by id. Does not delete the widget.
    void removeStatusBarItem(const QByteArray& id);
    /// Shows or hides a registered item, persisting it when the item asked for that.
    void setStatusBarItemEnabled(const QByteArray& id, bool enabled);
    /// Appends a checkable toggle action for every registered item to \a menu.
    void buildStatusBarContextMenu(QMenu& menu);
    /// The registered widget under \a id, null when there is none.
    QWidget* statusBarItem(const QByteArray& id) const;
    /// Whether \a widget is a registered item (the tool bar manager then
    /// leaves such a tool bar where it is instead of adopting it).
    bool isStatusBarItem(const QWidget* widget) const;
    //@}

    void initDockWindows(bool show);

    /** Whether the combo view dock carries the model tree.
     *
     * False once the tree and property views have docks of their own, which
     * leaves the combo view holding nothing but the task panel.
     */
    static bool comboViewShowsModel();
    /// The side the combo view dock is parked on by default.
    static Qt::DockWidgetArea comboViewDockArea();

    bool isRestoringWindowState() const;

public Q_SLOTS:
    /**
     * Updates the standard actions of a text editor such as Cut, Copy, Paste, Undo and Redo.
     */
    void updateEditorActions();
    /**
     * Sets text to the pane in the status bar.
     */
    void setPaneText(int i, QString text);
    /**
     * Sets the userschema in the status bar
    */
    void setUserSchema(int userSchema);
    /**
     * Arranges all child windows in a tile pattern.
     */
    void tile();
    /**
     * Arranges all the child windows in a cascade pattern.
     */
    void cascade();
    /**
     * Closes the child window that is currently active.
     */
    void closeActiveWindow ();
    /**
     * Closes all document window.
     */
    bool closeAllDocuments (bool close=true);
    /// Report if the main window is trying to close all sub window
    bool isClosingAll() const;
    /** Pop up a message box asking for saving document
     */
    int confirmSave(const char *docName, QWidget *parent=nullptr, bool addCheckBox=false);
    /**
     * Activates the next window in the child window chain.
     */
    void activateNextWindow ();
    /**
     * Activates the previous window in the child window chain.
     */
    void activatePreviousWindow ();
    /**
     * Just emits the workbenchActivated() signal to notify all receivers.
     */
    void activateWorkbench(const QString&);
    /**
     * Starts the what's this mode.
     */
    void whatsThis();
    void switchToTopLevelMode();
    void switchToDockedMode();

    void statusMessageChanged(const QString &);

    void showMessage (const QString & message, int timeout = 0);

protected:
    /**
     * This method checks if the main window can be closed by checking all open documents and views.
     */
    void closeEvent (QCloseEvent * e) override;
    void showEvent  (QShowEvent  * e) override;
    void hideEvent  (QHideEvent  * e) override;
    void timerEvent (QTimerEvent *  ) override {
        Q_EMIT timeEvent();
    }
    void customEvent(QEvent      * e) override;
    bool event      (QEvent      * e) override;
    /**
     * Try to interpret dropped elements.
     */
    void dropEvent  (QDropEvent  * e) override;
    /**
     * Checks if a mime source object can be interpreted.
     */
    void dragEnterEvent(QDragEnterEvent * e) override;
    /**
     * This method is called from the Qt framework automatically whenever a
     * QTranslator object has been installed. This allows to translate all
     * relevant user visible text.
     */
    void changeEvent(QEvent *e) override;
    void childEvent(QChildEvent *e) override;

private:
    /*! Fold the menu bar behind a logo button in the title bar, and tell the
     * stylesheets which platform backend is drawing it. Called on every entry
     * into custom mode -- the constructor's and the run-time switch's -- because
     * the kit installs its own inline integration each time.
     */
    void setupTitleBarMenu();

    /// Re-place every registered status-bar item in slot and order.
    void relayoutStatusBar();

    void setupDockWindows();
    bool setupSelectionView();
    bool setupReportView();
    bool setupPythonConsole();

    static void renderDevBuildWarning(QPainter &painter, const QPoint startPosition, const QSize maxSize);

private Q_SLOTS:
    /**
     * \internal
     */
    void onSetActiveSubWindow(QWidget *window);
    /**
     * Activates the associated tab to this widget.
     */
    void onWindowActivated(QMdiSubWindow*);
    /**
     * Close tab at position index.
     */
    void tabCloseRequested(int index);
    /**
     * Fills up the menu with the current windows in the workspace.
     */
    void onWindowsMenuAboutToShow();
    /**
     * Fills up the menu with the current toolbars.
     */
    void onToolBarMenuAboutToShow();
    /**
     * Fills up the menu with the current dock windows.
     */
    void onDockWindowMenuAboutToShow();
    /**
     * This method gets frequently activated and test the commands if they are still active.
     */
    void _updateActions();
    /**
     * \internal
     */
    void delayedStartup();
    /**
     * \internal
     */
    void processMessages(const QList<QByteArray> &);
    /**
     * \internal
     */
    void clearStatus();

Q_SIGNALS:
    void timeEvent();
    void windowStateChanged(QWidget*);
    void workbenchActivated(const QString&);
    void mainWindowClosed();

private:
    /// some kind of singleton
    static MainWindow* instance;
    struct MainWindowP* d;
};

inline MainWindow* getMainWindow()
{
    return MainWindow::getInstance();
}

// -------------------------------------------------------------

/** The status bar observer displays the text on the status bar of the main window
 * in an appropriate color. Normal text messages are black, warnings are orange and
 * error messages are in red. Log messages are completely ignored.
 * The class is implemented to be thread-safe.
 * @see Console
 * @see ILogger
 * @author Werner Mayer
 */
class StatusBarObserver: public WindowParameter, public Base::ILogger
{
public:
    StatusBarObserver();
    ~StatusBarObserver() override;

    /** Observes its parameter group. */
    void OnChange(Base::Subject<const char*> &rCaller, const char * sReason) override;

    void SendLog(const std::string& notifiername, const std::string& msg, Base::LogStyle level,
                 Base::IntendedRecipient recipient, Base::ContentType content) override;

    /// name of the observer
    const char *Name() override {return "StatusBar";}

    friend class MainWindow;
private:
    QString msg, wrn, err, critical;
};

// -------------------------------------------------------------

/** This is a helper class needed when a style sheet is restored or cleared.
 * @author Werner Mayer
 */
class ActionStyleEvent : public QEvent
{
public:
    static int EventType;
    enum Style {Restore, Clear};

    explicit ActionStyleEvent(Style type);
    Style getType() const;

private:
    Style type;
};

} // namespace Gui

#endif // GUI_MAINWINDOW_H

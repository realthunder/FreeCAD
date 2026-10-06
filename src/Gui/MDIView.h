/***************************************************************************
 *   Copyright (c) 2007 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef GUI_MDIVIEW_H
#define GUI_MDIVIEW_H

#include <memory>
#include <fastsignals/signal.h>
#include <QMainWindow>
#include <Gui/ActiveObjectList.h>
#include <Gui/View.h>


QT_BEGIN_NAMESPACE
class QPrinter;
QT_END_NAMESPACE

namespace Gui
{
class Document;
class MainWindow;
class ViewProvider;
class ViewProviderDocumentObject;
class MDIViewPy;
class SelectionSingleton;

/** Base class of all windows belonging to a document.
 * There are two ways of belonging to a document:
 * \li belong to a fix document
 * \li always belong to the active document
 * The latter means whenever the active document is changing the view belongs to
 * this document. It also means that the view belongs sometimes to no document at
 * all.
 * @see TreeView
 * @see Gui::Document
 * @see Application
 * @author Jürgen Riegel, Werner Mayer
 */
class GuiExport MDIView : public QMainWindow, public BaseView
{
    Q_OBJECT

    PROPERTY_HEADER_WITH_OVERRIDE(Gui::MDIView);

public:
    /** View constructor
     * Attach the view to the given document. If the document is zero
     * the view will attach to the active document. Be aware, there isn't
     * always an active document.
     */
    MDIView(Gui::Document* pcDocument, QWidget* parent, Qt::WindowFlags wflags=Qt::WindowFlags());
    /** View destructor
     * Detach the view from the document, if attached.
     */
    ~MDIView() override;

    /// A new view of the same kind on the same document, or null when
    /// the view type does not support cloning (upstream MDIView API).
    virtual MDIView* clone();
    /// Copy the window chrome -- title, icon, size, window state --
    /// from \a from onto this view (upstream MDIView API).
    void cloneFrom(const MDIView& from);

    using QMainWindow::isHidden;

    /// get called when the document is updated
    void onRelabel(Gui::Document *pDoc) override;
    virtual void viewAll();

    /** The view that should be reported active when this view's window
     * activates. Container views (Gui::ViewArea) return their focused
     * embedded child so that MainWindow::activeWindow() always resolves
     * to a working view; plain views return themselves.
     */
    virtual MDIView *activeSubView() { return this; }

    /** Whether nobody is looking at this view -- hidden, minimized, or
     * behind a maximized sibling in the tabbed MDI area. Render engines
     * use this to release per-view GPU targets.
     */
    virtual bool isBackgroundView() const;

    /// Message handler
    bool onMsg(const char* pMsg,const char** ppReturn) override;
    /// Message handler test
    bool onHasMsg(const char* pMsg) const override;
    /// overwrite when checking on close state
    bool canClose() override;
    /// delete itself
    void deleteSelf() override;
    PyObject *getPyObject() override;
    MDIViewPy *getMDIViewPyObject();
    static MDIViewPy *castFrom(PyObject *);
    /** @name Printing */
    //@{
public Q_SLOTS:
    virtual void print(QPrinter* printer);

public:
    /** Print content of view */
    virtual void print();
    /** Print to PDF file */
    virtual void printPdf();
    /** Show a preview dialog */
    virtual void printPreview();
    /** Save the printer configuration */
    void savePrinterSettings(QPrinter* printer);
    /** Restore the printer configuration */
    void restorePrinterSettings(QPrinter* printer);
    //@}

    /** @name Undo/Redo actions */
    //@{
    virtual QStringList undoActions() const;
    virtual QStringList redoActions() const;
    //@}

    QSize minimumSizeHint () const override;

    /// MDI view mode enum
    enum ViewMode {
        Child,      /**< Child viewing, view is docked inside the MDI application window */
        TopLevel,   /**< The view becomes a top level window and can be moved outsinde the application window */
        FullScreen  /**< The view goes to full screen viewing */
    };
    /**
     * If \a b is set to \a FullScreen the MDI view is displayed in full screen mode, if \a b
     * is set to \a TopLevel then it is displayed as an own top-level window, otherwise (\a Normal)
     * as tabbed window. For more hints refer to the Qt documentation to
     * QWidget::showFullScreen ().
     */
    virtual void setCurrentViewMode(ViewMode mode);
    ViewMode currentViewMode() const { return currentMode; }


    /// access getter for the active object list
    template<typename _T>
    inline _T getActiveObject(const char* name, App::DocumentObject **parent=nullptr, std::string *subname=nullptr) const
    {
        return ActiveObjects.getObject<_T>(name,parent,subname);
    }
    void setActiveObject(App::DocumentObject*o, const char*n, const char *subname=nullptr)
    {
        ActiveObjects.setObject(o, n, subname);
    }
    bool hasActiveObject(const char*n) const
    {
        return ActiveObjects.hasObject(n);
    }
    bool isActiveObject(App::DocumentObject*o, const char*n, const char *subname=nullptr) const
    {
        return ActiveObjects.hasObject(o,n,subname);
    }

    /*!
     * \brief containsViewProvider
     * Checks if the given view provider is part of this view. The default implementation
     * returns false.
     * \return bool
     */
    virtual bool containsViewProvider(const ViewProvider*) const {
        return false;
    }

public Q_SLOTS:
    virtual void setOverrideCursor(const QCursor&);
    virtual void restoreOverrideCursor();

Q_SIGNALS:
    void message(const QString&, int);

protected Q_SLOTS:
    /** This method gets called from the main window this view is attached to
     * whenever the window state of the active view changes.
     * The default implementation does nothing.
     */
    virtual void windowStateChanged(QWidget*);

protected:
    void closeEvent(QCloseEvent *e) override;
    /** \internal */
    void changeEvent(QEvent *e) override;

public:
    /** @name This view's own selection (docs/TaskPanelPerView.md sec 12)
     *
     * The views of the main window share one selection instance, the
     * room, until one of them has a reason to select on its own: it is in
     * an edit, or it owns a task dialog. For as long as it has, what is
     * picked in it, what a dialog's gate lets through, and what the tree
     * and the commands do while it is the active view are its own, and
     * the other views are not disturbed. With the preference
     * PerViewSelection every view has its own from the start.
     */
    //@{
    /// The instance this view selects into, or null while it shares the room
    SelectionSingleton *selectionInstance() const
    { return ownSelection.get(); }
    /** Take an instance of this view's own. Counted: every take is given
     * back with releaseOwnSelection(). The first one makes the instance,
     * as a copy of what is selected in the room -- an edit and a dialog
     * start on the selection their user had.
     */
    void takeOwnSelection();
    /** Give one take back. The last one hands what is selected here back
     * to the room and ends the instance.
     */
    void releaseOwnSelection();
    /// Make the active view's instance the one that is current while no
    /// scope is open (SelectionSingleton::setAmbient)
    static void updateAmbientSelection();
    //@}

private:
    /// Tell the viewers of this view, and the ambient instance
    void selectionInstanceChanged();
    std::unique_ptr<SelectionSingleton> ownSelection;
    int ownSelectionTakes = 0;
    /// Its own for good (PerViewSelection), not for the length of a take
    bool ownSelectionKept = false;

    ViewMode currentMode;
    Qt::WindowStates wstate;
    // list of active objects of this view
    ActiveObjectList ActiveObjects;
    using Connection = fastsignals::connection;
    Connection connectDelObject; //remove active object upon delete.
    MDIViewPy *mdiViewPy = nullptr;

    friend class MainWindow;
    // Embedded split-view children re-make MainWindow's
    // windowStateChanged connection themselves (ViewArea.cpp
    // hostView/releaseView).
    friend class ViewAreaCell;
};

} // namespace Gui

#endif // GUI_MDIVIEW_H

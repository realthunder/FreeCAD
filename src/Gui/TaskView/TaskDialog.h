/***************************************************************************
 *   Copyright (c) 2009 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef GUI_TASKVIEW_TASKDIALOG_H
#define GUI_TASKVIEW_TASKDIALOG_H

#include <string>
#include <vector>

#include <QDialogButtonBox>
#include <QPointer>
#include <FCGlobal.h>

#include <Gui/TaskOwner.h>


namespace App {

}

namespace Gui {
class ControlSingleton;
class MDIView;
namespace TaskView {

class TaskContent;
class TaskDialogAttorney;
class TaskDialogPy;
class TaskView;

/// Father class of content with header and Icon
class GuiExport TaskDialog : public QObject
{
    Q_OBJECT

public:
    enum ButtonPosition {
        North, South
    };

    TaskDialog();
    ~TaskDialog() override;

    void addTaskBox(QWidget*);
    /// As above, but with an icon on the box header. Upstream spells the
    /// icon-less form as a default argument; keeping a separate overload
    /// leaves every existing call site here binding to the same function.
    void addTaskBox(const QPixmap& icon, QWidget*);

    void setButtonPosition(ButtonPosition p)
    { pos = p; }
    ButtonPosition buttonPosition() const
    { return pos; }
    const std::vector<QWidget*> &getDialogContent() const;
    bool canClose() const;
    bool tryClose();

    /// tells the framework which buttons are wished for the dialog
    virtual QDialogButtonBox::StandardButtons getStandardButtons() const
    { return QDialogButtonBox::Ok|QDialogButtonBox::Cancel; }
    virtual void modifyStandardButtons(QDialogButtonBox*)
    {}

    /// Defines whether a task dialog can be rejected by pressing Esc
    void setEscapeButtonEnabled(bool on) {
        escapeButton = on;
    }
    bool isEscapeButtonEnabled() const {
        return escapeButton;
    }
    /// The role of the button Esc presses: rejecting by default
    QDialogButtonBox::ButtonRole roleOnEscape {QDialogButtonBox::RejectRole};

    /// Defines whether a task dialog must be closed if the document changed the
    /// active transaction.
    void setAutoCloseOnTransactionChange(bool on) {
        autoCloseTransaction = on;
    }
    bool isAutoCloseOnTransactionChange() const {
        return autoCloseTransaction;
    }

    /** @name Closing with what the dialog was opened for
     *  Upstream's switches (docs/TaskPanelPerView.md sec 5.4), each off
     *  by default. A dialog that sets one is told by the matching
     *  autoClosedOn...() and then removed; it is neither accepted nor
     *  rejected.
     */
    //@{
    /// Close when the edit running in the dialog's view is left
    void setAutoCloseOnResetEdit(bool on) {
        autoCloseResetEdit = on;
    }
    bool isAutoCloseOnResetEdit() const {
        return autoCloseResetEdit;
    }
    /// Close when the document the dialog names (setDocumentName), or
    /// the document of its view, is closed
    void setAutoCloseOnDeletedDocument(bool on) {
        autoCloseDeletedDocument = on;
    }
    bool isAutoCloseOnDeletedDocument() const {
        return autoCloseDeletedDocument;
    }
    /** Be told when the dialog's view is closed.
     *
     * A dialog never outlives its view: there is nobody left to answer
     * it. One that sets this is told by autoClosedOnClosedView(); any
     * other is rejected.
     */
    void setAutoCloseOnClosedView(bool on) {
        autoCloseClosedView = on;
    }
    bool isAutoCloseOnClosedView() const {
        return autoCloseClosedView;
    }
    //@}

    const std::string& getDocumentName() const
    { return documentName; }
    void setDocumentName(const std::string& doc)
    { documentName = doc; }

    /** The view this dialog belongs to (docs/TaskPanelPerView.md sec 3).
     *
     * Named when the dialog is shown: the view Control().showDialog() was
     * given, else the view being handled at that moment. Nobody before
     * that, and for a dialog shown with no view to name.
     */
    const TaskOwner& owner() const
    { return taskOwner; }
    /// The owner's desktop view, or null. Upstream's name for it.
    MDIView* getAssociatedView() const;
    /*!
      Indicates whether this task dialog allows other commands to modify
      the document while it is open.
    */
    virtual bool isAllowedAlterDocument() const
    { return false; }
    /*!
      Indicates whether this task dialog allows other commands to modify
      the 3d view while it is open.
    */
    virtual bool isAllowedAlterView() const
    { return true; }
    /*!
      Indicates whether this task dialog allows other commands to modify
      the selection while it is open.
    */
    virtual bool isAllowedAlterSelection() const
    { return true; }
    virtual bool needsFullSpace() const
    { return false; }
    /*!
      Whether the view this dialog is shown for selects into an instance of
      its own while the dialog is open (docs/TaskPanelPerView.md sec 12):
      what is picked for the dialog, and the gate it sets, then stay out of
      the other views. A dialog that works on the selection every view
      shares says false.
    */
    virtual bool usesOwnSelection() const
    { return true; }

public:
    /// is called by the framework when the dialog is opened
    virtual void open();
    /// is called by the framework when the dialog is closed
    virtual void closed();
    /// is called by the framework when the dialog is automatically closed due to
    /// changing the active transaction
    virtual void autoClosedOnTransactionChange();
    /// is called by the framework when the dialog is automatically closed due to
    /// leaving the edit of its view
    virtual void autoClosedOnResetEdit();
    /// is called by the framework when the dialog is automatically closed due to
    /// closing its document
    virtual void autoClosedOnDeletedDocument();
    /// is called by the framework when the dialog is automatically closed due to
    /// closing its view
    virtual void autoClosedOnClosedView();
    /** Called when the dialog becomes the one its user is working in, and
     * when it stops being so (docs/TaskPanelPerView.md sec 4.3).
     *
     * A dialog is active while its view is the active one: first after
     * open(), then every time its view is come back to. deactivate()
     * ends each activate(), the last one before closed(). They are for
     * what a dialog holds outside its own widgets and only one dialog can
     * hold at a time: an event filter on the main window, a cursor. The
     * selection gate is not among them -- a dialog's view selects into
     * an instance of its own, gate included (sec 12).
     *
     * A served client's dialog is active from open() to closed(): its
     * view is the only one its client has.
     */
    virtual void activate();
    virtual void deactivate();
    /// is called by the framework if a button is clicked which has no accept or reject role
    virtual void clicked(int);
    /// is called by the framework if the dialog is accepted (Ok)
    virtual bool accept();
    /// is called by the framework if the dialog is rejected (Cancel)
    virtual bool reject();
    /// is called by the framework if the user press the help button 
    virtual void helpRequested();

    void emitDestructionSignal() {
        Q_EMIT aboutToBeDestroyed();
    }
    
Q_SIGNALS:
    void aboutToBeDestroyed();
    
protected:
    QPointer<QDialogButtonBox> buttonBox;
    /// List of TaskBoxes of that dialog
    std::vector<QWidget*> Content;
    ButtonPosition pos;

private:
    std::string documentName;
    TaskOwner taskOwner;
    bool escapeButton;
    bool autoCloseTransaction;
    bool autoCloseResetEdit {false};
    bool autoCloseDeletedDocument {false};
    bool autoCloseClosedView {false};

    friend class TaskDialogAttorney;
};

class TaskDialogAttorney {
private:
    static void setButtonBox(TaskDialog* dlg, QDialogButtonBox* box) {
        dlg->buttonBox = box;
    }
    static QDialogButtonBox* getButtonBox(TaskDialog* dlg) {
        return dlg->buttonBox;
    }
    /// The owner is the showing's to name, once: Control().showDialog().
    static void setOwner(TaskDialog* dlg, const TaskOwner& owner) {
        dlg->taskOwner = owner;
    }

    friend class TaskDialogPy;
    friend class TaskView;
    friend class Gui::ControlSingleton;
};

} //namespace TaskView
} //namespace Gui

#endif // GUI_TASKVIEW_TASKDIALOG_H

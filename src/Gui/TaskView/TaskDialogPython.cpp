/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <sstream>
# include <QEvent>
# include <QFile>
# include <QPointer>
#endif

#include <App/Document.h>
#include <App/DocumentPy.h>
#include <Base/Interpreter.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/DocumentPy.h>
#include <Gui/MDIView.h>
#include <Gui/MDIViewPy.h>
#include <Gui/UiLoader.h>
#include <Gui/PythonWrapper.h>

#include "TaskDialogPython.h"
#include "TaskView.h"


using namespace Gui;
using namespace Gui::TaskView;

ControlPy* ControlPy::instance = nullptr;

namespace
{
/// What Gui.Control.currentOwner() hands out: a TaskOwner by value. A view
/// of the main window has a Python object to be named by; a served
/// client's has none, and this is how Python carries either one across a
/// timer.
const char* const ownerCapsule = "Gui.TaskOwner";

void deleteOwnerCapsule(PyObject* capsule)
{
    delete static_cast<Gui::TaskOwner*>(PyCapsule_GetPointer(capsule, ownerCapsule));
}

/** Whose dialog a call is about, as Python names it.
 *
 * `view` is a view of the main window (Gui.ActiveDocument.ActiveView and
 * the like) or what currentOwner() returned; `attachTo` is a document,
 * which is upstream's keyword for the same thing one notch coarser.
 * Neither, or None for both, is the argument left out.
 */
struct PyTaskTarget
{
    Gui::TaskOwner owner;
    App::Document* document {nullptr};

    PyTaskTarget(PyObject* view, PyObject* attachTo)
    {
        if (view && PyCapsule_IsValid(view, ownerCapsule)) {
            owner = *static_cast<Gui::TaskOwner*>(PyCapsule_GetPointer(view, ownerCapsule));
        }
        else if (view && view != Py_None) {
            owner = Gui::TaskOwner(viewOf(view));
        }
        if (attachTo && attachTo != Py_None) {
            if (!owner.isNull()) {
                throw Py::TypeError("give either view or attachTo, not both");
            }
            if (PyObject_TypeCheck(attachTo, &App::DocumentPy::Type)) {
                document = static_cast<App::DocumentPy*>(attachTo)->getDocumentPtr();
            }
            else if (PyObject_TypeCheck(attachTo, &Gui::DocumentPy::Type)) {
                document = static_cast<Gui::DocumentPy*>(attachTo)->getDocumentPtr()->getDocument();
            }
            else {
                throw Py::TypeError("attachTo must be a document, or None");
            }
        }
    }

    Gui::TaskView::TaskDialog* activeDialog() const
    {
        if (document) {
            return Gui::Control().activeDialog(document);
        }
        if (!owner.isNull()) {
            return Gui::Control().activeDialog(owner);
        }
        return Gui::Control().activeDialog();
    }

private:
    /// The view behind a Python view object: an MDIViewPy, or a view type
    /// of a module's own that says how to reach its base (cast_to_base).
    static Gui::MDIView* viewOf(PyObject* obj)
    {
        if (PyObject_TypeCheck(obj, &Gui::MDIViewPy::Type)) {
            return static_cast<Gui::MDIViewPy*>(obj)->getMDIViewPtr();
        }
        if (PyObject_HasAttrString(obj, "cast_to_base")) {
            Py::Object base = Py::Object(obj).callMemberFunction("cast_to_base");
            if (PyObject_TypeCheck(base.ptr(), &Gui::MDIViewPy::Type)) {
                return static_cast<Gui::MDIViewPy*>(base.ptr())->getMDIViewPtr();
            }
        }
        throw Py::TypeError("view must be a view of the main window, what "
                            "currentOwner() returned, or None");
    }
};

PyTaskTarget parseTaskTarget(const Py::Tuple& args, const Py::Dict& kwds)
{
    PyObject* view = Py_None;
    PyObject* attachTo = Py_None;
    static const char* names[] = {"view", "attachTo", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args.ptr(), kwds.ptr(), "|OO",
                                     const_cast<char**>(names), &view, &attachTo)) {
        throw Py::Exception();
    }
    return {view, attachTo};
}
}  // namespace

ControlPy* ControlPy::getInstance()
{
    if (!instance)
        instance = new ControlPy();
    return instance;
}

void ControlPy::init_type()
{
    behaviors().name("Control");
    behaviors().doc("Control for task dialogs");
    // you must have overwritten the virtual functions
    behaviors().supportRepr();
    behaviors().supportGetattr();
    behaviors().supportSetattr();
    add_keyword_method("showDialog",&ControlPy::showDialog,
                        "show the given dialog in the task panel\n"
                        "showDialog(dialog, view=None, attachTo=None)\n"
                        "--\n"
                        "A task dialog belongs to a view: 'view', else the view of the\n"
                        "document 'attachTo', else the view being handled when this is\n"
                        "called. A deferred call (a timer) should pass the view it was\n"
                        "asked under.\n"
                        "Returns the task dialog.\n"
                        "if a task is already active a RuntimeError is raised");
    add_keyword_method("activeDialog",&ControlPy::activeDialog,
                        "check if a dialog is active in the task panel\n"
                        "activeDialog(view=None, attachTo=None) --> bool\n"
                        "--\n"
                        "With 'view' or 'attachTo', whether that view or that document's\n"
                        "view has one.");
    add_keyword_method("activeTaskDialog",&ControlPy::activeTaskDialog,
                        "return the active task dialog if there is one\n"
                        "activeTaskDialog(view=None, attachTo=None) --> TaskDialog or None");
    add_keyword_method("closeDialog",&ControlPy::closeDialog,
                        "close the active dialog\n"
                        "closeDialog(view=None, attachTo=None)");
    add_varargs_method("currentOwner",&ControlPy::currentOwner,
                        "name the view being handled now, to be passed as 'view' later\n"
                        "currentOwner() --> object\n"
                        "--\n"
                        "For a call that is deferred: take this when the timer is set\n"
                        "and pass it to showDialog() when it fires, so the dialog belongs\n"
                        "to the view it was asked in and not to whichever is active by\n"
                        "then. It names a served client's view too, which has no view\n"
                        "object.");
    add_varargs_method("addTaskWatcher",&ControlPy::addTaskWatcher,
                        "install a (list of) TaskWatcher\n"
                        "addTaskWatcher(TaskWatcher | list)");
    add_varargs_method("clearTaskWatcher",&ControlPy::clearTaskWatcher,
                        "remove all TaskWatchers\n"
                        "clearTaskWatcher()");
    add_varargs_method("isAllowedAlterDocument",&ControlPy::isAllowedAlterDocument,
                        "return the permission to alter the current Document\n"
                        "isAllowedAlterDocument() --> bool");
    add_varargs_method("isAllowedAlterView",&ControlPy::isAllowedAlterView,
                        "return the permission to alter the current View\n"
                        "isAllowedAlterView() --> bool");
    add_varargs_method("isAllowedAlterSelection",&ControlPy::isAllowedAlterSelection,
                        "return the permission to alter the current Selection\n"
                        "isAllowedAlterSelection() --> bool");
    add_varargs_method("showTaskView",&ControlPy::showTaskView,
                        "show the Task panel\n"
                        "showTaskView()");
    add_varargs_method("showModelView",&ControlPy::showModelView,
                        "show the Model panel\n"
                        "showModelView()");
}

ControlPy::ControlPy() = default;

ControlPy::~ControlPy() = default;

Py::Object ControlPy::repr()
{
    std::string s;
    std::ostringstream s_out;
    s_out << "Control Task Dialog";
    return Py::String(s_out.str());
}

Py::Object ControlPy::showDialog(const Py::Tuple& args, const Py::Dict& kwds)
{
    PyObject* arg0;
    PyObject* view = Py_None;
    PyObject* attachTo = Py_None;
    static const char* names[] = {"dialog", "view", "attachTo", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args.ptr(), kwds.ptr(), "O|OO",
                                     const_cast<char**>(names), &arg0, &view, &attachTo))
        throw Py::Exception();
    PyTaskTarget target(view, attachTo);
    // Asked before the dialog is built: building it takes the panel's
    // form, and a refused dialog would take the form with it
    const bool may = target.document ? Gui::Control().mayShowDialog(target.document)
                                     : Gui::Control().mayShowDialog(target.owner);
    if (!may)
        throw Py::RuntimeError("Active task dialog found");
    auto dlg = new TaskDialogPython(Py::Object(arg0));
    // Held by a guarded pointer: the panel's own open() may close it
    Py::Object task = Py::asObject(new TaskDialogPy(dlg));
    if (target.document)
        Gui::Control().showDialog(dlg, target.document);
    else
        Gui::Control().showDialog(dlg, target.owner);
    return task;
}

Py::Object ControlPy::activeDialog(const Py::Tuple& args, const Py::Dict& kwds)
{
    Gui::TaskView::TaskDialog* dlg = parseTaskTarget(args, kwds).activeDialog();
    return Py::Boolean(dlg != nullptr);
}

Py::Object ControlPy::activeTaskDialog(const Py::Tuple& args, const Py::Dict& kwds)
{
    Gui::TaskView::TaskDialog* dlg = parseTaskTarget(args, kwds).activeDialog();
    return (dlg ? Py::asObject(new TaskDialogPy(dlg)) : Py::None());
}

Py::Object ControlPy::closeDialog(const Py::Tuple& args, const Py::Dict& kwds)
{
    PyTaskTarget target = parseTaskTarget(args, kwds);
    if (target.document)
        Gui::Control().closeDialog(target.document);
    else if (!target.owner.isNull())
        Gui::Control().closeDialog(target.owner);
    else
        Gui::Control().closeDialog();
    return Py::None();
}

Py::Object ControlPy::currentOwner(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    auto owner = new Gui::TaskOwner(Gui::TaskOwner::current());
    PyObject* capsule = PyCapsule_New(owner, ownerCapsule, deleteOwnerCapsule);
    if (!capsule) {
        delete owner;
        throw Py::Exception();
    }
    return Py::asObject(capsule);
}

Py::Object ControlPy::addTaskWatcher(const Py::Tuple& args)
{
    PyObject* arg0;
    if (!PyArg_ParseTuple(args.ptr(), "O", &arg0))
        throw Py::Exception();

    std::vector<Gui::TaskView::TaskWatcher*> watcher;
    Py::Sequence list(arg0);
    for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
        auto w = new TaskWatcherPython(*it);
        watcher.push_back(w);
    }

    Gui::TaskView::TaskView* taskView = Gui::Control().taskWatcherPanel();
    if (taskView)
        taskView->addTaskWatcher(watcher);
    return Py::None();
}

Py::Object ControlPy::clearTaskWatcher(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    Gui::TaskView::TaskView* taskView = Gui::Control().taskWatcherPanel();
    if (taskView)
        taskView->clearTaskWatcher();
    return Py::None();
}

Py::Object ControlPy::isAllowedAlterDocument(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    bool ok = Gui::Control().isAllowedAlterDocument();
    return Py::Boolean(ok);
}

Py::Object ControlPy::isAllowedAlterView(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    bool ok = Gui::Control().isAllowedAlterView();
    return Py::Boolean(ok);
}

Py::Object ControlPy::isAllowedAlterSelection(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    bool ok = Gui::Control().isAllowedAlterSelection();
    return Py::Boolean(ok);
}

Py::Object ControlPy::showTaskView(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    Gui::Control().showTaskView();
    return Py::None();
}

Py::Object ControlPy::showModelView(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    Gui::Control().showModelView();
    return Py::None();
}

// ------------------------------------------------------------------

TaskWatcherPython::TaskWatcherPython(const Py::Object& o)
  : TaskWatcher(nullptr), watcher(o)
{
    QString title;
    if (watcher.hasAttr(std::string("title"))) {
        Py::String name(watcher.getAttr(std::string("title")));
        std::string s = static_cast<std::string>(name);
        title = QString::fromUtf8(s.c_str());
    }

    QPixmap icon;
    if (watcher.hasAttr(std::string("icon"))) {
        Py::String name(watcher.getAttr(std::string("icon")));
        std::string s = static_cast<std::string>(name);
        icon = BitmapFactory().pixmap(s.c_str());
    }

    Gui::TaskView::TaskBox *tb = nullptr;
    if (watcher.hasAttr(std::string("commands"))) {
        tb = new Gui::TaskView::TaskBox(icon, title, true, nullptr);
        Py::Sequence cmds(watcher.getAttr(std::string("commands")));
        CommandManager &mgr = Gui::Application::Instance->commandManager();
        for (Py::Sequence::iterator it = cmds.begin(); it != cmds.end(); ++it) {
            Py::String name(*it);
            std::string s = static_cast<std::string>(name);
            Command *c = mgr.getCommandByName(s.c_str());
            if (c)
                c->addTo(tb);
        }
    }

    if (watcher.hasAttr(std::string("widgets"))) {
        if (!tb && !title.isEmpty())
            tb = new Gui::TaskView::TaskBox(icon, title, true, nullptr);
        Py::Sequence list(watcher.getAttr(std::string("widgets")));

        Gui::PythonWrapper wrap;
        if (wrap.loadCoreModule()) {
            for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                QObject* object = wrap.toQObject(*it);
                if (object) {
                    QWidget* w = qobject_cast<QWidget*>(object);
                    if (w) {
                        if (tb)
                            tb->groupLayout()->addWidget(w);
                        else
                            Content.push_back(w);
                    }
                }
            }
        }
    }

    if (tb) Content.push_back(tb);

    if (watcher.hasAttr(std::string("filter"))) {
        Py::String name(watcher.getAttr(std::string("filter")));
        std::string s = static_cast<std::string>(name);
        this->setFilter(s.c_str());
    }
}

TaskWatcherPython::~TaskWatcherPython()
{
    std::vector< QPointer<QWidget> > guarded;
    guarded.insert(guarded.begin(), Content.begin(), Content.end());
    Content.clear();
    Base::PyGILStateLocker lock;
    this->watcher = Py::None();
    Content.insert(Content.begin(), guarded.begin(), guarded.end());
}

bool TaskWatcherPython::shouldShow()
{
    Base::PyGILStateLocker lock;
    try {
        if (watcher.hasAttr(std::string("shouldShow"))) {
            Py::Callable method(watcher.getAttr(std::string("shouldShow")));
            Py::Tuple args;
            Py::Boolean ret(method.apply(args));
            return (bool)ret;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    if (!this->Filter.empty())
        return match();
    else
        return TaskWatcher::shouldShow();
}

// ------------------------------------------------------------------

void TaskDialogPy::init_type()
{
    behaviors().name("TaskDialog");
    behaviors().doc("Task dialog");
    // you must have overwritten the virtual functions
    behaviors().supportRepr();
    behaviors().supportGetattr();
    behaviors().supportSetattr();
    add_varargs_method("getDialogContent",&TaskDialogPy::getDialogContent,
                       "Returns the widgets of the task dialog -> list");
    add_varargs_method("getStandardButtons",&TaskDialogPy::getStandardButtons,
                       "Get the standard buttons of the box -> flags");
    add_varargs_method("setEscapeButtonEnabled",&TaskDialogPy::setEscapeButtonEnabled,
                       "Defines whether the task dialog can be rejected by pressing Esc");
    add_varargs_method("isEscapeButtonEnabled",&TaskDialogPy::isEscapeButtonEnabled,
                       "Checks if the task dialog can be rejected by pressing Esc -> bool");
    add_varargs_method("setAutoCloseOnTransactionChange",&TaskDialogPy::setAutoCloseOnTransactionChange,
                       "Defines whether a task dialog must be closed if the document changes the\n"
                       "active transaction");
    add_varargs_method("isAutoCloseOnTransactionChange",&TaskDialogPy::isAutoCloseOnTransactionChange,
                       "Checks if the task dialog will be closed when the active transaction has changed -> bool");
    add_varargs_method("setAutoCloseOnResetEdit",&TaskDialogPy::setAutoCloseOnResetEdit,
                       "Defines whether a task dialog must be closed when the edit running in\n"
                       "its view is left; the panel's autoClosedOnResetEdit() is called");
    add_varargs_method("isAutoCloseOnResetEdit",&TaskDialogPy::isAutoCloseOnResetEdit,
                       "Checks if the task dialog will be closed when the edit is left -> bool");
    add_varargs_method("setAutoCloseOnDeletedDocument",&TaskDialogPy::setAutoCloseOnDeletedDocument,
                       "Defines whether a task dialog must be closed when the document it names\n"
                       "(setDocumentName), or the document of its view, is closed; the panel's\n"
                       "autoClosedOnDeletedDocument() is called");
    add_varargs_method("isAutoCloseOnDeletedDocument",&TaskDialogPy::isAutoCloseOnDeletedDocument,
                       "Checks if the task dialog will be closed with its document -> bool");
    add_varargs_method("setAutoCloseOnClosedView",&TaskDialogPy::setAutoCloseOnClosedView,
                       "Defines whether the panel's autoClosedOnClosedView() is called when the\n"
                       "view the task dialog belongs to is closed. A dialog never outlives its\n"
                       "view: one that does not ask for this is rejected.");
    add_varargs_method("isAutoCloseOnClosedView",&TaskDialogPy::isAutoCloseOnClosedView,
                       "Checks if the task dialog is told when its view is closed -> bool");
    add_varargs_method("getDocumentName",&TaskDialogPy::getDocumentName,
                       "Get the name of the document the task dialog is attached to -> str");
    add_varargs_method("setDocumentName",&TaskDialogPy::setDocumentName,
                       "Set the name of the document the task dialog is attached to");
    add_varargs_method("getAssociatedView",&TaskDialogPy::getAssociatedView,
                       "Get the view of the main window the task dialog belongs to -> view or None\n"
                       "None for a dialog that belongs to no view, or to a served client's.");
    add_varargs_method("getOwnerKind",&TaskDialogPy::getOwnerKind,
                       "Say what the task dialog belongs to -> str\n"
                       "'view': a view of the main window (getAssociatedView)\n"
                       "'client': a served client's view\n"
                       "'viewer': a 3D viewer that sits in no view\n"
                       "'none': no view; the dialog is everybody's\n"
                       "'gone': a view that no longer exists");
    add_varargs_method("isAllowedAlterDocument",&TaskDialogPy::isAllowedAlterDocument,
                       "Indicates whether this task dialog allows other commands to modify\n"
                       "the document while it is open -> bool");
    add_varargs_method("isAllowedAlterView",&TaskDialogPy::isAllowedAlterView,
                       "Indicates whether this task dialog allows other commands to modify\n"
                       "the 3d view while it is open -> bool");
    add_varargs_method("isAllowedAlterSelection",&TaskDialogPy::isAllowedAlterSelection,
                       "Indicates whether this task dialog allows other commands to modify\n"
                       "the selection while it is open -> bool");
    add_varargs_method("needsFullSpace",&TaskDialogPy::needsFullSpace,
                       "Indicates whether the task dialog fully requires the available space -> bool");
    add_varargs_method("accept",&TaskDialogPy::accept,
                       "Accept the task dialog");
    add_varargs_method("reject",&TaskDialogPy::reject,
                       "Reject the task dialog");
}

TaskDialogPy::TaskDialogPy(TaskDialog* dlg)
  : dialog(dlg)
{
}

TaskDialogPy::~TaskDialogPy() = default;

Py::Object TaskDialogPy::repr()
{
    std::string s;
    std::ostringstream s_out;
    s_out << "Task Dialog";
    return Py::String(s_out.str());
}

Py::Object TaskDialogPy::getattr(const char * attr)
{
    if (!dialog) {
        std::ostringstream s_out;
        s_out << "Cannot access attribute '" << attr << "' of deleted object";
        throw Py::RuntimeError(s_out.str());
    }
    return BaseType::getattr(attr);
}

int TaskDialogPy::setattr(const char *attr, const Py::Object &value)
{
    if (!dialog) {
        std::ostringstream s_out;
        s_out << "Cannot access attribute '" << attr << "' of deleted object";
        throw Py::RuntimeError(s_out.str());
    }
    return BaseType::setattr(attr, value);
}

Py::Object TaskDialogPy::getDialogContent(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();

    PythonWrapper wrap;
    wrap.loadWidgetsModule();

    Py::List list;
    auto widgets = dialog->getDialogContent();
    for (auto it : widgets) {
        list.append(wrap.fromQWidget(it));
    }

    return list;
}

Py::Object TaskDialogPy::getStandardButtons(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    auto buttons = dialog->getStandardButtons();
    return Py::Long(static_cast<int>(buttons));
}

Py::Object TaskDialogPy::setEscapeButtonEnabled(const Py::Tuple& args)
{
    Py::Boolean value(args[0]);
    dialog->setEscapeButtonEnabled(static_cast<bool>(value));
    return Py::None();
}

Py::Object TaskDialogPy::isEscapeButtonEnabled(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isEscapeButtonEnabled());
}

Py::Object TaskDialogPy::setAutoCloseOnTransactionChange(const Py::Tuple& args)
{
    Py::Boolean value(args[0]);
    dialog->setAutoCloseOnTransactionChange(static_cast<bool>(value));
    return Py::None();
}

Py::Object TaskDialogPy::isAutoCloseOnTransactionChange(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isAutoCloseOnTransactionChange());
}

Py::Object TaskDialogPy::setAutoCloseOnResetEdit(const Py::Tuple& args)
{
    Py::Boolean value(args[0]);
    dialog->setAutoCloseOnResetEdit(static_cast<bool>(value));
    return Py::None();
}

Py::Object TaskDialogPy::isAutoCloseOnResetEdit(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isAutoCloseOnResetEdit());
}

Py::Object TaskDialogPy::setAutoCloseOnDeletedDocument(const Py::Tuple& args)
{
    Py::Boolean value(args[0]);
    dialog->setAutoCloseOnDeletedDocument(static_cast<bool>(value));
    return Py::None();
}

Py::Object TaskDialogPy::isAutoCloseOnDeletedDocument(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isAutoCloseOnDeletedDocument());
}

Py::Object TaskDialogPy::setAutoCloseOnClosedView(const Py::Tuple& args)
{
    Py::Boolean value(args[0]);
    dialog->setAutoCloseOnClosedView(static_cast<bool>(value));
    return Py::None();
}

Py::Object TaskDialogPy::isAutoCloseOnClosedView(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isAutoCloseOnClosedView());
}

Py::Object TaskDialogPy::getDocumentName(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::String(dialog->getDocumentName());
}

Py::Object TaskDialogPy::setDocumentName(const Py::Tuple& args)
{
    const char* name = "";
    if (!PyArg_ParseTuple(args.ptr(), "s", &name))
        throw Py::Exception();
    dialog->setDocumentName(name);
    return Py::None();
}

Py::Object TaskDialogPy::getAssociatedView(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    if (Gui::MDIView* view = dialog->getAssociatedView())
        return Py::asObject(view->getPyObject());
    return Py::None();
}

Py::Object TaskDialogPy::getOwnerKind(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    const Gui::TaskOwner& owner = dialog->owner();
    const char* kind = "viewer";
    if (owner.isNull())
        kind = "none";
    else if (!owner.isValid())
        kind = "gone";
    else if (owner.mdiView())
        kind = "view";
    else if (owner.isRemote())
        kind = "client";
    return Py::String(kind);
}

Py::Object TaskDialogPy::isAllowedAlterDocument(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isAllowedAlterDocument());
}

Py::Object TaskDialogPy::isAllowedAlterView(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isAllowedAlterView());
}

Py::Object TaskDialogPy::isAllowedAlterSelection(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->isAllowedAlterSelection());
}

Py::Object TaskDialogPy::needsFullSpace(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::Boolean(dialog->needsFullSpace());
}

namespace {
auto clickButton = [](QDialogButtonBox* buttonBox, QDialogButtonBox::ButtonRole role) {
    if (buttonBox) {
        QList<QAbstractButton*> list = buttonBox->buttons();
        for (auto pb : list) {
            if (buttonBox->buttonRole(pb) == role) {
                if (pb->isEnabled()) {
                    pb->click();
                    break;
                }
            }
        }
    }
};
}

Py::Object TaskDialogPy::accept(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    auto buttonBox = TaskDialogAttorney::getButtonBox(dialog);
    clickButton(buttonBox, QDialogButtonBox::AcceptRole);
    return Py::None();
}

Py::Object TaskDialogPy::reject(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    auto buttonBox = TaskDialogAttorney::getButtonBox(dialog);
    clickButton(buttonBox, QDialogButtonBox::RejectRole);
    return Py::None();
}

// ------------------------------------------------------------------

TaskDialogPython::TaskDialogPython(const Py::Object& o) : dlg(o)
{
    if (!tryLoadUiFile()) {
        tryLoadForm();
    }
}

TaskDialogPython::~TaskDialogPython()
{
    std::vector< QPointer<QWidget> > guarded;
    guarded.insert(guarded.begin(), Content.begin(), Content.end());
    Content.clear();

    Base::PyGILStateLocker lock;
    clearForm();

    // Assigning None to 'dlg' may destroy some of the stored widgets.
    // By guarding them with QPointer their pointers will be set to null
    // so that the destructor of the base class can reliably call 'delete'.
    Content.insert(Content.begin(), guarded.begin(), guarded.end());
}

bool TaskDialogPython::tryLoadUiFile()
{
    if (dlg.hasAttr(std::string("ui"))) {
        auto loader = UiLoader::newInstance();
        QString fn, icon;
        Py::String ui(dlg.getAttr(std::string("ui")));
        std::string path = static_cast<std::string>(ui);
        fn = QString::fromUtf8(path.c_str());

        QFile file(fn);
        QWidget* form = nullptr;
        if (file.open(QFile::ReadOnly))
            form = loader->load(&file, nullptr);
        file.close();
        if (form) {
            appendForm(form, QPixmap(icon));
        }
        else {
            Base::Console().Error("Failed to load UI file from '%s'\n",
                (const char*)fn.toUtf8());
        }

        return true;
    }

    return false;
}

bool TaskDialogPython::tryLoadForm()
{
    if (dlg.hasAttr(std::string("form"))) {
        Py::Object f(dlg.getAttr(std::string("form")));
        Py::List widgets;
        if (f.isList()) {
            widgets = f;
        }
        else {
            widgets.append(f);
        }

        Gui::PythonWrapper wrap;
        if (wrap.loadCoreModule()) {
            for (Py::List::iterator it = widgets.begin(); it != widgets.end(); ++it) {
                QObject* object = wrap.toQObject(*it);
                if (object) {
                    QWidget* form = qobject_cast<QWidget*>(object);
                    if (form) {
                        appendForm(form, form->windowIcon().pixmap(32));
                    }
                }
            }
        }

        return true;
    }

    return false;
}

void TaskDialogPython::appendForm(QWidget* form, const QPixmap& icon)
{
    form->installEventFilter(this);
    auto taskbox = new Gui::TaskView::TaskBox(
        icon, form->windowTitle(), true, nullptr);
    taskbox->groupLayout()->addWidget(form);
    Content.push_back(taskbox);
}

void TaskDialogPython::clearForm()
{
    try {
        // The widgets stored in the 'form' attribute will be deleted.
        // Thus, set this attribute to None to make sure that when using
        // the same dialog instance for a task panel won't segfault.
        if (this->dlg.hasAttr(std::string("form"))) {
            this->dlg.setAttr(std::string("form"), Py::None());
        }
        this->dlg = Py::None();
    }
    catch (Py::AttributeError& e) {
        e.clear();
    }
}

void TaskDialogPython::open()
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("open"))) {
            Py::Callable method(dlg.getAttr(std::string("open")));
            Py::Tuple args;
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void TaskDialogPython::clicked(int i)
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("clicked"))) {
            Py::Callable method(dlg.getAttr(std::string("clicked")));
            Py::Tuple args(1);
            args.setItem(0, Py::Int(i));
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

bool TaskDialogPython::accept()
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("accept"))) {
            Py::Callable method(dlg.getAttr(std::string("accept")));
            Py::Tuple args;
            Py::Boolean ret(method.apply(args));
            return (bool)ret;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return TaskDialog::accept();
}

bool TaskDialogPython::reject()
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("reject"))) {
            Py::Callable method(dlg.getAttr(std::string("reject")));
            Py::Tuple args;
            Py::Boolean ret(method.apply(args));
            return (bool)ret;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return TaskDialog::reject();
}

void TaskDialogPython::helpRequested()
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("helpRequested"))) {
            Py::Callable method(dlg.getAttr(std::string("helpRequested")));
            Py::Tuple args;
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void TaskDialogPython::callHook(const char *name)
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string(name))) {
            Py::Callable method(dlg.getAttr(std::string(name)));
            Py::Tuple args;
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void TaskDialogPython::autoClosedOnTransactionChange()
{
    callHook("autoClosedOnTransactionChange");
}

void TaskDialogPython::autoClosedOnResetEdit()
{
    callHook("autoClosedOnResetEdit");
}

void TaskDialogPython::autoClosedOnDeletedDocument()
{
    callHook("autoClosedOnDeletedDocument");
}

void TaskDialogPython::autoClosedOnClosedView()
{
    callHook("autoClosedOnClosedView");
}

void TaskDialogPython::activate()
{
    callHook("panelActivated");
}

void TaskDialogPython::deactivate()
{
    callHook("panelDeactivated");
}

bool TaskDialogPython::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        Base::PyGILStateLocker lock;
        try {
            if (dlg.hasAttr(std::string("changeEvent"))) {
                Py::Callable method(dlg.getAttr(std::string("changeEvent")));
                Py::Tuple args{1};
                args.setItem(0, Py::Long(static_cast<int>(event->type())));
                method.apply(args);
            }
        }
        catch (Py::Exception&) {
            Base::PyException e; // extract the Python error text
            e.ReportException();
        }
    }

    return TaskDialog::eventFilter(watched, event);
}

QDialogButtonBox::StandardButtons TaskDialogPython::getStandardButtons() const
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("getStandardButtons"))) {
            Py::Callable method(dlg.getAttr(std::string("getStandardButtons")));
            Py::Tuple args;
            int value = static_cast<int>(Gui::PythonWrapper::toEnum(method.apply(args)));
            return {value};
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return TaskDialog::getStandardButtons();
}

void TaskDialogPython::modifyStandardButtons(QDialogButtonBox *buttonBox)
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("modifyStandardButtons"))) {
            Gui::PythonWrapper wrap;
            wrap.loadGuiModule();
            wrap.loadWidgetsModule();
            Py::Callable method(dlg.getAttr(std::string("modifyStandardButtons")));
            Py::Tuple args(1);
            args.setItem(0, wrap.fromQWidget(buttonBox, "QDialogButtonBox"));
            method.apply(args);
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

bool TaskDialogPython::isAllowedAlterDocument() const
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("isAllowedAlterDocument"))) {
            Py::Callable method(dlg.getAttr(std::string("isAllowedAlterDocument")));
            Py::Tuple args;
            Py::Boolean ret(method.apply(args));
            return (bool)ret;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return TaskDialog::isAllowedAlterDocument();
}

bool TaskDialogPython::isAllowedAlterView() const
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("isAllowedAlterView"))) {
            Py::Callable method(dlg.getAttr(std::string("isAllowedAlterView")));
            Py::Tuple args;
            Py::Boolean ret(method.apply(args));
            return (bool)ret;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return TaskDialog::isAllowedAlterView();
}

bool TaskDialogPython::isAllowedAlterSelection() const
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("isAllowedAlterSelection"))) {
            Py::Callable method(dlg.getAttr(std::string("isAllowedAlterSelection")));
            Py::Tuple args;
            Py::Boolean ret(method.apply(args));
            return (bool)ret;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return TaskDialog::isAllowedAlterSelection();
}

bool TaskDialogPython::needsFullSpace() const
{
    Base::PyGILStateLocker lock;
    try {
        if (dlg.hasAttr(std::string("needsFullSpace"))) {
            Py::Callable method(dlg.getAttr(std::string("needsFullSpace")));
            Py::Tuple args;
            Py::Boolean ret(method.apply(args));
            return (bool)ret;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return TaskDialog::needsFullSpace();
}


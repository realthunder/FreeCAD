/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#endif

#include "SandboxGui.h"

#ifdef FC_EXPR_IMAGE_HOST

#include <nlohmann/json.hpp>
#include <CXX/Objects.hxx>
#include <App/Application.h>
#include <App/Document.h>
#include <App/ExpressionGuestProxy.h>
#include <App/ExpressionImageBridge.h>
#include <App/ExpressionImageHost.h>
#include <App/ExpressionSecurityRuntime.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>
#include <Base/Type.h>

#include "Application.h"
#include "Command.h"
#include "Control.h"
#include "Document.h"
#include "Fw/FwPy.h"
#include "Fw/FwQtPanel.h"
#include "Fw/FwQtView.h"
#include "Fw/FwStore.h"
#include "Fw/FwWidgets.h"
#include "MainWindow.h"
#include "Selection/SelectionObserverPython.h"
#include "TaskView/TaskDialog.h"
#include "TaskView/TaskDialogPython.h"
#include "TaskView/TaskView.h"

#include <QAction>
#include <QColorDialog>
#include <QCursor>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVariant>

using namespace Gui;
using json = nlohmann::json;
using App::ExpressionSandbox::HandleTable;
using App::ExpressionSandbox::guestProxyId;
using App::ExpressionSandbox::isGuestProxy;

namespace
{

// Requests and replies cross the App/Gui boundary as CBOR (a json in a
// signature does not link across the two nlohmann copies); these are
// the bridge's reply helpers over that.
using Reply = std::vector<unsigned char>;

Reply replyOk(json val)
{
    json r;
    r["ok"] = true;
    r["val"] = std::move(val);
    return json::to_cbor(r);
}

Reply replyErr(const char* exc, const std::string& msg)
{
    json r;
    r["ok"] = false;
    r["exc"] = exc;
    r["msg"] = msg;
    return json::to_cbor(r);
}

Reply replyPyError()
{
    return App::ExpressionSandbox::pyErrorReplyCbor();
}

/// `result` stolen
Reply replyResult(HandleTable& table, PyObject* result)
{
    return App::ExpressionSandbox::encodeResultCbor(table, result);
}

PyObject* decodeValue(const HandleTable& table, const json& value)
{
    return App::ExpressionSandbox::decodeHostValueCbor(table, json::to_cbor(value));
}

// The host-side workbench wrapper (docs/Sandbox.md 7.9 item 4).  The
// host writes __Workbench__ onto a handler after GetClassName, and a
// stand-in refuses host objects, so the handler the workbench registry
// holds is this wrapper: the five hooks forward to the stand-in, the
// attributes read at registration live here, and `make` derives a
// class NAMED after the workbench, since addWorkbench keys its registry
// by the handler's class name.
const char WrapperSource[] = R"PY(
import FreeCADGui

class GuestWorkbench(FreeCADGui.Workbench):
    """A workbench whose handler lives in the sandbox guest."""

    def __init__(self, standin, name, menutext, tooltip, icon):
        self._standin = standin
        self._fcx_name = name
        self.MenuText = menutext
        self.ToolTip = tooltip
        if icon:
            self.Icon = icon

    def GetClassName(self):
        return self._standin.GetClassName()

    def _hook(self, name):
        """A hook forwarded to the guest.  Its failure is an error in
        the report view with the guest's traceback, not the modal
        "Workbench failure" a native handler's raise ends in: a guest
        workbench comes up with what it registered before the raise
        (docs/Sandbox.md 7.9, G2b -- the InitGui runner's InitGui.py
        failures are logged the same way natively)."""
        try:
            getattr(self._standin, name)()
        except Exception as exc:
            import FreeCAD
            FreeCAD.Console.PrintError("%s.%s() in the sandbox guest failed: %s\n"
                                       % (self._fcx_name, name, exc))

    def Initialize(self):
        self._hook("Initialize")

    def Activated(self):
        if hasattr(self._standin, "Activated"):
            self._hook("Activated")

    def Deactivated(self):
        if hasattr(self._standin, "Deactivated"):
            self._hook("Deactivated")

    def ContextMenu(self, recipient):
        if hasattr(self._standin, "ContextMenu"):
            self._standin.ContextMenu(recipient)


def make(standin, name, menutext, tooltip, icon):
    cls = type(name, (GuestWorkbench,), {"__module__": "FreeCADGui"})
    return cls(standin, name, menutext, tooltip, icon)


def is_guest(wb):
    return isinstance(wb, GuestWorkbench)


def user_input():
    """FreeCADGui.UserInput by value, for the guest's mirror of it."""
    return {name: int(member.value) for name, member in FreeCADGui.UserInput.__members__.items()}


def show_hints(spec):
    """The guest's hints, [[message, [input | [input, ...], ...]], ...] by
    value, shown on the main window as the native HintManager does."""
    hints = []
    for message, seqs in spec:
        sequences = []
        for seq in seqs:
            if isinstance(seq, (list, tuple)):
                sequences.append(tuple(FreeCADGui.UserInput(int(v)) for v in seq))
            else:
                sequences.append(FreeCADGui.UserInput(int(seq)))
        hints.append(FreeCADGui.InputHint(str(message), *sequences))
    FreeCADGui.getMainWindow().showHint(*hints)


def name_of(wb):
    if wb is None:
        return None
    return getattr(wb, "_fcx_name", None) or type(wb).__name__


def _selection_value(value):
    """A SelectionObject crosses by value (docs/Sandbox.md 7.11, G3d):
    its objects as handles, its names and picked points as data; the
    guest's SubObjects resolve through Object.getSubObject on read."""
    if isinstance(value, (list, tuple)):
        return type(value)(_selection_value(v) for v in value)
    if type(value).__name__ == "SelectionObject":  # Gui.SelectionObject, not a module attribute
        return {"Object": value.Object, "Document": value.Document,
                "ObjectName": value.ObjectName, "DocumentName": value.DocumentName,
                "FullName": value.FullName, "TypeName": value.TypeName,
                "SubElementNames": list(value.SubElementNames),
                "PickedPoints": [(p.x, p.y, p.z) for p in value.PickedPoints]}
    return value


def selection_call(name, args):
    """FreeCADGui.Selection.<name>(*args) for the guest, the args
    decoded (a handle is the host object)."""
    return _selection_value(getattr(FreeCADGui.Selection, name)(*args))
)PY";

/// The wrapper namespace, built on first use (needs FreeCADGui.Workbench,
/// which exists once FreeCADGuiInit has run).  Borrowed; nullptr with a
/// Python error set.
PyObject* wrapperNamespace()
{
    static PyObject* ns = nullptr;
    if (!ns) {
        PyObject* dict = PyDict_New();
        if (!dict)
            return nullptr;
        PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
        PyObject* r = PyRun_String(WrapperSource, Py_file_input, dict, dict);
        if (!r) {
            Py_DECREF(dict);
            return nullptr;
        }
        Py_DECREF(r);
        ns = dict;
    }
    return ns;
}

/// FreeCADGui's function `name` called with `args` (a tuple, STOLEN);
/// the result (new reference) or nullptr with a Python error set.
PyObject* callGui(const char* name, PyObject* args)
{
    PyObject* mod = PyImport_ImportModule("FreeCADGui");
    PyObject* fn = mod ? PyObject_GetAttrString(mod, name) : nullptr;
    Py_XDECREF(mod);
    PyObject* res = fn ? PyObject_CallObject(fn, args) : nullptr;
    Py_XDECREF(fn);
    Py_DECREF(args);
    return res;
}

/// The names the guest registered commands under: what a second
/// registration (a guest reset re-running its InitGui) may replace.
std::map<std::string, uint64_t>& guestCommands()
{
    static std::map<std::string, uint64_t> names;
    return names;
}

Reply addCommand(HandleTable& table, const json& a)
{
    // [name, descriptor, group, activation]
    if (!a.is_array() || a.size() < 2 || !a[0].is_string() || !a[1].is_object())
        return replyErr("ProtocolError", "gui.cmd.add: [name, descriptor, group, activation]");
    const std::string name = a[0].get<std::string>();
    const std::string group = a.size() > 2 && a[2].is_string() ? a[2].get<std::string>() : "";
    const std::string activation = a.size() > 3 && a[3].is_string() ? a[3].get<std::string>() : "";
    PyObject* standin = decodeValue(table, a[1]);
    if (!standin)
        return replyPyError();
    if (!isGuestProxy(standin)) {
        Py_DECREF(standin);
        return replyErr("TypeError", "gui.cmd.add: the command object is not a guest proxy");
    }
    CommandManager& manager = Application::Instance->commandManager();
    if (Command* existing = manager.getCommandByName(name.c_str())) {
        // a command an earlier guest registered is replaced (the guest
        // was reset and registers again); a native one is not
        auto it = guestCommands().find(name);
        if (it == guestCommands().end()) {
            Py_DECREF(standin);
            return replyErr("KeyError", "command '" + name + "' already exists");
        }
        manager.removeCommand(existing);
        guestCommands().erase(it);
    }
    bool isGroup = false;
    auto hooks = a[1].find("hooks");
    if (hooks != a[1].end() && hooks->is_array())
        for (const auto& h : *hooks)
            if (h.is_string() && h.get_ref<const std::string&>() == "GetCommands")
                isGroup = true;
    Command* cmd = nullptr;
    try {
        // the constructor reads GetResources() -- a round trip into
        // the guest nested in this op
        if (isGroup)
            cmd = new PythonGroupCommand(name.c_str(), standin);
        else
            cmd = new PythonCommand(name.c_str(), standin,
                                    activation.empty() ? nullptr : activation.c_str());
    }
    catch (const Py::Exception&) {
        Py_DECREF(standin);
        return replyPyError();
    }
    catch (const Base::Exception& e) {
        Py_DECREF(standin);
        if (PyErr_Occurred())
            PyErr_Clear();
        return replyErr("TypeError", e.what());
    }
    const uint64_t pid = guestProxyId(standin);
    Py_DECREF(standin);  // the command holds its own reference
    if (!group.empty())
        cmd->setGroupName(group.c_str());
    manager.addCommand(cmd);
    guestCommands()[name] = pid;
    return replyOk(true);
}

/// Whether the registered workbench `name` is a guest wrapper: 1, 0,
/// or -1 when there is no such workbench (the KeyError cleared).
int guestWorkbench(PyObject* ns, const char* name, PyObject** out = nullptr)
{
    PyObject* wb = callGui("getWorkbench", Py_BuildValue("(s)", name));
    if (!wb) {
        PyErr_Clear();
        return -1;
    }
    PyObject* isGuest = PyObject_CallFunction(PyDict_GetItemString(ns, "is_guest"), "O", wb);
    const int guest = isGuest && PyObject_IsTrue(isGuest) ? 1 : 0;
    Py_XDECREF(isGuest);
    if (!isGuest)
        PyErr_Clear();
    if (out)
        *out = wb;
    else
        Py_DECREF(wb);
    return guest;
}

Reply addWorkbench(HandleTable& table, const json& a)
{
    // [name, descriptor, MenuText, ToolTip, Icon | null]
    if (!a.is_array() || a.size() < 4 || !a[0].is_string() || !a[1].is_object()
        || !a[2].is_string() || !a[3].is_string())
        return replyErr("ProtocolError", "gui.wb.add: [name, descriptor, MenuText, ToolTip, Icon]");
    PyObject* ns = wrapperNamespace();
    if (!ns)
        return replyPyError();
    const std::string name = a[0].get<std::string>();
    const int existing = guestWorkbench(ns, name.c_str());
    if (existing == 0)
        return replyErr("KeyError", "'" + name + "' already exists.");
    if (existing == 1) {
        // an earlier guest's wrapper: replaced
        PyObject* r = callGui("removeWorkbench", Py_BuildValue("(s)", name.c_str()));
        if (!r)
            return replyPyError();
        Py_DECREF(r);
    }
    PyObject* standin = decodeValue(table, a[1]);
    if (!standin)
        return replyPyError();
    if (!isGuestProxy(standin)) {
        Py_DECREF(standin);
        return replyErr("TypeError", "gui.wb.add: the workbench object is not a guest proxy");
    }
    PyObject* icon = a.size() > 4 && a[4].is_string()
        ? PyUnicode_FromString(a[4].get_ref<const std::string&>().c_str())
        : Py_NewRef(Py_None);
    PyObject* inst = PyObject_CallFunction(PyDict_GetItemString(ns, "make"), "OsssO", standin,
                                           name.c_str(), a[2].get_ref<const std::string&>().c_str(),
                                           a[3].get_ref<const std::string&>().c_str(), icon);
    Py_DECREF(icon);
    Py_DECREF(standin);
    if (!inst)
        return replyPyError();
    PyObject* r = callGui("addWorkbench", Py_BuildValue("(O)", inst));
    Py_DECREF(inst);
    if (!r)
        return replyPyError();
    Py_DECREF(r);
    return replyOk(true);
}

Reply removeWorkbench(const json& a)
{
    if (!a.is_string())
        return replyErr("ProtocolError", "gui.wb.remove: name");
    PyObject* ns = wrapperNamespace();
    if (!ns)
        return replyPyError();
    const std::string name = a.get<std::string>();
    if (guestWorkbench(ns, name.c_str()) != 1)
        return replyErr("KeyError", "workbench '" + name + "' is not registered from the sandbox");
    PyObject* r = callGui("removeWorkbench", Py_BuildValue("(s)", name.c_str()));
    if (!r)
        return replyPyError();
    Py_DECREF(r);
    return replyOk(true);
}

Reply workbenchCall(HandleTable& table, const json& a)
{
    // [name, method, args]: the Workbench base-class methods only
    static const char* const methods[] = {
        "appendToolbar",    "removeToolbar",     "listToolbars",      "getToolbarItems",
        "appendCommandbar", "removeCommandbar",  "listCommandbars",   "appendMenu",
        "removeMenu",       "listMenus",         "appendContextMenu", "removeContextMenu",
        "reloadActive",     "name"};
    if (!a.is_array() || a.size() < 3 || !a[0].is_string() || !a[1].is_string() || !a[2].is_array())
        return replyErr("ProtocolError", "gui.wb: [name, method, args]");
    const std::string& method = a[1].get_ref<const std::string&>();
    bool known = false;
    for (const char* m : methods)
        if (method == m)
            known = true;
    if (!known)
        return replyErr("AttributeError", "'" + method + "' is not a Workbench method");
    PyObject* ns = wrapperNamespace();
    if (!ns)
        return replyPyError();
    const std::string name = a[0].get<std::string>();
    PyObject* wb = nullptr;
    if (guestWorkbench(ns, name.c_str(), &wb) != 1) {
        Py_XDECREF(wb);
        return replyErr("KeyError", "workbench '" + name + "' is not registered from the sandbox");
    }
    PyObject* args = PyTuple_New(static_cast<Py_ssize_t>(a[2].size()));
    if (!args) {
        Py_DECREF(wb);
        return replyPyError();
    }
    Py_ssize_t i = 0;
    for (const auto& item : a[2]) {
        PyObject* value = decodeValue(table, item);
        if (!value) {
            Py_DECREF(args);
            Py_DECREF(wb);
            return replyPyError();
        }
        PyTuple_SET_ITEM(args, i++, value);
    }
    PyObject* fn = PyObject_GetAttrString(wb, method.c_str());
    Py_DECREF(wb);
    PyObject* res = fn ? PyObject_CallObject(fn, args) : nullptr;
    Py_XDECREF(fn);
    Py_DECREF(args);
    if (!res)
        return replyPyError();
    return replyResult(table, res);
}

Reply activeWorkbench(HandleTable& table)
{
    PyObject* ns = wrapperNamespace();
    if (!ns)
        return replyPyError();
    PyObject* wb = callGui("activeWorkbench", PyTuple_New(0));
    if (!wb)
        return replyPyError();
    PyObject* name = PyObject_CallFunction(PyDict_GetItemString(ns, "name_of"), "O", wb);
    Py_DECREF(wb);
    if (!name)
        return replyPyError();
    return replyResult(table, name);
}

Reply listWorkbenches(HandleTable& table)
{
    PyObject* dict = callGui("listWorkbenches", PyTuple_New(0));
    if (!dict)
        return replyPyError();
    PyObject* keys = PyDict_Check(dict) ? PyDict_Keys(dict) : nullptr;
    Py_DECREF(dict);
    if (!keys)
        return replyPyError();
    return replyResult(table, keys);
}

// ---- U3, the forms (docs/Sandbox.md 7.3): the guest's ipywidgets
// models cross as Jupyter comm traffic to freecad.widgets on the host,
// which keeps the models and renders them (Qt first).  The guest's comm
// manager registers once as a guest proxy; the host manager holds it
// and drives it back through its host_* hooks.

/// `freecad.widgets.manager()`; new reference, nullptr with an error.
PyObject* widgetManager()
{
    PyObject* mod = PyImport_ImportModule("freecad.widgets");
    if (!mod)
        return nullptr;
    PyObject* mgr = PyObject_CallMethod(mod, "manager", nullptr);
    Py_DECREF(mod);
    return mgr;
}

// ---- H0, the host widget layer (docs/Sandbox.md 7.12): a guest model of
// the `freecad.widgets` module is a C++ object in Fw::Store, rendered by
// the C++ Qt view; the plain ipywidgets models (Probe B) still go to the
// Python manager.  The split is by `_model_module` at comm_open, and by
// who holds the comm id after.

QVariant jsonToVariant(const json& j)
{
    switch (j.type()) {
        case json::value_t::null:
            return QVariant();
        case json::value_t::boolean:
            return QVariant(j.get<bool>());
        case json::value_t::number_integer:
        case json::value_t::number_unsigned: {
            long long v = j.get<long long>();
            if (v >= INT_MIN && v <= INT_MAX)
                return QVariant(static_cast<int>(v));
            return QVariant(static_cast<qlonglong>(v));
        }
        case json::value_t::number_float:
            return QVariant(j.get<double>());
        case json::value_t::string:
            return QVariant(QString::fromUtf8(j.get_ref<const std::string&>().c_str()));
        case json::value_t::array: {
            QVariantList out;
            bool allStrings = !j.empty();
            for (const auto& item : j) {
                if (!item.is_string())
                    allStrings = false;
                out.append(jsonToVariant(item));
            }
            if (allStrings) {
                QStringList sl;
                for (const auto& v : out)
                    sl.append(v.toString());
                return QVariant(sl);
            }
            return QVariant(out);
        }
        case json::value_t::object: {
            QVariantMap out;
            for (auto it = j.begin(); it != j.end(); ++it)
                out.insert(QString::fromUtf8(it.key().c_str()), jsonToVariant(it.value()));
            return QVariant(out);
        }
        default:
            return QVariant();
    }
}

/// The guest's comm manager stand-in, held for the C++ store's way back
/// (the Python manager holds its own reference for the plain models).
PyObject*& storeDispatcher()
{
    static PyObject* standin = nullptr;
    return standin;
}

/// Send `content` to the guest's comm `id` as `method` ("update" with
/// the q_ state, or "custom") through the stand-in's host_msg hook.
void storeSink(const QString& id, const QString& method, const QVariantMap& content)
{
    PyObject* standin = storeDispatcher();
    if (!standin) {
        Base::Console().Warning("SandboxGui: no guest comm manager to send to\n");
        return;
    }
    Base::PyGILStateLocker lock;
    PyObject* msg = PyDict_New();
    PyObject* m = PyUnicode_FromString(method.toUtf8().constData());
    PyDict_SetItemString(msg, "method", m);
    Py_DECREF(m);
    PyObject* body = Gui::Fw::variantToPy(content);
    if (method == QLatin1String("update")) {
        PyDict_SetItemString(msg, "state", body);
        PyObject* paths = PyList_New(0);
        PyDict_SetItemString(msg, "buffer_paths", paths);
        Py_DECREF(paths);
    }
    else {
        PyDict_SetItemString(msg, "content", body);
    }
    Py_DECREF(body);
    PyObject* buffers = PyList_New(0);
    PyObject* r = PyObject_CallMethod(standin, "host_msg", "sOO", id.toUtf8().constData(), msg,
                                      buffers);
    Py_DECREF(buffers);
    Py_DECREF(msg);
    if (!r) {
        // the guest is gone (a reset) or raised: the store's objects
        // are stale either way
        Base::PyException e;
        e.ReportException();
        return;
    }
    Py_DECREF(r);
}

void setStoreDispatcher(PyObject* standin)
{
    PyObject*& held = storeDispatcher();
    if (held && held != standin) {
        // a new guest: the old guest's objects are gone
        Gui::Fw::Store::instance().reset();
    }
    Py_XINCREF(standin);
    Py_XDECREF(held);
    held = standin;
    Gui::Fw::Store::instance().setSink(&storeSink);
}

Reply commManager(HandleTable& table, const json& a)
{
    if (!a.is_object())
        return replyErr("ProtocolError", "gui.comm.manager: descriptor");
    PyObject* standin = decodeValue(table, a);
    if (!standin)
        return replyPyError();
    if (!isGuestProxy(standin)) {
        Py_DECREF(standin);
        return replyErr("TypeError", "gui.comm.manager: the manager is not a guest proxy");
    }
    setStoreDispatcher(standin);
    PyObject* mgr = widgetManager();
    PyObject* r = mgr ? PyObject_CallMethod(mgr, "set_dispatcher", "O", standin) : nullptr;
    Py_XDECREF(mgr);
    Py_DECREF(standin);
    if (!r)
        return replyPyError();
    Py_DECREF(r);
    return replyOk(true);
}

/// A dialog root the guest showed (`form.show()`: `visible` set true)
/// becomes a window of its own, as it would natively (docs/Sandbox.md
/// 7.11, G3b).  Only a dialog or a form: a named child's `show()` is a
/// property of a widget in a layout.
void showTopLevelIfAsked(Gui::Fw::Widget* w)
{
    if (!w || w->parentWidget() || !qobject_cast<Gui::Fw::QDialog*>(w))
        return;
    if (!w->isTouched(QStringLiteral("visible")) || !w->property("visible").toBool())
        return;
    if (Gui::FwQt::View::of(w))
        return;
    try {
        if (QWidget* window = Gui::FwQt::realizeTopLevel(w)) {
            window->show();
            w->setProperties(QVariantMap {{QStringLiteral("width"), window->width()},
                                          {QStringLiteral("height"), window->height()}},
                             Gui::Fw::Source::Backend);
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("SandboxGui: cannot show the guest's dialog: %s\n", e.what());
    }
}

/// A comm message for the C++ store: true when it took it.
bool storeComm(const std::string& type, const QString& id, const json& data)
{
    Gui::Fw::Store& store = Gui::Fw::Store::instance();
    if (type == "comm_open") {
        if (!data.is_object())
            return false;
        auto st = data.find("state");
        if (st == data.end() || !st->is_object())
            return false;
        QVariantMap state = jsonToVariant(*st).toMap();
        if (!Gui::Fw::Store::owns(state))
            return false;
        store.commOpen(id, state);
        return true;
    }
    if (!store.object(id))
        return false;
    if (type == "comm_close") {
        store.commClose(id);
        return true;
    }
    if (!data.is_object())
        return true;
    const std::string method = data.value("method", "");
    if (method == "update") {
        auto st = data.find("state");
        if (st != data.end() && st->is_object()) {
            store.commUpdate(id, jsonToVariant(*st).toMap());
            showTopLevelIfAsked(store.object(id));
        }
    }
    else if (method == "custom") {
        auto c = data.find("content");
        if (c != data.end() && c->is_object())
            store.commCustom(id, jsonToVariant(*c).toMap());
    }
    else if (method != "echo_update") {
        Base::Console().Warning("SandboxGui: unknown comm method '%s'\n", method.c_str());
    }
    return true;
}

Reply commPublish(HandleTable& table, const json& a)
{
    // [msg_type, comm_id, target_name, data, metadata, buffers]
    if (!a.is_array() || a.size() != 6 || !a[0].is_string() || !a[1].is_string()
        || !a[2].is_string() || !a[5].is_array())
        return replyErr("ProtocolError",
                        "gui.comm: [msg_type, comm_id, target_name, data, metadata, buffers]");
    const std::string& type = a[0].get_ref<const std::string&>();
    const char* method = type == "comm_open" ? "comm_open"
        : type == "comm_msg"                 ? "comm_msg"
        : type == "comm_close"               ? "comm_close"
                                             : nullptr;
    if (!method)
        return replyErr("ProtocolError", "gui.comm: unknown message type '" + type + "'");
    if (a[2].get_ref<const std::string&>() == "jupyter.widget"
        && storeComm(type, QString::fromUtf8(a[1].get_ref<const std::string&>().c_str()), a[3]))
        return replyOk(true);
    PyObject* data = decodeValue(table, a[3]);
    PyObject* metadata = data ? decodeValue(table, a[4]) : nullptr;
    PyObject* buffers = metadata ? decodeValue(table, a[5]) : nullptr;
    PyObject* mgr = buffers ? widgetManager() : nullptr;
    PyObject* r = nullptr;
    if (mgr) {
        const char* commId = a[1].get_ref<const std::string&>().c_str();
        if (type == "comm_open")
            r = PyObject_CallMethod(mgr, method, "ssOOO", commId,
                                    a[2].get_ref<const std::string&>().c_str(), data, metadata,
                                    buffers);
        else if (type == "comm_msg")
            r = PyObject_CallMethod(mgr, method, "sOO", commId, data, buffers);
        else
            r = PyObject_CallMethod(mgr, method, "sO", commId, data);
    }
    Py_XDECREF(mgr);
    Py_XDECREF(buffers);
    Py_XDECREF(metadata);
    Py_XDECREF(data);
    if (!r)
        return replyPyError();
    return replyResult(table, r);
}

Reply widgetShow(HandleTable& table, const json& a)
{
    // [model_id, title | null, where]
    if (!a.is_array() || a.size() != 3 || !a[0].is_string()
        || !(a[1].is_string() || a[1].is_null()) || !a[2].is_string())
        return replyErr("ProtocolError", "gui.widget.show: [model_id, title, where]");
    PyObject* mgr = widgetManager();
    if (!mgr)
        return replyPyError();
    PyObject* title = a[1].is_string()
        ? PyUnicode_FromString(a[1].get_ref<const std::string&>().c_str())
        : Py_NewRef(Py_None);
    PyObject* r = PyObject_CallMethod(mgr, "show", "sOs",
                                      a[0].get_ref<const std::string&>().c_str(), title,
                                      a[2].get_ref<const std::string&>().c_str());
    Py_DECREF(title);
    Py_DECREF(mgr);
    if (!r)
        return replyPyError();
    // the toolkit object stays on the host; the guest learns it is shown
    Py_DECREF(r);
    (void)table;
    return replyOk(true);
}

Reply widgetHide(HandleTable& table, const json& a)
{
    if (!a.is_string())
        return replyErr("ProtocolError", "gui.widget.hide: model_id");
    PyObject* mgr = widgetManager();
    if (!mgr)
        return replyPyError();
    PyObject* r = PyObject_CallMethod(mgr, "hide", "s", a.get_ref<const std::string&>().c_str());
    Py_DECREF(mgr);
    if (!r)
        return replyPyError();
    return replyResult(table, r);
}

// ---- G3a, the forms (docs/Sandbox.md 7.11): a .ui file's text for
// the guest's loader, and the task panel.  The panel object on the
// host is the manager's (freecad.widgets.show_panel): its form is the
// rendered widgets and its hooks forward to the guest's stand-in.

/// A .ui file the host's own uic would load: a Qt resource (`:/ui/...`)
/// or a file under one of the module roots, and nothing else -- data
/// for the guest's parser, never code.
Reply uiRead(const json& a)
{
    if (!a.is_string())
        return replyErr("ProtocolError", "gui.ui.read: path");
    const std::string& path = a.get_ref<const std::string&>();
    QString qpath = QString::fromStdString(path);
    if (!qpath.startsWith(QLatin1String(":/")) && !qpath.startsWith(QLatin1String(":ui"))) {
        QString canon = QFileInfo(qpath).canonicalFilePath();
        bool under = false;
        for (const auto& root : Base::Type::getModuleRoots()) {
            QString r = QFileInfo(QString::fromStdString(root)).canonicalFilePath();
            if (!r.isEmpty() && canon.startsWith(r + QLatin1Char('/')))
                under = true;
        }
        if (!under)
            return replyErr("PermissionError",
                            "gui.ui.read: '" + path + "' is not a resource or under a module root");
    }
    if (!qpath.endsWith(QLatin1String(".ui")))
        return replyErr("ValueError", "gui.ui.read: '" + path + "' is not a .ui file");
    QFile file(qpath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return replyErr("FileNotFoundError", "gui.ui.read: cannot read '" + path + "'");
    QByteArray text = file.readAll();
    return replyOk(json(std::string(text.constData(), static_cast<size_t>(text.size()))));
}

/// The guest panel's hooks: each a method of the stand-in, called
/// through the bridge (a round trip into the guest nested in the Qt
/// event that fired it).  `hooks` is the descriptor's list, so no
/// hasattr crosses.
class GuestPanelHooks : public Gui::FwQt::PanelHooks
{
public:
    GuestPanelHooks(PyObject* standin, std::set<std::string> hooks)
        : standin(standin)
        , hooks(std::move(hooks))
    {
        Py_INCREF(standin);
    }
    ~GuestPanelHooks() override
    {
        Base::PyGILStateLocker lock;
        Py_DECREF(standin);
    }
    bool has(const char* hook) const override
    {
        return hooks.count(hook) > 0;
    }
    QVariant call(const char* hook, const QVariantList& args) override
    {
        Base::PyGILStateLocker lock;
        PyObject* tuple = PyTuple_New(args.size());
        for (int i = 0; i < args.size(); ++i)
            PyTuple_SET_ITEM(tuple, i, Gui::Fw::variantToPy(args.at(i)));
        PyObject* fn = PyObject_GetAttrString(standin, hook);
        PyObject* r = fn ? PyObject_CallObject(fn, tuple) : nullptr;
        Py_XDECREF(fn);
        Py_DECREF(tuple);
        if (!r) {
            Base::PyException e;
            e.ReportException();
            return QVariant();
        }
        // None answers as TaskDialogPython reads it: False
        QVariant v = r == Py_None ? QVariant(false) : Gui::Fw::pyToVariant(r);
        Py_DECREF(r);
        return v;
    }

private:
    PyObject* standin;
    std::set<std::string> hooks;
};

Reply controlShow(HandleTable& table, const json& a)
{
    // [panel descriptor, [form model ids]]
    if (!a.is_array() || a.size() != 2 || !a[0].is_object() || !a[1].is_array())
        return replyErr("ProtocolError", "gui.control.show: [panel, form ids]");
    PyObject* standin = decodeValue(table, a[0]);
    if (!standin)
        return replyPyError();
    if (!isGuestProxy(standin)) {
        Py_DECREF(standin);
        return replyErr("TypeError", "gui.control.show: the panel is not a guest proxy");
    }
    std::set<std::string> hookSet;
    for (const auto& h : a[0].value("hooks", json::array()))
        if (h.is_string())
            hookSet.insert(h.get<std::string>());

    // every form in the C++ store: the C++ panel; else (plain ipywidgets
    // roots) the Python manager's
    Gui::Fw::Store& store = Gui::Fw::Store::instance();
    QList<Gui::Fw::Widget*> forms;
    bool allOurs = !a[1].empty();
    for (const auto& id : a[1]) {
        Gui::Fw::Widget* w = id.is_string()
            ? store.object(QString::fromUtf8(id.get_ref<const std::string&>().c_str()))
            : nullptr;
        if (!w) {
            allOurs = false;
            break;
        }
        forms.append(w);
    }
    if (allOurs) {
        if (Gui::Control().activeDialog()) {
            Py_DECREF(standin);
            return replyErr("RuntimeError", "Control.showDialog: a task dialog is already active");
        }
        Gui::FwQt::PanelDialog* dlg = nullptr;
        try {
            dlg = new Gui::FwQt::PanelDialog(std::make_unique<GuestPanelHooks>(standin, hookSet),
                                             forms);
        }
        catch (const Base::Exception& e) {
            Py_DECREF(standin);
            return replyErr("RuntimeError", e.what());
        }
        Py_DECREF(standin);
        Gui::Control().showDialog(dlg);
        return replyOk(true);
    }

    PyObject* ids = decodeValue(table, a[1]);
    PyObject* hooks = PyList_New(0);
    for (const auto& h : hookSet)
        PyList_Append(hooks, PyUnicode_FromString(h.c_str()));
    PyObject* mgr = ids ? widgetManager() : nullptr;
    PyObject* r = mgr ? PyObject_CallMethod(mgr, "show_panel", "OOO", standin, ids, hooks)
                      : nullptr;
    Py_XDECREF(mgr);
    Py_DECREF(hooks);
    Py_XDECREF(ids);
    Py_DECREF(standin);
    if (!r)
        return replyPyError();
    Py_DECREF(r);
    return replyOk(true);
}

/// `gui.dialog.exec [form id]`: the guest's `QDialog.exec_()`.  The
/// root is realized as a window and run modally in a nested event
/// loop inside this one op; what the user does in it reaches the
/// guest's slots nested (the comm messages call into the guest), and
/// the dialog code comes back as the op's result.
Reply dialogExec(HandleTable& table, const json& a)
{
    (void)table;
    if (!a.is_string())
        return replyErr("ProtocolError", "gui.dialog.exec: form id");
    Gui::Fw::Widget* w = Gui::Fw::Store::instance().object(
        QString::fromUtf8(a.get_ref<const std::string&>().c_str()));
    if (!w)
        return replyErr("KeyError", "gui.dialog.exec: no such form");
    if (w->parentWidget())
        return replyErr("TypeError", "gui.dialog.exec: the form is not a top-level widget");
    try {
        int r = Gui::FwQt::execDialog(w);
        return replyOk(r);
    }
    catch (const Base::Exception& e) {
        return replyErr("TypeError", e.what());
    }
}

/// `gui.doc [document name, member, args | null]`: the guest's
/// `FreeCADGui.getDocument(name)` / `ActiveDocument` over the host's
/// Gui.Document of an App document within the principal's reach (S1,
/// docs/Sandbox.md 7.13; App::ExpressionSandbox::documentReachable) --
/// `setEdit`, `resetEdit`, `getInEdit`, `activeObject` as calls with
/// the decoded args, `Modified` as a read; the result by value or as
/// a handle (a view provider resolves through the view family).
Reply guiDocumentCall(HandleTable& table, const json& a)
{
    if (!a.is_array() || a.size() != 3 || !a[0].is_string() || !a[1].is_string())
        return replyErr("ProtocolError", "gui.doc: [document, member, args]");
    const std::string& name = a[0].get_ref<const std::string&>();
    const std::string& member = a[1].get_ref<const std::string&>();
    static const std::set<std::string> calls = {"setEdit", "resetEdit", "getInEdit",
                                               "activeObject"};
    static const std::set<std::string> reads = {"Modified"};
    if (!calls.count(member) && !reads.count(member))
        return replyErr("AttributeError",
                        "Gui.Document." + member + " is not in the sandbox's subset");
    App::Document* appDoc = App::GetApplication().getDocument(name.c_str());
    if (!appDoc)
        return replyErr("NameError", "Unknown document '" + name + "'");
    if (!App::ExpressionSandbox::documentReachable(table, appDoc))
        return replyErr("PermissionError",
                        "gui.doc: document '" + name + "' is out of this principal's reach");
    Gui::Document* doc = Application::Instance->getDocument(appDoc);
    if (!doc)
        return replyErr("RuntimeError", "document '" + name + "' has no GUI document");
    PyObject* py = doc->getPyObject();
    if (reads.count(member)) {
        PyObject* v = PyObject_GetAttrString(py, member.c_str());
        Py_DECREF(py);
        return v ? replyResult(table, v) : replyPyError();
    }
    PyObject* args = a[2].is_null() ? PyTuple_New(0) : decodeValue(table, a[2]);
    if (args && !PyTuple_Check(args)) {
        PyObject* t = PySequence_Tuple(args);
        Py_DECREF(args);
        args = t;
    }
    if (!args) {
        Py_DECREF(py);
        return replyPyError();
    }
    PyObject* fn = PyObject_GetAttrString(py, member.c_str());
    Py_DECREF(py);
    if (!fn) {
        Py_DECREF(args);
        return replyPyError();
    }
    PyObject* r = PyObject_CallObject(fn, args);
    Py_DECREF(fn);
    Py_DECREF(args);
    return r ? replyResult(table, r) : replyPyError();
}

/// `gui.control.close` / `.active` / `.clear_watcher` / `.query name`.
Reply controlCall(HandleTable& table, const std::string& op, const json& a)
{
    (void)table;
    if (op == "gui.control.close") {
        if (auto dlg = dynamic_cast<Gui::FwQt::PanelDialog*>(Gui::Control().activeDialog())) {
            // the dialog deletes its forms; the views detach now, not
            // when the deferred delete lands
            dlg->detachViews();
        }
        else {
            // a Python-managed panel (plain ipywidgets): its views too
            PyObject* mgr = widgetManager();
            PyObject* r = mgr ? PyObject_CallMethod(mgr, "control", "sO", "close", Py_None)
                              : nullptr;
            Py_XDECREF(mgr);
            if (!r)
                return replyPyError();
            Py_DECREF(r);
            return replyOk(true);
        }
        Gui::Control().closeDialog();
        return replyOk(true);
    }
    if (op == "gui.control.active")
        return replyOk(Gui::Control().activeDialog() != nullptr);
    if (op == "gui.control.clear_watcher") {
        if (Gui::TaskView::TaskView* view = Gui::Control().taskWatcherPanel())
            view->clearTaskWatcher();
        return replyOk(true);
    }
    // gui.control.query
    if (!a.is_string())
        return replyErr("ProtocolError", "gui.control.query: name");
    const std::string& q = a.get_ref<const std::string&>();
    if (q == "isAllowedAlterDocument")
        return replyOk(Gui::Control().isAllowedAlterDocument());
    if (q == "isAllowedAlterView")
        return replyOk(Gui::Control().isAllowedAlterView());
    if (q == "isAllowedAlterSelection")
        return replyOk(Gui::Control().isAllowedAlterSelection());
    if (q == "activeDocument")
        return replyOk(Application::Instance->activeDocument() != nullptr);
    if (q == "resetEdit") {
        if (Gui::Document* doc = Application::Instance->activeDocument())
            doc->resetEdit();
        return replyOk(true);
    }
    return replyErr("ValueError", "Control: unknown query '" + q + "'");
}

// ---- G3c (docs/Sandbox.md 7.11): the main window as a shim, a
// command run by name, a menu's exec, the task watchers.

/// Call one hook of a guest stand-in (no arguments), reporting a raise.
void callHook(PyObject* standin, const char* hook)
{
    Base::PyGILStateLocker lock;
    PyObject* r = PyObject_CallMethod(standin, hook, nullptr);
    if (!r) {
        Base::PyException e;
        e.ReportException();
        return;
    }
    Py_DECREF(r);
}

PyObject*& mainWindowWatcher()
{
    static PyObject* standin = nullptr;
    return standin;
}

/// `gui.mainwindow [method, args...]`: `getMainWindow()` in the guest.
/// `addToolBar id` realizes a tool bar model under the main window
/// (the real bar dies with the model); `watch descriptor` connects the
/// guest's `mainWindowClosed` slot (a stand-in hook); `showMessage`,
/// `windowTitle`, `cursorPos` are data.
Reply mainWindowCall(HandleTable& table, const json& a)
{
    if (!a.is_array() || a.empty() || !a[0].is_string())
        return replyErr("ProtocolError", "gui.mainwindow: [method, ...]");
    const std::string& m = a[0].get_ref<const std::string&>();
    MainWindow* mw = getMainWindow();
    if (!mw)
        return replyErr("RuntimeError", "no main window");
    if (m == "addToolBar" || m == "removeToolBar") {
        if (a.size() != 2 || !a[1].is_string())
            return replyErr("ProtocolError", "gui.mainwindow: [" + m + ", model id]");
        Gui::Fw::Widget* w = Gui::Fw::Store::instance().object(
            QString::fromUtf8(a[1].get_ref<const std::string&>().c_str()));
        if (!w)
            return replyErr("KeyError", "gui.mainwindow: no such widget");
        if (!qobject_cast<Gui::Fw::QToolBar*>(w))
            return replyErr("TypeError", "gui.mainwindow." + m + ": not a tool bar");
        QWidget* qw = Gui::FwQt::widgetOf(w);
        if (m == "removeToolBar") {
            if (auto tb = qobject_cast<QToolBar*>(qw))
                mw->removeToolBar(tb);
            return replyOk(true);
        }
        if (!qw) {
            qw = Gui::FwQt::realize(w, mw);
            QObject::connect(w, &QObject::destroyed, qw, &QObject::deleteLater);
        }
        auto tb = qobject_cast<QToolBar*>(qw);
        if (!tb)
            return replyErr("RuntimeError", "gui.mainwindow.addToolBar: no tool bar realized");
        mw->addToolBar(tb);
        return replyOk(true);
    }
    if (m == "watch") {
        if (a.size() != 2 || !a[1].is_object())
            return replyErr("ProtocolError", "gui.mainwindow: [watch, descriptor]");
        PyObject* standin = decodeValue(table, a[1]);
        if (!standin)
            return replyPyError();
        if (!isGuestProxy(standin)) {
            Py_DECREF(standin);
            return replyErr("TypeError", "gui.mainwindow.watch: not a guest proxy");
        }
        PyObject*& held = mainWindowWatcher();
        bool first = held == nullptr;
        Py_XDECREF(held);
        held = standin;
        if (first) {
            QObject::connect(mw, &MainWindow::mainWindowClosed, mw, []() {
                if (PyObject* s = mainWindowWatcher())
                    callHook(s, "mainWindowClosed");
            });
        }
        return replyOk(true);
    }
    if (m == "showHint" || m == "hideHint") {
        PyObject* ns = wrapperNamespace();
        if (!ns)
            return replyPyError();
        PyObject* r = nullptr;
        if (m == "hideHint") {
            mw->hideHints();
            return replyOk(true);
        }
        if (a.size() != 2 || !a[1].is_array())
            return replyErr("ProtocolError", "gui.mainwindow: [showHint, hints]");
        PyObject* spec = decodeValue(table, a[1]);
        if (!spec)
            return replyPyError();
        r = PyObject_CallFunction(PyDict_GetItemString(ns, "show_hints"), "O", spec);
        Py_DECREF(spec);
        if (!r)
            return replyPyError();
        Py_DECREF(r);
        return replyOk(true);
    }
    if (m == "showMessage") {
        if (a.size() < 2 || !a[1].is_string())
            return replyErr("ProtocolError", "gui.mainwindow: [showMessage, text, ms]");
        int ms = a.size() > 2 && a[2].is_number() ? a[2].get<int>() : 0;
        mw->showMessage(QString::fromUtf8(a[1].get_ref<const std::string&>().c_str()), ms);
        return replyOk(true);
    }
    if (m == "windowTitle")
        return replyOk(json(mw->windowTitle().toStdString()));
    if (m == "cursorPos") {
        QPoint p = QCursor::pos();
        return replyOk(json::array({p.x(), p.y()}));
    }
    return replyErr("ValueError", "gui.mainwindow: unknown method '" + m + "'");
}

/// `gui.cmd.run [name, index]`: `FreeCADGui.runCommand`.
Reply runCommandByName(const json& a)
{
    if (!a.is_array() || a.empty() || !a[0].is_string())
        return replyErr("ProtocolError", "gui.cmd.run: [name, index]");
    int idx = a.size() > 1 && a[1].is_number() ? a[1].get<int>() : 0;
    Application::Instance->commandManager().runCommandByName(
        a[0].get_ref<const std::string&>().c_str(), idx);
    return replyOk(true);
}

/// `gui.menu.exec [id]`: the guest's `QMenu.exec_()`, a nested loop at
/// the cursor; the chosen action's model id, or null.
Reply menuExec(const json& a)
{
    if (!a.is_string())
        return replyErr("ProtocolError", "gui.menu.exec: menu id");
    Gui::Fw::Store& store = Gui::Fw::Store::instance();
    Gui::Fw::Widget* w = store.object(QString::fromUtf8(a.get_ref<const std::string&>().c_str()));
    if (!w || !qobject_cast<Gui::Fw::QMenu*>(w))
        return replyErr("KeyError", "gui.menu.exec: no such menu");
    QWidget* qw = Gui::FwQt::widgetOf(w);
    if (!qw) {
        qw = Gui::FwQt::realize(w, getMainWindow());
        QObject::connect(w, &QObject::destroyed, qw, &QObject::deleteLater);
    }
    auto menu = qobject_cast<QMenu*>(qw);
    if (!menu)
        return replyErr("RuntimeError", "gui.menu.exec: no menu realized");
    QAction* chosen = menu->exec(QCursor::pos());
    Gui::Fw::Widget* m = Gui::FwQt::modelOfAction(chosen);
    QString id = m ? store.idOf(m) : QString();
    if (id.isEmpty())
        return replyOk(nullptr);
    return replyOk(json(id.toStdString()));
}

/// `gui.control.add_watcher [descriptors]`: `Control.addTaskWatcher`.
/// Each watcher is a guest stand-in: `TaskWatcherPython` reads its
/// `title`, `icon`, `commands`, `filter` through the proxy and calls
/// `shouldShow` on every selection change (one hop each).
Reply addTaskWatchers(HandleTable& table, const json& a)
{
    if (!a.is_array())
        return replyErr("ProtocolError", "gui.control.add_watcher: [descriptors]");
    std::vector<Gui::TaskView::TaskWatcher*> watchers;
    for (const auto& d : a) {
        if (!d.is_object())
            return replyErr("ProtocolError", "gui.control.add_watcher: descriptor");
        PyObject* standin = decodeValue(table, d);
        if (!standin)
            return replyPyError();
        if (!isGuestProxy(standin)) {
            Py_DECREF(standin);
            return replyErr("TypeError", "gui.control.add_watcher: not a guest proxy");
        }
        try {
            Base::PyGILStateLocker lock;
            watchers.push_back(new Gui::TaskView::TaskWatcherPython(Py::Object(standin, true)));
        }
        catch (Py::Exception&) {
            return replyPyError();
        }
    }
    if (Gui::TaskView::TaskView* view = Gui::Control().taskWatcherPanel())
        view->addTaskWatcher(watchers);
    return replyOk(true);
}

// ---- G3d (docs/Sandbox.md 7.11): the selection and the U2 dialogs.

/// `gui.sel.call [name, args]`: `FreeCADGui.Selection.<name>(*args)`
/// on the host for the guest's `Selection` -- the methods Draft and
/// BIM call, the arguments decoded (an object handle is the host
/// object, a name a string), the result by value with the objects as
/// handles (a SelectionObject as a dict, see selection_call).
Reply selectionCall(HandleTable& table, const json& a)
{
    if (!a.is_array() || a.size() != 2 || !a[0].is_string() || !a[1].is_array())
        return replyErr("ProtocolError", "gui.sel.call: [name, args]");
    const std::string& name = a[0].get_ref<const std::string&>();
    static const std::set<std::string> subset = {
        "getSelection",      "getSelectionEx",   "getCompleteSelection", "addSelection",
        "removeSelection",   "clearSelection",   "hasSelection",         "isSelected",
        "getPreselection",   "setPreselection",  "removePreselection",   "countObjectsOfType",
        "getSelectionObject", "hasSubSelection", "updateSelection",      "getSelectedObjects",
        "getSelectionFromStack", "getPickedList", "enablePickedList",    "setVisible",
    };
    if (!subset.count(name))
        return replyErr("AttributeError", "Selection." + name + " is not in the sandbox's subset");
    PyObject* args = PyTuple_New(Py_ssize_t(a[1].size()));
    if (!args)
        return replyPyError();
    Py_ssize_t i = 0;
    for (const auto& v : a[1]) {
        PyObject* pv = decodeValue(table, v);
        if (!pv) {
            Py_DECREF(args);
            return replyPyError();
        }
        PyTuple_SET_ITEM(args, i++, pv);
    }
    PyObject* ns = wrapperNamespace();
    if (!ns) {
        Py_DECREF(args);
        return replyPyError();
    }
    PyObject* r = PyObject_CallFunction(PyDict_GetItemString(ns, "selection_call"), "sO",
                                        name.c_str(), args);
    Py_DECREF(args);
    return r ? replyResult(table, r) : replyPyError();
}

/// The guest's selection observers: proxy id -> the stand-in the
/// host's SelectionObserverPython holds (one owned reference each).
std::map<uint64_t, PyObject*>& selectionObservers()
{
    static std::map<uint64_t, PyObject*> observers;
    return observers;
}

/// Drop every guest observer: a guest reset left their stand-ins
/// pointing at a guest that is gone.
void dropSelectionObservers()
{
    auto& observers = selectionObservers();
    if (observers.empty())
        return;
    Base::PyGILStateLocker lock;
    for (auto& kv : observers) {
        Gui::SelectionObserverPython::removeObserver(Py::Object(kv.second, false));
        Py_DECREF(kv.second);
    }
    observers.clear();
}

/// `gui.sel.observer ["add", descriptor, resolve] | ["remove",
/// descriptor]`: `Selection.addObserver` / `removeObserver`.  The
/// observer is a guest stand-in with the observer hook list; the
/// host's own SelectionObserverPython drives it exactly as it drives a
/// native Python observer (one hop per hook it defines, the arguments
/// by value: document and object NAMES, the sub-element, the point).
Reply selectionObserver(HandleTable& table, const json& a)
{
    if (!a.is_array() || a.size() < 2 || !a[0].is_string() || !a[1].is_object()
        || !a[1].contains("id") || !a[1]["id"].is_number_unsigned())
        return replyErr("ProtocolError", "gui.sel.observer: [add|remove, descriptor, resolve]");
    const std::string& what = a[0].get_ref<const std::string&>();
    const uint64_t pid = a[1]["id"].get<uint64_t>();
    auto& observers = selectionObservers();
    if (what == "remove") {
        auto it = observers.find(pid);
        if (it == observers.end())
            return replyOk(false);
        Gui::SelectionObserverPython::removeObserver(Py::Object(it->second, false));
        Py_DECREF(it->second);
        observers.erase(it);
        return replyOk(true);
    }
    if (what != "add")
        return replyErr("ProtocolError", "gui.sel.observer: add or remove");
    if (observers.count(pid))
        return replyOk(false);
    int resolve = a.size() > 2 && a[2].is_number_integer() ? a[2].get<int>() : 1;
    if (resolve < 0 || resolve > 3)
        return replyErr("ValueError", "gui.sel.observer: resolve mode 0..3");
    PyObject* standin = decodeValue(table, a[1]);
    if (!standin)
        return replyPyError();
    if (!isGuestProxy(standin)) {
        Py_DECREF(standin);
        return replyErr("TypeError", "gui.sel.observer: not a guest proxy");
    }
    try {
        Gui::SelectionObserverPython::addObserver(Py::Object(standin, false),
                                                  Gui::ResolveMode(resolve));
    }
    catch (Py::Exception&) {
        Py_DECREF(standin);
        return replyPyError();
    }
    observers[pid] = standin;  // the reference decodeValue made
    return replyOk(true);
}

QString jstr(const json& a, const char* key, const char* fallback = "")
{
    auto it = a.find(key);
    if (it == a.end() || !it->is_string())
        return QString::fromUtf8(fallback);
    return QString::fromUtf8(it->get_ref<const std::string&>().c_str());
}

template<class T>
T jnum(const json& a, const char* key, T fallback)
{
    auto it = a.find(key);
    if (it == a.end() || !it->is_number())
        return fallback;
    return it->get<T>();
}

/// `gui.dialog.message {icon, title, text, informative, detailed,
/// buttons, default}`: the QMessageBox statics (question, information,
/// warning, critical) and an instance's exec, one nested loop under
/// the main window; the button pressed, Qt's StandardButton value.
Reply dialogMessage(const json& a)
{
    if (!a.is_object())
        return replyErr("ProtocolError", "gui.dialog.message: {icon, title, text, buttons, default}");
    QMessageBox box(getMainWindow());
    box.setIcon(QMessageBox::Icon(std::clamp(jnum<int>(a, "icon", 0), 0, 4)));
    box.setWindowTitle(jstr(a, "title"));
    box.setText(jstr(a, "text"));
    box.setInformativeText(jstr(a, "informative"));
    box.setDetailedText(jstr(a, "detailed"));
    const int buttons = jnum<int>(a, "buttons", int(QMessageBox::Ok));
    box.setStandardButtons(QMessageBox::StandardButtons(buttons ? buttons : int(QMessageBox::Ok)));
    if (int def = jnum<int>(a, "default", 0))
        box.setDefaultButton(QMessageBox::StandardButton(def));
    return replyOk(int(box.exec()));
}

/// `gui.dialog.input {kind, title, label, value, items, current,
/// editable, echo, min, max, step, decimals}`: the QInputDialog
/// statics; `[value, ok]`.
Reply dialogInput(const json& a)
{
    if (!a.is_object() || !a.contains("kind") || !a["kind"].is_string())
        return replyErr("ProtocolError", "gui.dialog.input: {kind, title, label, ...}");
    const std::string& kind = a["kind"].get_ref<const std::string&>();
    QWidget* parent = getMainWindow();
    const QString title = jstr(a, "title");
    const QString label = jstr(a, "label");
    bool ok = false;
    if (kind == "text") {
        int echo = std::clamp(jnum<int>(a, "echo", 0), 0, 3);
        QString v = QInputDialog::getText(parent, title, label, QLineEdit::EchoMode(echo),
                                          jstr(a, "value"), &ok);
        return replyOk(json::array({v.toStdString(), ok}));
    }
    if (kind == "multiline") {
        QString v = QInputDialog::getMultiLineText(parent, title, label, jstr(a, "value"), &ok);
        return replyOk(json::array({v.toStdString(), ok}));
    }
    if (kind == "int") {
        int v = QInputDialog::getInt(parent, title, label, jnum<int>(a, "value", 0),
                                     jnum<int>(a, "min", -2147483647),
                                     jnum<int>(a, "max", 2147483647), jnum<int>(a, "step", 1),
                                     &ok);
        return replyOk(json::array({v, ok}));
    }
    if (kind == "double") {
        double v = QInputDialog::getDouble(parent, title, label, jnum<double>(a, "value", 0.0),
                                           jnum<double>(a, "min", -2147483647.0),
                                           jnum<double>(a, "max", 2147483647.0),
                                           jnum<int>(a, "decimals", 1), &ok);
        return replyOk(json::array({v, ok}));
    }
    if (kind == "item") {
        QStringList items;
        auto it = a.find("items");
        if (it != a.end() && it->is_array()) {
            for (const auto& s : *it)
                items << (s.is_string() ? QString::fromUtf8(s.get_ref<const std::string&>().c_str())
                                        : QString::fromStdString(s.dump()));
        }
        QString v = QInputDialog::getItem(parent, title, label, items, jnum<int>(a, "current", 0),
                                          a.value("editable", true), &ok);
        return replyOk(json::array({v.toStdString(), ok}));
    }
    return replyErr("ValueError", "gui.dialog.input: unknown kind '" + kind + "'");
}

/// `gui.dialog.file {mode, caption, dir, filter, selected, options}`:
/// the QFileDialog statics -- open, opens, save, dir; `[path(s),
/// selected filter]`.  The path is DATA to the guest: what it may
/// then read or write there is the file-system grant's business, not
/// the dialog's (docs/Sandbox.md 7.1, U2) -- except that a path the
/// user chose here is BLESSED for this guest (S1, 7.13:
/// App::ExpressionSandbox::blessPath): `Document.saveAs` accepts
/// exactly those, the seed of the fs slice.
Reply dialogFile(const json& a)
{
    if (!a.is_object() || !a.contains("mode") || !a["mode"].is_string())
        return replyErr("ProtocolError", "gui.dialog.file: {mode, caption, dir, filter}");
    const std::string& mode = a["mode"].get_ref<const std::string&>();
    QWidget* parent = getMainWindow();
    const QString caption = jstr(a, "caption");
    const QString dir = jstr(a, "dir");
    const QString filter = jstr(a, "filter");
    QString selected = jstr(a, "selected");
    QFileDialog::Options options(jnum<int>(a, "options", 0));
    if (mode == "open") {
        QString v = QFileDialog::getOpenFileName(parent, caption, dir, filter, &selected, options);
        App::ExpressionSandbox::blessPath(v.toStdString());
        return replyOk(json::array({v.toStdString(), selected.toStdString()}));
    }
    if (mode == "opens") {
        QStringList v = QFileDialog::getOpenFileNames(parent, caption, dir, filter, &selected,
                                                      options);
        json paths = json::array();
        for (const QString& p : v) {
            App::ExpressionSandbox::blessPath(p.toStdString());
            paths.push_back(p.toStdString());
        }
        return replyOk(json::array({paths, selected.toStdString()}));
    }
    if (mode == "save") {
        QString v = QFileDialog::getSaveFileName(parent, caption, dir, filter, &selected, options);
        App::ExpressionSandbox::blessPath(v.toStdString());
        return replyOk(json::array({v.toStdString(), selected.toStdString()}));
    }
    if (mode == "dir") {
        QString v = QFileDialog::getExistingDirectory(parent, caption, dir,
                                                      options ? options : QFileDialog::ShowDirsOnly);
        App::ExpressionSandbox::blessPath(v.toStdString());
        return replyOk(json::array({v.toStdString(), ""}));
    }
    return replyErr("ValueError", "gui.dialog.file: unknown mode '" + mode + "'");
}

/// `gui.dialog.color {initial: [r, g, b, a], title, options}`:
/// `QColorDialog.getColor`; `[r, g, b, a]` as floats, or null when
/// canceled.
Reply dialogColor(const json& a)
{
    if (!a.is_object())
        return replyErr("ProtocolError", "gui.dialog.color: {initial, title, options}");
    QColor initial = Qt::white;
    auto it = a.find("initial");
    if (it != a.end() && it->is_array() && it->size() >= 3) {
        const json& c = *it;
        initial = QColor::fromRgbF(c[0].get<double>(), c[1].get<double>(), c[2].get<double>(),
                                   c.size() > 3 ? c[3].get<double>() : 1.0);
    }
    QColorDialog::ColorDialogOptions options(jnum<int>(a, "options", 0));
    QColor v = QColorDialog::getColor(initial, getMainWindow(), jstr(a, "title"), options);
    if (!v.isValid())
        return replyOk(nullptr);
    return replyOk(json::array({v.redF(), v.greenF(), v.blueF(), v.alphaF()}));
}

Reply guiOp(HandleTable& table, const Reply& requestCbor)
{
    const json req = json::from_cbor(requestCbor);
    const std::string op = req.value("op", "");
    if (!Application::Instance)
        return replyErr("RuntimeError", "no GUI application");
    // data any principal may read: the UserInput enum (Qt key codes),
    // which Draft's tool modules read at import -- and a document guest
    // imports them under `if App.GuiUp:` now (docs/Sandbox.md 7.9, G2b)
    if (op == "gui.user_input") {
        PyObject* ns = wrapperNamespace();
        if (!ns)
            return replyPyError();
        PyObject* v = PyObject_CallFunction(PyDict_GetItemString(ns, "user_input"), nullptr);
        return v ? replyResult(table, v) : replyPyError();
    }
    // the catalog's `gui`: DENY for a document (not promptable), ALLOW
    // for the session and addons
    App::ExpressionSecurity::checkPermission(App::ExpressionSecurity::Permission::Gui);
    auto a = req.find("a");
    const json none;
    const json& arg = a != req.end() ? *a : none;
    if (op == "gui.cmd.add")
        return addCommand(table, arg);
    if (op == "gui.cmd.run")
        return runCommandByName(arg);
    if (op == "gui.mainwindow")
        return mainWindowCall(table, arg);
    if (op == "gui.menu.exec")
        return menuExec(arg);
    if (op == "gui.control.add_watcher")
        return addTaskWatchers(table, arg);
    if (op == "gui.cmd.list") {
        PyObject* names = callGui("listCommands", PyTuple_New(0));
        return names ? replyResult(table, names) : replyPyError();
    }
    if (op == "gui.wb.add")
        return addWorkbench(table, arg);
    if (op == "gui.wb.remove")
        return removeWorkbench(arg);
    if (op == "gui.wb")
        return workbenchCall(table, arg);
    if (op == "gui.wb.active")
        return activeWorkbench(table);
    if (op == "gui.wb.list")
        return listWorkbenches(table);
    if (op == "gui.sodb_version") {
        PyObject* v = callGui("getSoDBVersion", PyTuple_New(0));
        return v ? replyResult(table, v) : replyPyError();
    }
    if (op == "gui.icon_path" || op == "gui.lang_path") {
        if (!arg.is_string())
            return replyErr("ProtocolError", op + ": path");
        PyObject* r = callGui(op == "gui.icon_path" ? "addIconPath" : "addLanguagePath",
                              Py_BuildValue("(s)", arg.get_ref<const std::string&>().c_str()));
        return r ? replyResult(table, r) : replyPyError();
    }
    if (op == "gui.comm")
        return commPublish(table, arg);
    if (op == "gui.comm.manager")
        return commManager(table, arg);
    if (op == "gui.widget.show")
        return widgetShow(table, arg);
    if (op == "gui.widget.hide")
        return widgetHide(table, arg);
    if (op == "gui.ui.read")
        return uiRead(arg);
    if (op == "gui.control.show")
        return controlShow(table, arg);
    if (op == "gui.dialog.exec")
        return dialogExec(table, arg);
    if (op == "gui.sel.call")
        return selectionCall(table, arg);
    if (op == "gui.sel.observer")
        return selectionObserver(table, arg);
    if (op == "gui.dialog.message")
        return dialogMessage(arg);
    if (op == "gui.dialog.input")
        return dialogInput(arg);
    if (op == "gui.dialog.file")
        return dialogFile(arg);
    if (op == "gui.dialog.color")
        return dialogColor(arg);
    if (op == "gui.control.close" || op == "gui.control.active" || op == "gui.control.query"
        || op == "gui.control.clear_watcher")
        return controlCall(table, op, arg);
    if (op == "gui.doc")
        return guiDocumentCall(table, arg);
    if (op == "gui.pref_page") {
        if (!arg.is_array() || arg.size() != 2 || !arg[0].is_string() || !arg[1].is_string())
            return replyErr("ProtocolError", "gui.pref_page: [ui file, group]");
        PyObject* r = callGui("addPreferencePage",
                              Py_BuildValue("(ss)", arg[0].get_ref<const std::string&>().c_str(),
                                            arg[1].get_ref<const std::string&>().c_str()));
        return r ? replyResult(table, r) : replyPyError();
    }
    return replyErr("ProtocolError", "unknown gui op '" + op + "'");
}

}  // namespace

void Gui::SandboxGui::registerOps()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    App::ExpressionSandbox::registerBridgeOps("gui.", &guiOp);
    // A fresh guest has none of the stand-ins the previous one
    // registered: the InitGui runner (FreeCADGuiInit.py, docs/Sandbox.md
    // 7.9 G2b) re-runs the InitGui.py's it ran in the guest.  Queued on
    // the event loop rather than run from the listener, so it never
    // nests in the evaluation that booted the guest.
    App::ExpressionSandbox::ImageHost::instance().addBootListener([](int boot) {
        // the previous guest's selection observers point at nothing now
        dropSelectionObservers();
        QTimer::singleShot(0, [boot]() {
            if (!Application::Instance)
                return;
            Base::PyGILStateLocker lock;
            PyObject* mod = PyImport_ImportModule("FreeCADGui");
            PyObject* fn = mod ? PyObject_GetAttrString(mod, "_onGuestBoot") : nullptr;
            Py_XDECREF(mod);
            if (!fn) {
                PyErr_Clear();
                return;
            }
            PyObject* r = PyObject_CallFunction(fn, "i", boot);
            Py_DECREF(fn);
            if (!r) {
                Base::PyException e;
                Base::Console().Error("sandbox: FreeCADGui._onGuestBoot(%d) failed: %s\n",
                                      boot, e.what());
            }
            Py_XDECREF(r);
        });
    });
}

#else

void Gui::SandboxGui::registerOps()
{}

#endif  // FC_EXPR_IMAGE_HOST

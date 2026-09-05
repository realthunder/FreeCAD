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
#include <cstring>
#include <map>
#include <string>
#include <vector>
#endif

#include "SandboxGui.h"

#ifdef FC_EXPR_IMAGE_HOST

#include <nlohmann/json.hpp>
#include <CXX/Objects.hxx>
#include <App/ExpressionGuestProxy.h>
#include <App/ExpressionImageBridge.h>
#include <App/ExpressionSecurityRuntime.h>
#include <Base/Exception.h>

#include "Application.h"
#include "Command.h"

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

    def Initialize(self):
        self._standin.Initialize()

    def Activated(self):
        if hasattr(self._standin, "Activated"):
            self._standin.Activated()

    def Deactivated(self):
        if hasattr(self._standin, "Deactivated"):
            self._standin.Deactivated()

    def ContextMenu(self, recipient):
        if hasattr(self._standin, "ContextMenu"):
            self._standin.ContextMenu(recipient)


def make(standin, name, menutext, tooltip, icon):
    cls = type(name, (GuestWorkbench,), {"__module__": "FreeCADGui"})
    return cls(standin, name, menutext, tooltip, icon)


def is_guest(wb):
    return isinstance(wb, GuestWorkbench)


def name_of(wb):
    if wb is None:
        return None
    return getattr(wb, "_fcx_name", None) or type(wb).__name__
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
    PyObject* mgr = widgetManager();
    PyObject* r = mgr ? PyObject_CallMethod(mgr, "set_dispatcher", "O", standin) : nullptr;
    Py_XDECREF(mgr);
    Py_DECREF(standin);
    if (!r)
        return replyPyError();
    Py_DECREF(r);
    return replyOk(true);
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
    PyObject* r = PyObject_CallMethod(mgr, "show", "sOs", a[0].get_ref<const std::string&>().c_str(),
                                      title, a[2].get_ref<const std::string&>().c_str());
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

Reply guiOp(HandleTable& table, const Reply& requestCbor)
{
    const json req = json::from_cbor(requestCbor);
    // the catalog's `gui`: DENY for a document (not promptable), ALLOW
    // for the session and addons
    App::ExpressionSecurity::checkPermission(App::ExpressionSecurity::Permission::Gui);
    if (!Application::Instance)
        return replyErr("RuntimeError", "no GUI application");
    const std::string op = req.value("op", "");
    auto a = req.find("a");
    const json none;
    const json& arg = a != req.end() ? *a : none;
    if (op == "gui.cmd.add")
        return addCommand(table, arg);
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
}

#else

void Gui::SandboxGui::registerOps()
{}

#endif  // FC_EXPR_IMAGE_HOST

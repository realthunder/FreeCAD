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

#include <cstddef>
#include <string>
#include <unordered_map>

#include <Base/Interpreter.h>

#include "DocumentObjectPy.h"
#include "ExpressionGuestProxy.h"
#include "ExpressionImageHost.h"

using json = nlohmann::json;

namespace App
{
namespace ExpressionSandbox
{

namespace
{

struct GuestProxyObject
{
    PyObject_HEAD
    PyObject* dict;
    uint64_t id;
};

PyObject* baseType = nullptr;

/// The live stand-ins by guest proxy id (borrowed): one guest proxy has
/// ONE stand-in, so a descriptor decoded twice (write_prop Proxy, then
/// the proxy_new reply) names the same object and a dying duplicate can
/// never drop a proxy that is still in use.
std::unordered_map<uint64_t, PyObject*>& liveStandIns()
{
    static std::unordered_map<uint64_t, PyObject*> table;
    return table;
}

void guestProxyDealloc(PyObject* self)
{
    auto* o = reinterpret_cast<GuestProxyObject*>(self);
    if (o->id) {
        auto& live = liveStandIns();
        auto it = live.find(o->id);
        if (it != live.end() && it->second == self)
            live.erase(it);
        ImageHost::instance().dropProxy(o->id);
    }
    Py_CLEAR(o->dict);
    PyTypeObject* type = Py_TYPE(self);
    type->tp_free(self);
    // A heap type's instance dealloc decrefs its type; the per-class
    // subtype (made by type()) relies on this base doing it, since
    // subtype_dealloc skips the decref when the base is a heap type too.
    Py_DECREF(type);
}

PyObject* guestProxyRepr(PyObject* self)
{
    auto* o = reinterpret_cast<GuestProxyObject*>(self);
    PyObject* mod = PyObject_GetAttrString(self, "__module__");
    const char* modName = mod && PyUnicode_Check(mod) ? PyUnicode_AsUTF8(mod) : "?";
    PyObject* r = PyUnicode_FromFormat("<guest proxy %s.%s #%llu>", modName,
                                       Py_TYPE(self)->tp_name,
                                       static_cast<unsigned long long>(o->id));
    Py_XDECREF(mod);
    if (!r)
        PyErr_Clear();
    return r;
}

/// Raise a failed guest round trip on the host: the guest's exception
/// type when it is a builtin, else a RuntimeError carrying both fields
/// -- as the guest bridge does for host errors.
void raiseGuestError(const ImageResult& r)
{
    PyObject* builtins = PyEval_GetBuiltins();
    PyObject* type = builtins ? PyDict_GetItemString(builtins, r.excType.c_str()) : nullptr;
    if (type && PyExceptionClass_Check(type))
        PyErr_SetString(type, r.message.c_str());
    else
        PyErr_Format(PyExc_RuntimeError, "%s: %s", r.excType.c_str(), r.message.c_str());
}

/// The hook forwarder: `self` is the (proxy id, hook name) pair bound
/// into the PyCFunction; the first positional argument, when it is a
/// document object, is the owner the guest may write.
PyObject* hookCall(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!PyTuple_Check(self) || PyTuple_GET_SIZE(self) != 2) {
        PyErr_SetString(PyExc_SystemError, "guest proxy hook without its binding");
        return nullptr;
    }
    const uint64_t id = PyLong_AsUnsignedLongLong(PyTuple_GET_ITEM(self, 0));
    const char* hook = PyUnicode_AsUTF8(PyTuple_GET_ITEM(self, 1));
    if (!hook || PyErr_Occurred())
        return nullptr;
    const App::DocumentObject* owner = nullptr;
    if (args && PyTuple_Check(args) && PyTuple_GET_SIZE(args) > 0) {
        PyObject* first = PyTuple_GET_ITEM(args, 0);
        if (PyObject_TypeCheck(first, &DocumentObjectPy::Type))
            owner = static_cast<DocumentObjectPy*>(first)->getDocumentObjectPtr();
    }
    ImageResult r = ImageHost::instance().proxyCall(id, hook, args, kwargs, owner);
    if (!r.ok) {
        raiseGuestError(r);
        return nullptr;
    }
    PyObject* value = ImageHost::instance().decodeResult(r);
    if (!value && !PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError, "guest proxy hook returned an undecodable value");
    return value;
}

PyMethodDef HookDef = {"hook", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(hookCall)),
                       METH_VARARGS | METH_KEYWORDS,
                       "A guest proxy hook: forwards to the Proxy living in the sandbox guest."};

PyMemberDef GuestProxyMembers[] = {
    {"__dictoffset__", Py_T_PYSSIZET, offsetof(GuestProxyObject, dict), Py_READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr},
};

PyType_Slot GuestProxySlots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(guestProxyDealloc)},
    {Py_tp_repr, reinterpret_cast<void*>(guestProxyRepr)},
    {Py_tp_getattro, reinterpret_cast<void*>(PyObject_GenericGetAttr)},
    {Py_tp_setattro, reinterpret_cast<void*>(PyObject_GenericSetAttr)},
    {Py_tp_members, GuestProxyMembers},
    {Py_tp_new, reinterpret_cast<void*>(PyType_GenericNew)},
    {Py_tp_doc, const_cast<char*>("Stand-in for a scripted object's Proxy that lives in the"
                                  " sandbox guest; its hooks forward there.")},
    {0, nullptr},
};

PyType_Spec GuestProxySpec = {
    "FreeCAD.ExpressionSandbox.GuestProxy",
    sizeof(GuestProxyObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    GuestProxySlots,
};

PyObject* guestProxyBase()
{
    if (!baseType)
        baseType = PyType_FromSpec(&GuestProxySpec);
    return baseType;
}

/// The per-class heap subtype: `class <cls>(GuestProxy)` with the
/// guest module's name, one per module.class, kept for the process.
PyObject* classFor(const std::string& module, const std::string& cls)
{
    static std::unordered_map<std::string, PyObject*> classes;
    const std::string key = module + "." + cls;
    auto it = classes.find(key);
    if (it != classes.end())
        return it->second;
    PyObject* base = guestProxyBase();
    if (!base)
        return nullptr;
    PyObject* ns = Py_BuildValue("{s:s}", "__module__", module.c_str());
    if (!ns)
        return nullptr;
    PyObject* type = PyObject_CallFunction(reinterpret_cast<PyObject*>(&PyType_Type), "s(O)O",
                                           cls.c_str(), base, ns);
    Py_DECREF(ns);
    if (type)
        classes[key] = type;  // the table keeps the reference
    return type;
}

}  // namespace

PyObject* makeGuestProxy(const json& desc)
{
    auto id = desc.find("id");
    const std::string module = desc.value("mod", "");
    const std::string cls = desc.value("cls", "");
    if (id == desc.end() || !id->is_number_integer() || module.empty() || cls.empty()) {
        PyErr_SetString(PyExc_ValueError, "malformed guest proxy descriptor");
        return nullptr;
    }
    auto& live = liveStandIns();
    auto existing = live.find(id->get<uint64_t>());
    if (existing != live.end()) {
        Py_INCREF(existing->second);
        return existing->second;
    }
    PyObject* type = classFor(module, cls);
    if (!type)
        return nullptr;
    PyObject* inst = PyObject_CallNoArgs(type);
    if (!inst)
        return nullptr;
    auto* o = reinterpret_cast<GuestProxyObject*>(inst);
    o->id = id->get<uint64_t>();
    live[o->id] = inst;
    auto hooks = desc.find("hooks");
    if (hooks != desc.end() && hooks->is_array()) {
        for (const auto& h : *hooks) {
            if (!h.is_string())
                continue;
            const std::string& name = h.get_ref<const std::string&>();
            PyObject* binding = Py_BuildValue("(Ks)", static_cast<unsigned long long>(o->id),
                                              name.c_str());
            PyObject* fwd = binding ? PyCFunction_NewEx(&HookDef, binding, nullptr) : nullptr;
            Py_XDECREF(binding);
            int rc = fwd ? PyObject_SetAttrString(inst, name.c_str(), fwd) : -1;
            Py_XDECREF(fwd);
            if (rc != 0) {
                Py_DECREF(inst);
                return nullptr;
            }
        }
    }
    return inst;
}

bool isGuestProxy(PyObject* obj)
{
    PyObject* base = baseType;  // never built: no stand-in exists
    return obj && base && PyObject_TypeCheck(obj, reinterpret_cast<PyTypeObject*>(base));
}

uint64_t guestProxyId(PyObject* obj)
{
    if (!isGuestProxy(obj))
        return 0;
    return reinterpret_cast<GuestProxyObject*>(obj)->id;
}

PyObject* restoreGuestProxy(const std::string& module,
                            const std::string& cls,
                            const App::DocumentObject* owner)
{
    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;
    ImageResult r = ImageHost::instance().proxyNew(module, cls, args, true, owner);
    Py_DECREF(args);
    if (!r.ok) {
        raiseGuestError(r);
        return nullptr;
    }
    PyObject* standIn = ImageHost::instance().decodeResult(r);
    if (!standIn || !isGuestProxy(standIn)) {
        Py_XDECREF(standIn);
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_RuntimeError, "guest allocated no proxy for %s.%s",
                         module.c_str(), cls.c_str());
        return nullptr;
    }
    return standIn;
}

}  // namespace ExpressionSandbox
}  // namespace App

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
#include <cstring>
#include <string>
#include <unordered_map>

#include <Base/Interpreter.h>

#include "DocumentObjectPy.h"
#include "ExpressionEvaluator.h"
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

/// A forwarder bound to (id, name): what a hook attribute is, and what
/// a callable attribute read through the guest becomes.
PyObject* makeForwarder(uint64_t id, const char* name)
{
    PyObject* binding = Py_BuildValue("(Ks)", static_cast<unsigned long long>(id), name);
    PyObject* fwd = binding ? PyCFunction_NewEx(&HookDef, binding, nullptr) : nullptr;
    Py_XDECREF(binding);
    return fwd;
}

bool isForwarder(PyObject* obj)
{
    return obj && PyCFunction_Check(obj)
        && PyCFunction_GET_FUNCTION(obj)
            == reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(hookCall));
}

/// Attribute read (G1d, docs/Sandbox.md 7.6): what the stand-in holds
/// itself -- its hooks, the class attributes -- answers at once; any
/// other name is the guest instance's, read through proxy_get.  A
/// dunder never crosses: Python probes them by the dozen (copy,
/// pickle, repr) and a Proxy defines none the host needs.  A method
/// read once is kept as a forwarder on the stand-in, so `hasattr`
/// followed by the call is one trip, not two; data is read every
/// time, since the guest instance may change it.
/// The hook names FeaturePythonImp probes on every Proxy (the guest
/// prelude's HOOKS, kept in step): a hook the descriptor did not list
/// is one the class does not define, answered without a trip.
bool isHookName(const char* attr)
{
    static const char* const hooks[] = {
        "execute", "mustExecute", "skipRecompute", "onBeforeChange", "onBeforeChangeLabel",
        "onChanged", "onDocumentRestored", "unsetupObject", "getViewProviderName",
        "getSubObject", "getSubObjects", "getLinkedObject", "canLinkProperties",
        "allowDuplicateLabel", "redirectSubName", "canLoadPartial", "hasChildElement",
        "isElementVisible", "isElementVisibleEx", "setElementVisible", "getElementMapVersion",
        "editProperty", "dumps", "loads"};
    for (const char* h : hooks)
        if (std::strcmp(h, attr) == 0)
            return true;
    return false;
}

PyObject* guestProxyGetAttr(PyObject* self, PyObject* name)
{
    PyObject* value = PyObject_GenericGetAttr(self, name);
    if (value || !PyErr_ExceptionMatches(PyExc_AttributeError))
        return value;
    auto* o = reinterpret_cast<GuestProxyObject*>(self);
    const char* attr = PyUnicode_Check(name) ? PyUnicode_AsUTF8(name) : nullptr;
    if (!attr || o->id == 0 || (attr[0] == '_' && attr[1] == '_') || isHookName(attr))
        return nullptr;
    PyErr_Clear();
    ImageResult r = ImageHost::instance().proxyGet(o->id, attr);
    if (!r.ok) {
        raiseGuestError(r);
        return nullptr;
    }
    value = ImageHost::instance().decodeResult(r);
    if (!value) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_RuntimeError, "guest proxy attribute '%s' is not transferable", attr);
        return nullptr;
    }
    if (isForwarder(value) && PyObject_GenericSetAttr(self, name, value) != 0)
        PyErr_Clear();
    return value;
}

/// Attribute write: the guest instance's, through proxy_set (a value
/// only; a host object would be a handle no transaction outlives).
/// The stand-in's own dict is written by makeGuestProxy alone.
int guestProxySetAttr(PyObject* self, PyObject* name, PyObject* value)
{
    auto* o = reinterpret_cast<GuestProxyObject*>(self);
    const char* attr = PyUnicode_Check(name) ? PyUnicode_AsUTF8(name) : nullptr;
    if (!attr)
        return -1;
    if (o->id == 0)
        return PyObject_GenericSetAttr(self, name, value);
    if (!value) {
        PyErr_Format(PyExc_AttributeError, "cannot delete attribute '%s' of a guest proxy", attr);
        return -1;
    }
    ImageResult r = ImageHost::instance().proxySet(o->id, attr, value);
    if (!r.ok) {
        raiseGuestError(r);
        return -1;
    }
    // a forwarder cached by a read is now shadowed by the guest's value
    if (PyObject_GenericSetAttr(self, name, nullptr) != 0)
        PyErr_Clear();
    return 0;
}

PyMemberDef GuestProxyMembers[] = {
    {"__dictoffset__", Py_T_PYSSIZET, offsetof(GuestProxyObject, dict), Py_READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr},
};

PyType_Slot GuestProxySlots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(guestProxyDealloc)},
    {Py_tp_repr, reinterpret_cast<void*>(guestProxyRepr)},
    {Py_tp_getattro, reinterpret_cast<void*>(guestProxyGetAttr)},
    {Py_tp_setattro, reinterpret_cast<void*>(guestProxySetAttr)},
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
    const uint64_t pid = id->get<uint64_t>();
    // the hooks go into the stand-in's own dict (id still 0: the
    // setattr slot forwards to the guest once it is bound)
    auto hooks = desc.find("hooks");
    if (hooks != desc.end() && hooks->is_array()) {
        for (const auto& h : *hooks) {
            if (!h.is_string())
                continue;
            const std::string& name = h.get_ref<const std::string&>();
            PyObject* fwd = makeForwarder(pid, name.c_str());
            PyObject* key = fwd ? PyUnicode_FromString(name.c_str()) : nullptr;
            int rc = key ? PyObject_GenericSetAttr(inst, key, fwd) : -1;
            Py_XDECREF(key);
            Py_XDECREF(fwd);
            if (rc != 0) {
                Py_DECREF(inst);
                return nullptr;
            }
        }
    }
    o->id = pid;
    live[o->id] = inst;
    return inst;
}

PyObject* makeGuestMethod(uint64_t id, const std::string& name)
{
    return makeForwarder(id, name.c_str());
}

PyObject* constructGuestProxy(PyObject* cls, PyObject* args, PyObject* kwargs)
{
    if (!proxyRestoreRouted())
        Py_RETURN_NONE;
    if (!cls || !PyType_Check(cls)) {
        PyErr_SetString(PyExc_TypeError, "constructGuestProxy: a class is required");
        return nullptr;
    }
    // `cls.__new__(cls)` alone -- copy, pickle, a native alloc -- is
    // no construction: native.  A first argument that is a document
    // object is the owner __init__ may write; None (Draft's
    // `Array(None)`, installed later by addObject(attach=True)) or
    // anything else constructs with no owner.
    if (!args || !PyTuple_Check(args) || PyTuple_GET_SIZE(args) == 0)
        Py_RETURN_NONE;
    PyObject* first = PyTuple_GET_ITEM(args, 0);
    const App::DocumentObject* owner = PyObject_TypeCheck(first, &DocumentObjectPy::Type)
        ? static_cast<DocumentObjectPy*>(first)->getDocumentObjectPtr()
        : nullptr;
    PyObject* mod = PyObject_GetAttrString(cls, "__module__");
    PyObject* qual = PyObject_GetAttrString(cls, "__qualname__");
    std::string module = mod && PyUnicode_Check(mod) ? PyUnicode_AsUTF8(mod) : "";
    std::string name = qual && PyUnicode_Check(qual) ? PyUnicode_AsUTF8(qual) : "";
    Py_XDECREF(mod);
    Py_XDECREF(qual);
    if (module.empty() || name.empty()) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_TypeError, "constructGuestProxy: the class has no module or name");
        return nullptr;
    }
    ImageResult r = ImageHost::instance().proxyNew(module, name, args, kwargs, false, owner);
    if (!r.ok) {
        raiseGuestError(r);
        return nullptr;
    }
    PyObject* standIn = ImageHost::instance().decodeResult(r);
    if (!standIn)
        return nullptr;
    if (!isGuestProxy(standIn)) {
        // never a native fallback: the guest ran __init__, and running
        // it again on the host would repeat its side effects
        Py_DECREF(standIn);
        PyErr_Format(PyExc_RuntimeError, "guest construction of %s.%s returned no proxy",
                     module.c_str(), name.c_str());
        return nullptr;
    }
    return standIn;
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

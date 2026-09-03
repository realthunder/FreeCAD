/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
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

#include <Python.h>

#include <Base/Exception.h>
#include <Base/PyObjectBase.h>

#include "Application.h"
#include "Document.h"
#include "ExpressionSecurityRuntime.h"

// The FreeCAD.ExpressionSecurity module: the grant-management API over the
// expression permission runtime (phase 1 step 3b). Used by the Document
// Permissions panel, by tests, and by headless scripts.

using namespace App;
namespace Sec = App::ExpressionSecurity;

namespace {

// Parse a permission name that may carry a host.import module
// ("host.import:numpy"); an explicit target argument wins over the carried
// module only if the name carried none.
bool parsePermission(const char *name, const char *target,
        Sec::Permission &perm, std::string &targetOut)
{
    std::string carried;
    auto p = Sec::permissionFromName(name ? name : "", &carried);
    if (!p) {
        PyErr_Format(PyExc_ValueError, "unknown permission '%s'",
                name ? name : "");
        return false;
    }
    perm = *p;
    if (!carried.empty())
        targetOut = carried;
    else if (target && target[0])
        targetOut = target;
    else
        targetOut = "*";
    return true;
}

PyObject *grantFunc(PyObject *, PyObject *args, PyObject *kwds)
{
    const char *principal {};
    const char *permission {};
    const char *target = "";
    int allow = 1;
    const char *scope = "always";
    const char *label = "";
    const char *path = "";
    static const char *kwlist[] = {"principal", "permission", "target",
            "allow", "scope", "label", "path", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss|spsss",
                const_cast<char **>(kwlist), &principal, &permission, &target,
                &allow, &scope, &label, &path))
        return nullptr;
    Sec::Permission perm;
    std::string t;
    if (!parsePermission(permission, target, perm, t))
        return nullptr;
    try {
        Sec::Runtime::instance().grant(principal, perm, t, allow != 0, scope,
                label, path);
    } catch (Base::Exception &e) {
        e.setPyException();
        return nullptr;
    }
    Py_Return;
}

PyObject *revokeFunc(PyObject *, PyObject *args)
{
    const char *principal {};
    const char *permission {};
    const char *target = "";
    if (!PyArg_ParseTuple(args, "ss|s", &principal, &permission, &target))
        return nullptr;
    Sec::Permission perm;
    std::string t;
    if (!parsePermission(permission, target, perm, t))
        return nullptr;
    try {
        return PyLong_FromSize_t(
                Sec::Runtime::instance().revoke(principal, perm, t));
    } catch (Base::Exception &e) {
        e.setPyException();
        return nullptr;
    }
}

PyObject *grantsFunc(PyObject *, PyObject *)
{
    try {
        auto &store = Sec::Runtime::instance().store();
        PyObject *list = PyList_New(0);
        for (const auto &g : store.grants()) {
            PyObject *d = Py_BuildValue(
                    "{s:s, s:s, s:s, s:O, s:s, s:s, s:s}",
                    "principal", g.principal.c_str(),
                    "permission", g.permission.c_str(),
                    "target", g.target.c_str(),
                    "allow", g.allow ? Py_True : Py_False,
                    "granted", g.grantedUtc.c_str(),
                    "label", g.displayLabel.c_str(),
                    "path", g.displayPath.c_str());
            PyList_Append(list, d);
            Py_XDECREF(d);
        }
        return list;
    } catch (Base::Exception &e) {
        e.setPyException();
        return nullptr;
    }
}

PyObject *pendingFunc(PyObject *, PyObject *args)
{
    const char *document = "";
    if (!PyArg_ParseTuple(args, "|s", &document))
        return nullptr;
    auto reqs = Sec::Runtime::instance().pendingRequests(document);
    PyObject *list = PyList_New(0);
    for (const auto &r : reqs) {
        PyObject *d = Py_BuildValue(
                "{s:s, s:s, s:s, s:s, s:s, s:s, s:i}",
                "principal", r.principal.c_str(),
                "permission", Sec::permissionName(r.permission),
                "target", r.target.c_str(),
                "document", r.documentName.c_str(),
                "object", r.objectName.c_str(),
                "first", r.firstUtc.c_str(),
                "count", r.count);
        PyList_Append(list, d);
        Py_XDECREF(d);
    }
    return list;
}

PyObject *principalOfFunc(PyObject *, PyObject *args)
{
    const char *docName {};
    if (!PyArg_ParseTuple(args, "s", &docName))
        return nullptr;
    auto doc = GetApplication().getDocument(docName);
    if (!doc) {
        PyErr_Format(PyExc_ValueError, "no document '%s'", docName);
        return nullptr;
    }
    try {
        return PyUnicode_FromString(
                Sec::Runtime::instance().documentPrincipal(doc).c_str());
    } catch (Base::Exception &e) {
        e.setPyException();
        return nullptr;
    }
}

PyObject *resolveFunc(PyObject *, PyObject *args)
{
    const char *principal {};
    const char *permission {};
    const char *target = "";
    if (!PyArg_ParseTuple(args, "ss|s", &principal, &permission, &target))
        return nullptr;
    Sec::Permission perm;
    std::string t;
    if (!parsePermission(permission, target, perm, t))
        return nullptr;
    auto d = Sec::Runtime::instance().resolve(principal, perm, t);
    return PyUnicode_FromString(Sec::decisionName(d));
}

PyObject *clearOnceFunc(PyObject *, PyObject *)
{
    Sec::Runtime::instance().clearOnce();
    Py_Return;
}

PyObject *clearPendingFunc(PyObject *, PyObject *args)
{
    const char *principal {};
    const char *permission {};
    const char *target = "*";
    if (!PyArg_ParseTuple(args, "ss|s", &principal, &permission, &target))
        return nullptr;
    Sec::Permission perm;
    std::string t;
    if (!parsePermission(permission, target, perm, t))
        return nullptr;
    Sec::Runtime::instance().clearPending(principal, perm, t);
    Py_Return;
}

PyObject *enforcedFunc(PyObject *, PyObject *)
{
    return PyBool_FromLong(Sec::Runtime::instance().enforced());
}

PyMethodDef Methods[] = {
    {"grant", reinterpret_cast<PyCFunction>(grantFunc),
     METH_VARARGS | METH_KEYWORDS,
     "grant(principal, permission, target='*', allow=True, scope='always',"
     " label='', path='') -- record a permission decision.\n"
     "scope is 'once', 'session' or 'always' (persisted in grants.json)."},
    {"revoke", revokeFunc, METH_VARARGS,
     "revoke(principal, permission, target='*') -> int -- remove grants;\n"
     "a '*' target removes every target of that permission."},
    {"grants", grantsFunc, METH_NOARGS,
     "grants() -> list of dict -- the persisted grant store."},
    {"pending", pendingFunc, METH_VARARGS,
     "pending(document='') -> list of dict -- unanswered permission"
     " requests, optionally filtered by document name."},
    {"principalOf", principalOfFunc, METH_VARARGS,
     "principalOf(documentName) -> str -- the document principal id"
     " (content hash) used to key its grants."},
    {"resolve", resolveFunc, METH_VARARGS,
     "resolve(principal, permission, target='*') -> 'allow'|'deny'|'prompt'"},
    {"clearOnce", clearOnceFunc, METH_NOARGS,
     "clearOnce() -- drop all once-scoped answers."},
    {"clearPending", clearPendingFunc, METH_VARARGS,
     "clearPending(principal, permission, target='*') -- drop pending"
     " requests matching the triple ('*' matches any); what the sandbox"
     " package installer does once a pkg.install request is satisfied."},
    {"enforced", enforcedFunc, METH_NOARGS,
     "enforced() -> bool -- whether expression permission enforcement is on."},
    {nullptr, nullptr, 0, nullptr},
};

}  // namespace

namespace App {
namespace ExpressionSecurity {

void initPyModule(PyObject *appModule)
{
    static struct PyModuleDef moduleDef = {
        PyModuleDef_HEAD_INIT,
        "ExpressionSecurity", "Expression permission service", -1,
        Methods,
        nullptr, nullptr, nullptr, nullptr
    };
    PyObject *module = PyModule_Create(&moduleDef);
    Py_INCREF(module);
    PyModule_AddObject(appModule, "ExpressionSecurity", module);
}

}  // namespace ExpressionSecurity
}  // namespace App

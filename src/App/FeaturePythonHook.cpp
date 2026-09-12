/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#include "DocumentObject.h"
#include "FeaturePythonHook.h"

using namespace App;

Py::Object App::pyHookArg(App::DocumentObject* arg)
{
    // a null object is None, which is what every hand-written body did
    return arg ? Py::asObject(arg->getPyObject()) : Py::Object();
}

Py::Object App::pyHookArg(const std::vector<std::string>& arg)
{
    Py::Tuple tuple(arg.size());
    Py_ssize_t i = 0;
    for (const auto& item : arg) {
        tuple.setItem(i++, Py::String(item));
    }
    return tuple;
}

PyHookImp::PyHookImp(const PyHookDef* table, std::size_t count)
    : _hookTable(table)
    , _hooks(count)
{}

PyHookImp::~PyHookImp()
{
    Base::PyGILStateLocker lock;
    try {
        for (auto& slot : _hooks) {
            slot.proxy = Py::None();
        }
    }
    catch (Py::Exception& e) {
        e.clear();
    }
}

void PyHookImp::init(PyObject* pyobj)
{
    Base::PyGILStateLocker lock;
    _has__object__ = PyObject_HasAttrString(pyobj, "__object__") != 0;

    for (std::size_t i = 0; i < _hooks.size(); ++i) {
        HookSlot& slot = _hooks[i];
        const char* name = _hookTable[i].name;
        FC_PY_GetCallable(pyobj, name, slot.proxy);
        slot.allowRecursive = false;
        if (slot.proxy.isNone()) {
            continue;
        }
        std::string attr("__allow_recursive_");
        attr += name;
        PyObject* pyRecursive = PyObject_GetAttrString(pyobj, attr.c_str());
        if (!pyRecursive) {
            PyErr_Clear();
        }
        else {
            slot.allowRecursive = PyObject_IsTrue(pyRecursive) != 0;
            Py_DECREF(pyRecursive);
        }
    }
}

bool PyHookImp::canCallHook(int hook) const
{
    const HookSlot& slot = _hooks[hook];
    if (slot.proxy.isNone()) {
        return false;
    }
    return !(_hookTable[hook].guarded && slot.calling && !slot.allowRecursive);
}

void PyHookImp::reportHookError(const PyHookDef& def)
{
    switch (def.error) {
        case PyHookError::Throw:
            Base::PyException::ThrowException();
            break;
        case PyHookError::ReportThrow: {
            Base::PyException e;
            e.ReportException();
            throw e;
        }
        case PyHookError::Report:
        default: {
            Base::PyException e;
            e.ReportException();
            break;
        }
    }
}

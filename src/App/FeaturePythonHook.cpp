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
#include "PropertyPythonObject.h"
#ifdef FC_EXPR_IMAGE_HOST
#include "ExpressionGuestProxy.h"
#endif

using namespace App;

namespace
{
/// docs/ProxyChain.md sec 2.4.  Bumped from the three places a hook definition
/// can change under a feature that itself did not change; never wraps in any
/// run a human could sit through, and a wrap would only force one rebuild.
unsigned long _proxyChainGeneration = 1;
}  // namespace

unsigned long App::ProxyChain::generation()
{
    return _proxyChainGeneration;
}

void App::ProxyChain::bump()
{
    ++_proxyChainGeneration;
}

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
            // under the lock: the chain and the snapshot hold references
            // too, and a vector destroyed with the rest of the object would
            // drop them without it
            slot.chain.clear();
            slot.snapshot.clear();
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

void PyHookImp::setHookExtensions(std::vector<App::DocumentObject*> objs)
{
    _expObjects = std::move(objs);
    // drop every resolved chain; each hook rebuilds its own on its next call
    Base::PyGILStateLocker lock;
    for (auto& slot : _hooks) {
        slot.chain.clear();
        slot.chainValid = false;
        // the list itself moved, so there is nothing left to compare against;
        // the next look re-seeds.  A list change is a property change and
        // touches the owner, so the recompute it deserves comes from there.
        slot.snapshot.clear();
        slot.snapshotValid = false;
    }
    _chainGeneration = ProxyChain::generation();
}

void PyHookImp::ensureChain(int hook) const
{
    if (_chainGeneration != ProxyChain::generation()) {
        _chainGeneration = ProxyChain::generation();
        Base::PyGILStateLocker lock;
        for (auto& slot : _hooks) {
            slot.chain.clear();
            slot.chainValid = false;
        }
    }
    HookSlot& slot = _hooks[hook];
    if (!slot.chainValid) {
        // before resolving, not after: resolution runs Python, which may reach
        // this hook again, and one unresolved pass is better than no bottom
        slot.chainValid = true;
        resolveChain(hook, slot);
    }
}

void PyHookImp::resolveChain(int hook, HookSlot& slot) const
{
    const char* name = _hookTable[hook].expName;
    std::string recursive("__allow_recursive_");
    recursive += name;

    Base::PyGILStateLocker lock;
    for (auto* obj : _expObjects) {
        if (!obj || !obj->isAttachedToDocument()) {
            // an XLink whose file is not loaded, or an object on its way out
            continue;
        }
        Py::Object callable;
        Py::Object owner;
        try {
            // 1. the linked object's own Proxy, if it holds one -- a scripted
            //    object that chooses to extend others.  Proxy first, as it is
            //    everywhere else.
            auto* prop = freecad_dynamic_cast<PropertyPythonObject>(
                obj->getPropertyByName("Proxy"));
            if (prop) {
                Py::Object proxy = prop->getValue();
                if (!proxy.isNone()) {
                    FC_PY_GetCallable(proxy.ptr(), name, callable);
                    if (!callable.isNone()) {
                        owner = proxy;
                    }
                }
            }
            // 2. else the object itself: a spreadsheet alias whose cell is a
            //    lambda, a function stored on it at runtime, or any callable
            //    attribute a C++ type provides.  The type is never inspected.
            if (callable.isNone()) {
                Py::Object pyobj(obj->getPyObject(), true);
                FC_PY_GetCallable(pyobj.ptr(), name, callable);
                if (!callable.isNone()) {
                    owner = pyobj;
                }
            }
            if (callable.isNone()) {
                continue;
            }
            ChainEntry entry;
            entry.callable = callable;
            entry.identity = callable;
            // a bound method is rebuilt on every getattr; the function under
            // it is not.  docs/ProxyChain.md sec 4.5
            if (PyObject* func = PyObject_GetAttrString(callable.ptr(), "__func__")) {
                entry.identity = Py::Object(func, true);
            }
            else {
                PyErr_Clear();
            }
#ifdef FC_EXPR_IMAGE_HOST
            // A function a routed cell left (docs/ProxyChain.md P3): called by
            // the chain it runs as THIS object's file -- the object it extends
            // is its write owner and that document its principal (RULED
            // 2026-09-13).  The identity stays the unbound function.
            if (ExpressionSandbox::isRoutedFunction(callable.ptr())) {
                PyObject* bound =
                    ExpressionSandbox::bindRoutedFunction(callable.ptr(), hookSelf(hook).ptr());
                if (!bound) {
                    throw Py::Exception();
                }
                entry.callable = Py::asObject(bound);
            }
#endif
            PyObject* pyRecursive = PyObject_GetAttrString(owner.ptr(), recursive.c_str());
            if (!pyRecursive) {
                PyErr_Clear();
            }
            else {
                entry.allowRecursive = PyObject_IsTrue(pyRecursive) != 0;
                Py_DECREF(pyRecursive);
            }
            slot.chain.push_back(std::move(entry));
        }
        catch (Py::Exception&) {
            // a property getter of the linked object raising is that object's
            // problem; it simply does not extend this hook
            Base::PyException e;
            e.ReportException();
        }
        catch (const Base::Exception& e) {
            e.ReportException();
        }
    }
}

void PyHookImp::takeSnapshot(HookSlot& slot) const
{
    slot.snapshot.clear();
    slot.snapshot.reserve(slot.chain.size());
    for (const auto& entry : slot.chain) {
        slot.snapshot.push_back(entry.identity);
    }
    slot.snapshotGeneration = ProxyChain::generation();
    slot.snapshotValid = true;
}

void PyHookImp::snapshotChain(int hook) const
{
    if (_expObjects.empty()) {
        return;
    }
    // the call this follows resolved the chain already; a callable free to do
    // anything may have bumped the generation while it ran, and then what we
    // want on record is what the chain resolves to NOW
    ensureChain(hook);
    Base::PyGILStateLocker lock;
    takeSnapshot(_hooks[hook]);
}

bool PyHookImp::chainDefinitionChanged(int hook) const
{
    if (_expObjects.empty()) {
        return false;
    }
    HookSlot& slot = _hooks[hook];
    if (slot.snapshotValid && slot.snapshotGeneration == ProxyChain::generation()) {
        return false;
    }
    ensureChain(hook);
    Base::PyGILStateLocker lock;
    if (!slot.snapshotValid) {
        // The first look is the baseline, not a change: nothing has run yet to
        // compare against, and answering "changed" here would recompute every
        // chained feature once on load, where the restore already forces the
        // recompute that is actually needed.
        takeSnapshot(slot);
        return false;
    }
    if (slot.snapshot.size() == slot.chain.size()) {
        std::size_t i = 0;
        for (; i < slot.chain.size(); ++i) {
            if (slot.snapshot[i].ptr() != slot.chain[i].identity.ptr()) {
                break;
            }
        }
        if (i == slot.chain.size()) {
            // whatever moved in the process was not ours -- an unrelated cell
            // of the same sheet, another document's Proxy.  Charge the next
            // query one integer rather than another resolution.
            slot.snapshotGeneration = ProxyChain::generation();
            return false;
        }
    }
    // Deliberately NOT re-snapshotting here: the answer has to stay the same
    // until the run that acts on it, and it is that run which records the new
    // definition.
    return true;
}

bool PyHookImp::canCallHook(int hook) const
{
    if (!_expObjects.empty()) {
        ensureChain(hook);
    }
    const HookSlot& slot = _hooks[hook];
    const bool nested = _hookTable[hook].guarded && slot.calling;
    for (const auto& entry : slot.chain) {
        if (!nested || entry.allowRecursive) {
            return true;
        }
    }
    if (slot.proxy.isNone()) {
        return false;
    }
    return !(nested && !slot.allowRecursive);
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

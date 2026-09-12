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

#ifndef APP_FEATUREPYTHONHOOK_H
#define APP_FEATUREPYTHONHOOK_H

#include <cstddef>
#include <string>
#include <vector>

#include <Base/Interpreter.h>
#include <FCGlobal.h>

/** @file
 * The machinery behind FeaturePython's and ViewProviderFeaturePython's hooks.
 *
 * Both Imp classes were one hand-written pattern per hook: an absence check, a
 * recursion guard, a GIL lock, an argument tuple with the owner object first,
 * the call, a result mapping, a NotImplementedError catch and an error report.
 * That pattern is PyHookImp::callHook below; only the per-hook TABLE is
 * generated, from src/App/FeaturePythonHooks.py.  docs/ProxyChain.md sec 3.
 */

namespace App
{

class DocumentObject;

/// Where the owner object goes in the argument tuple.
enum class PyHookSelf : signed char
{
    None = 0,    ///< never passed; the callable sees only its own arguments
    Modern = 1,  ///< passed first, dropped in the old __object__ form
    Always = 2,  ///< passed first in both forms
};

/// What a Python error other than NotImplementedError does.
enum class PyHookError : signed char
{
    Report = 0,       ///< report it; the call reports Failed
    ReportThrow = 1,  ///< report it, then throw the Base::PyException
    Throw = 2,        ///< Base::PyException::ThrowException()
};

/// One row of the generated hook table.
struct PyHookDef
{
    const char* name;     ///< the Proxy attribute:            "execute"
    const char* expName;  ///< what a ProxyExp element exposes: "expExecute"
    PyHookSelf self;
    /// false: absence is the only check, and there is NO recursion guard --
    /// a nested call is the normal case for these (a property set inside
    /// onChanged), and guarding them would silently drop it.
    bool guarded;
    PyHookError error;
};

/// What one hook call came to.
enum class PyHookState : signed char
{
    NotHandled,  ///< absent, guarded out, or the callable declined
    Handled,     ///< the callable answered; the decoder took its result
    Failed,      ///< a Python error was reported
};

/** Marshalling of a C++ hook argument to Python
 *
 * A hook body names its arguments in C++ terms and callHook turns them into
 * the tuple.  Anything without an overload here is passed as a ready Py::Object
 * -- which is how the irregular arguments (a matrix, a pivy pointer, a Qt
 * widget) reach the tuple.
 */
inline Py::Object pyHookArg(const Py::Object& arg)
{
    return arg;
}
inline Py::Object pyHookArg(const char* arg)
{
    return Py::String(arg ? arg : "");
}
inline Py::Object pyHookArg(const std::string& arg)
{
    return Py::String(arg);
}
inline Py::Object pyHookArg(bool arg)
{
    return Py::Boolean(arg);
}
inline Py::Object pyHookArg(int arg)
{
    return Py::Int(arg);
}
inline Py::Object pyHookArg(double arg)
{
    return Py::Float(arg);
}
AppExport Py::Object pyHookArg(App::DocumentObject* arg);
AppExport Py::Object pyHookArg(const std::vector<std::string>& arg);

/** The resolved hooks of one Proxy, and the call
 *
 * A side (App or view) derives from this, hands the constructor its generated
 * table, and writes one short body per hook that calls callHook() with a
 * decoder.  The cache is what it always was: init() resolves every hook once
 * when the Proxy changes, so a call is an isNone() test, a flag test, a GIL
 * lock and the pyCall.
 */
class AppExport PyHookImp
{
public:
    enum ValueT
    {
        NotImplemented = 0,  ///< not handled
        Accepted = 1,        ///< handled and accepted
        Rejected = 2         ///< handled and rejected
    };

    /// Resolve every hook on pyobj.  Called when the Proxy changes.
    void init(PyObject* pyobj);

protected:
    PyHookImp(const PyHookDef* table, std::size_t count);
    virtual ~PyHookImp();

    PyHookImp(const PyHookImp&) = delete;
    PyHookImp(PyHookImp&&) = delete;
    PyHookImp& operator=(const PyHookImp&) = delete;
    PyHookImp& operator=(PyHookImp&&) = delete;

    /// The object this hook's first argument names.  Almost always the one
    /// owner, but the view side's updateData names the document object.
    virtual Py::Object hookSelf(int hook) const = 0;

    /** Call one hook and decode its result
     *
     * @param hook: an enumerator of the side's generated hook enum
     * @param decode: called with the result; returns whether it was handled.
     *                It may throw Py::Exception to reject a malformed return.
     * @param args: the hook's own arguments, the owner excluded -- callHook
     *              places that itself, per the table's PyHookSelf.
     *
     * Handled stops here.  NotHandled is the caller's cue to fall through to
     * the C++ base.  Failed means a Python error was reported and the caller
     * should answer with whatever this hook says an error means -- which is
     * not always what "not handled" means.
     */
    template<class Decode, class... Args>
    PyHookState callHook(int hook, Decode&& decode, Args&&... args) const
    {
        const PyHookDef& def = _hookTable[hook];
        HookSlot& slot = _hooks[hook];
        if (slot.proxy.isNone()) {
            return PyHookState::NotHandled;
        }
        if (def.guarded && slot.calling && !slot.allowRecursive) {
            return PyHookState::NotHandled;
        }
        CallGuard guard(slot.calling);

        Base::PyGILStateLocker lock;
        try {
            Py::Object res = callProxy(hook, def, slot, std::forward<Args>(args)...);
            return decode(res) ? PyHookState::Handled : PyHookState::NotHandled;
        }
        catch (Py::Exception&) {
            if (PyErr_ExceptionMatches(PyExc_NotImplementedError)) {
                PyErr_Clear();
                return PyHookState::NotHandled;
            }
            reportHookError(def);
            return PyHookState::Failed;
        }
        catch (const Base::Exception& e) {
            // The pivy conversions of the view side throw this rather than a
            // Python error; every hook that can see it reported and carried on.
            e.ReportException();
            return PyHookState::Failed;
        }
    }

    /// Whether the Proxy is one of the old ones, carrying __object__ and so
    /// taking no owner argument.
    bool hasObjectAttr() const
    {
        return _has__object__;
    }

    /// Whether a call would happen at all: the hook resolved, and the recursion
    /// guard is not holding it.  A body with work to do before the call --
    /// building a matrix, a pivy pointer, a Qt wrapper -- tests this first, the
    /// way the retired FC_PY_CALL_CHECK macro did.
    bool canCallHook(int hook) const;

private:
    struct HookSlot
    {
        Py::Object proxy;            ///< the callable found on the Proxy
        bool calling {false};        ///< inside a call, for the recursion guard
        bool allowRecursive {false}; ///< __allow_recursive_<hook> said so
    };

    /// Sets a bool for the scope of a call, the way Base::BitsetLocker did for
    /// the retired flag bitset.
    class CallGuard
    {
    public:
        explicit CallGuard(bool& flag)
            : _flag(flag)
            , _old(flag)
        {
            flag = true;
        }
        ~CallGuard()
        {
            _flag = _old;
        }
        CallGuard(const CallGuard&) = delete;
        CallGuard& operator=(const CallGuard&) = delete;

    private:
        bool& _flag;
        bool _old;
    };

    template<class... Args>
    Py::Object callProxy(int hook, const PyHookDef& def, const HookSlot& slot,
                         Args&&... args) const
    {
        const bool withSelf = def.self == PyHookSelf::Always
            || (def.self == PyHookSelf::Modern && !_has__object__);
        const std::size_t count = sizeof...(Args) + (withSelf ? 1U : 0U);
        if (count == 0) {
            return Base::pyCall(slot.proxy.ptr());
        }
        Py::Tuple tuple(count);
        std::size_t i = 0;
        if (withSelf) {
            tuple.setItem(i++, hookSelf(hook));
        }
        // a comma fold: left to right, which is the argument order
        ((void)tuple.setItem(i++, pyHookArg(std::forward<Args>(args))), ...);
        return Base::pyCall(slot.proxy.ptr(), tuple.ptr());
    }

    /// Report, rethrow or throw, per the hook's error policy.  Out of line so
    /// callHook stays small at every instantiation.
    static void reportHookError(const PyHookDef& def);

    const PyHookDef* _hookTable;
    mutable std::vector<HookSlot> _hooks;
    bool _has__object__ {false};
};

/// A hook whose result nothing reads.
inline auto pyHookDecodeNotify()
{
    return [](const Py::Object&) {
        return true;
    };
}

/// The result's truth is Accepted or Rejected; always handled.
inline auto pyHookDecodeValueT(PyHookImp::ValueT& out)
{
    return [&out](const Py::Object& res) {
        Py::Boolean ok(res);
        out = static_cast<bool>(ok) ? PyHookImp::Accepted : PyHookImp::Rejected;
        return true;
    };
}

/// An integer result; always handled.  The hook's own "not handled" value
/// (-2 for the element-visibility hooks, -1 for canLoadPartial) travels back
/// as an ordinary integer, exactly as it did.
inline auto pyHookDecodeInt(int& out)
{
    return [&out](const Py::Object& res) {
        out = static_cast<int>(Py::Int(res));
        return true;
    };
}

}  // namespace App

#endif  // APP_FEATUREPYTHONHOOK_H

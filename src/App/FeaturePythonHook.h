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
    /// true: nothing reads the result, so the chain calls EVERY element
    /// instead of stopping at the first that answers; only an explicit True
    /// from a ProxyExp element stops it before the Proxy.
    bool notify;
    PyHookError error;
};

/** The generation of the hook definitions in this process
 *
 * A ProxyExp element's definition can change under a feature that does not:
 * a cell re-typed on a sheet, a Proxy replaced on the linked object, a
 * function stored on it.  No signal reaches the feature for those, and an
 * observer per feature on every linked object would be far too heavy, so the
 * three places a definition can change bump one process-wide counter and a
 * resolved chain remembers what it was built against.  A hook call then
 * compares one integer.  docs/ProxyChain.md sec 2.4.
 */
namespace ProxyChain
{
/// The current generation.
AppExport unsigned long generation();
/// A hook definition somewhere may have changed: every resolved chain in the
/// process rebuilds on its next use.
AppExport void bump();
}  // namespace ProxyChain

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
 *
 * Beside the Proxy sits the CHAIN: the objects of the owner's ProxyExp list,
 * each resolved for the hook's `exp` name and called before the Proxy, the
 * first that answers stopping the walk -- an effect like multiple
 * inheritance, method resolution left to right.  The list is empty for nearly
 * every object in a document, and the whole of it then costs one empty()
 * test.  docs/ProxyChain.md sec 2.
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

    /** The objects extending this Proxy's hooks, in chain order
     *
     * The owner hands over its ProxyExp list: when it changes, when the
     * document is restored (the links resolve late), and at construction.
     * Every resolved chain is dropped; the next call to a hook rebuilds that
     * hook's, and only that hook's.
     */
    void setHookExtensions(std::vector<App::DocumentObject*> objs);

    /// Whether anything extends this Proxy at all.
    bool hasHookExtensions() const
    {
        return !_expObjects.empty();
    }

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
     * Handled stops the chain.  NotHandled is the caller's cue to fall
     * through to the C++ base.  Failed means a Python error was reported and
     * the caller should answer with whatever this hook says an error means --
     * which is not always what "not handled" means.
     *
     * A notify hook (the table's flag) is the exception to "the first that
     * answers wins": nothing reads its result, so every element is called,
     * and only an explicit True from a chain element stops the walk before
     * the Proxy.
     */
    template<class Decode, class... Args>
    PyHookState callHook(int hook, Decode&& decode, Args&&... args) const
    {
        const PyHookDef& def = _hookTable[hook];
        HookSlot& slot = _hooks[hook];
        if (!_expObjects.empty()) {
            ensureChain(hook);
        }
        if (slot.chain.empty() && slot.proxy.isNone()) {
            return PyHookState::NotHandled;
        }
        // The guard is one flag per hook and covers the whole chain, as it
        // covered the one Proxy call before; an element opts out of it with
        // __allow_recursive_<name> on the object its callable was found on.
        const bool nested = def.guarded && slot.calling;
        CallGuard guard(slot.calling);

        Base::PyGILStateLocker lock;
        PyHookState state = PyHookState::NotHandled;
        // By index, with the entry copied: a callable is free to do anything,
        // including something that rebuilds this very chain underneath us.  A
        // copy is one reference count against a Python call.
        for (std::size_t i = 0; i < slot.chain.size(); ++i) {
            const ChainEntry entry = slot.chain[i];
            if (nested && !entry.allowRecursive) {
                continue;
            }
            bool stop = false;
            PyHookState one = callOne(hook, def, entry.callable, false, stop, decode, args...);
            if (one != PyHookState::NotHandled) {
                state = one;
            }
            if (one == PyHookState::Failed || stop
                || (!def.notify && one == PyHookState::Handled)) {
                return state;
            }
        }
        if (!slot.proxy.isNone() && !(nested && !slot.allowRecursive)) {
            const Py::Object proxy = slot.proxy;
            bool stop = false;
            PyHookState one = callOne(hook, def, proxy, true, stop, decode, args...);
            if (one != PyHookState::NotHandled) {
                state = one;
            }
        }
        return state;
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

    /** Record what this hook's chain resolves to now
     *
     * Called by the side that WATCHES a hook, right after it has run it, so
     * that chainDefinitionChanged() below has something to compare against.
     * Only App's execute is watched: an expViewGetIcon re-typed on the same
     * sheet must not rebuild geometry.  docs/ProxyChain.md sec 4.5.
     */
    void snapshotChain(int hook) const;

    /** Whether THIS feature's definition of the hook has moved since then
     *
     * A chain element's definition can change with nothing on the feature
     * touched and no revision moving anywhere: a Spreadsheet::Sheet pins
     * getRevision() to 0 on purpose, and a function stored from Python is no
     * property at all.  So a value comparison, gated by the generation
     * counter, in the shape the expression engine already uses -- a cheap
     * "something moved somewhere" test first, and only then the fine one.
     *
     * Empty ProxyExp: one empty() test, which is nearly every object.
     * Generation unchanged: one integer.  Otherwise the chain is re-resolved
     * -- which the next call was going to do anyway -- and the resolved
     * callables are compared by identity against the snapshot, so that an
     * UNRELATED cell of the same sheet moves nothing.  docs/ProxyChain.md 4.5.
     */
    bool chainDefinitionChanged(int hook) const;

private:
    /// One resolved ProxyExp element for one hook.
    struct ChainEntry
    {
        Py::Object callable;
        /** What the callable IS, for comparing one resolution against the next
         *
         * `__func__` where the attribute has one, else the callable itself.
         * FC_PY_GetCallable is PyObject_GetAttrString, so the Proxy path hands
         * back a freshly built bound method on every access and raw identity
         * would differ forever; the underlying function is the stable thing.
         * The sheet path returns the cell's stored callable and is already
         * stable.  docs/ProxyChain.md sec 4.5.
         */
        Py::Object identity;
        /// __allow_recursive_<expName>, read from the object the callable was
        /// found on -- the linked object's Proxy, or the object itself.
        bool allowRecursive {false};
    };

    struct HookSlot
    {
        Py::Object proxy;            ///< the callable found on the Proxy
        std::vector<ChainEntry> chain;  ///< the ProxyExp elements, in order
        bool chainValid {false};     ///< the chain is resolved and current
        bool calling {false};        ///< inside a call, for the recursion guard
        bool allowRecursive {false}; ///< __allow_recursive_<hook> said so
        /// The chain's identities as of the last snapshotChain(); only a
        /// watched hook ever carries one.  Kept beside the chain rather than
        /// in it because a generation bump clears the chain and this has to
        /// survive that -- it is what the rebuilt chain is compared against.
        std::vector<Py::Object> snapshot;
        unsigned long snapshotGeneration {0};
        bool snapshotValid {false};
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

    /// One element of the chain, or the Proxy itself: the call, the decode and
    /// the error policy.  Arguments are passed as lvalues, never forwarded --
    /// the same pack serves every element of the chain.
    template<class Decode, class... Args>
    PyHookState callOne(int hook, const PyHookDef& def, const Py::Object& callable,
                        bool isProxy, bool& stop, Decode& decode, Args&... args) const
    {
        try {
            Py::Object res = callPy(hook, def, callable, isProxy, args...);
            if (def.notify) {
                // an element saying True has consumed the notification; the
                // Proxy is last, so its own answer decides nothing
                stop = !isProxy && res.isTrue();
                decode(res);
                return PyHookState::Handled;
            }
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

    template<class... Args>
    Py::Object callPy(int hook, const PyHookDef& def, const Py::Object& callable,
                      bool isProxy, Args&... args) const
    {
        // A chain element is a separate object serving possibly many features,
        // so it is always told which one it is extending -- even for the hooks
        // whose Proxy method is passed nothing, its own self being the proxy.
        // The __object__ form is the Proxy's alone.
        const bool withSelf = !isProxy || def.self == PyHookSelf::Always
            || (def.self == PyHookSelf::Modern && !_has__object__);
        const std::size_t count = sizeof...(Args) + (withSelf ? 1U : 0U);
        if (count == 0) {
            return Base::pyCall(callable.ptr());
        }
        Py::Tuple tuple(count);
        std::size_t i = 0;
        if (withSelf) {
            tuple.setItem(i++, hookSelf(hook));
        }
        // a comma fold: left to right, which is the argument order
        ((void)tuple.setItem(i++, pyHookArg(args)), ...);
        return Base::pyCall(callable.ptr(), tuple.ptr());
    }

    /// Report, rethrow or throw, per the hook's error policy.  Out of line so
    /// callHook stays small at every instantiation.
    static void reportHookError(const PyHookDef& def);

    /// Bring one hook's chain up to date: rebuild everything if a definition
    /// changed anywhere in the process (the generation counter), then resolve
    /// this hook if it has not been resolved yet.  Out of line -- it runs once
    /// per hook per edit, against thousands of calls.
    void ensureChain(int hook) const;
    void resolveChain(int hook, HookSlot& slot) const;
    /// The snapshot itself, with the GIL already held.
    void takeSnapshot(HookSlot& slot) const;

    const PyHookDef* _hookTable;
    mutable std::vector<HookSlot> _hooks;
    /// The ProxyExp list, in order.  Empty for nearly every object.
    std::vector<App::DocumentObject*> _expObjects;
    /// What ProxyChain::generation() was when the chains were last resolved.
    mutable unsigned long _chainGeneration {0};
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

/** An integer result, with the hook's own value for "not handled"
 *
 * These hooks answer "not mine" with a number rather than with an exception:
 * -2 for the element-visibility hooks, -1 for canLoadPartial.  The number
 * travels back to the caller as an ordinary integer, exactly as it did -- and
 * it is also what moves the chain on to the next element, which is why the
 * sentinel has to be named here rather than only at the caller.
 */
inline auto pyHookDecodeInt(int& out, int notHandled)
{
    return [&out, notHandled](const Py::Object& res) {
        out = static_cast<int>(Py::Int(res));
        return out != notHandled;
    };
}

}  // namespace App

#endif  // APP_FEATUREPYTHONHOOK_H

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
#include <Base/Parameter.h>
#include <Base/PyObjectBase.h>

#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentObjectPy.h"
#include "Expression.h"
#include "ExpressionEvaluator.h"

#ifdef FC_EXPR_IMAGE_HOST
#include "ExpressionImageHost.h"
#endif
#ifdef FC_EXPR_PYODIDE_HOST
#include "ExpressionPyodide.h"
#endif

// The FreeCAD.ExpressionSandbox module: drive the evaluation switch-over
// from Python.  Its reason to exist is the compatibility gate -- the
// corpus regression evaluates every stored expression BOTH ways and
// compares -- and it doubles as the debugging handle for the routing
// preference.

using namespace App;

namespace
{

App::DocumentObject* ownerArg(PyObject* obj)
{
    if (!obj || obj == Py_None)
        return nullptr;
    if (PyObject_TypeCheck(obj, &DocumentObjectPy::Type))
        return static_cast<DocumentObjectPy*>(obj)->getDocumentObjectPtr();
    PyErr_SetString(PyExc_TypeError, "expected a document object or None");
    return nullptr;
}

ParameterGrp::handle sandboxParams()
{
    return GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Sandbox");
}

PyObject* routedFunc(PyObject*, PyObject*)
{
    return PyBool_FromLong(ExpressionSandbox::evaluationRouted());
}

PyObject* setRoutingFunc(PyObject*, PyObject* args)
{
    PyObject* on = nullptr;
    if (!PyArg_ParseTuple(args, "O", &on))
        return nullptr;
    sandboxParams()->SetBool("Evaluate", PyObject_IsTrue(on) == 1);
    Py_Return;
}

/// Shared body: parse under `owner`, evaluate either through the router
/// (which decides) or explicitly in-process.
PyObject* evalCommon(PyObject* args, bool forceNative)
{
    PyObject* pyOwner = nullptr;
    const char* src = nullptr;
    int options = 0;
    if (!PyArg_ParseTuple(args, "Os|i", &pyOwner, &src, &options))
        return nullptr;
    if (pyOwner != Py_None && !PyObject_TypeCheck(pyOwner, &DocumentObjectPy::Type)) {
        PyErr_SetString(PyExc_TypeError, "expected a document object or None");
        return nullptr;
    }
    App::DocumentObject* owner = ownerArg(pyOwner);
    PY_TRY
    {
        // Python mode is a lexer start state as well as an eval option,
        // so it has to reach the parse -- the same rule the router
        // applies when it re-parses in the image.
        auto expr = Expression::parse(
                owner, src, 0, false,
                (options & Expression::OptionPythonMode) != 0);
        if (!expr)
            Py_Return;
        if (forceNative)
            return Py::new_reference_to(expr->getPyValue(options));
        return ExpressionSandbox::evaluatePy(expr.get(), options);
    }
    PY_CATCH
}

PyObject* evaluateFunc(PyObject*, PyObject* args)
{
    return evalCommon(args, false);
}

PyObject* evaluateNativeFunc(PyObject*, PyObject* args)
{
    return evalCommon(args, true);
}

PyObject* evalCountFunc(PyObject*, PyObject*)
{
#ifdef FC_EXPR_IMAGE_HOST
    return PyLong_FromUnsignedLongLong(
        ExpressionSandbox::ImageHost::instance().evalCount());
#else
    return PyLong_FromLong(0);
#endif
}

PyObject* statsFunc(PyObject*, PyObject*)
{
    PyObject* dict = PyDict_New();
    if (!dict)
        return nullptr;
    PyObject* ops = PyDict_New();
    if (!ops) {
        Py_DECREF(dict);
        return nullptr;
    }
    auto put = [](PyObject* target, const char* key, PyObject* value) {
        if (!value)
            return false;
        int rc = PyDict_SetItemString(target, key, value);
        Py_DECREF(value);
        return rc == 0;
    };
    bool ok = true;
#ifdef FC_EXPR_IMAGE_HOST
    ExpressionSandbox::ImageHost::Stats s = ExpressionSandbox::ImageHost::instance().stats();
    ok = put(dict, "evals", PyLong_FromSize_t(s.evals))
        && put(dict, "handles", PyLong_FromSize_t(s.handles));
    for (const auto& [name, count] : s.ops)
        ok = ok && put(ops, name.c_str(), PyLong_FromSize_t(count));
#else
    ok = put(dict, "evals", PyLong_FromLong(0)) && put(dict, "handles", PyLong_FromLong(0));
#endif
    if (!ok || PyDict_SetItemString(dict, "ops", ops) != 0) {
        Py_DECREF(ops);
        Py_DECREF(dict);
        return nullptr;
    }
    Py_DECREF(ops);
    return dict;
}

PyObject* resetStatsFunc(PyObject*, PyObject*)
{
#ifdef FC_EXPR_IMAGE_HOST
    ExpressionSandbox::ImageHost::instance().resetStats();
#endif
    Py_RETURN_NONE;
}

PyObject* availableFunc(PyObject*, PyObject*)
{
#ifdef FC_EXPR_IMAGE_HOST
    return PyBool_FromLong(ExpressionSandbox::ImageHost::instance().available());
#else
    Py_RETURN_FALSE;
#endif
}

PyObject* imageInfoFunc(PyObject*, PyObject*)
{
#ifdef FC_EXPR_IMAGE_HOST
    auto loc = ExpressionSandbox::ImageHost::instance().location();
    Py::Dict info;
    info.setItem("image", Py::String(loc.image));
    info.setItem("stdlib", Py::String(loc.stdlib));
    info.setItem("cache", Py::String(loc.cache));
    info.setItem("packages", Py::String(loc.packages));
    info.setItem("runtime", Py::String(ExpressionSandbox::ImageHost::instance().runtime()));
    info.setItem("host", Py::Boolean(true));
    return Py::new_reference_to(info);
#else
    Py::Dict info;
    info.setItem("image", Py::String(""));
    info.setItem("stdlib", Py::String(""));
    info.setItem("cache", Py::String(""));
    info.setItem("packages", Py::String(""));
    info.setItem("runtime", Py::String(""));
    info.setItem("host", Py::Boolean(false));
    return Py::new_reference_to(info);
#endif
}

PyObject* resetFunc(PyObject*, PyObject*)
{
#ifdef FC_EXPR_IMAGE_HOST
    ExpressionSandbox::ImageHost::instance().reset();
#endif
    Py_Return;
}

// ---- the pyodide bootstrap facts (docs/PyodideHost.md sec 12), for the
// host installer freecad.pyodide: what this binary agrees to run, and
// where it keeps things.

PyObject* pyodideReleasesFunc(PyObject*, PyObject*)
{
    Py::List list;
#ifdef FC_EXPR_PYODIDE_HOST
    for (const auto& r : ExpressionSandbox::Pyodide::releases()) {
        Py::Dict d;
        d.setItem("version", Py::String(r.version));
        d.setItem("abi", Py::String(r.abi));
        d.setItem("python", Py::String(r.python));
        Py::Dict files;
        for (const auto& f : r.files)
            files.setItem(f.name.c_str(), Py::String(f.sha256));
        d.setItem("files", files);
        d.setItem("core_tarball", Py::String(r.coreTarball));
        d.setItem("core_sha256", Py::String(r.coreSha256));
        list.append(d);
    }
#endif
    return Py::new_reference_to(list);
}

PyObject* pyodideLayoutFunc(PyObject*, PyObject*)
{
    Py::Dict d;
#ifdef FC_EXPR_PYODIDE_HOST
    auto l = ExpressionSandbox::Pyodide::layout();
    d.setItem("user_dir", Py::String(l.userDir));
    d.setItem("packages", Py::String(l.packages));
    d.setItem("manifest", Py::String(l.manifest));
    d.setItem("wheel_dir", Py::String(l.wheelDir));
    d.setItem("current", Py::String(l.current));
    Py::List installed;
    for (const auto& v : l.installed)
        installed.append(Py::String(v));
    d.setItem("installed", installed);
    Py::Dict wheels;
    for (const auto& w : l.wheels)
        wheels.setItem(w.first.c_str(), Py::String(w.second));
    d.setItem("wheels", wheels);
#endif
    return Py::new_reference_to(d);
}

PyObject* pyodideVerifyFunc(PyObject*, PyObject* args)
{
    const char* dir = nullptr;
    if (!PyArg_ParseTuple(args, "s", &dir))
        return nullptr;
#ifdef FC_EXPR_PYODIDE_HOST
    return Py::new_reference_to(Py::String(ExpressionSandbox::Pyodide::verifyDirectory(dir)));
#else
    return Py::new_reference_to(Py::String("this build has no pyodide host"));
#endif
}

PyObject* pyodideAbiFunc(PyObject*, PyObject* args)
{
    const char* dir = nullptr;
    if (!PyArg_ParseTuple(args, "s", &dir))
        return nullptr;
#ifdef FC_EXPR_PYODIDE_HOST
    return Py::new_reference_to(Py::String(ExpressionSandbox::Pyodide::directoryAbi(dir)));
#else
    return Py::new_reference_to(Py::String(""));
#endif
}

PyMethodDef Methods[] = {
    {"routed", routedFunc, METH_NOARGS,
     "routed() -> bool -- whether evaluation is currently routed through"
     " the sandbox image (preference set AND an image available)."},
    {"setRouting", setRoutingFunc, METH_VARARGS,
     "setRouting(bool) -- set BaseApp/Preferences/Expression/Sandbox:"
     "Evaluate.  Default OFF."},
    {"available", availableFunc, METH_NOARGS,
     "available() -> bool -- whether a sandbox image could be loaded."},
    {"imageInfo", imageInfoFunc, METH_NOARGS,
     "imageInfo() -> dict -- where the guest (the fcx_image wheel, or the"
     " reference image), its stdlib and its compiled-module cache resolve"
     " to, which runtime ('pyodide' or 'wasi') is selected, and whether"
     " this build has a sandbox host at all.  What to look at when"
     " available() is False."},
    {"evaluate", evaluateFunc, METH_VARARGS,
     "evaluate(owner, source, options=0) -> value -- evaluate one"
     " expression the way the desktop would right now (routed or not,"
     " per the preference).  `options` is an EvalOption mask; pass"
     " OptionCallFrame|OptionPythonMode to evaluate it the way a"
     " spreadsheet cell in python mode is evaluated."},
    {"evaluateNative", evaluateNativeFunc, METH_VARARGS,
     "evaluateNative(owner, source, options=0) -> value -- evaluate"
     " in-process, whatever the preference says.  The comparison half"
     " of the compatibility gate."},
    {"evalCount", evalCountFunc, METH_NOARGS,
     "evalCount() -> int -- evaluations that have crossed into the image."},
    {"stats", statsFunc, METH_NOARGS,
     "stats() -> dict -- bridge traffic since startup or resetStats():"
     " {'evals': n, 'handles': n minted, 'ops': {wire op name: count}}."
     "  How many guest->host hops (read_prop, get_attr, call, get_item,"
     " len, release, pkg.missing) a workload really makes; what prices a"
     " snapshot op before one is designed."},
    {"resetStats", resetStatsFunc, METH_NOARGS,
     "resetStats() -- zero the stats() counters (handles stay live)."},
    {"reset", resetFunc, METH_NOARGS,
     "reset() -- drop the live sandbox instance; the next evaluation starts"
     " a fresh one (after installing a package, so the guest boots with it)."},
    {"pyodideReleases", pyodideReleasesFunc, METH_NOARGS,
     "pyodideReleases() -> list of dicts -- the pyodide versions this build"
     " agrees to run, each with the sha256 of every runtime file, the ABI"
     " tag and the GitHub core tarball name and hash.  What the installer"
     " (freecad.pyodide) verifies against."},
    {"pyodideLayout", pyodideLayoutFunc, METH_NOARGS,
     "pyodideLayout() -> dict -- where the bootstrap keeps runtimes and"
     " packages (user_dir, packages, manifest, current, installed) and"
     " the fcx_image wheels found per ABI tag."},
    {"pyodideVerify", pyodideVerifyFunc, METH_VARARGS,
     "pyodideVerify(dir) -> str -- '' when the runtime directory is a"
     " pinned version with every file's sha256 as pinned, else the reason."},
    {"pyodideAbi", pyodideAbiFunc, METH_VARARGS,
     "pyodideAbi(dir) -> str -- the ABI tag of a runtime directory"
     " ('2026_0'), from the pinned table or its lock file."},
    {nullptr, nullptr, 0, nullptr},
};

}  // namespace

namespace App
{
namespace ExpressionSandbox
{

void initPyModule(PyObject* appModule)
{
    static struct PyModuleDef moduleDef = {
        PyModuleDef_HEAD_INIT,
        "ExpressionSandbox", "Expression sandbox evaluation", -1,
        Methods,
        nullptr, nullptr, nullptr, nullptr
    };
    PyObject* module = PyModule_Create(&moduleDef);
    // The eval-option mask, so a caller can reproduce exactly how a
    // given site evaluates (the spreadsheet uses both of these).
    PyModule_AddIntConstant(module, "OptionCallFrame",
                            Expression::OptionCallFrame);
    PyModule_AddIntConstant(module, "OptionPythonMode",
                            Expression::OptionPythonMode);
    Py_INCREF(module);
    PyModule_AddObject(appModule, "ExpressionSandbox", module);
}

}  // namespace ExpressionSandbox
}  // namespace App

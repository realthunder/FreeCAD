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
    info.setItem("host", Py::Boolean(true));
    return Py::new_reference_to(info);
#else
    Py::Dict info;
    info.setItem("image", Py::String(""));
    info.setItem("stdlib", Py::String(""));
    info.setItem("cache", Py::String(""));
    info.setItem("host", Py::Boolean(false));
    return Py::new_reference_to(info);
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
     "imageInfo() -> dict -- where the image, its stdlib slice and its"
     " compiled-module cache resolve to, and whether this build has a"
     " wasmtime host at all.  What to look at when available() is False."},
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

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
    if (!PyArg_ParseTuple(args, "Os", &pyOwner, &src))
        return nullptr;
    if (pyOwner != Py_None && !PyObject_TypeCheck(pyOwner, &DocumentObjectPy::Type)) {
        PyErr_SetString(PyExc_TypeError, "expected a document object or None");
        return nullptr;
    }
    App::DocumentObject* owner = ownerArg(pyOwner);
    PY_TRY
    {
        auto expr = Expression::parse(owner, src);
        if (!expr)
            Py_Return;
        if (forceNative)
            return Py::new_reference_to(expr->getPyValue());
        return ExpressionSandbox::evaluatePy(expr.get());
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

PyMethodDef Methods[] = {
    {"routed", routedFunc, METH_NOARGS,
     "routed() -> bool -- whether evaluation is currently routed through"
     " the sandbox image (preference set AND an image available)."},
    {"setRouting", setRoutingFunc, METH_VARARGS,
     "setRouting(bool) -- set BaseApp/Preferences/Expression/Sandbox:"
     "Evaluate.  Default OFF."},
    {"available", availableFunc, METH_NOARGS,
     "available() -> bool -- whether a sandbox image could be loaded."},
    {"evaluate", evaluateFunc, METH_VARARGS,
     "evaluate(owner, source) -> value -- evaluate one expression the way"
     " the desktop would right now (routed or not, per the preference)."},
    {"evaluateNative", evaluateNativeFunc, METH_VARARGS,
     "evaluateNative(owner, source) -> value -- evaluate in-process,"
     " whatever the preference says.  The comparison half of the"
     " compatibility gate."},
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
    Py_INCREF(module);
    PyModule_AddObject(appModule, "ExpressionSandbox", module);
}

}  // namespace ExpressionSandbox
}  // namespace App

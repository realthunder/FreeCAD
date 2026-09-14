/****************************************************************************
 *   Copyright (c) 2018 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <sstream>
#endif

#ifdef FC_EXPR_IMAGE
// The sandbox image (docs/Sandbox.md 7.17 D1): the owner is the adapter
// object of ONE evaluation, not a host binding, so a function object
// records that evaluation where the host holds the owner's Python face,
// and is dead once the evaluation has returned.
# include <App/ExpressionImage/FcxDocument.h>
#else
# include "Application.h"
# include <App/DocumentObject.h>
#endif
#include <App/ExpressionParser.h>
#include <App/ExpressionPy.h>
#include <App/ExpressionPy.cpp>

using namespace App;

bool ExpressionPy::ownerAlive() const
{
#ifdef FC_EXPR_IMAGE
    return Fcx::EvalTransaction::alive(evalSerial);
#else
    return pyOwner->isValid();
#endif
}

// returns a string which represent the object e.g. when printed in python
std::string ExpressionPy::representation(void) const
{
    if (!ownerAlive()){
        PyErr_Format(PyExc_ReferenceError, "Owner document object expired");
        return NULL;
    }
    auto expr = dynamic_cast<CallableExpression*>(getExpressionPtr());
    if(expr && expr->getName().size())
        return std::string("<Function ") + expr->getName() + ">";
    return "<Function>";
}

static PyObject *ExpressionPy_Call( PyObject *self, PyObject *args, PyObject *kw ) {
    assert(PyObject_TypeCheck(self,&ExpressionPy::Type));
    return static_cast<ExpressionPy*>(self)->__call__(args,kw);
}

int ExpressionPy::initialization() {
    if(!Type.tp_call) 
        Type.tp_call = ExpressionPy_Call;
#ifdef FC_EXPR_IMAGE
    auto tx = Fcx::EvalTransaction::current();
    const uint64_t library = Fcx::EvalTransaction::libraryBuild();
    evalSerial = library ? library : (tx ? tx->serial() : 0);
#else
    pyOwner = static_cast<PyObjectBase*>(getExpressionPtr()->getOwner()->getPyObject());
#endif
    return 1;
}

int ExpressionPy::finalization() {
#ifndef FC_EXPR_IMAGE
    Py_DECREF(pyOwner);
#endif
    delete getExpressionPtr();
    return 1;
}

PyObject *ExpressionPy::__call__(PyObject *args, PyObject *kwds) const{
    if (!ownerAlive()){
        PyErr_Format(PyExc_ReferenceError, "Owner document object expired");
        return NULL;
    }
    PY_TRY {
        auto expr = dynamic_cast<CallableExpression*>(getExpressionPtr());
        if(expr)
            return Py::new_reference_to(expr->evaluate(args,kwds));
        Py_Return;
    }PY_CATCH
}

Py::String ExpressionPy::get__doc__() const {
    auto expr = dynamic_cast<CallableExpression*>(getExpressionPtr());
    std::string doc;
    if(expr) {
        doc = expr->getDocString();
        if(doc.empty())
            doc = expr->getName();
    }
    return Py::String(doc);
}

PyObject *ExpressionPy::getCustomAttributes(const char* /*attr*/) const
{
    return 0;
}

int ExpressionPy::setCustomAttributes(const char* /*attr*/, PyObject * /*obj*/)
{
    return 0;
}

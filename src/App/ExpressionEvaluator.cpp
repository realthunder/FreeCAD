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

#include <limits>

#include <Python.h>
#include <sstream>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Base/Parameter.h>

#include "Application.h"
#include "Expression.h"
#include "ExpressionEvaluator.h"
#include "ObjectIdentifier.h"

#ifdef FC_EXPR_IMAGE_HOST
#include "ExpressionImageHost.h"
#endif

FC_LOG_LEVEL_INIT("Expression", true, true)

using namespace App;

namespace
{

#ifdef FC_EXPR_IMAGE_HOST

/// One image evaluation is in flight on this thread: anything the
/// bindings pack resolves while it runs is the HOST half of that same
/// evaluation and must not try to re-enter the image.
thread_local bool inImageEvaluation = false;

struct ReentryGuard
{
    ReentryGuard()
    {
        inImageEvaluation = true;
    }
    ~ReentryGuard()
    {
        inImageEvaluation = false;
    }
};

/// The image reports a Python exception type name; raise the host
/// exception that carries the same meaning.  The MESSAGE is passed
/// through unchanged -- parity with the native engine's text (down to
/// the ParserError wording) is verified, and rewriting it here would
/// break it.
[[noreturn]] void raiseImageError(const std::string& excType,
                                  const std::string& message)
{
    std::string msg = message.empty() ? excType : message;
    if (excType == "ParserError")
        throw Base::ParserError(msg);
    if (excType == "TypeError")
        throw Base::TypeError(msg);
    if (excType == "ValueError")
        throw Base::ValueError(msg);
    throw Base::RuntimeError(msg);
}

/// Returns a NEW reference, or throws.
PyObject* evaluateInImage(const Expression* expr, int options)
{
    ReentryGuard guard;

    auto& host = ExpressionSandbox::ImageHost::instance();
    // Ship every bit of an in-memory literal: toString() prints 15
    // significant digits for display and persistence, and a 16- or
    // 17-digit literal would reach the guest rounded while the native
    // evaluator keeps the full double.  Found by TestSpreadsheet with
    // routing on (1.000000000000001 -> 1).
    struct WirePrecision
    {
        int saved = expressionNumberPrecision();
        WirePrecision()
        {
            expressionNumberPrecision() = std::numeric_limits<double>::max_digits10;
        }
        ~WirePrecision()
        {
            expressionNumberPrecision() = saved;
        }
    };
    std::string source;
    {
        WirePrecision wire;
        source = expr->toString();
    }
    auto result = host.evalExpression(expr->getOwner(), source, expr, options);
    // decode BEFORE dropping the transaction's handles: a result that
    // is itself a host object resolves against the live table
    PyObject* value = host.decodeResult(result);
    host.clearHandles();

    if (!result.ok) {
        if (value)
            Py_DECREF(value);
        raiseImageError(result.excType, result.message);
    }
    if (!value)
        throw Base::RuntimeError("sandboxed evaluation returned a value the "
                                 "host cannot decode");
    return value;
}

#endif  // FC_EXPR_IMAGE_HOST

}  // namespace

namespace
{
ParameterGrp::handle sandboxParams()
{
    static ParameterGrp::handle handle;
    if (!handle)
        handle = GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Expression/Sandbox");
    return handle;
}
}  // namespace

ExpressionSandbox::SandboxStatus ExpressionSandbox::sandboxStatus()
{
    SandboxStatus status;
    status.enabled = sandboxParams()->GetBool("Evaluate", false);
#ifdef FC_EXPR_IMAGE_HOST
    status.hostBuilt = true;
    auto where = ImageHost::instance().location();
    status.image = where.image;
    status.stdlib = where.stdlib;
    status.imagePresent = Base::FileInfo(status.image).isFile()
            && Base::FileInfo(status.stdlib).isDir();
    status.runtime = ImageHost::instance().runtime();
#endif
    return status;
}

void ExpressionSandbox::resetSandbox()
{
#ifdef FC_EXPR_IMAGE_HOST
    ImageHost::instance().reset();
#endif
}

void ExpressionSandbox::setEvaluationRouted(bool on)
{
    sandboxParams()->SetBool("Evaluate", on);
}

bool ExpressionSandbox::proxyRestoreRouted()
{
#ifdef FC_EXPR_IMAGE_HOST
    return sandboxParams()->GetBool("Evaluate", false);
#else
    return false;
#endif
}

bool ExpressionSandbox::evaluationRouted()
{
#ifdef FC_EXPR_IMAGE_HOST
    if (!sandboxParams()->GetBool("Evaluate", false))
        return false;
    return ImageHost::instance().available();
#else
    return false;
#endif
}

namespace
{
/// True when this evaluation should cross into the image.
///
/// No eval option refuses any more.  Python mode used to: it looked
/// like "a different language", but it is a lexer start state plus a
/// name-binding rule, both of which live in the core -- and the core is
/// what runs in the image.  It rides across in the options mask, and
/// its builtins are then the IMAGE's builtins under WASI, which is the
/// point: python-mode cells are exactly the ones in-process evaluation
/// could never confine.
bool routeThis(const App::Expression* expr)
{
#ifdef FC_EXPR_IMAGE_HOST
    return !inImageEvaluation && expr->getOwner()
            && ExpressionSandbox::evaluationRouted();
#else
    (void)expr;
    return false;
#endif
}
}  // namespace

App::any ExpressionSandbox::evaluate(const Expression* expr, int options)
{
    if (!expr)
        return App::any();
#ifdef FC_EXPR_IMAGE_HOST
    if (routeThis(expr)) {
        Base::PyGILStateLocker lock;
        Py::Object held(evaluateInImage(expr, options), true);
        return pyObjectToAny(held);
    }
#else
    (void)routeThis;
#endif
    return expr->getValueAsAny(options);
}

PyObject* ExpressionSandbox::evaluatePy(const Expression* expr, int options)
{
    if (!expr)
        Py_RETURN_NONE;
#ifdef FC_EXPR_IMAGE_HOST
    if (routeThis(expr)) {
        Base::PyGILStateLocker lock;
        return evaluateInImage(expr, options);
    }
#endif
    Base::PyGILStateLocker lock;
    return Py::new_reference_to(expr->getPyValue(options));
}

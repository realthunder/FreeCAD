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

#ifndef APP_EXPRESSION_EVALUATOR_H
#define APP_EXPRESSION_EVALUATOR_H

/* The evaluation switch-over (docs/ExpressionSandbox.md sec 4B,
 * docs/ExpressionImage.md "The evaluation switch-over"): the single
 * place a TOP-LEVEL stored expression is evaluated, so that turning the
 * sandbox on is one decision made in one place rather than a fallback
 * scattered through the engine.
 *
 * This is host-only and deliberately NOT part of ExpressionCore -- the
 * core compiles into the image itself and must never know that a
 * sandbox exists.  Callers are the places that evaluate a stored
 * expression as a whole (PropertyExpressionEngine, and later the
 * spreadsheet); nested nodes keep using Expression's own accessors.
 */

#include <FCConfig.h>

#include "ObjectIdentifier.h"

namespace App
{

class Expression;

namespace ExpressionSandbox
{

/** True when sandboxed evaluation is switched on AND possible: the
 * preference BaseApp/Preferences/Expression/Sandbox:Evaluate is set
 * (default OFF) and an image is available.  The corpus regression is
 * the gate to changing that default.
 */
AppExport bool evaluationRouted();

/** Evaluate a top-level expression: through the sandbox image when
 * routing is on and this expression qualifies, in-process otherwise.
 *
 * When routing is on there is NO silent fallback -- an expression the
 * image cannot evaluate fails, it does not quietly run in the host
 * (that would defeat the boundary).  The one exception is re-entrancy:
 * an evaluation triggered WHILE an image evaluation is in flight (a
 * bindings-pack resolve that recomputes something) runs natively,
 * because it is the host-side half of the outer sandboxed evaluation
 * by construction.
 */
AppExport App::any evaluate(const Expression* expr, int options = 0);

/** The Python-valued twin of evaluate(), for callers that keep the
 * value as a PyObject (the spreadsheet, the Python API).  Returns a NEW
 * reference; throws on failure exactly as evaluate() does.
 */
AppExport PyObject* evaluatePy(const Expression* expr, int options = 0);

/// Install the FreeCAD.ExpressionSandbox Python module.
void initPyModule(PyObject* appModule);

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_EVALUATOR_H

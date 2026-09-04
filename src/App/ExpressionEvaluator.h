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
 * expression as a whole (PropertyExpressionEngine and the spreadsheet's
 * PropertySheet::eval); nested nodes keep using Expression's own
 * accessors.
 */

#include <string>

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

/** What the user surface needs to say about confinement, without
 * paying for it.  `evaluationRouted()` asks the host whether an image
 * LOADS, which instantiates one (~20 ms warm, ~600 ms the first time a
 * compiled-module cache has to be made) -- far too much for drawing a
 * status bar.  These fields are all cheap: a preference read and two
 * stat() calls.
 *
 * `enabled` is the preference alone, so `enabled && !imagePresent` is a
 * real and reportable state: the user asked for confinement and is not
 * getting it.
 */
struct SandboxStatus
{
    /// This build embeds the wasm runtime (BUILD_EXPR_IMAGE_HOST).
    bool hostBuilt = false;
    /// An image and its stdlib slice exist where the host will look.
    bool imagePresent = false;
    /// The Evaluate preference, whatever the image situation is.
    bool enabled = false;
    /// Where the image was looked for (empty without a host).
    std::string image;
    /// Where the stdlib slice was looked for (empty without a host).
    std::string stdlib;
    /// The runtime the host would use ("pyodide" or "wasi"; empty
    /// without a host).  "pyodide" is the one FreeCAD can bootstrap
    /// (freecad.pyodide.install_runtime) when the image is missing.
    std::string runtime;

    /// Expressions actually run confined.
    bool confined() const
    {
        return hostBuilt && imagePresent && enabled;
    }
};

AppExport SandboxStatus sandboxStatus();

/// Drop the live sandbox instance; the next evaluation starts a fresh
/// one (after a package install, so the guest boots with it).  A no-op
/// without a host.
AppExport void resetSandbox();

/// Set the Evaluate preference.  Takes effect on the next evaluation --
/// there is nothing to restart.
AppExport void setEvaluationRouted(bool on);

/** True when a document object's saved Proxy (`<Python module=".."
 * class="..">`) is to be built in the sandbox guest at document open
 * (docs/Sandbox.md 7.6 mechanism item 2, sec 13): the Evaluate
 * preference alone, on a build with a host.  Unlike evaluationRouted()
 * this never instantiates an image -- the restore itself does, through
 * proxy_new, and when the guest cannot serve the module the property
 * FAILS CLOSED rather than importing a document-chosen module name
 * into the host.  Off, PropertyPythonObject::Restore imports natively
 * as it always has.
 */
AppExport bool proxyRestoreRouted();

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
 *
 * `options` is an App::Expression::EvalOption mask and crosses the
 * boundary with the source, so the image parses and walks under the
 * same options -- including OptionPythonMode, which a spreadsheet in
 * python mode passes for every cell.
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

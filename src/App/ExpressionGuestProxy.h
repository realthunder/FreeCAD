/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef APP_EXPRESSION_GUEST_PROXY_H
#define APP_EXPRESSION_GUEST_PROXY_H

/* The host stand-in for a scripted object's Proxy that lives in the
 * sandbox guest (rung 2, docs/Sandbox.md 7.6 G1c).
 *
 * The guest registers the instance and ships a descriptor
 * (FcxWire::TagGuestProxy: proxy id, module, class, the hook names the
 * class defines).  From it the host builds an instance of a per-class
 * heap type named after the guest class -- `__module__` and
 * `__class__.__name__` read as the guest's, so PropertyPythonObject
 * saves the same <Python module=".." class=".."> a native session would
 * -- whose hook attributes are forwarders: FeaturePythonImp finds
 * exactly the hooks the guest class has (FC_PY_GetCallable) and each
 * call crosses as ImageHost::proxyCall with the object as a handle.
 * Nothing else lives here: the instance state is the guest's.  A dying
 * stand-in queues its id for the guest to drop.
 *
 * Host-only (BUILD_EXPR_IMAGE_HOST), never part of ExpressionCore.
 */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <FCConfig.h>

typedef struct _object PyObject;

namespace App
{
class Document;
class DocumentObject;

namespace ExpressionSandbox
{

/** Build a stand-in from the guest's descriptor.  New reference;
 * nullptr with a Python error set.  Caller holds the GIL.
 */
AppExport PyObject* makeGuestProxy(const nlohmann::json& desc);

/// True when `obj` is a stand-in (an instance of the stand-in base type).
AppExport bool isGuestProxy(PyObject* obj);

/// The stand-in's guest proxy id; 0 when `obj` is not a stand-in.
AppExport uint64_t guestProxyId(PyObject* obj);

/** A forwarder bound to guest proxy `id`'s callable attribute `name`
 * (FcxWire::TagGuestMethod): calling it is a proxy_call, the object
 * among the arguments the owner.  New reference.
 */
AppExport PyObject* makeGuestMethod(uint64_t id, const std::string& name);

/** A function a routed evaluation left as its value (docs/Sandbox.md
 * 7.17 P3; FcxWire TagGuestFunction).  The function itself cannot leave
 * the guest -- its owner is that one evaluation -- so the stand-in keeps
 * what makes it again: the evaluation's `owner`, its `source` and eval
 * `options`.  Calling it is ImageHost::callFunction, an evaluation of its
 * own, so the body reads what is current at call time, as natively.  It
 * depends on no guest state and survives a reset.  `name` is the def's
 * (empty for a lambda), for the repr native gives.  New reference;
 * nullptr with a Python error set.  Caller holds the GIL.
 */
AppExport PyObject* makeRoutedFunction(const App::DocumentObject* owner,
                                       const std::string& source,
                                       int options,
                                       const std::string& name);

/// True for a routed function's stand-in, bound or not.
AppExport bool isRoutedFunction(PyObject* obj);

/// The def's name of a routed function; empty for a lambda or a non-stand-in.
AppExport std::string routedFunctionName(PyObject* obj);

/** The chain's binding (docs/ProxyChain.md 2.5, RULED 2026-09-13: "Run as
 * the feature's file"): a callable that calls routed function `func`
 * with its own arguments unchanged, running as the document object
 * `self` is -- or, for a view provider, the object it shows.  That object
 * is the one the call may write and its document the principal, so a
 * method linked from another file writes the feature as a same-file
 * write, with that file's grants.  New reference; nullptr with a Python
 * error set.  Caller holds the GIL.
 */
AppExport PyObject* bindRoutedFunction(PyObject* func, PyObject* self);

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_GUEST_PROXY_H

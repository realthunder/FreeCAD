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

#include <nlohmann/json.hpp>

#include <FCConfig.h>

typedef struct _object PyObject;

namespace App
{
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

/** The Restore route (docs/Sandbox.md 7.6 mechanism item 2): allocate
 * an instance of the guest's `module`.`cls` WITHOUT running __init__
 * (`proxy_new alloc` = `cls.__new__(cls)`, what PyType_GenericAlloc is
 * to a native restore) and return its stand-in, ready for `loads`.
 * The guest imports the module; nothing is imported on the host.  New
 * reference; nullptr with a Python error set -- the guest's own
 * exception when it is a builtin (ModuleNotFoundError, AttributeError),
 * else a RuntimeError naming it, or the image being unavailable -- and
 * the caller fails closed.  `owner` is the object being restored; its
 * document is the principal.  Caller holds the GIL.
 */
AppExport PyObject* restoreGuestProxy(const std::string& module,
                                      const std::string& cls,
                                      const App::DocumentObject* owner);

/** The construction dispatch (docs/Sandbox.md 7.6 G1d): a scripted
 * object class's host `__new__` calls this with the class and the
 * constructor arguments.  With routing on (the Evaluate preference)
 * and at least one argument, the class is constructed IN THE GUEST
 * (`proxy_new`: `cls(*args, **kwargs)` there; a document object first
 * argument is the owner its `obj.Proxy = self` installs the stand-in
 * on, None -- Draft's `Array(None)`, installed later by
 * addObject(attach=True) -- constructs with no owner) and the stand-in
 * is returned -- not an instance of `cls`, so Python skips the host
 * `__init__`.  None (new reference) when the construction is native:
 * routing off, or a bare `cls.__new__(cls)` (copy, pickle, a native
 * alloc).  nullptr with a Python error when the guest cannot construct
 * it (the module is not served, the class raised) -- fail closed, the
 * same as the Restore route.  Caller holds the GIL.
 */
AppExport PyObject* constructGuestProxy(PyObject* cls, PyObject* args, PyObject* kwargs);

/** A forwarder bound to guest proxy `id`'s callable attribute `name`
 * (FcxWire::TagGuestMethod): calling it is a proxy_call, the object
 * among the arguments the owner.  New reference.
 */
AppExport PyObject* makeGuestMethod(uint64_t id, const std::string& name);

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_GUEST_PROXY_H

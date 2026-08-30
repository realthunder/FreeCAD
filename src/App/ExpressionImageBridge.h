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

#ifndef APP_EXPRESSION_IMAGE_BRIDGE_H
#define APP_EXPRESSION_IMAGE_BRIDGE_H

/* Host side of the image->host bridge ops (docs/ExpressionImage.md,
 * wire shapes in ExpressionImage/FcxWire.h): the per-transaction handle
 * table over live host PyObjects, the host value marshal, and the op
 * dispatcher.  Every op resolves its permission through
 * App::ExpressionSecurity before touching the object, so grants apply
 * to sandboxed evaluation exactly as they do to the native path.
 *
 * Host-only (BUILD_EXPR_IMAGE_HOST), never part of ExpressionCore.
 */

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <FCConfig.h>

typedef struct _object PyObject;

namespace App
{
namespace ExpressionSandbox
{

/** Live host Python objects handed to the image as {"t":"h"} wire
 * handles.  One table per image instance, cleared per recompute
 * transaction; the table owns one reference per entry.  Callers hold
 * the GIL for every member that touches refcounts.
 */
class AppExport HandleTable
{
public:
    /// Register an object (increfs); returns its wire id.
    uint64_t add(PyObject* obj);
    /// Borrowed reference, nullptr when stale/released.
    PyObject* get(uint64_t id) const;
    /// Drop one entry (image-proxy __del__).
    void release(uint64_t id);
    /// Drop everything (end of transaction).
    void clear();
    std::size_t size() const
    {
        return objects.size();
    }

private:
    std::unordered_map<uint64_t, PyObject*> objects;
    uint64_t nextId = 1;
};

/** Host Python object -> wire value.  Objects outside the by-value set
 * (and values the wire cannot carry, e.g. ints beyond 64 bits) become
 * handles in `table`; encoding never fails.  Caller holds the GIL.
 */
AppExport nlohmann::json encodeHostValue(HandleTable& table, PyObject* obj);

/** Wire value -> new reference; handles resolve through `table` (a
 * stale id raises ReferenceError).  nullptr + Python error on failure.
 * Caller holds the GIL.
 */
AppExport PyObject* decodeHostValue(const HandleTable& table, const nlohmann::json& v);

/** One bridge op request -> reply (both per FcxWire.h).  Acquires the
 * GIL itself; catches everything -- the reply is always well-formed.
 */
AppExport nlohmann::json dispatchHostOp(HandleTable& table, const nlohmann::json& req);

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_IMAGE_BRIDGE_H

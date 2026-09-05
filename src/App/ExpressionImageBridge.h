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
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <FCConfig.h>

typedef struct _object PyObject;
typedef struct _typeobject PyTypeObject;

namespace App
{
namespace ExpressionSandbox
{

/** The closed member set the dispatcher serves, generated from the
 * <Sandbox tier=.../> annotations in the binding XMLs into
 * FcxDispatch.inc (docs/ExpressionSandbox.md sec 7.5).  An op naming
 * any other member is a protocol error -- never a getattr.
 */
enum class FacadeKind
{
    Attribute,
    Method,
};

enum class FacadeTier
{
    Value,   ///< attribute; the result must marshal by value
    Handle,  ///< attribute; the result may cross as a handle
    Call,    ///< method; invocable via the call op, member-addressed
};

struct FacadeMember
{
    const char* type;    ///< tp_name of the declaring binding type
    const char* member;
    FacadeKind kind;
    FacadeTier tier;
};

/** The module facades (generateSandboxFacades.py MODULE_FACADES): host
 * module names the guest may call (FcxWire mod_call), read (mod_get) or
 * catch (a guest-local exception class raised by name).  Also generated
 * into FcxDispatch.inc; absent means DENY.
 */
enum class ModuleKind
{
    Callable,
    Constant,
    Exception,
};

struct ModuleMember
{
    const char* module;
    const char* name;
    ModuleKind kind;
    /// the catalog permission a mod_call/mod_get on this member is
    /// checked against ("geom.call", "app.query", ...)
    const char* permission;
};

/// The declared module member for "Module.name" (split at the last
/// dot), or nullptr.
AppExport const ModuleMember* moduleMemberLookup(const std::string& qualified);

/** The nearest annotated type in `type`'s mro, or nullptr.  Shipped as
 * the "fc" field on handle values so the image picks the right facade
 * class.  Caller holds the GIL (tp_mro access).
 */
AppExport const char* facadeKeyFor(PyTypeObject* type);

/** Declared-member lookup along `type`'s mro; nullptr when the member
 * is not annotated anywhere in the chain.  Caller holds the GIL.
 */
AppExport const FacadeMember* facadeMemberLookup(PyTypeObject* type, const char* member);

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
    /// The wire id an object is registered under; 0 when it is not.
    uint64_t idOf(PyObject* obj) const;
    /// Drop one entry (image-proxy __del__).  While releases are
    /// deferred the id is only QUEUED -- see setDeferReleases.
    void release(uint64_t id);
    /** Hold releases until flushDeferred().
     *
     * The image destroys its proxies as the evaluation unwinds, which
     * happens BEFORE the host decodes the reply.  A result that is (or
     * contains) a host object -- `tuple(.cells, <<B4>>, <<ZZ4>>)`, the
     * spreadsheet binding idiom, is a real one -- would therefore
     * arrive as a handle whose entry had just been erased, and decode
     * as a stale-handle error.  Deferring for the length of one
     * transaction keeps the reply decodable; clear() still frees
     * everything.
     */
    void setDeferReleases(bool on)
    {
        defer = on;
    }
    /// Apply the queued releases (start of the next transaction).
    void flushDeferred();
    /// Drop everything (end of transaction).
    void clear();
    std::size_t size() const
    {
        return objects.size();
    }
    /// Handles minted since construction or resetCreated(): pack
    /// exports and reply values alike (ImageHost::stats()).
    std::size_t created() const
    {
        return minted;
    }
    void resetCreated()
    {
        minted = 0;
    }
    /** The evaluation owner's Python face (borrowed; the table holds
     * the reference through its handle), or nullptr outside an owned
     * transaction.  Writes are allowed on this object only.
     */
    void setOwner(PyObject* obj)
    {
        ownerObj = obj;
    }
    PyObject* owner() const
    {
        return ownerObj;
    }

private:
    std::unordered_map<uint64_t, PyObject*> objects;
    /// the id an object already has, and how many exports hold it
    std::unordered_map<PyObject*, uint64_t> ids;
    std::unordered_map<uint64_t, int> uses;
    std::vector<uint64_t> deferred;
    bool defer = false;
    uint64_t nextId = 1;
    std::size_t minted = 0;
    PyObject* ownerObj = nullptr;
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

/// Reply builders for the op handlers registered below (the same ones
/// the built-in ops use): a value reply, an error reply by exception
/// type name, the pending Python error as an error reply (clears it),
/// and a host result (reference STOLEN) encoded through the table.
AppExport nlohmann::json okReply(nlohmann::json val);
AppExport nlohmann::json errReply(const char* exc, const std::string& msg);
AppExport nlohmann::json pyErrorReply();
AppExport nlohmann::json encodeResult(HandleTable& table, PyObject* result);

/** An op family another library owns (Gui's `gui.*`, docs/Sandbox.md
 * 7.9).  A request whose op starts with `prefix` goes to `handler`,
 * inside dispatchHostOp's GIL and exception net -- a
 * PermissionNeededException it throws becomes a PermissionError reply,
 * any other exception a RuntimeError reply.  The built-in ops are
 * matched first; one handler per prefix, a later registration replaces
 * the earlier.
 */
using BridgeOpHandler =
    std::function<std::vector<unsigned char>(HandleTable&, const std::vector<unsigned char>&)>;
AppExport void registerBridgeOps(const std::string& prefix, BridgeOpHandler handler);

/// The same helpers over CBOR, for a registered handler in another
/// library: a json object in a signature does not link across libraries
/// (two nlohmann copies, 12), so the request and reply cross as CBOR
/// bytes.  decodeHostValueCbor takes one wire value; encodeResultCbor
/// steals `result` and returns a value reply; pyErrorReplyCbor is the
/// pending Python error as an error reply.
AppExport PyObject* decodeHostValueCbor(const HandleTable& table,
                                        const std::vector<unsigned char>& valueCbor);
AppExport std::vector<unsigned char> encodeResultCbor(HandleTable& table, PyObject* result);
AppExport std::vector<unsigned char> pyErrorReplyCbor();

/** The fixed-layout form of a bare read_prop / get_attr (FcxWire.h,
 * FixedRequestMagic): `data` is the whole request, the reply comes
 * back in the fixed layout (a scalar or vector result inline, anything
 * else -- and every error -- as CBOR behind kind 0).  `opName` receives
 * the wire op name for the counters.
 */
AppExport std::vector<unsigned char> dispatchHostOpFixed(HandleTable& table,
                                                          const unsigned char* data,
                                                          std::size_t len,
                                                          std::string& opName);

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_IMAGE_BRIDGE_H

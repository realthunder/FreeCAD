/* The sandbox wire protocol constants (docs/ExpressionSandbox.md sec
 * 7.6, amended by docs/ExpressionSandboxPhase0.md sec 6.4).  Shared
 * verbatim by the image dispatcher and the host embedding; the value
 * encoding itself is CBOR via nlohmann::json on both sides.
 *
 * Requests and replies are CBOR maps.  Request: {"op": <op>, ...}.
 * Reply: {"ok": true, "val": <value>} or
 *        {"ok": false, "exc": <python type name>, "msg": <text>}.
 *
 * Typed values are maps carrying the key "t":
 *   {"t":"quantity", "v":double, "u":[8 int exponents]}
 *   {"t":"vec", "v":[x,y,z]}
 *   {"t":"rot", "v":[x,y,z,w]}
 *   {"t":"pla", "p":[x,y,z], "r":[x,y,z,w]}
 *   {"t":"mat", "v":[16 doubles, row major]}
 *   {"t":"bb",  "v":[xmin,ymin,zmin,xmax,ymax,zmax]}
 *   {"t":"h",   "id":uint64, "ty":str}   (host handle, per-transaction)
 * Everything else marshals as native CBOR: null, bool, int (64-bit),
 * float, str, bytes, array, map with string keys.  A result outside
 * this set is an evaluation ERROR (sec 7.7), never a silent wrapper.
 */
#ifndef APP_FCX_WIRE_H
#define APP_FCX_WIRE_H

namespace FcxWire
{

// ops, host -> image
inline const char* const OpEval = "eval";
// {op:"exec", src, module?}: run statements in the guest; with "module"
// the source becomes a module of that name in sys.modules -- how the
// host pushes workbench Python into the guest before a package loader
// exists (tests; G1's loader later).  Reply {ok:true} or the error.
inline const char* const OpExec = "exec";

// ops, image -> host (mid-eval bridge).  Request fields: "h" = handle
// id (uint64), "a" = wire-encoded op argument (attr/prop name string
// for get_attr/read_prop, args array for call, item key for get_item),
// "k" = call kwargs map.  Transport is the two-call pattern on the
// "fcx" import module: host_call(req,len)->reply_len, then
// host_fetch(dst,cap) copies the pending reply (both -1 on failure).
inline const char* const OpReadProp = "read_prop";
inline const char* const OpGetAttr = "get_attr";
inline const char* const OpCall = "call";
inline const char* const OpGetItem = "get_item";
inline const char* const OpLen = "len";
inline const char* const OpRelease = "release";
inline const char* const OpResolveAlias = "resolve_alias";
// {op:"write_prop", h, a: property name, v: wire value}: set a property
// on the handle's object.  Owner only -- the handle must be the
// evaluation owner -- under Permission::DocWriteSelf; the value is
// decoded through the handle table, so a host object crossing back as
// a handle (a TopoShape result) dereferences to the live object and
// Property::setPyObject re-maps it.  The guest proxy's __setattr__.
// addProperty / setPropertyStatus / removeProperty ride the `call` op
// with the same owner-only write gate.
inline const char* const OpWriteProp = "write_prop";
// No handle (h = 0): the module facades (generateSandboxFacades.py
// MODULE_FACADES).  {op:"mod_call", m:"Part.LineSegment", a: args, k:
// kwargs} calls a declared module callable on the host and returns its
// result (a shape or curve comes back as a handle); {op:"mod_get",
// m:"Part.OCC_VERSION"} reads a declared constant.  Both under
// Permission::GeomCall: a curated constructor list is a geometry call,
// not a host import.  An undeclared name is a protocol error.
inline const char* const OpModCall = "mod_call";
inline const char* const OpModGet = "mod_get";
// {op:"bool", h} -> the host object's truth value; {op:"str", h} -> its
// str().  What the guest proxy's __bool__ and __str__ do: natively any
// object is truthy unless its type says otherwise (`if plane:`), and
// Draft compares curves by str().  Both answered by the C type's own
// slot; a heap type (Python-defined __bool__/__str__ = host code) rides
// the unsafe gate.
inline const char* const OpBool = "bool";
inline const char* const OpStr = "str";
// No handle: the guest's last-in-line import finder asking what the host
// knows about a module it could not import (docs/SandboxNetwork.md sec
// 9.3).  "a" = the import name; the reply value is "" (unknown) or the
// message the finder raises (offer / installed).
inline const char* const OpPkgMissing = "pkg.missing";

// value type tags
inline const char* const TagKey = "t";
inline const char* const TagQuantity = "quantity";
inline const char* const TagVector = "vec";
inline const char* const TagRotation = "rot";
inline const char* const TagPlacement = "pla";
inline const char* const TagMatrix = "mat";
inline const char* const TagBoundBox = "bb";
inline const char* const TagHandle = "h";
// a module facade's class object as a call argument -- Draft's
// `shape.ancestorsOfType(v, Part.Edge)` -- crosses as {"t":"ty",
// "q":"Part.Edge"} and decodes on the host to the declared object
inline const char* const TagType = "ty";
// A tuple is NOT a list: the expression engine hands tuples to Enum
// properties and to tuple(), and collapsing them to lists on the wire
// loses type identity the same way bool-as-long would (Phase 0 sec
// 6.4).  Plain JSON arrays stay lists; a tuple carries this tag.
inline const char* const TagTuple = "tup";

}  // namespace FcxWire

#endif  // APP_FCX_WIRE_H

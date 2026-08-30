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

// ops, image -> host (mid-eval bridge; arrive with 4c/4d)
inline const char* const OpReadProp = "read_prop";
inline const char* const OpGetAttr = "get_attr";
inline const char* const OpCall = "call";
inline const char* const OpGetItem = "get_item";
inline const char* const OpLen = "len";
inline const char* const OpRelease = "release";
inline const char* const OpResolveAlias = "resolve_alias";

// value type tags
inline const char* const TagKey = "t";
inline const char* const TagQuantity = "quantity";
inline const char* const TagVector = "vec";
inline const char* const TagRotation = "rot";
inline const char* const TagPlacement = "pla";
inline const char* const TagMatrix = "mat";
inline const char* const TagBoundBox = "bb";
inline const char* const TagHandle = "h";

}  // namespace FcxWire

#endif  // APP_FCX_WIRE_H

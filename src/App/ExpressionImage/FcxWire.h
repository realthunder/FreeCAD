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

#include <cstdint>

namespace FcxWire
{

// ops, host -> image
inline const char* const OpEval = "eval";
// {op:"exec", src, module?}: run statements in the guest; with "module"
// the source becomes a module of that name in sys.modules -- how the
// host pushes workbench Python into the guest before a package loader
// exists (tests; G1's loader later).  Reply {ok:true} or the error.
inline const char* const OpExec = "exec";
// Rung 2 (docs/Sandbox.md 7.6, G1c): a scripted object's Proxy lives in
// the guest and the host holds a stand-in in the Proxy property.
// {op:"proxy_new", mod, cls, a:[args], k?, alloc?, owner_h?}: import
// `mod`, call `cls(*args, **k)` -- or only `cls.__new__(cls)` with
// alloc, the Restore path -- and reply with the proxy's descriptor
// (TagGuestProxy below); the instance is registered whether or not
// `__init__` installed it as a Proxy (Draft's `Array(None)` is
// installed by addObject(attach=True), which then reads `attach` off
// the stand-in).  The construction dispatch (docs/Sandbox.md 7.6 G1d)
// is this op from a class's host `__new__`.
// {op:"proxy_call", id, m, a:[args], k?, owner_h?}: call hook `m` of the
// registered proxy `id` with the decoded args (the object rides as a
// handle, exactly the native `execute(self, obj)` signature) and reply
// with the result by value.
// {op:"proxy_get", id, n}: a host read of attribute `n` of proxy `id`
// (Draft's get_type reads `obj.Proxy.Type`): the value by value, or
// TagGuestMethod when it is callable -- the host binds a forwarder as
// it does for a hook.  {op:"proxy_set", id, n, v}: a host write of it;
// `v` is a value, never a handle (a handle outlives no transaction).
// Any host->guest request may carry "pd":[ids], proxies whose host
// stand-in died: the guest drops them.
inline const char* const OpProxyNew = "proxy_new";
inline const char* const OpProxyCall = "proxy_call";
inline const char* const OpProxyGet = "proxy_get";
inline const char* const OpProxySet = "proxy_set";

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
// {op:"ext", h}: the facade keys of the object's extensions NOW -- what
// the handle's "ext" carried when it was made, re-read after the guest
// called addExtension on it (WorkingPlaneProxy.__init__ adds the
// attach extension and calls changeAttacherType in the same breath),
// so the guest recomposes the proxy's class.
inline const char* const OpExt = "ext";
// {op:"active_doc"}: the guest's FreeCAD.ActiveDocument.  For a DOCUMENT
// principal the document of the transaction's owner (the object whose
// hook runs), answered exactly as `read_prop Document` on that owner
// would be, None when no object owns the transaction; for the session
// and an addon the host's LIVE active document (docs/Sandbox.md 7.13,
// S1: reach follows the principal), None with nothing open.
inline const char* const OpActiveDoc = "active_doc";
// The application's document set, no handle (S1).  {op:"app.docs"} ->
// {name: Document} for every document the principal reaches
// (FreeCAD.listDocuments); {op:"app.doc", a: name} -> that document
// (FreeCAD.getDocument: NameError when not open, PermissionError when
// open but out of the principal's reach); both under app.query.
// {op:"app.new_doc", a: [name, label, hidden]} (FreeCAD.newDocument),
// {op:"app.close_doc", a: name}, {op:"app.set_active_doc", a: name}:
// under app.write, DENY for a document principal and not promptable.
inline const char* const OpAppDocs = "app.docs";
inline const char* const OpAppDoc = "app.doc";
inline const char* const OpAppNewDoc = "app.new_doc";
inline const char* const OpAppCloseDoc = "app.close_doc";
inline const char* const OpAppSetActiveDoc = "app.set_active_doc";
// release: "h" one id, or "a" an array of ids.  In practice releases
// never cross as an op: a proxy's __del__ queues its id and the queue
// rides as "r" (an array of ids) on the next guest->host request or on
// the evaluation's reply, released host-side before the op / after the
// trip under the transaction's deferral.
inline const char* const OpRelease = "release";
inline const char* const OpResolveAlias = "resolve_alias";
// {op:"resolve", a:[document, name]} or {op:"resolve", a:[document]}: a
// durable document-object handle re-resolved by its key (docs/Sandbox.md
// 3.2).  A handle's id lives one transaction (the table is cleared after
// an expression evaluation), but a Proxy keeps document objects on
// itself across hooks natively (ArchReport's Result sheet, the `self.obj
// = obj` of onDocumentRestored), so every handle to a DocumentObject or
// Document carries its key in "k", the guest proxy compares by it, and
// on a stale id the guest asks for a fresh one here: the object of the
// evaluation owner's document with that name -- what
// `owner.Document.getObject(name)` already gives under doc.read.self;
// another document is refused, a deleted object is a ReferenceError.
// Reply: the new id (an integer, with one use minted for the caller).
// A stale handle with a key that arrives as an ARGUMENT (a PropertyLink
// write of a cached object) re-resolves the same way while decoding.
inline const char* const OpResolve = "resolve";
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

// ---- fixed layout for the hot ops (step 7 of the coding order) ----
// A read_prop / get_attr with a name and nothing else is by far the
// most frequent bridge request (2738 of 3575 hops in the draftgeoutils
// gate) and may skip CBOR entirely, on both sides:
//   request  0xF1, op u8 (1 read_prop, 2 get_attr), handle u64 LE,
//            name u16 LE length + UTF-8 bytes
//   reply    0xF2, kind u8: 1 float64 LE | 2 bool u8 | 3 int64 LE |
//            4 string u32 LE length + UTF-8 | 5 vector 3 x float64 LE |
//            0 CBOR follows (any other value, and every error)
// Neither magic byte begins a CBOR document this wire produces (a map
// starts 0xA0-0xBF); a request without the magic is CBOR as before.
// Releases pending in the guest force the CBOR form (they ride "r").
inline constexpr uint8_t FixedRequestMagic = 0xF1;
inline constexpr uint8_t FixedReplyMagic = 0xF2;
inline constexpr uint8_t FixedOpReadProp = 1;
inline constexpr uint8_t FixedOpGetAttr = 2;
inline constexpr uint8_t FixedKindCbor = 0;
inline constexpr uint8_t FixedKindFloat = 1;
inline constexpr uint8_t FixedKindBool = 2;
inline constexpr uint8_t FixedKindInt = 3;
inline constexpr uint8_t FixedKindString = 4;
inline constexpr uint8_t FixedKindVector = 5;

// value type tags
inline const char* const TagKey = "t";
inline const char* const TagQuantity = "quantity";
inline const char* const TagVector = "vec";
inline const char* const TagRotation = "rot";
inline const char* const TagPlacement = "pla";
inline const char* const TagMatrix = "mat";
inline const char* const TagBoundBox = "bb";
// {"t":"h", "id":N, "ty":tp_name, "fc"?:facade key, "ext"?:[facade keys],
// "m"?:bound method, "k"?:[document, name] | [document]} -- "k" is the
// durable key of a DocumentObject or Document handle (OpResolve above).
inline const char* const TagHandle = "h";
// a module facade's class object as a call argument -- Draft's
// `shape.ancestorsOfType(v, Part.Edge)` -- crosses as {"t":"ty",
// "q":"Part.Edge"} and decodes on the host to the declared object
inline const char* const TagType = "ty";
// A guest-resident Proxy (rung 2).  Guest -> host, as the value of
// `write_prop Proxy` or a proxy_new reply: {"t":"gproxy", "id":N,
// "mod":"draftobjects.wire", "cls":"Wire", "hooks":[names]} -- the host
// builds the stand-in from it (hooks = the FeaturePython hook names the
// class defines, plus dumps/loads).  Host -> guest (a Proxy read, an
// argument): {"t":"gproxy", "id":N} resolves to the registered instance.
inline const char* const TagGuestProxy = "gproxy";
// A callable attribute of a guest proxy, the reply to a proxy_get
// whose value is a method: {"t":"gmethod", "id":N, "n":"name"} -- the
// host binds it into a forwarder (proxy_call on the read), the same
// object a hook attribute is.
inline const char* const TagGuestMethod = "gmethod";
// A tuple is NOT a list: the expression engine hands tuples to Enum
// properties and to tuple(), and collapsing them to lists on the wire
// loses type identity the same way bool-as-long would (Phase 0 sec
// 6.4).  Plain JSON arrays stay lists; a tuple carries this tag.
inline const char* const TagTuple = "tup";

}  // namespace FcxWire

#endif  // APP_FCX_WIRE_H

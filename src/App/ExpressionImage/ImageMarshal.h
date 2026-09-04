/* Image-side value marshalling: CBOR (as nlohmann::json values) to and
 * from Python objects, per FcxWire.h.  Compiled only into the image;
 * the host embedding has its own encoder over Base types.
 */
#ifndef APP_FCX_IMAGE_MARSHAL_H
#define APP_FCX_IMAGE_MARSHAL_H

#include <Python.h>
#include <string>

#include <nlohmann/json.hpp>

namespace FcxImage
{

/// The opaque host-handle type; created once at init.
PyObject* handleType();

/// Decoded wire value -> new reference; nullptr + Python error on failure.
PyObject* decodeValue(const nlohmann::json& v);

/// Python object -> wire value.  Returns false and fills err for a
/// result outside the by-value set (docs/ExpressionSandbox.md 7.7).
bool encodeValue(PyObject* obj, nlohmann::json& out, std::string& err);

/** One image->host bridge round trip from C++ (the Python side rides
 * _fcx.op).  Returns false with a Python error set on transport
 * failure; a delivered reply may still be {"ok":false,...}.
 * Implemented in ImageBridge.cpp.
 */
bool hostOp(const nlohmann::json& req, nlohmann::json& reply);

/// The handle ids proxies have released since the last take (a JSON
/// array, possibly empty), cleared.  They ride out as "r" on the next
/// bridge request or on the evaluation's reply; never as an op.
nlohmann::json takePendingReleases();

/** Build the module facades (generated MODULES: Part today) into
 * sys.modules -- callables over mod_call, constants over mod_get,
 * exception classes local to the guest.  Called from initEvalGlobals;
 * false with a Python error set.
 */
bool installModuleFacades();

/// The guest-local exception class a module facade declared under
/// `name` (borrowed), or nullptr: what the bridge raises when a host
/// reply names it (Part.OCCError).
PyObject* guestExceptionType(const char* name);

}  // namespace FcxImage

#endif  // APP_FCX_IMAGE_MARSHAL_H

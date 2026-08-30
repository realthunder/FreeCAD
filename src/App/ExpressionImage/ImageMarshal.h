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

}  // namespace FcxImage

#endif  // APP_FCX_IMAGE_MARSHAL_H

/* Image-side host bridge: the in-image `_fcx` module that carries the
 * mid-eval image->host ops (get_attr/call/get_item/len/release, per
 * FcxWire.h).  The transport is the two-call size-then-fetch pattern:
 * `host_call` hands the CBOR request to the host and returns only the
 * reply length (the host must not re-enter the guest to allocate), then
 * `host_fetch` copies the pending reply into a guest buffer the guest
 * allocated itself.  Both are wasm imports from module "fcx", satisfied
 * by the host embedding (App::ExpressionSandbox::ImageHost) or by the
 * error-returning stubs in tools/smokehost.c.
 */
#include <Python.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "FcxWire.h"
#include "ImageMarshal.h"

using nlohmann::json;

#ifdef FC_EXPR_PYODIDE
// The pyodide guest has no wasm imports of its own: a side module only
// sees what the main module exports.  Its transport is a Python callable
// the host installs at load (_fcx_image.set_host), which takes the CBOR
// request as bytes and returns the CBOR reply as a bytes-like object --
// on the other side of it is a V8 native function in the host process.
static PyObject *g_hostCallable = nullptr;
static bool g_hostBuffered = false;
static PyObject *g_requestBuffer = nullptr;
static PyObject *g_replyBuffer = nullptr;

void FcxImage::setHostCallable(PyObject *callable, bool buffered)
{
    Py_XINCREF(callable);
    Py_XDECREF(g_hostCallable);
    g_hostCallable = callable;
    g_hostBuffered = buffered;
}

static PyObject *buffer(PyObject *&slot)
{
    if (!slot)
        slot = PyByteArray_FromStringAndSize(nullptr, 65536);
    return slot;
}

PyObject *FcxImage::requestBuffer()
{
    return buffer(g_requestBuffer);
}

PyObject *FcxImage::replyBuffer()
{
    return buffer(g_replyBuffer);
}

bool FcxImage::ensureCapacity(PyObject *ba, size_t n)
{
    if ((size_t)PyByteArray_GET_SIZE(ba) >= n)
        return true;
    size_t grown = (size_t)PyByteArray_GET_SIZE(ba);
    while (grown < n)
        grown *= 2;
    return PyByteArray_Resize(ba, (Py_ssize_t)grown) == 0;
}

/// The buffered shape: request into replyBuffer(), an int across, the
/// reply out of requestBuffer().
static bool hostTransportBuffered(const std::vector<uint8_t> &request,
                                  std::vector<uint8_t> &replyBytes)
{
    PyObject *rep = FcxImage::replyBuffer();
    PyObject *req = FcxImage::requestBuffer();
    if (!rep || !req || !FcxImage::ensureCapacity(rep, request.size()))
        return false;
    memcpy(PyByteArray_AS_STRING(rep), request.data(), request.size());
    PyObject *arg = PyLong_FromSize_t(request.size());
    if (!arg)
        return false;
    PyObject *res = PyObject_CallOneArg(g_hostCallable, arg);
    Py_DECREF(arg);
    if (!res)
        return false;
    long n = PyLong_AsLong(res);
    Py_DECREF(res);
    if (n < 0) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_RuntimeError, "host bridge returned no reply");
        return false;
    }
    if ((size_t)n > (size_t)PyByteArray_GET_SIZE(req)) {
        PyErr_SetString(PyExc_RuntimeError, "host bridge reply exceeds the request buffer");
        return false;
    }
    const uint8_t *data = reinterpret_cast<const uint8_t *>(PyByteArray_AS_STRING(req));
    replyBytes.assign(data, data + n);
    return true;
}

/// The one round trip: bytes out, bytes-like back.  A JsProxy of a
/// Uint8Array supports the buffer protocol; anything else that does is
/// accepted too.
static bool hostTransport(const std::vector<uint8_t> &request,
                          std::vector<uint8_t> &replyBytes)
{
    if (!g_hostCallable) {
        PyErr_SetString(PyExc_RuntimeError, "host bridge unavailable");
        return false;
    }
    if (g_hostBuffered)
        return hostTransportBuffered(request, replyBytes);
    PyObject *arg = PyBytes_FromStringAndSize(
            reinterpret_cast<const char *>(request.data()),
            (Py_ssize_t)request.size());
    if (!arg)
        return false;
    PyObject *res = PyObject_CallOneArg(g_hostCallable, arg);
    Py_DECREF(arg);
    if (!res)
        return false;
    Py_buffer view;
    if (PyObject_GetBuffer(res, &view, PyBUF_SIMPLE) != 0) {
        // A JsProxy of a Uint8Array does not expose the buffer protocol
        // directly; its to_bytes() copies the array into a bytes object.
        PyErr_Clear();
        PyObject *copy = PyObject_CallMethod(res, "to_bytes", nullptr);
        Py_DECREF(res);
        if (!copy)
            return false;
        res = copy;
        if (PyObject_GetBuffer(res, &view, PyBUF_SIMPLE) != 0) {
            Py_DECREF(res);
            return false;
        }
    }
    replyBytes.assign(static_cast<const uint8_t *>(view.buf),
                      static_cast<const uint8_t *>(view.buf) + view.len);
    PyBuffer_Release(&view);
    Py_DECREF(res);
    return true;
}
#else
extern "C" {
__attribute__((import_module("fcx"), import_name("host_call")))
int32_t fcx_host_call(const uint8_t *req, uint32_t len);
__attribute__((import_module("fcx"), import_name("host_fetch")))
int32_t fcx_host_fetch(uint8_t *dst, uint32_t cap);
}

/// The one round trip over the two wasm imports: size, then fetch into
/// a buffer this side allocated.
static bool hostTransport(const std::vector<uint8_t> &request,
                          std::vector<uint8_t> &replyBytes)
{
    int32_t n = fcx_host_call(request.data(), (uint32_t)request.size());
    if (n < 0) {
        PyErr_SetString(PyExc_RuntimeError, "host bridge unavailable");
        return false;
    }
    replyBytes.resize((size_t)n);
    if (fcx_host_fetch(replyBytes.data(), (uint32_t)n) != n) {
        PyErr_SetString(PyExc_RuntimeError, "host bridge fetch mismatch");
        return false;
    }
    return true;
}
#endif

static bool hostRoundTrip(const json &req, json &reply)
{
    std::vector<uint8_t> buf;
    if (!hostTransport(json::to_cbor(req), buf))
        return false;
    try {
        reply = json::from_cbor(buf.begin(), buf.end());
    }
    catch (const json::exception &e) {
        PyErr_Format(PyExc_RuntimeError, "undecodable host reply: %s",
                     e.what());
        return false;
    }
    return true;
}

bool FcxImage::hostOp(const json &req, json &reply)
{
    return hostRoundTrip(req, reply);
}

/// {"ok":false,"exc":...,"msg":...} -> raise the builtin of that name
/// when one exists (PermissionError, AttributeError, IndexError, ...),
/// else RuntimeError carrying both fields.
static void raiseFromReply(const json &reply)
{
    std::string exc = reply.value("exc", "Exception");
    std::string msg = reply.value("msg", "");
    PyObject *builtins = PyEval_GetBuiltins();
    PyObject *type =
        builtins ? PyDict_GetItemString(builtins, exc.c_str()) : nullptr;
    if (type && PyExceptionClass_Check(type))
        PyErr_SetString(type, msg.c_str());
    else
        PyErr_Format(PyExc_RuntimeError, "%s: %s", exc.c_str(), msg.c_str());
}

/// _fcx.op(op, handle_id[, a[, k]]): request {"op","h","a"?,"k"?}, both
/// extras wire-encoded ("a" = attr/prop name / item key, "k" = call
/// kwargs).  The call op is member-addressed (docs/ExpressionSandbox.md
/// sec 7.5): _fcx.op('call', id, member, args, kwargs) -> {"m","a","k"}.
/// Returns the decoded reply value or raises.
static PyObject *fcx_op(PyObject *, PyObject *args)
{
    const char *op = nullptr;
    unsigned long long id = 0;
    PyObject *a1 = nullptr;
    PyObject *a2 = nullptr;
    PyObject *a3 = nullptr;
    if (!PyArg_ParseTuple(args, "sK|OOO", &op, &id, &a1, &a2, &a3))
        return nullptr;

    json req;
    req["op"] = op;
    req["h"] = (uint64_t)id;
    std::string err;
    bool isCall = strcmp(op, "call") == 0;
    if (isCall) {
        if (!a1 || !PyUnicode_Check(a1)) {
            PyErr_SetString(PyExc_TypeError, "call op needs a member name");
            return nullptr;
        }
        req["m"] = PyUnicode_AsUTF8(a1);
        a1 = a2;   // args tuple
        a2 = a3;   // kwargs
    }
    else if (a3) {
        PyErr_SetString(PyExc_TypeError, "too many op arguments");
        return nullptr;
    }
    if (a1) {
        json v;
        if (!FcxImage::encodeValue(a1, v, err)) {
            PyErr_SetString(PyExc_TypeError, err.c_str());
            return nullptr;
        }
        req["a"] = std::move(v);
    }
    if (a2) {
        json v;
        if (!FcxImage::encodeValue(a2, v, err)) {
            PyErr_SetString(PyExc_TypeError, err.c_str());
            return nullptr;
        }
        req["k"] = std::move(v);
    }

    json reply;
    if (!hostRoundTrip(req, reply))
        return nullptr;
    if (!reply.value("ok", false)) {
        raiseFromReply(reply);
        return nullptr;
    }
    auto val = reply.find("val");
    return FcxImage::decodeValue(val != reply.end() ? *val : json());
}

static PyMethodDef FcxMethods[] = {
    {"op", fcx_op, METH_VARARGS, "One image->host bridge op."},
    {nullptr, nullptr, 0, nullptr},
};

static PyModuleDef FcxModuleDef = {
    PyModuleDef_HEAD_INIT, "_fcx",
    "Host bridge transport (sandbox image internal)", -1,
    FcxMethods, nullptr, nullptr, nullptr, nullptr,
};

extern "C" PyObject *PyInit__fcx(void)
{
    return PyModule_Create(&FcxModuleDef);
}

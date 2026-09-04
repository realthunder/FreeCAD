/* Image-side host bridge: the in-image `_fcx` module that carries the
 * mid-eval image->host ops (get_attr/call/get_item/len/release, per
 * FcxWire.h).  The transport is the two-call size-then-fetch pattern:
 * `host_call` hands the CBOR request to the host and returns only the
 * reply length (the host must not re-enter the guest to allocate), then
 * `host_fetch` copies the pending reply into a guest buffer the guest
 * allocated itself.  Both are wasm imports, on BOTH runtimes:
 *
 *  - the wasi reactor imports them from module "fcx", defined by the
 *    wasmtime linker (ExpressionWasmtimeRuntime.cpp) or by the
 *    error-returning stubs in tools/smokehost.c;
 *  - the pyodide side module imports from "env", the only module
 *    emscripten's dynamic linker resolves, where the host put them with
 *    `Module.mergeLibSymbols({fcx_host_call, fcx_host_fetch}, "fcx")`
 *    before the wheel was loaded (pyodide_glue.js).  On the other side
 *    is a V8 native function reading wasm memory in place -- no Python
 *    callable, no proxy, no JS copy on the way out.
 *
 * Only integers cross either way; the bytes stay in wasm memory.
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
#define FCX_IMPORT_MODULE "env"
#define FCX_IMPORT_CALL "fcx_host_call"
#define FCX_IMPORT_FETCH "fcx_host_fetch"
#else
#define FCX_IMPORT_MODULE "fcx"
#define FCX_IMPORT_CALL "host_call"
#define FCX_IMPORT_FETCH "host_fetch"
#endif

extern "C" {
__attribute__((import_module(FCX_IMPORT_MODULE), import_name(FCX_IMPORT_CALL)))
int32_t fcx_host_call(const uint8_t *req, uint32_t len);
__attribute__((import_module(FCX_IMPORT_MODULE), import_name(FCX_IMPORT_FETCH)))
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

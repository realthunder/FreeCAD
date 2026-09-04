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

#include <Base/PyObjectBase.h>
#include <Base/VectorPy.h>

#include "FcxWire.h"
#include "ImageMarshal.h"

using nlohmann::json;

/* Releases never cross on their own.  A proxy's __del__ queues its
 * handle id here; the queue rides out as "r" on the next bridge request
 * (fcx_op) or, when the evaluation ends first, on the reply itself
 * (ImageDispatch dispatch()).  Measured before this: 58 percent of all
 * hops in a draftgeoutils workload were single releases.
 */
static std::vector<uint64_t> g_pendingReleases;

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
    // not a builtin: a module facade's exception class (Part.OCCError)
    if (!type)
        type = FcxImage::guestExceptionType(exc.c_str());
    if (type && PyExceptionClass_Check(type))
        PyErr_SetString(type, msg.c_str());
    else
        PyErr_Format(PyExc_RuntimeError, "%s: %s", exc.c_str(), msg.c_str());
}

/// _fcx.op(op, handle_id[, a[, k]]): request {"op","h","a"?,"k"?}, both
/// extras wire-encoded ("a" = attr/prop name / item key, "k" = call
/// kwargs).  The call op is member-addressed (docs/ExpressionSandbox.md
/// sec 7.5): _fcx.op('call', id, member, args, kwargs) -> {"m","a","k"}.
/// write_prop carries its value as "v": _fcx.op('write_prop', id, name,
/// value) -> {"a","v"}.  Returns the decoded reply value or raises.
static PyObject *fcx_op(PyObject *, PyObject *args)
{
    const char *op = nullptr;
    unsigned long long id = 0;
    PyObject *a1 = nullptr;
    PyObject *a2 = nullptr;
    PyObject *a3 = nullptr;
    if (!PyArg_ParseTuple(args, "sK|OOO", &op, &id, &a1, &a2, &a3))
        return nullptr;

    // The fixed layout (FcxWire.h): a bare read_prop / get_attr with a
    // name, no releases waiting -- the hop that dominates, without CBOR
    // on either side.  Anything else takes the CBOR form below.
    if (g_pendingReleases.empty() && a1 && !a2 && PyUnicode_Check(a1)
            && (strcmp(op, FcxWire::OpReadProp) == 0 || strcmp(op, FcxWire::OpGetAttr) == 0)) {
        Py_ssize_t nlen = 0;
        const char *name = PyUnicode_AsUTF8AndSize(a1, &nlen);
        if (name && nlen <= 0xFFFF) {
            std::vector<uint8_t> fixed(12 + (size_t)nlen);
            uint8_t *p = fixed.data();
            *p++ = FcxWire::FixedRequestMagic;
            *p++ = strcmp(op, FcxWire::OpReadProp) == 0 ? FcxWire::FixedOpReadProp
                                                        : FcxWire::FixedOpGetAttr;
            for (int i = 0; i < 8; ++i)
                *p++ = (uint8_t)((uint64_t)id >> (8 * i));
            *p++ = (uint8_t)(nlen & 0xFF);
            *p++ = (uint8_t)(nlen >> 8);
            memcpy(p, name, (size_t)nlen);
            std::vector<uint8_t> rep;
            if (!hostTransport(fixed, rep))
                return nullptr;
            if (rep.size() < 2 || rep[0] != FcxWire::FixedReplyMagic) {
                PyErr_SetString(PyExc_RuntimeError, "malformed fixed-layout reply");
                return nullptr;
            }
            const uint8_t *q = rep.data() + 2;
            const size_t n = rep.size() - 2;
            auto le64 = [](const uint8_t *b) {
                uint64_t v = 0;
                for (int i = 7; i >= 0; --i)
                    v = (v << 8) | b[i];
                return v;
            };
            auto dbl = [&](const uint8_t *b) {
                uint64_t bits = le64(b);
                double d;
                memcpy(&d, &bits, 8);
                return d;
            };
            switch (rep[1]) {
            case FcxWire::FixedKindFloat:
                if (n >= 8)
                    return PyFloat_FromDouble(dbl(q));
                break;
            case FcxWire::FixedKindBool:
                if (n >= 1)
                    return PyBool_FromLong(q[0]);
                break;
            case FcxWire::FixedKindInt:
                if (n >= 8)
                    return PyLong_FromLongLong((int64_t)le64(q));
                break;
            case FcxWire::FixedKindString:
                if (n >= 4) {
                    uint32_t l = q[0] | (q[1] << 8) | (q[2] << 16) | ((uint32_t)q[3] << 24);
                    if (n >= 4 + (size_t)l)
                        return PyUnicode_FromStringAndSize((const char *)q + 4, l);
                }
                break;
            case FcxWire::FixedKindVector:
                if (n >= 24)
                    return new Base::VectorPy(Base::Vector3d(dbl(q), dbl(q + 8), dbl(q + 16)));
                break;
            case FcxWire::FixedKindCbor: {
                json reply;
                try {
                    reply = json::from_cbor(q, q + n);
                }
                catch (const json::exception &e) {
                    PyErr_Format(PyExc_RuntimeError, "undecodable host reply: %s", e.what());
                    return nullptr;
                }
                if (!reply.value("ok", false)) {
                    raiseFromReply(reply);
                    return nullptr;
                }
                auto val = reply.find("val");
                return FcxImage::decodeValue(val != reply.end() ? *val : json());
            }
            default:
                break;
            }
            PyErr_SetString(PyExc_RuntimeError, "malformed fixed-layout reply");
            return nullptr;
        }
    }

    json req;
    req["op"] = op;
    req["h"] = (uint64_t)id;
    std::string err;
    // member-addressed ops carry the name as "m" and shift the extras
    bool isCall = strcmp(op, FcxWire::OpCall) == 0 || strcmp(op, FcxWire::OpModCall) == 0
        || strcmp(op, FcxWire::OpModGet) == 0;
    if (isCall) {
        if (!a1 || !PyUnicode_Check(a1)) {
            PyErr_SetString(PyExc_TypeError, "member-addressed op needs a name");
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
        req[strcmp(op, FcxWire::OpWriteProp) == 0 ? "v" : "k"] = std::move(v);
    }

    // queued releases ride along, free
    if (!g_pendingReleases.empty())
        req["r"] = FcxImage::takePendingReleases();

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

static PyObject *fcx_release_later(PyObject *, PyObject *arg)
{
    unsigned long long id = PyLong_AsUnsignedLongLong(arg);
    if (id == (unsigned long long)-1 && PyErr_Occurred())
        return nullptr;
    g_pendingReleases.push_back((uint64_t)id);
    Py_RETURN_NONE;
}

json FcxImage::takePendingReleases()
{
    json ids = json::array();
    for (uint64_t id : g_pendingReleases)
        ids.push_back(id);
    g_pendingReleases.clear();
    return ids;
}

/// _fcx.track(value, parent, name) -> value: stamp a value a handle
/// proxy hands out for attribute `name` as that attribute of `parent`,
/// so a nested write (`shape.Placement.Rotation = r`) writes the whole
/// value back through the proxy's __setattr__ -- the write-back native
/// FreeCAD does for every PyObjectBase an attribute returns.  Anything
/// that is not a PyObjectBase passes through untouched.
static PyObject *fcx_track(PyObject *, PyObject *args)
{
    PyObject *value = nullptr, *parent = nullptr;
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "OOs", &value, &parent, &name))
        return nullptr;
    Base::PyObjectBase::trackAttributeOf(value, name, parent);
    Py_INCREF(value);
    return value;
}

static PyMethodDef FcxMethods[] = {
    {"op", fcx_op, METH_VARARGS, "One image->host bridge op."},
    {"track", fcx_track, METH_VARARGS,
     "Stamp a value as an attribute of its handle proxy, for the write-back."},
    {"release_later", fcx_release_later, METH_O,
     "Queue a handle release to ride the next request or reply."},
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

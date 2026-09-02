/* The pyodide guest: the sandbox image as a CPython extension module
 * (`_fcx_image`) for pyodide's wasm32-emscripten interpreter, loaded from
 * a wheel by the host (src/App/PyodideHost, docs/PyodideHost.md).
 *
 * Same dispatcher, marshaller, bridge and core carve as the wasm32-wasi
 * reactor in ImageMain.cpp; what differs is only the two ends of the
 * pipe.  There is no interpreter to start -- pyodide's is running -- so
 * module init registers the in-image FreeCAD module and the _fcx bridge
 * in sys.modules and builds the eval globals.  Requests arrive as bytes
 * through `call`, and the bridge reaches the host through the callable
 * given to `set_host` (a V8 native function, on the other side).
 *
 *   import _fcx_image
 *   _fcx_image.set_host(host_fn)        # bytes -> bytes-like
 *   reply = _fcx_image.call(request)    # CBOR in, CBOR out
 */
#include <Python.h>
#include <cstdint>
#include <vector>

#include "FcxDocument.h"
#include "ImageDispatch.h"
#include "ImageMarshal.h"

extern "C" PyObject *PyInit__fcx(void);  // ImageBridge.cpp

namespace
{

PyObject *fcx_call(PyObject *, PyObject *arg)
{
    Py_buffer view;
    PyObject *owned = nullptr;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) != 0) {
        // The host hands a Uint8Array, which arrives as a JsProxy without
        // the buffer protocol; its to_bytes() is the copy in.
        PyErr_Clear();
        owned = PyObject_CallMethod(arg, "to_bytes", nullptr);
        if (!owned || PyObject_GetBuffer(owned, &view, PyBUF_SIMPLE) != 0) {
            Py_XDECREF(owned);
            return nullptr;
        }
    }
    std::vector<uint8_t> reply = FcxImage::dispatchCbor(
            static_cast<const uint8_t *>(view.buf), (size_t)view.len);
    PyBuffer_Release(&view);
    Py_XDECREF(owned);
    return PyBytes_FromStringAndSize(
            reinterpret_cast<const char *>(reply.data()),
            (Py_ssize_t)reply.size());
}

PyObject *fcx_set_host(PyObject *, PyObject *arg)
{
    if (arg != Py_None && !PyCallable_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "set_host: callable or None");
        return nullptr;
    }
    FcxImage::setHostCallable(arg == Py_None ? nullptr : arg, false);
    Py_RETURN_NONE;
}

/// The buffered transport (ImageMarshal.h): the host callable takes the
/// bridge request's length (bytes already in the reply buffer) and
/// returns its reply's length (bytes in the request buffer).
PyObject *fcx_set_host_buffered(PyObject *, PyObject *arg)
{
    if (arg != Py_None && !PyCallable_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "set_host_buffered: callable or None");
        return nullptr;
    }
    FcxImage::setHostCallable(arg == Py_None ? nullptr : arg, true);
    Py_RETURN_NONE;
}

/// (request_buffer, reply_buffer): the two bytearrays the host views in
/// place.  Growing either replaces its storage, so the host re-acquires
/// its views whenever they detach.
PyObject *fcx_buffers(PyObject *, PyObject *)
{
    PyObject *req = FcxImage::requestBuffer();
    PyObject *rep = FcxImage::replyBuffer();
    if (!req || !rep)
        return nullptr;
    return PyTuple_Pack(2, req, rep);
}

/// grow_request(n): make the request buffer hold at least n bytes.
PyObject *fcx_grow_request(PyObject *, PyObject *arg)
{
    size_t n = PyLong_AsSize_t(arg);
    if (n == (size_t)-1 && PyErr_Occurred())
        return nullptr;
    if (!FcxImage::ensureCapacity(FcxImage::requestBuffer(), n))
        return nullptr;
    return PyLong_FromSsize_t(PyByteArray_GET_SIZE(FcxImage::requestBuffer()));
}

/// call_len(n): dispatch the n request bytes sitting in the request
/// buffer; the reply lands in the reply buffer and its length is the
/// result.  Only integers cross.
PyObject *fcx_call_len(PyObject *, PyObject *arg)
{
    size_t n = PyLong_AsSize_t(arg);
    if (n == (size_t)-1 && PyErr_Occurred())
        return nullptr;
    PyObject *req = FcxImage::requestBuffer();
    if ((Py_ssize_t)n > PyByteArray_GET_SIZE(req)) {
        PyErr_SetString(PyExc_ValueError, "call_len: length exceeds the request buffer");
        return nullptr;
    }
    // Decode-then-dispatch: dispatchCbor has fully parsed the request
    // before any evaluation, so the request buffer is free for bridge
    // replies while the evaluation runs.
    std::vector<uint8_t> reply = FcxImage::dispatchCbor(
            reinterpret_cast<const uint8_t *>(PyByteArray_AS_STRING(req)), n);
    PyObject *rep = FcxImage::replyBuffer();
    if (!FcxImage::ensureCapacity(rep, reply.size()))
        return nullptr;
    memcpy(PyByteArray_AS_STRING(rep), reply.data(), reply.size());
    return PyLong_FromSize_t(reply.size());
}

PyMethodDef methods[] = {
    {"call", fcx_call, METH_O, "One CBOR request -> CBOR reply."},
    {"set_host", fcx_set_host, METH_O, "Install the host bridge callable (bytes shape)."},
    {"set_host_buffered", fcx_set_host_buffered, METH_O,
     "Install the host bridge callable (buffered shape: lengths only)."},
    {"buffers", fcx_buffers, METH_NOARGS, "(request_buffer, reply_buffer) bytearrays."},
    {"grow_request", fcx_grow_request, METH_O, "Grow the request buffer to n bytes."},
    {"call_len", fcx_call_len, METH_O, "Dispatch n request-buffer bytes; reply length."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_fcx_image",
    "The FreeCAD expression sandbox image, as a pyodide extension", -1,
    methods, nullptr, nullptr, nullptr, nullptr,
};

/// Put `module` in sys.modules under `name`; true on success.
bool registerModule(const char *name, PyObject *module)
{
    PyObject *modules = PyImport_GetModuleDict();
    return module && modules
        && PyDict_SetItemString(modules, name, module) == 0;
}

}  // namespace

PyMODINIT_FUNC PyInit__fcx_image(void)
{
    Fcx::initCoreTypes();

    PyObject *fc = FcxImage::initFreeCADModule();
    if (!registerModule("FreeCAD", fc) || !registerModule("App", fc)) {
        Py_XDECREF(fc);
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_RuntimeError, "cannot register the FreeCAD module");
        return nullptr;
    }
    Py_DECREF(fc);

    PyObject *bridge = PyInit__fcx();
    if (!registerModule("_fcx", bridge)) {
        Py_XDECREF(bridge);
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_RuntimeError, "cannot register the _fcx module");
        return nullptr;
    }
    Py_DECREF(bridge);

    if (int rc = FcxImage::initEvalGlobals()) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_RuntimeError, "eval globals failed (rc %d)", rc);
        return nullptr;
    }
    return PyModule_Create(&moduledef);
}

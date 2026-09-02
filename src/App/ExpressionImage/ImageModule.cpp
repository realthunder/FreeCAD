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
    FcxImage::setHostCallable(arg == Py_None ? nullptr : arg);
    Py_RETURN_NONE;
}

PyMethodDef methods[] = {
    {"call", fcx_call, METH_O, "One CBOR request -> CBOR reply."},
    {"set_host", fcx_set_host, METH_O, "Install the host bridge callable."},
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

/* The pyodide guest: the sandbox image as a CPython extension module
 * (`_fcx_image`) for pyodide's wasm32-emscripten interpreter, loaded from
 * a wheel by the host (src/App/PyodideHost, docs/Sandbox.md).
 *
 * Same dispatcher, marshaller, bridge and core carve as the wasm32-wasi
 * reactor in ImageMain.cpp; what differs is only the two ends of the
 * pipe.  There is no interpreter to start -- pyodide's is running -- so
 * module init registers the in-image FreeCAD module and the _fcx bridge
 * in sys.modules and builds the eval globals.
 *
 * Host->guest is the reactor's own shape, as C exports of the side
 * module the host calls directly from V8 (no Python callable, no proxy):
 *
 *   fcx_alloc(n) / fcx_free(p)
 *   fcx_call(req, len) -> reply buffer: u32 length + CBOR bytes
 *
 * The host writes the request into wasm memory at fcx_alloc's pointer,
 * calls fcx_call, reads the reply out of memory and frees both.  The
 * Python-level `call(bytes) -> bytes` stays for smoke tests from inside
 * the guest.  Guest->host is ImageBridge.cpp's pair of "env" imports.
 */
#include <Python.h>
#include <emscripten.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "FcxDocument.h"
#include "ImageDispatch.h"
#include "ImageMarshal.h"

extern "C" PyObject *PyInit__fcx(void);  // ImageBridge.cpp

extern "C" {

EMSCRIPTEN_KEEPALIVE void *fcx_alloc(uint32_t n)
{
    return malloc(n);
}

EMSCRIPTEN_KEEPALIVE void fcx_free(void *p)
{
    free(p);
}

EMSCRIPTEN_KEEPALIVE void *fcx_call(const uint8_t *req, uint32_t len)
{
    std::vector<uint8_t> payload = FcxImage::dispatchCbor(req, len);
    uint8_t *buf = (uint8_t *)malloc(4 + payload.size());
    if (!buf)
        return nullptr;
    uint32_t total = (uint32_t)payload.size();
    memcpy(buf, &total, 4);
    memcpy(buf + 4, payload.data(), payload.size());
    return buf;
}

}  // extern "C"

namespace
{

PyObject *fcx_call_py(PyObject *, PyObject *arg)
{
    Py_buffer view;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) != 0)
        return nullptr;
    std::vector<uint8_t> reply = FcxImage::dispatchCbor(
            static_cast<const uint8_t *>(view.buf), (size_t)view.len);
    PyBuffer_Release(&view);
    return PyBytes_FromStringAndSize(
            reinterpret_cast<const char *>(reply.data()),
            (Py_ssize_t)reply.size());
}

PyMethodDef methods[] = {
    {"call", fcx_call_py, METH_O, "One CBOR request -> CBOR reply."},
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

/* Sandbox image entry: CPython embedded in a wasm32-wasi reactor with
 * the Base math bindings registered as an in-image FreeCAD module
 * (Ring 0 of docs/ExpressionSandbox.md sec 7.4).
 *
 * Exports:
 *   fcx_init() -> 0 ok
 *   fcx_alloc(n) / fcx_free(p)
 *   fcx_call(req, len) -> reply buffer: u32 length + CBOR bytes
 *       (ops and value encoding per FcxWire.h)
 *   fcx_eval(src, len) -> u32 length + "T:payload" -- DEBUG path only,
 *       repr round trip without the wire encoding
 */
#include <Python.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <nlohmann/json.hpp>


#include "FcxDocument.h"
#include "ImageDispatch.h"

using nlohmann::json;

#define EXPORT(name) __attribute__((export_name(#name)))

extern "C" {

EXPORT(fcx_alloc) void *fcx_alloc(uint32_t n)
{
    return malloc(n);
}

EXPORT(fcx_free) void fcx_free(void *p)
{
    free(p);
}

}  // extern "C"

/// The FreeCAD module for PyImport_AppendInittab, which wants a plain
/// C function pointer.
static PyObject *init_freecad()
{
    return FcxImage::initFreeCADModule();
}

extern "C" PyObject *PyInit__fcx(void);  // ImageBridge.cpp

extern "C" {

EXPORT(fcx_init) int fcx_init(void)
{
    Fcx::initCoreTypes();
    PyImport_AppendInittab("FreeCAD", init_freecad);
    PyImport_AppendInittab("_fcx", PyInit__fcx);

    PyStatus status;
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    status = PyConfig_SetBytesString(&config, &config.home, "/");
    if (PyStatus_Exception(status))
        return 1;
    config.module_search_paths_set = 1;
    status = PyWideStringList_Append(&config.module_search_paths, L"/Lib");
    if (PyStatus_Exception(status))
        return 2;
    status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status))
        return 3;
    return FcxImage::initEvalGlobals();
}

static void *reply(char tag, const char *text)
{
    uint32_t n = (uint32_t)strlen(text);
    uint8_t *buf = (uint8_t *)malloc(4 + 2 + n);
    if (!buf)
        return nullptr;
    uint32_t total = 2 + n;
    memcpy(buf, &total, 4);
    buf[4] = (uint8_t)tag;
    buf[5] = ':';
    memcpy(buf + 6, text, n);
    return buf;
}

static void *replyBytes(const std::vector<uint8_t> &payload)
{
    uint8_t *buf = (uint8_t *)malloc(4 + payload.size());
    if (!buf)
        return nullptr;
    uint32_t total = (uint32_t)payload.size();
    memcpy(buf, &total, 4);
    memcpy(buf + 4, payload.data(), payload.size());
    return buf;
}

EXPORT(fcx_call) void *fcx_call(const uint8_t *req_bytes, uint32_t len)
{
    return replyBytes(FcxImage::dispatchCbor(req_bytes, len));
}

EXPORT(fcx_eval) void *fcx_eval(const char *src, uint32_t len)
{
    char *source = (char *)malloc(len + 1);
    if (!source)
        return nullptr;
    memcpy(source, src, len);
    source[len] = 0;
    PyObject *result = PyRun_String(source, Py_eval_input,
                                    FcxImage::evalGlobals(), FcxImage::evalGlobals());
    free(source);
    if (!result) {
        PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
        PyErr_Fetch(&type, &value, &trace);
        PyObject *msg = value ? PyObject_Str(value) : nullptr;
        const char *text = msg ? PyUnicode_AsUTF8(msg) : "unknown error";
        void *r = reply('E', text ? text : "unprintable error");
        Py_XDECREF(msg);
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(trace);
        return r;
    }
    PyObject *repr = PyObject_Repr(result);
    Py_DECREF(result);
    const char *text = repr ? PyUnicode_AsUTF8(repr) : "unprintable";
    void *r = reply('R', text ? text : "unprintable");
    Py_XDECREF(repr);
    return r;
}

}  // extern "C"

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

#include <Base/BoundBoxPy.h>
#include <Base/MatrixPy.h>
#include <Base/PlacementPy.h>
#include <Base/QuantityPy.h>
#include <Base/RotationPy.h>
#include <Base/UnitPy.h>
#include <Base/VectorPy.h>

#include "FcxWire.h"
#include "ImageMarshal.h"

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

static PyObject *eval_globals;

static void add_type(PyObject *module, const char *name, PyTypeObject *type)
{
    if (PyType_Ready(type) == 0) {
        Py_INCREF(type);
        PyModule_AddObject(module, name, reinterpret_cast<PyObject *>(type));
    }
}

static PyModuleDef FreeCADModuleDef = {
    PyModuleDef_HEAD_INIT, "FreeCAD",
    "In-image FreeCAD math module (sandbox Ring 0)", -1,
    nullptr, nullptr, nullptr, nullptr, nullptr,
};

static PyModuleDef UnitsModuleDef = {
    PyModuleDef_HEAD_INIT, "FreeCAD.Units",
    "In-image unit types (sandbox Ring 0)", -1,
    nullptr, nullptr, nullptr, nullptr, nullptr,
};

static PyObject *init_freecad_module()
{
    PyObject *module = PyModule_Create(&FreeCADModuleDef);
    if (!module)
        return nullptr;
    add_type(module, "Vector", &Base::VectorPy::Type);
    add_type(module, "Rotation", &Base::RotationPy::Type);
    add_type(module, "Placement", &Base::PlacementPy::Type);
    add_type(module, "Matrix", &Base::MatrixPy::Type);
    add_type(module, "BoundBox", &Base::BoundBoxPy::Type);

    PyObject *units = PyModule_Create(&UnitsModuleDef);
    if (units) {
        add_type(units, "Quantity", &Base::QuantityPy::Type);
        add_type(units, "Unit", &Base::UnitPy::Type);
        PyModule_AddObject(module, "Units", units);
        PyObject *modules = PyImport_GetModuleDict();
        PyDict_SetItemString(modules, "FreeCAD.Units", units);
    }
    return module;
}

extern "C" PyObject *PyInit__fcx(void);  // ImageBridge.cpp

extern "C" {

EXPORT(fcx_init) int fcx_init(void)
{
    PyImport_AppendInittab("FreeCAD", init_freecad_module);
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
    eval_globals = PyDict_New();
    if (!eval_globals)
        return 4;
    PyDict_SetItemString(eval_globals, "__builtins__", PyEval_GetBuiltins());
    PyObject *fc = PyImport_ImportModule("FreeCAD");
    if (!fc) {
        PyErr_Print();
        return 5;
    }
    PyDict_SetItemString(eval_globals, "FreeCAD", fc);
    PyDict_SetItemString(eval_globals, "App", fc);
    PyObject *units = PyObject_GetAttrString(fc, "Units");
    if (units)
        PyDict_SetItemString(eval_globals, "Units", units);
    Py_XDECREF(units);
    Py_DECREF(fc);
    return 0;
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

/// Current Python error -> {"ok":false,"exc":<type>,"msg":<text>}.
static json errorReply()
{
    json r;
    r["ok"] = false;
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    PyErr_NormalizeException(&type, &value, &trace);
    const char *excName = "Exception";
    if (type && PyType_Check(type))
        excName = ((PyTypeObject *)type)->tp_name;
    // strip a module prefix: the wire carries the bare type name
    if (const char *dot = strrchr(excName, '.'))
        excName = dot + 1;
    r["exc"] = excName;
    PyObject *msg = value ? PyObject_Str(value) : nullptr;
    const char *text = msg ? PyUnicode_AsUTF8(msg) : nullptr;
    r["msg"] = text ? text : "unprintable error";
    Py_XDECREF(msg);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(trace);
    return r;
}

static json protocolError(const char *what)
{
    json r;
    r["ok"] = false;
    r["exc"] = "ProtocolError";
    r["msg"] = what;
    return r;
}

static json dispatchEval(const json &req)
{
    auto src = req.find("src");
    if (src == req.end() || !src->is_string())
        return protocolError("eval without src");

    PyObject *globals = PyDict_Copy(eval_globals);
    if (!globals)
        return errorReply();

    auto bindings = req.find("bindings");
    if (bindings != req.end() && bindings->is_object()) {
        for (auto it = bindings->begin(); it != bindings->end(); ++it) {
            PyObject *obj = FcxImage::decodeValue(it.value());
            if (!obj || PyDict_SetItemString(globals, it.key().c_str(), obj) < 0) {
                Py_XDECREF(obj);
                Py_DECREF(globals);
                return errorReply();
            }
            Py_DECREF(obj);
        }
    }

    PyObject *result = PyRun_String(src->get_ref<const std::string &>().c_str(),
                                    Py_eval_input, globals, globals);
    Py_DECREF(globals);
    if (!result)
        return errorReply();

    json reply;
    std::string err;
    json value;
    if (!FcxImage::encodeValue(result, value, err)) {
        Py_DECREF(result);
        reply["ok"] = false;
        reply["exc"] = "MarshalError";
        reply["msg"] = err;
        return reply;
    }
    Py_DECREF(result);
    reply["ok"] = true;
    reply["val"] = std::move(value);
    return reply;
}

EXPORT(fcx_call) void *fcx_call(const uint8_t *req_bytes, uint32_t len)
{
    json reply;
    try {
        json req = json::from_cbor(req_bytes, req_bytes + len);
        auto op = req.find("op");
        if (op == req.end() || !op->is_string())
            reply = protocolError("request without op");
        else if (op->get_ref<const std::string &>() == FcxWire::OpEval)
            reply = dispatchEval(req);
        else
            reply = protocolError("unknown op");
    }
    catch (const json::exception &e) {
        reply = protocolError(e.what());
    }
    if (PyErr_Occurred())
        PyErr_Clear();
    return replyBytes(json::to_cbor(reply));
}

EXPORT(fcx_eval) void *fcx_eval(const char *src, uint32_t len)
{
    char *source = (char *)malloc(len + 1);
    if (!source)
        return nullptr;
    memcpy(source, src, len);
    source[len] = 0;
    PyObject *result = PyRun_String(source, Py_eval_input,
                                    eval_globals, eval_globals);
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

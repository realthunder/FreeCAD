/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#include "PreCompiled.h"

#include <cstring>
#include <string>

#include <Base/BoundBoxPy.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/MatrixPy.h>
#include <Base/PlacementPy.h>
#include <Base/QuantityPy.h>
#include <Base/RotationPy.h>
#include <Base/UnitPy.h>
#include <Base/VectorPy.h>

#include "ExpressionImage/FcxWire.h"
#include "ExpressionImageBridge.h"
#include "ExpressionSecurityRuntime.h"

using json = nlohmann::json;

namespace App
{
namespace ExpressionSandbox
{

uint64_t HandleTable::add(PyObject* obj)
{
    Py_INCREF(obj);
    uint64_t id = nextId++;
    objects[id] = obj;
    return id;
}

PyObject* HandleTable::get(uint64_t id) const
{
    auto it = objects.find(id);
    return it == objects.end() ? nullptr : it->second;
}

void HandleTable::release(uint64_t id)
{
    auto it = objects.find(id);
    if (it != objects.end()) {
        Py_DECREF(it->second);
        objects.erase(it);
    }
}

void HandleTable::clear()
{
    for (auto& entry : objects)
        Py_DECREF(entry.second);
    objects.clear();
}

// ---- host value marshal (mirror of the image's ImageMarshal.cpp,
// ---- except non-marshalable objects become handles, never errors) ----

static json encodeUnit(const Base::Unit& u)
{
    const Base::UnitSignature& s = u.getSignature();
    return json::array({s.Length, s.Mass, s.Time, s.ElectricCurrent,
                        s.ThermodynamicTemperature, s.AmountOfSubstance,
                        s.LuminousIntensity, s.Angle});
}

json encodeHostValue(HandleTable& table, PyObject* obj)
{
    if (!obj || obj == Py_None)
        return json();
    if (PyBool_Check(obj))
        return obj == Py_True;
    if (PyLong_Check(obj)) {
        int overflow = 0;
        long long v = PyLong_AsLongLongAndOverflow(obj, &overflow);
        if (overflow == 0 && !PyErr_Occurred())
            return (int64_t)v;
        PyErr_Clear();
        unsigned long long u = PyLong_AsUnsignedLongLong(obj);
        if (!PyErr_Occurred())
            return (uint64_t)u;
        PyErr_Clear();
        // beyond 64 bits: keep it usable as a handle
    }
    else if (PyFloat_Check(obj))
        return PyFloat_AS_DOUBLE(obj);
    else if (PyUnicode_Check(obj)) {
        Py_ssize_t n = 0;
        const char* s = PyUnicode_AsUTF8AndSize(obj, &n);
        if (s)
            return std::string(s, (size_t)n);
        PyErr_Clear();
    }
    else if (PyBytes_Check(obj))
        return json::binary(std::vector<uint8_t>(
            (uint8_t*)PyBytes_AS_STRING(obj),
            (uint8_t*)PyBytes_AS_STRING(obj) + PyBytes_GET_SIZE(obj)));
    else if (PyObject_TypeCheck(obj, &Base::VectorPy::Type)) {
        const Base::Vector3d& v =
            *static_cast<Base::VectorPy*>(obj)->getVectorPtr();
        return {{FcxWire::TagKey, FcxWire::TagVector},
                {"v", json::array({v.x, v.y, v.z})}};
    }
    else if (PyObject_TypeCheck(obj, &Base::RotationPy::Type)) {
        double q0, q1, q2, q3;
        static_cast<Base::RotationPy*>(obj)->getRotationPtr()->getValue(
            q0, q1, q2, q3);
        return {{FcxWire::TagKey, FcxWire::TagRotation},
                {"v", json::array({q0, q1, q2, q3})}};
    }
    else if (PyObject_TypeCheck(obj, &Base::PlacementPy::Type)) {
        const Base::Placement& p =
            *static_cast<Base::PlacementPy*>(obj)->getPlacementPtr();
        const Base::Vector3d& t = p.getPosition();
        double q0, q1, q2, q3;
        p.getRotation().getValue(q0, q1, q2, q3);
        return {{FcxWire::TagKey, FcxWire::TagPlacement},
                {"p", json::array({t.x, t.y, t.z})},
                {"r", json::array({q0, q1, q2, q3})}};
    }
    else if (PyObject_TypeCheck(obj, &Base::MatrixPy::Type)) {
        const Base::Matrix4D& m =
            *static_cast<Base::MatrixPy*>(obj)->getMatrixPtr();
        json arr = json::array();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                arr.push_back(m[r][c]);
        return {{FcxWire::TagKey, FcxWire::TagMatrix}, {"v", std::move(arr)}};
    }
    else if (PyObject_TypeCheck(obj, &Base::BoundBoxPy::Type)) {
        const Base::BoundBox3d& b =
            *static_cast<Base::BoundBoxPy*>(obj)->getBoundBoxPtr();
        return {{FcxWire::TagKey, FcxWire::TagBoundBox},
                {"v", json::array({b.MinX, b.MinY, b.MinZ,
                                   b.MaxX, b.MaxY, b.MaxZ})}};
    }
    else if (PyObject_TypeCheck(obj, &Base::QuantityPy::Type)) {
        const Base::Quantity& q =
            *static_cast<Base::QuantityPy*>(obj)->getQuantityPtr();
        return {{FcxWire::TagKey, FcxWire::TagQuantity},
                {"v", q.getValue()},
                {"u", encodeUnit(q.getUnit())}};
    }
    else if (PyList_Check(obj) || PyTuple_Check(obj)) {
        PyObject* seq = PySequence_Fast(obj, "sequence");
        if (seq) {
            json arr = json::array();
            Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
            for (Py_ssize_t i = 0; i < n; ++i)
                arr.push_back(encodeHostValue(
                    table, PySequence_Fast_GET_ITEM(seq, i)));
            Py_DECREF(seq);
            return arr;
        }
        PyErr_Clear();
    }
    else if (PyDict_Check(obj)) {
        json map = json::object();
        PyObject *key = nullptr, *value = nullptr;
        Py_ssize_t pos = 0;
        bool allStringKeys = true;
        while (PyDict_Next(obj, &pos, &key, &value)) {
            if (!PyUnicode_Check(key)) {
                allStringKeys = false;
                break;
            }
            map[PyUnicode_AsUTF8(key)] = encodeHostValue(table, value);
        }
        if (allStringKeys)
            return map;
    }
    uint64_t id = table.add(obj);
    return {{FcxWire::TagKey, FcxWire::TagHandle},
            {"id", id},
            {"ty", Py_TYPE(obj)->tp_name}};
}

static bool getDoubles(const json& arr, double* out, size_t n)
{
    if (!arr.is_array() || arr.size() != n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (!arr[i].is_number())
            return false;
        out[i] = arr[i].get<double>();
    }
    return true;
}

PyObject* decodeHostValue(const HandleTable& table, const json& v)
{
    switch (v.type()) {
        case json::value_t::null:
            Py_RETURN_NONE;
        case json::value_t::boolean:
            return PyBool_FromLong(v.get<bool>());
        case json::value_t::number_integer:
            return PyLong_FromLongLong(v.get<int64_t>());
        case json::value_t::number_unsigned:
            return PyLong_FromUnsignedLongLong(v.get<uint64_t>());
        case json::value_t::number_float:
            return PyFloat_FromDouble(v.get<double>());
        case json::value_t::string: {
            const auto& s = v.get_ref<const std::string&>();
            return PyUnicode_FromStringAndSize(s.data(), (Py_ssize_t)s.size());
        }
        case json::value_t::binary: {
            const auto& b = v.get_binary();
            return PyBytes_FromStringAndSize(
                reinterpret_cast<const char*>(b.data()), (Py_ssize_t)b.size());
        }
        case json::value_t::array: {
            PyObject* list = PyList_New((Py_ssize_t)v.size());
            if (!list)
                return nullptr;
            Py_ssize_t i = 0;
            for (const auto& item : v) {
                PyObject* obj = decodeHostValue(table, item);
                if (!obj) {
                    Py_DECREF(list);
                    return nullptr;
                }
                PyList_SET_ITEM(list, i++, obj);
            }
            return list;
        }
        case json::value_t::object:
            break;
        default:
            PyErr_SetString(PyExc_ValueError, "unsupported wire value");
            return nullptr;
    }

    auto tag = v.find(FcxWire::TagKey);
    if (tag == v.end()) {
        PyObject* dict = PyDict_New();
        if (!dict)
            return nullptr;
        for (auto it = v.begin(); it != v.end(); ++it) {
            PyObject* obj = decodeHostValue(table, it.value());
            if (!obj || PyDict_SetItemString(dict, it.key().c_str(), obj) < 0) {
                Py_XDECREF(obj);
                Py_DECREF(dict);
                return nullptr;
            }
            Py_DECREF(obj);
        }
        return dict;
    }

    const std::string& t = tag->get_ref<const std::string&>();
    if (t == FcxWire::TagVector) {
        double d[3];
        if (getDoubles(v.value("v", json()), d, 3))
            return new Base::VectorPy(Base::Vector3d(d[0], d[1], d[2]));
    }
    else if (t == FcxWire::TagRotation) {
        double d[4];
        if (getDoubles(v.value("v", json()), d, 4))
            return new Base::RotationPy(Base::Rotation(d[0], d[1], d[2], d[3]));
    }
    else if (t == FcxWire::TagPlacement) {
        double p[3], r[4];
        if (getDoubles(v.value("p", json()), p, 3)
                && getDoubles(v.value("r", json()), r, 4))
            return new Base::PlacementPy(Base::Placement(
                Base::Vector3d(p[0], p[1], p[2]),
                Base::Rotation(r[0], r[1], r[2], r[3])));
    }
    else if (t == FcxWire::TagMatrix) {
        double m[16];
        if (getDoubles(v.value("v", json()), m, 16))
            return new Base::MatrixPy(Base::Matrix4D(
                m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9],
                m[10], m[11], m[12], m[13], m[14], m[15]));
    }
    else if (t == FcxWire::TagBoundBox) {
        double b[6];
        if (getDoubles(v.value("v", json()), b, 6))
            return new Base::BoundBoxPy(new Base::BoundBox3d(
                b[0], b[1], b[2], b[3], b[4], b[5]));
    }
    else if (t == FcxWire::TagQuantity) {
        auto val = v.find("v");
        auto unit = v.find("u");
        if (val != v.end() && val->is_number() && unit != v.end()
                && unit->is_array() && unit->size() == 8) {
            int8_t e[8];
            for (size_t i = 0; i < 8; ++i) {
                if (!(*unit)[i].is_number_integer())
                    goto bad;
                e[i] = (int8_t)(*unit)[i].get<int>();
            }
            return new Base::QuantityPy(new Base::Quantity(
                val->get<double>(),
                Base::Unit(e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7])));
        }
    }
    else if (t == FcxWire::TagHandle) {
        auto id = v.find("id");
        if (id != v.end() && id->is_number_integer()) {
            PyObject* obj = table.get(id->get<uint64_t>());
            if (!obj) {
                PyErr_SetString(PyExc_ReferenceError, "stale host handle");
                return nullptr;
            }
            Py_INCREF(obj);
            return obj;
        }
    }
bad:
    PyErr_SetString(PyExc_ValueError, "malformed typed wire value");
    return nullptr;
}

// ---- op dispatch ----

static json okReply(json val)
{
    json r;
    r["ok"] = true;
    r["val"] = std::move(val);
    return r;
}

static json errReply(const char* exc, const std::string& msg)
{
    json r;
    r["ok"] = false;
    r["exc"] = exc;
    r["msg"] = msg;
    return r;
}

/// Current Python error -> error reply, mirroring the image's encoding
/// (bare type name, str(value)).
static json pyErrorReply()
{
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    PyErr_NormalizeException(&type, &value, &trace);
    const char* excName = "Exception";
    if (type && PyType_Check(type))
        excName = ((PyTypeObject*)type)->tp_name;
    if (const char* dot = strrchr(excName, '.'))
        excName = dot + 1;
    PyObject* msg = value ? PyObject_Str(value) : nullptr;
    const char* text = msg ? PyUnicode_AsUTF8(msg) : nullptr;
    json r = errReply(excName, text ? text : "unprintable error");
    Py_XDECREF(msg);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(trace);
    return r;
}

/// "module.qualname" of a callable for the permission classification
/// (checkCallablePermission routes on the module root).
static std::string callableName(PyObject* obj)
{
    std::string name;
    PyObject* mod = PyObject_GetAttrString(obj, "__module__");
    if (!mod)
        PyErr_Clear();
    else if (PyUnicode_Check(mod))
        name = PyUnicode_AsUTF8(mod);
    Py_XDECREF(mod);
    PyObject* qn = PyObject_GetAttrString(obj, "__qualname__");
    if (!qn)
        PyErr_Clear();
    else if (PyUnicode_Check(qn)) {
        if (!name.empty())
            name += ".";
        name += PyUnicode_AsUTF8(qn);
    }
    Py_XDECREF(qn);
    if (name.empty())
        name = Py_TYPE(obj)->tp_name;
    return name;
}

static json encodeResult(HandleTable& table, PyObject* result)
{
    json val = encodeHostValue(table, result);
    Py_DECREF(result);
    return okReply(std::move(val));
}

json dispatchHostOp(HandleTable& table, const json& req)
{
    Base::PyGILStateLocker lock;
    try {
        std::string op = req.value("op", "");
        uint64_t id = req.value("h", (uint64_t)0);
        if (op == FcxWire::OpRelease) {
            table.release(id);
            return okReply(json());
        }
        PyObject* base = table.get(id);
        if (!base)
            return errReply("ReferenceError", "stale host handle");

        if (op == FcxWire::OpGetAttr || op == FcxWire::OpReadProp) {
            auto a = req.find("a");
            if (a == req.end() || !a->is_string())
                return errReply("ProtocolError", "get_attr without a name");
            const std::string& name = a->get_ref<const std::string&>();
            PyObject* result = PyObject_GetAttrString(base, name.c_str());
            if (!result)
                return pyErrorReply();
            try {
                ExpressionSecurity::checkGetattr(base, name.c_str(), result);
            }
            catch (...) {
                Py_DECREF(result);
                throw;
            }
            return encodeResult(table, result);
        }

        if (op == FcxWire::OpCall) {
            ExpressionSecurity::checkCallablePermission(callableName(base),
                                                        base);
            PyObject* argTuple = nullptr;
            auto a = req.find("a");
            if (a == req.end())
                argTuple = PyTuple_New(0);
            else {
                PyObject* list = decodeHostValue(table, *a);
                if (!list)
                    return pyErrorReply();
                argTuple = PySequence_Tuple(list);
                Py_DECREF(list);
            }
            if (!argTuple)
                return pyErrorReply();
            PyObject* kwargs = nullptr;
            auto k = req.find("k");
            if (k != req.end() && k->is_object() && !k->empty()) {
                kwargs = decodeHostValue(table, *k);
                if (!kwargs) {
                    Py_DECREF(argTuple);
                    return pyErrorReply();
                }
            }
            PyObject* result = PyObject_Call(base, argTuple, kwargs);
            Py_DECREF(argTuple);
            Py_XDECREF(kwargs);
            if (!result)
                return pyErrorReply();
            return encodeResult(table, result);
        }

        if (op == FcxWire::OpGetItem) {
            auto a = req.find("a");
            if (a == req.end())
                return errReply("ProtocolError", "get_item without a key");
            PyObject* key = decodeHostValue(table, *a);
            if (!key)
                return pyErrorReply();
            PyObject* result = PyObject_GetItem(base, key);
            Py_DECREF(key);
            if (!result)
                return pyErrorReply();
            return encodeResult(table, result);
        }

        if (op == FcxWire::OpLen) {
            Py_ssize_t n = PyObject_Size(base);
            if (n < 0)
                return pyErrorReply();
            return okReply(json((int64_t)n));
        }

        if (op == FcxWire::OpResolveAlias) {
            // RangeExpression::getRange's reach-back: alias -> cell
            // address on the evaluation owner (a sheet).  This is
            // identifier resolution, part of the document read the
            // evaluation already holds -- not an app.query call.
            ExpressionSecurity::checkPermission(
                ExpressionSecurity::Permission::DocReadSelf);
            auto a = req.find("a");
            if (a == req.end() || !a->is_string())
                return errReply("ProtocolError",
                                "resolve_alias without an alias");
            PyObject* func = PyObject_GetAttrString(base, "getCellFromAlias");
            if (!func)
                return pyErrorReply();
            PyObject* result = PyObject_CallFunction(
                func, "s", a->get_ref<const std::string&>().c_str());
            Py_DECREF(func);
            if (!result)
                return pyErrorReply();
            PyObject* str = PyObject_Str(result);
            Py_DECREF(result);
            if (!str)
                return pyErrorReply();
            const char* text = PyUnicode_AsUTF8(str);
            json val = std::string(text ? text : "");
            Py_DECREF(str);
            return okReply(std::move(val));
        }
        return errReply("ProtocolError", "unknown bridge op");
    }
    catch (const ExpressionSecurity::PermissionNeededException& e) {
        if (PyErr_Occurred())
            PyErr_Clear();
        return errReply("PermissionError", e.what());
    }
    catch (const Base::Exception& e) {
        if (PyErr_Occurred())
            PyErr_Clear();
        return errReply("RuntimeError", e.what());
    }
    catch (const std::exception& e) {
        if (PyErr_Occurred())
            PyErr_Clear();
        return errReply("RuntimeError", e.what());
    }
}

}  // namespace ExpressionSandbox
}  // namespace App

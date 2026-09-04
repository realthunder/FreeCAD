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
#include "ExpressionGuestProxy.h"
#include "ExpressionImageBridge.h"
#include "ExpressionSecurityRuntime.h"
#include "Extension.h"
#include "ExtensionContainer.h"
#include "ExtensionContainerPy.h"
#include "PropertyContainerPy.h"
#ifdef FC_EXPR_PYODIDE_HOST
#include "ExpressionPyodide.h"
#endif

using json = nlohmann::json;

namespace App
{
namespace ExpressionSandbox
{

// ---- the closed member table (generated from the <Sandbox/> XML
// ---- annotations; docs/ExpressionSandbox.md sec 7.5) ----

#include "FcxDispatch.inc"

using MemberIndex =
    std::unordered_map<std::string, std::unordered_map<std::string, const FacadeMember*>>;

static const MemberIndex& facadeIndex()
{
    static const MemberIndex index = [] {
        MemberIndex idx;
        for (const auto& m : FacadeTable)
            idx[m.type][m.member] = &m;
        return idx;
    }();
    return index;
}

const ModuleMember* moduleMemberLookup(const std::string& qualified)
{
    using ModuleIndex =
        std::unordered_map<std::string, std::unordered_map<std::string, const ModuleMember*>>;
    static const ModuleIndex index = [] {
        ModuleIndex idx;
        for (const auto& m : ModuleTable)
            idx[m.module][m.name] = &m;
        return idx;
    }();
    auto dot = qualified.rfind('.');
    if (dot == std::string::npos)
        return nullptr;
    auto it = index.find(qualified.substr(0, dot));
    if (it == index.end())
        return nullptr;
    auto mit = it->second.find(qualified.substr(dot + 1));
    return mit == it->second.end() ? nullptr : mit->second;
}

const char* facadeKeyFor(PyTypeObject* type)
{
    const auto& idx = facadeIndex();
    PyObject* mro = type->tp_mro;
    if (!mro || !PyTuple_Check(mro)) {
        auto it = idx.find(type->tp_name);
        return it == idx.end() ? nullptr : it->second.begin()->second->type;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); ++i) {
        auto* t = reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(mro, i));
        auto it = idx.find(t->tp_name);
        if (it != idx.end())
            return it->second.begin()->second->type;
    }
    return nullptr;
}

const FacadeMember* facadeMemberLookup(PyTypeObject* type, const char* member)
{
    const auto& idx = facadeIndex();
    auto lookup = [&](const char* typeName) -> const FacadeMember* {
        auto it = idx.find(typeName);
        if (it == idx.end())
            return nullptr;
        auto mit = it->second.find(member);
        return mit == it->second.end() ? nullptr : mit->second;
    };
    PyObject* mro = type->tp_mro;
    if (!mro || !PyTuple_Check(mro))
        return lookup(type->tp_name);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); ++i) {
        auto* t = reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(mro, i));
        if (const FacadeMember* m = lookup(t->tp_name))
            return m;
    }
    return nullptr;
}

/** Extension methods (Part::AttachExtension's positionBySupport on a
 * Draft Wire) are injected per INSTANCE by ExtensionContainerPy's
 * attribute lookup and never appear in the type's mro, so a declared
 * member is looked up on the type first and then on each extension's
 * own binding type.  Caller holds the GIL.
 */
static const FacadeMember* memberLookupOn(PyObject* base, const char* member)
{
    if (const FacadeMember* m = facadeMemberLookup(Py_TYPE(base), member))
        return m;
    if (!PyObject_TypeCheck(base, &App::ExtensionContainerPy::Type))
        return nullptr;
    auto* container = static_cast<App::ExtensionContainerPy*>(base)->getExtensionContainerPtr();
    if (!container)
        return nullptr;
    for (auto it = container->extensionBegin(); it != container->extensionEnd(); ++it) {
        PyObject* ext = it->second->getExtensionPyObject();
        if (!ext) {
            PyErr_Clear();
            continue;
        }
        const FacadeMember* m = facadeMemberLookup(Py_TYPE(ext), member);
        Py_DECREF(ext);
        if (m)
            return m;
    }
    return nullptr;
}

/// The facade keys of the object's extensions that have one (the
/// handle's "ext"), so the guest composes its proxy class from them.
static json extensionFacadeKeys(PyObject* obj)
{
    json keys = json::array();
    if (!PyObject_TypeCheck(obj, &App::ExtensionContainerPy::Type))
        return keys;
    auto* container = static_cast<App::ExtensionContainerPy*>(obj)->getExtensionContainerPtr();
    if (!container)
        return keys;
    for (auto it = container->extensionBegin(); it != container->extensionEnd(); ++it) {
        PyObject* ext = it->second->getExtensionPyObject();
        if (!ext) {
            PyErr_Clear();
            continue;
        }
        if (const char* fc = facadeKeyFor(Py_TYPE(ext)))
            keys.push_back(fc);
        Py_DECREF(ext);
    }
    return keys;
}

/** Bound method of an annotated call-tier member?  Then it crosses as
 * a bound-member handle on its base object ({"t":"h","id":<base>,
 * "fc":...,"m":<member>}): the image resolves the facade method on the
 * base proxy, so `Obj.Shape.isNull` packed at pack time stays callable
 * in-image while an UNDECLARED method crosses as an inert handle.
 */
static bool boundDeclaredMethod(PyObject* obj, PyObject** self, std::string& name)
{
    name.clear();
    if (PyMethod_Check(obj)) {
        *self = PyMethod_GET_SELF(obj);
        PyObject* fn = PyMethod_GET_FUNCTION(obj);
        PyObject* n = fn ? PyObject_GetAttrString(fn, "__name__") : nullptr;
        if (n && PyUnicode_Check(n))
            name = PyUnicode_AsUTF8(n);
        Py_XDECREF(n);
        PyErr_Clear();
    }
    else if (PyCFunction_Check(obj)) {
        *self = PyCFunction_GET_SELF(obj);
        if (const char* mlName = reinterpret_cast<PyCFunctionObject*>(obj)->m_ml->ml_name)
            name = mlName;
    }
    else
        return false;
    if (!*self || name.empty() || PyModule_Check(*self))
        return false;
    const FacadeMember* m = memberLookupOn(*self, name.c_str());
    return m && m->kind == FacadeKind::Method;
}

uint64_t HandleTable::add(PyObject* obj)
{
    Py_INCREF(obj);
    uint64_t id = nextId++;
    objects[id] = obj;
    ++minted;
    return id;
}

PyObject* HandleTable::get(uint64_t id) const
{
    auto it = objects.find(id);
    return it == objects.end() ? nullptr : it->second;
}

void HandleTable::release(uint64_t id)
{
    if (defer) {
        deferred.push_back(id);
        return;
    }
    auto it = objects.find(id);
    if (it != objects.end()) {
        Py_DECREF(it->second);
        objects.erase(it);
    }
}

void HandleTable::flushDeferred()
{
    bool wasDeferring = defer;
    defer = false;
    auto ids = std::move(deferred);
    deferred.clear();
    for (uint64_t id : ids)
        release(id);
    defer = wasDeferring;
}

void HandleTable::clear()
{
    deferred.clear();
    defer = false;
    ownerObj = nullptr;
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
    // Part.ShapeList (this fork's Shape.Edges/Faces/...: a sequence
    // type of its own, not a list) crosses as a list of handles -- the
    // classic FreeCAD shape, which is what Draft's slicing and
    // concatenation of edge lists expect.
    else if (PyList_Check(obj) || PyTuple_Check(obj)
             || std::strcmp(Py_TYPE(obj)->tp_name, "Part.ShapeList") == 0) {
        PyObject* seq = PySequence_Fast(obj, "sequence");
        if (seq) {
            json arr = json::array();
            Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
            for (Py_ssize_t i = 0; i < n; ++i)
                arr.push_back(encodeHostValue(
                    table, PySequence_Fast_GET_ITEM(seq, i)));
            bool isTuple = PyTuple_Check(obj);
            Py_DECREF(seq);
            if (isTuple)
                return json {{FcxWire::TagKey, FcxWire::TagTuple},
                             {"v", std::move(arr)}};
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
    // a Proxy that lives in the guest: its stand-in crosses back as the
    // guest's own instance, never as a handle on the stand-in
    else if (isGuestProxy(obj))
        return {{FcxWire::TagKey, FcxWire::TagGuestProxy}, {"id", guestProxyId(obj)}};
    {
        PyObject* self = nullptr;
        std::string member;
        if (boundDeclaredMethod(obj, &self, member)) {
            json h = {{FcxWire::TagKey, FcxWire::TagHandle},
                      {"id", table.add(self)},
                      {"ty", Py_TYPE(self)->tp_name},
                      {"m", member}};
            if (const char* fc = facadeKeyFor(Py_TYPE(self)))
                h["fc"] = fc;
            json ext = extensionFacadeKeys(self);
            if (!ext.empty())
                h["ext"] = std::move(ext);
            return h;
        }
    }
    uint64_t id = table.add(obj);
    json h = {{FcxWire::TagKey, FcxWire::TagHandle},
              {"id", id},
              {"ty", Py_TYPE(obj)->tp_name}};
    if (const char* fc = facadeKeyFor(Py_TYPE(obj)))
        h["fc"] = fc;
    json ext = extensionFacadeKeys(obj);
    if (!ext.empty())
        h["ext"] = std::move(ext);
    return h;
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
    if (t == FcxWire::TagType) {
        // a declared module member named by the guest (Part.Edge as an
        // argument): the object itself, from the host module
        auto q = v.find("q");
        const ModuleMember* mm = q != v.end() && q->is_string()
            ? moduleMemberLookup(q->get_ref<const std::string&>())
            : nullptr;
        if (!mm || mm->kind != ModuleKind::Callable) {
            PyErr_SetString(PyExc_TypeError, "type reference is not a declared module member");
            return nullptr;
        }
        PyObject* mod = PyImport_ImportModule(mm->module);
        if (!mod)
            return nullptr;
        PyObject* attr = PyObject_GetAttrString(mod, mm->name);
        Py_DECREF(mod);
        return attr;
    }
    if (t == FcxWire::TagTuple) {
        auto items = v.find("v");
        if (items == v.end() || !items->is_array()) {
            PyErr_SetString(PyExc_ValueError, "malformed tuple value");
            return nullptr;
        }
        PyObject* tuple = PyTuple_New((Py_ssize_t)items->size());
        if (!tuple)
            return nullptr;
        Py_ssize_t i = 0;
        for (const auto& item : *items) {
            PyObject* obj = decodeHostValue(table, item);
            if (!obj) {
                Py_DECREF(tuple);
                return nullptr;
            }
            PyTuple_SET_ITEM(tuple, i++, obj);
        }
        return tuple;
    }
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
    else if (t == FcxWire::TagGuestProxy) {
        // the guest's Proxy descriptor (write_prop Proxy, a proxy_new
        // reply): the host stand-in, one per guest proxy
        return makeGuestProxy(v);
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

/// Call `callable` (stolen) with the request's "a" (args list) and "k"
/// (kwargs map), both decoded through the table so handles dereference
/// to live objects; the encoded result or the Python error.
static json callWithWireArgs(HandleTable& table, PyObject* callable, const json& req)
{
    PyObject* argTuple = nullptr;
    auto a = req.find("a");
    if (a == req.end())
        argTuple = PyTuple_New(0);
    else {
        PyObject* list = decodeHostValue(table, *a);
        if (!list) {
            Py_DECREF(callable);
            return pyErrorReply();
        }
        argTuple = PySequence_Tuple(list);
        Py_DECREF(list);
    }
    if (!argTuple) {
        Py_DECREF(callable);
        return pyErrorReply();
    }
    PyObject* kwargs = nullptr;
    auto k = req.find("k");
    if (k != req.end() && k->is_object() && !k->empty()) {
        kwargs = decodeHostValue(table, *k);
        if (!kwargs) {
            Py_DECREF(callable);
            Py_DECREF(argTuple);
            return pyErrorReply();
        }
    }
    PyObject* result = PyObject_Call(callable, argTuple, kwargs);
    Py_DECREF(callable);
    Py_DECREF(argTuple);
    Py_XDECREF(kwargs);
    if (!result)
        return pyErrorReply();
    return encodeResult(table, result);
}

std::vector<unsigned char> dispatchHostOpFixed(HandleTable& table,
                                               const unsigned char* data,
                                               std::size_t len,
                                               std::string& opName)
{
    // both ends are little-endian (wasm32, x86-64/arm64 hosts); the
    // layout is defined LE and read byte-wise to stay so
    auto le64 = [](const unsigned char* p) {
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i)
            v = (v << 8) | p[i];
        return v;
    };
    json reply;
    if (len < 12 || data[0] != FcxWire::FixedRequestMagic
            || (data[1] != FcxWire::FixedOpReadProp && data[1] != FcxWire::FixedOpGetAttr)) {
        reply = {{"ok", false}, {"exc", "ProtocolError"}, {"msg", "malformed fixed-layout request"}};
        opName = "?";
    }
    else {
        const uint64_t id = le64(data + 2);
        const size_t nlen = data[10] | (size_t(data[11]) << 8);
        if (12 + nlen > len) {
            reply = {{"ok", false}, {"exc", "ProtocolError"}, {"msg", "fixed-layout name overruns"}};
            opName = "?";
        }
        else {
            opName = data[1] == FcxWire::FixedOpReadProp ? FcxWire::OpReadProp : FcxWire::OpGetAttr;
            json req = {{"op", opName}, {"h", id},
                        {"a", std::string(reinterpret_cast<const char*>(data + 12), nlen)}};
            reply = dispatchHostOp(table, req);
        }
    }

    std::vector<unsigned char> out;
    out.reserve(32);
    out.push_back(FcxWire::FixedReplyMagic);
    auto put64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<unsigned char>(v >> (8 * i)));
    };
    auto putDouble = [&](double d) {
        uint64_t bits;
        std::memcpy(&bits, &d, 8);
        put64(bits);
    };
    if (reply.value("ok", false)) {
        auto val = reply.find("val");
        if (val != reply.end()) {
            if (val->is_number_float()) {
                out.push_back(FcxWire::FixedKindFloat);
                putDouble(val->get<double>());
                return out;
            }
            if (val->is_boolean()) {
                out.push_back(FcxWire::FixedKindBool);
                out.push_back(val->get<bool>() ? 1 : 0);
                return out;
            }
            if (val->is_number_integer()) {
                out.push_back(FcxWire::FixedKindInt);
                put64(static_cast<uint64_t>(val->get<int64_t>()));
                return out;
            }
            if (val->is_string()) {
                const auto& s = val->get_ref<const std::string&>();
                if (s.size() <= 0xFFFFFFFFu) {
                    out.push_back(FcxWire::FixedKindString);
                    uint32_t n = static_cast<uint32_t>(s.size());
                    for (int i = 0; i < 4; ++i)
                        out.push_back(static_cast<unsigned char>(n >> (8 * i)));
                    out.insert(out.end(), s.begin(), s.end());
                    return out;
                }
            }
            if (val->is_object() && val->value(FcxWire::TagKey, "") == FcxWire::TagVector) {
                auto v = val->find("v");
                if (v != val->end() && v->is_array() && v->size() == 3) {
                    out.push_back(FcxWire::FixedKindVector);
                    for (const auto& c : *v)
                        putDouble(c.get<double>());
                    return out;
                }
            }
        }
    }
    out.push_back(FcxWire::FixedKindCbor);
    auto cbor = json::to_cbor(reply);
    out.insert(out.end(), cbor.begin(), cbor.end());
    return out;
}

json dispatchHostOp(HandleTable& table, const json& req)
{
    Base::PyGILStateLocker lock;
    try {
        // releases piggybacking on this request (FcxWire "r")
        auto rides = req.find("r");
        if (rides != req.end() && rides->is_array())
            for (const auto& rid : *rides)
                if (rid.is_number_unsigned())
                    table.release(rid.get<uint64_t>());

        std::string op = req.value("op", "");
        uint64_t id = req.value("h", (uint64_t)0);
        if (op == FcxWire::OpRelease) {
            // one id in "h", or several in "a"
            auto a = req.find("a");
            if (a != req.end() && a->is_array()) {
                for (const auto& rid : *a)
                    if (rid.is_number_unsigned())
                        table.release(rid.get<uint64_t>());
            }
            else
                table.release(id);
            return okReply(json());
        }
        if (op == FcxWire::OpPkgMissing) {
#ifdef FC_EXPR_PYODIDE_HOST
            auto a = req.find("a");
            if (a == req.end() || !a->is_string())
                return errReply("ProtocolError", "pkg.missing without a name");
            return okReply(json(Pyodide::missingImport(a->get_ref<const std::string&>())));
#else
            return okReply(json(""));
#endif
        }
        if (op == FcxWire::OpModCall || op == FcxWire::OpModGet) {
            // The module facades: no handle, a declared "Module.name",
            // each module under the catalog permission its table row
            // names -- a curated constructor list is a geometry call,
            // not a host import (geom.call); Draft's preference reader
            // is a read-only parameter access (prefs.read).  docs/Sandbox.md 3.2.
            auto m = req.find("m");
            if (m == req.end() || !m->is_string())
                return errReply("ProtocolError", "module op without a name");
            const std::string& qual = m->get_ref<const std::string&>();
            const ModuleMember* mm = moduleMemberLookup(qual);
            const bool isGet = op == FcxWire::OpModGet;
            if (!mm || mm->kind != (isGet ? ModuleKind::Constant : ModuleKind::Callable))
                return errReply("ProtocolError",
                                "'" + qual + "' is not declared for sandbox access");
            auto perm = ExpressionSecurity::permissionFromName(mm->permission);
            if (!perm)
                return errReply("ProtocolError",
                                "'" + qual + "' names an unknown permission");
            ExpressionSecurity::checkPermission(*perm);
            PyObject* mod = PyImport_ImportModule(mm->module);
            if (!mod)
                return pyErrorReply();
            PyObject* attr = PyObject_GetAttrString(mod, mm->name);
            Py_DECREF(mod);
            if (!attr)
                return pyErrorReply();
            if (isGet)
                return encodeResult(table, attr);
            return callWithWireArgs(table, attr, req);
        }

        PyObject* base = table.get(id);
        if (!base)
            return errReply("ReferenceError", "stale host handle");

        if (op == FcxWire::OpGetAttr) {
            auto a = req.find("a");
            if (a == req.end() || !a->is_string())
                return errReply("ProtocolError", "get_attr without a name");
            const std::string& name = a->get_ref<const std::string&>();
            // The closed table is the whole reachable surface: an
            // undeclared member is a protocol error, never a getattr
            // (docs/ExpressionSandbox.md sec 7.5).
            const FacadeMember* fm = memberLookupOn(base, name.c_str());
            if (!fm || fm->kind != FacadeKind::Attribute)
                return errReply("ProtocolError",
                                "member '" + name + "' of '"
                                    + Py_TYPE(base)->tp_name
                                    + "' is not declared for sandbox access");
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
            json reply = encodeResult(table, result);
            if (fm->tier == FacadeTier::Value && reply.value("ok", false)) {
                const json& val = reply["val"];
                if (val.is_object() && val.value(FcxWire::TagKey, "") == FcxWire::TagHandle)
                    return errReply("ProtocolError",
                                    "by-value member '" + name
                                        + "' produced a non-marshalable result");
            }
            return reply;
        }

        if (op == FcxWire::OpReadProp) {
            // Answered from the C++ property system, never host Python
            // (sec 7.5): dynamic properties are not XML members, but
            // getPropertyByName is typed, side-effect free, and still
            // permission-classified per principal.
            auto a = req.find("a");
            if (a == req.end() || !a->is_string())
                return errReply("ProtocolError", "read_prop without a name");
            const std::string& name = a->get_ref<const std::string&>();
            if (!PyObject_TypeCheck(base, &App::PropertyContainerPy::Type))
                return errReply("AttributeError",
                                "'" + std::string(Py_TYPE(base)->tp_name)
                                    + "' object has no attribute '" + name + "'");
            auto* container = static_cast<App::PropertyContainerPy*>(base)
                                  ->getPropertyContainerPtr();
            App::Property* prop =
                container ? container->getPropertyByName(name.c_str()) : nullptr;
            if (!prop)
                return errReply("AttributeError",
                                "'" + std::string(Py_TYPE(base)->tp_name)
                                    + "' object has no attribute '" + name + "'");
            PyObject* result = prop->getPyObject();
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

        // The write gate (FcxWire::OpWriteProp and the write-family
        // calls): the object must be the evaluation owner -- rung 2
        // writes self and nothing else -- and the principal must hold
        // doc.write.self.  A PermissionError, not a ProtocolError: the
        // request is well-formed, the principal is not allowed.
        auto writeGate = [&](const char* what) -> json {
            if (!table.owner() || base != table.owner())
                return errReply("PermissionError",
                                std::string(what)
                                    + ": writes are allowed on the evaluation owner only");
            ExpressionSecurity::checkPermission(
                ExpressionSecurity::Permission::DocWriteSelf);
            return json();
        };

        if (op == FcxWire::OpWriteProp) {
            auto a = req.find("a");
            if (a == req.end() || !a->is_string())
                return errReply("ProtocolError", "write_prop without a name");
            const std::string& name = a->get_ref<const std::string&>();
            auto v = req.find("v");
            if (v == req.end())
                return errReply("ProtocolError", "write_prop without a value");
            if (!PyObject_TypeCheck(base, &App::PropertyContainerPy::Type)) {
                // Not a document write: a value handle the transaction
                // holds -- a shape the guest built or a property's
                // copy (`p.Placement = obj.Placement` on the plane
                // WorkingPlaneProxy.execute makes).  The same closed
                // table as get_attr: only a declared attribute of the
                // type, the type's own setter does the work (a read-only
                // one refuses natively), under geom.call like the
                // constructors that made the object.
                const FacadeMember* fm = memberLookupOn(base, name.c_str());
                if (!fm || fm->kind != FacadeKind::Attribute)
                    return errReply("ProtocolError",
                                    "member '" + name + "' of '"
                                        + Py_TYPE(base)->tp_name
                                        + "' is not declared for sandbox access");
                ExpressionSecurity::checkPermission(ExpressionSecurity::Permission::GeomCall);
                PyObject* value = decodeHostValue(table, *v);
                if (!value)
                    return pyErrorReply();
                int rc = PyObject_SetAttrString(base, name.c_str(), value);
                Py_DECREF(value);
                if (rc != 0)
                    return pyErrorReply();
                return okReply(json());
            }
            json denied = writeGate("write_prop");
            if (!denied.is_null())
                return denied;
            // The C++ property system, as read_prop: typed, and the
            // property's own setPyObject does the conversion (and the
            // element-map re-mapping for a shape).
            auto* container = static_cast<App::PropertyContainerPy*>(base)
                                  ->getPropertyContainerPtr();
            App::Property* prop =
                container ? container->getPropertyByName(name.c_str()) : nullptr;
            if (!prop)
                return errReply("AttributeError",
                                "'" + std::string(Py_TYPE(base)->tp_name)
                                    + "' object has no attribute '" + name + "'");
            // Native setattr's one refusal (PropertyContainerPy::
            // setCustomAttributes): Immutable.  ReadOnly is the
            // editor's status, and Python writes it there too.
            if (prop->testStatus(App::Property::Immutable))
                return errReply("AttributeError",
                                "Attribute '" + name + "' of object '"
                                    + Py_TYPE(base)->tp_name + "' is read-only");
            PyObject* value = decodeHostValue(table, *v);
            if (!value)
                return pyErrorReply();
            try {
                prop->setPyObject(value);
            }
            catch (...) {
                Py_DECREF(value);
                throw;
            }
            Py_DECREF(value);
            return okReply(json());
        }

        if (op == FcxWire::OpCall) {
            // Member-addressed and table-gated: only a declared
            // call-tier member of the handle's type is invocable.
            auto m = req.find("m");
            if (m == req.end() || !m->is_string())
                return errReply("ProtocolError", "call without a member");
            const std::string& member = m->get_ref<const std::string&>();
            const FacadeMember* fm = memberLookupOn(base, member.c_str());
            if (!fm || fm->kind != FacadeKind::Method)
                return errReply("ProtocolError",
                                "method '" + member + "' of '"
                                    + Py_TYPE(base)->tp_name
                                    + "' is not declared for sandbox access");
            // The write family: declared like any call, but a write to
            // the document, so the owner-only gate applies.
            static const char* const writeFamily[] = {
                "addProperty", "removeProperty", "setPropertyStatus", "setEditorMode",
                "setGroupOfProperty", "recompute", "configLinkProperty", "setLink"};
            for (const char* w : writeFamily) {
                if (member == w) {
                    json denied = writeGate(w);
                    if (!denied.is_null())
                        return denied;
                    break;
                }
            }
            PyObject* callable = PyObject_GetAttrString(base, member.c_str());
            if (!callable)
                return pyErrorReply();
            try {
                ExpressionSecurity::checkCallablePermission(
                    callableName(callable), callable);
            }
            catch (...) {
                Py_DECREF(callable);
                throw;
            }
            return callWithWireArgs(table, callable, req);
        }

        if (op == FcxWire::OpGetItem) {
            auto a = req.find("a");
            if (a == req.end())
                return errReply("ProtocolError", "get_item without a key");
            // Builtin containers carry only already-crossed data; on
            // anything else __getitem__ is arbitrary host code, so it
            // rides the unsafe gate.
            if (!PyDict_Check(base) && !PyList_Check(base)
                    && !PyTuple_Check(base) && !PyUnicode_Check(base)
                    && !PyBytes_Check(base))
                ExpressionSecurity::checkPermission(
                    ExpressionSecurity::Permission::UnsafeGetattr);
            PyObject* key = decodeHostValue(table, *a);
            if (!key)
                return pyErrorReply();
            PyObject* result = PyObject_GetItem(base, key);
            Py_DECREF(key);
            if (!result)
                return pyErrorReply();
            return encodeResult(table, result);
        }

        if (op == FcxWire::OpBool || op == FcxWire::OpStr) {
            // The type's own slot: a C type's nb_bool/tp_str is not host
            // code; a heap type's (Python-defined) is, so it rides the
            // unsafe gate like any undeclared reach.
            if (Py_TYPE(base)->tp_flags & Py_TPFLAGS_HEAPTYPE)
                ExpressionSecurity::checkPermission(
                    ExpressionSecurity::Permission::UnsafeGetattr);
            if (op == FcxWire::OpBool) {
                int truth = PyObject_IsTrue(base);
                if (truth < 0)
                    return pyErrorReply();
                return okReply(json(truth != 0));
            }
            PyObject* s = PyObject_Str(base);
            if (!s)
                return pyErrorReply();
            return encodeResult(table, s);
        }

        if (op == FcxWire::OpLen) {
            if (!PyDict_Check(base) && !PyList_Check(base)
                    && !PyTuple_Check(base) && !PyUnicode_Check(base)
                    && !PyBytes_Check(base))
                ExpressionSecurity::checkPermission(
                    ExpressionSecurity::Permission::UnsafeGetattr);
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

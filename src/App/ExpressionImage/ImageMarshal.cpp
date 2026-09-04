#include "ImageMarshal.h"

#include <cstdint>

#include <Base/BoundBoxPy.h>
#include <Base/MatrixPy.h>
#include <Base/PlacementPy.h>
#include <Base/QuantityPy.h>
#include <Base/RotationPy.h>
#include <Base/UnitPy.h>
#include <Base/VectorPy.h>

#include "FcxWire.h"

using nlohmann::json;

namespace FcxImage
{

// The generated facade classes (exactly the <Sandbox/>-declared
// members, docs/ExpressionSandbox.md sec 7.5), produced by
// src/Tools/bindings/generateSandboxFacades.py at build time.
#include "FcxFacades.inc"

/* The hand-written proxy prelude the generated classes build on.
 * HostHandle carries _id/_ty in slots; declared members forward
 * member-addressed ops, everything else falls through __getattr__ =
 * read_prop, which the host answers from the C++ property system only.
 * There is deliberately NO __call__: an undeclared callable that
 * crossed as a handle is inert in-image.  Iteration works via the
 * __getitem__ sequence fallback: IndexError crosses the wire and ends
 * the loop.  __setattr__ is the write_prop op (the host decides:
 * owner only, doc.write.self); the slots themselves stay local.
 * __del__ queues the host table entry's release, which rides the next
 * request or the reply (never a hop of its own); by then the bridge
 * may be gone, hence the bare except.
 */
static const char ProxyPrelude[] =
    "import _fcx\n"
    "class HostHandle:\n"
    "    __slots__ = ('_id', '_ty')\n"
    "    def __repr__(self):\n"
    "        return '<HostHandle %s #%d>' % (self._ty, self._id)\n"
    "    def __getattr__(self, name):\n"
    "        if name.startswith('_'):\n"
    "            raise AttributeError(name)\n"
    "        return _fcx.op('read_prop', self._id, name)\n"
    "    def __setattr__(self, name, value):\n"
    "        if name in HostHandle.__slots__:\n"
    "            object.__setattr__(self, name, value)\n"
    "        else:\n"
    "            _fcx.op('write_prop', self._id, name, value)\n"
    "    def __bool__(self):\n"
    "        return _fcx.op('bool', self._id)\n"
    "    def __str__(self):\n"
    "        return _fcx.op('str', self._id)\n"
    "    def __getitem__(self, key):\n"
    "        return _fcx.op('get_item', self._id, key)\n"
    "    def __len__(self):\n"
    "        return _fcx.op('len', self._id)\n"
    "    def __del__(self):\n"
    "        try:\n"
    "            _fcx.release_later(self._id)\n"
    "        except Exception:\n"
    "            pass\n"
    "def _attr(name):\n"
    "    def get(self):\n"
    "        return _fcx.op('get_attr', self._id, name)\n"
    "    return property(get)\n"
    "def _method(name):\n"
    "    def call(self, *args, **kw):\n"
    "        return _fcx.op('call', self._id, name, args, kw)\n"
    "    call.__name__ = name\n"
    "    return call\n"
    // The module facades (generated MODULES): a module object per
    // entry whose callables forward over mod_call, whose constants are
    // read once over mod_get on first access, and whose exception
    // classes are local -- registered in EXCEPTIONS so the bridge can
    // raise them by the name the host reply carries.
    "import sys, types\n"
    "EXCEPTIONS = {}\n"
    "def _mod_call(qual):\n"
    "    def call(*args, **kw):\n"
    "        return _fcx.op('mod_call', 0, qual, args, kw)\n"
    "    call.__name__ = qual.rsplit('.', 1)[1]\n"
    "    call.__qualname__ = qual\n"
    "    return call\n"
    "class _FcxModule(types.ModuleType):\n"
    "    def __getattr__(self, name):\n"
    "        if name in self.__dict__.get('_fcx_constants', ()):\n"
    "            value = _fcx.op('mod_get', 0, self.__name__ + '.' + name)\n"
    "            self.__dict__[name] = value\n"
    "            return value\n"
    "        raise AttributeError(\"module '%s' has no attribute '%s'\" % (self.__name__, name))\n"
    // A callable that is also a facade type (Part.Shape, Part.Edge,
    // Part.LineSegment, ...) becomes a class: calling it constructs on
    // the host as any callable, and isinstance/issubclass against it
    // recognise the proxy class of that type -- Draft's `isinstance(e,
    // Part.Edge)` and `issubclass(type(e.Curve), Part.LineSegment)`.
    "class _FcxTypeMeta(type):\n"
    "    def __call__(cls, *args, **kw):\n"
    "        return _fcx.op('mod_call', 0, cls._fcx_qual, args, kw)\n"
    "    def __instancecheck__(cls, obj):\n"
    "        proxy = FACADES.get(cls._fcx_qual)\n"
    "        return proxy is not None and isinstance(obj, proxy)\n"
    "    def __subclasscheck__(cls, sub):\n"
    "        proxy = FACADES.get(cls._fcx_qual)\n"
    "        return proxy is not None and isinstance(sub, type) and issubclass(sub, proxy)\n"
    "def _install_modules(modules):\n"
    "    for modname, spec in modules.items():\n"
    "        m = _FcxModule(modname)\n"
    "        for n in spec['callables']:\n"
    "            qual = modname + '.' + n\n"
    "            if qual in FACADES:\n"
    "                setattr(m, n, _FcxTypeMeta(n, (), {'_fcx_qual': qual, '__module__': modname}))\n"
    "            else:\n"
    "                setattr(m, n, _mod_call(qual))\n"
    "        for n in spec['exceptions']:\n"
    "            e = type(n, (Exception,), {'__module__': modname})\n"
    "            setattr(m, n, e)\n"
    "            EXCEPTIONS[n] = e\n"
    "        m._fcx_constants = tuple(spec['constants'])\n"
    "        sys.modules[modname] = m\n";

/// Namespace dict holding HostHandle + the generated FACADES map.
static PyObject* proxyNamespace()
{
    static PyObject* ns;
    if (!ns) {
        ns = PyDict_New();
        if (!ns)
            return nullptr;
        PyDict_SetItemString(ns, "__builtins__", PyEval_GetBuiltins());
        PyObject* r = PyRun_String(ProxyPrelude, Py_file_input, ns, ns);
        if (r) {
            Py_DECREF(r);
            r = PyRun_String(FcxFacadesSource, Py_file_input, ns, ns);
        }
        if (r) {
            Py_DECREF(r);
            r = PyRun_String("_install_modules(MODULES)\n", Py_file_input, ns, ns);
        }
        if (!r) {
            PyErr_Print();
            Py_CLEAR(ns);
            return nullptr;
        }
        Py_DECREF(r);
    }
    return ns;
}

bool installModuleFacades()
{
    return proxyNamespace() != nullptr;
}

PyObject* guestExceptionType(const char* name)
{
    PyObject* ns = proxyNamespace();
    PyObject* table = ns ? PyDict_GetItemString(ns, "EXCEPTIONS") : nullptr;
    return table ? PyDict_GetItemString(table, name) : nullptr;  // borrowed
}

PyObject* handleType()
{
    PyObject* ns = proxyNamespace();
    return ns ? PyDict_GetItemString(ns, "HostHandle") : nullptr;  // borrowed
}

/// Facade class for a wire facade key, HostHandle when unmapped.
static PyObject* facadeClass(const char* key)
{
    PyObject* ns = proxyNamespace();
    if (!ns)
        return nullptr;
    if (key) {
        PyObject* facades = PyDict_GetItemString(ns, "FACADES");
        if (facades) {
            PyObject* cls = PyDict_GetItemString(facades, key);  // borrowed
            if (cls)
                return cls;
        }
    }
    return PyDict_GetItemString(ns, "HostHandle");  // borrowed
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

PyObject* decodeValue(const json& v)
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
                PyObject* obj = decodeValue(item);
                if (!obj) {
                    Py_DECREF(list);
                    return nullptr;
                }
                PyList_SET_ITEM(list, i++, obj);
            }
            return list;
        }
        case json::value_t::object:
            break;  // fall through to the typed/map handling below
        default:
            PyErr_SetString(PyExc_ValueError, "unsupported wire value");
            return nullptr;
    }

    auto tag = v.find(FcxWire::TagKey);
    if (tag == v.end()) {
        // plain map
        PyObject* dict = PyDict_New();
        if (!dict)
            return nullptr;
        for (auto it = v.begin(); it != v.end(); ++it) {
            PyObject* obj = decodeValue(it.value());
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
            PyObject* obj = decodeValue(item);
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
        auto ty = v.find("ty");
        auto fc = v.find("fc");
        PyObject* type = facadeClass(
            fc != v.end() && fc->is_string() ? fc->get_ref<const std::string&>().c_str()
                                             : nullptr);
        if (type && id != v.end() && id->is_number_integer() && ty != v.end()
                && ty->is_string()) {
            PyObject* inst = PyObject_CallNoArgs(type);
            if (!inst)
                return nullptr;
            PyObject* pid = PyLong_FromUnsignedLongLong(id->get<uint64_t>());
            PyObject* pty = PyUnicode_FromString(
                ty->get_ref<const std::string&>().c_str());
            int rc = (pid && pty) ? PyObject_SetAttrString(inst, "_id", pid)
                                  : -1;
            if (rc == 0)
                rc = PyObject_SetAttrString(inst, "_ty", pty);
            Py_XDECREF(pid);
            Py_XDECREF(pty);
            if (rc != 0) {
                Py_DECREF(inst);
                return nullptr;
            }
            // A bound declared method crosses as its base handle plus
            // "m": hand back the facade's bound method, which keeps the
            // proxy (and so the host table entry) alive until dropped.
            auto m = v.find("m");
            if (m != v.end() && m->is_string()) {
                PyObject* method = PyObject_GetAttrString(
                    inst, m->get_ref<const std::string&>().c_str());
                Py_DECREF(inst);
                return method;
            }
            return inst;
        }
    }
bad:
    PyErr_SetString(PyExc_ValueError, "malformed typed wire value");
    return nullptr;
}

static json encodeUnit(const Base::Unit& u)
{
    const Base::UnitSignature& s = u.getSignature();
    return json::array({s.Length, s.Mass, s.Time, s.ElectricCurrent,
                        s.ThermodynamicTemperature, s.AmountOfSubstance,
                        s.LuminousIntensity, s.Angle});
}

bool encodeValue(PyObject* obj, json& out, std::string& err)
{
    if (obj == Py_None) {
        out = nullptr;
        return true;
    }
    if (PyBool_Check(obj)) {  // before the int check: bool stays bool
        out = (obj == Py_True);
        return true;
    }
    if (PyLong_Check(obj)) {
        int overflow = 0;
        long long v = PyLong_AsLongLongAndOverflow(obj, &overflow);
        if (overflow == 0 && !PyErr_Occurred()) {
            out = (int64_t)v;
            return true;
        }
        PyErr_Clear();
        unsigned long long u = PyLong_AsUnsignedLongLong(obj);
        if (!PyErr_Occurred()) {
            out = (uint64_t)u;
            return true;
        }
        PyErr_Clear();
        err = "integer result does not fit the wire (64-bit)";
        return false;
    }
    if (PyFloat_Check(obj)) {
        out = PyFloat_AS_DOUBLE(obj);
        return true;
    }
    if (PyUnicode_Check(obj)) {
        Py_ssize_t n = 0;
        const char* s = PyUnicode_AsUTF8AndSize(obj, &n);
        if (!s) {
            PyErr_Clear();
            err = "string result is not UTF-8 representable";
            return false;
        }
        out = std::string(s, (size_t)n);
        return true;
    }
    if (PyBytes_Check(obj)) {
        out = json::binary(std::vector<uint8_t>(
            (uint8_t*)PyBytes_AS_STRING(obj),
            (uint8_t*)PyBytes_AS_STRING(obj) + PyBytes_GET_SIZE(obj)));
        return true;
    }
    if (PyObject_TypeCheck(obj, &Base::VectorPy::Type)) {
        const Base::Vector3d& v =
            *static_cast<Base::VectorPy*>(obj)->getVectorPtr();
        out = {{FcxWire::TagKey, FcxWire::TagVector},
               {"v", json::array({v.x, v.y, v.z})}};
        return true;
    }
    if (PyObject_TypeCheck(obj, &Base::RotationPy::Type)) {
        double q0, q1, q2, q3;
        static_cast<Base::RotationPy*>(obj)->getRotationPtr()->getValue(
            q0, q1, q2, q3);
        out = {{FcxWire::TagKey, FcxWire::TagRotation},
               {"v", json::array({q0, q1, q2, q3})}};
        return true;
    }
    if (PyObject_TypeCheck(obj, &Base::PlacementPy::Type)) {
        const Base::Placement& p =
            *static_cast<Base::PlacementPy*>(obj)->getPlacementPtr();
        const Base::Vector3d& t = p.getPosition();
        double q0, q1, q2, q3;
        p.getRotation().getValue(q0, q1, q2, q3);
        out = {{FcxWire::TagKey, FcxWire::TagPlacement},
               {"p", json::array({t.x, t.y, t.z})},
               {"r", json::array({q0, q1, q2, q3})}};
        return true;
    }
    if (PyObject_TypeCheck(obj, &Base::MatrixPy::Type)) {
        const Base::Matrix4D& m =
            *static_cast<Base::MatrixPy*>(obj)->getMatrixPtr();
        json arr = json::array();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                arr.push_back(m[r][c]);
        out = {{FcxWire::TagKey, FcxWire::TagMatrix}, {"v", std::move(arr)}};
        return true;
    }
    if (PyObject_TypeCheck(obj, &Base::BoundBoxPy::Type)) {
        const Base::BoundBox3d& b =
            *static_cast<Base::BoundBoxPy*>(obj)->getBoundBoxPtr();
        out = {{FcxWire::TagKey, FcxWire::TagBoundBox},
               {"v", json::array({b.MinX, b.MinY, b.MinZ,
                                  b.MaxX, b.MaxY, b.MaxZ})}};
        return true;
    }
    if (PyObject_TypeCheck(obj, &Base::QuantityPy::Type)) {
        const Base::Quantity& q =
            *static_cast<Base::QuantityPy*>(obj)->getQuantityPtr();
        out = {{FcxWire::TagKey, FcxWire::TagQuantity},
               {"v", q.getValue()},
               {"u", encodeUnit(q.getUnit())}};
        return true;
    }
    // a module facade's class (Part.Edge) as an argument: a type
    // reference the host resolves to the declared object
    if (PyType_Check(obj) && PyObject_HasAttrString(obj, "_fcx_qual")) {
        PyObject* q = PyObject_GetAttrString(obj, "_fcx_qual");
        bool ok = q && PyUnicode_Check(q);
        if (ok)
            out = {{FcxWire::TagKey, FcxWire::TagType}, {"q", std::string(PyUnicode_AsUTF8(q))}};
        Py_XDECREF(q);
        if (ok)
            return true;
        PyErr_Clear();
    }
    PyObject* htype = handleType();
    if (htype && PyObject_IsInstance(obj, htype) == 1) {
        PyObject* pid = PyObject_GetAttrString(obj, "_id");
        PyObject* pty = PyObject_GetAttrString(obj, "_ty");
        bool ok = pid && pty && PyLong_Check(pid) && PyUnicode_Check(pty);
        if (ok)
            out = {{FcxWire::TagKey, FcxWire::TagHandle},
                   {"id", (uint64_t)PyLong_AsUnsignedLongLong(pid)},
                   {"ty", std::string(PyUnicode_AsUTF8(pty))}};
        Py_XDECREF(pid);
        Py_XDECREF(pty);
        if (ok)
            return true;
        PyErr_Clear();
        err = "malformed host handle";
        return false;
    }
    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        PyObject* seq = PySequence_Fast(obj, "sequence");
        if (!seq) {
            PyErr_Clear();
            err = "unreadable sequence result";
            return false;
        }
        json arr = json::array();
        Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
        for (Py_ssize_t i = 0; i < n; ++i) {
            json item;
            if (!encodeValue(PySequence_Fast_GET_ITEM(seq, i), item, err)) {
                Py_DECREF(seq);
                return false;
            }
            arr.push_back(std::move(item));
        }
        bool isTuple = PyTuple_Check(obj);
        Py_DECREF(seq);
        if (isTuple)
            out = json {{FcxWire::TagKey, FcxWire::TagTuple},
                        {"v", std::move(arr)}};
        else
            out = std::move(arr);
        return true;
    }
    if (PyDict_Check(obj)) {
        json map = json::object();
        PyObject *key = nullptr, *value = nullptr;
        Py_ssize_t pos = 0;
        while (PyDict_Next(obj, &pos, &key, &value)) {
            if (!PyUnicode_Check(key)) {
                err = "dict result with a non-string key";
                return false;
            }
            json item;
            if (!encodeValue(value, item, err))
                return false;
            map[PyUnicode_AsUTF8(key)] = std::move(item);
        }
        out = std::move(map);
        return true;
    }
    err = std::string("result of type '") + Py_TYPE(obj)->tp_name
        + "' does not marshal by value";
    return false;
}

}  // namespace FcxImage

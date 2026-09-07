/* The sandbox image's dispatcher (see ImageDispatch.h).  Moved out of
 * ImageMain.cpp unchanged when the pyodide guest arrived, so both guests
 * evaluate through one body of code.
 */
#include <Python.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <nlohmann/json.hpp>

#include <Base/BoundBoxPy.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>
#include <Base/MatrixPy.h>
#include <Base/PlacementPy.h>
#include <Base/QuantityPy.h>
#include <Base/RotationPy.h>
#include <Base/UnitPy.h>
#include <Base/VectorPy.h>

#include <App/Expression.h>

#include "FcxDocument.h"
#include "FcxWire.h"
#include "ImageDispatch.h"
#include "ImageMarshal.h"

using nlohmann::json;

namespace FcxImage
{

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

/// The FreeCAD exception type set of App/Application.cpp
/// initApplication -- Base::Exception::setPyException routes through
/// these, so the image must create them too or every C++ error
/// degenerates to raising a null object.
static void init_exception_types(PyObject *module)
{
    struct
    {
        PyObject **slot;
        const char *name;
        PyObject *base;
    } entries[] = {
        {&Base::PyExc_FC_GeneralError, "Base.FreeCADError", PyExc_RuntimeError},
        {&Base::PyExc_FC_FreeCADAbort, "Base.FreeCADAbort", PyExc_BaseException},
        {&Base::PyExc_FC_XMLBaseException, "Base.XMLBaseException", PyExc_Exception},
        {&Base::PyExc_FC_UnknownProgramOption, "Base.UnknownProgramOption", PyExc_BaseException},
        {&Base::PyExc_FC_PropertyError, "Base.PropertyError", PyExc_AttributeError},
    };
    for (auto &e : entries) {
        *e.slot = PyErr_NewException(e.name, e.base, nullptr);
        if (*e.slot) {
            Py_INCREF(*e.slot);
            PyModule_AddObject(module, strchr(e.name, '.') + 1, *e.slot);
        }
    }
    struct
    {
        PyObject **slot;
        const char *name;
        PyObject **base;
    } derived[] = {
        {&Base::PyExc_FC_XMLParseException, "Base.XMLParseException", &Base::PyExc_FC_XMLBaseException},
        {&Base::PyExc_FC_XMLAttributeError, "Base.XMLAttributeError", &Base::PyExc_FC_XMLBaseException},
        {&Base::PyExc_FC_BadFormatError, "Base.BadFormatError", &Base::PyExc_FC_GeneralError},
        {&Base::PyExc_FC_BadGraphError, "Base.BadGraphError", &Base::PyExc_FC_GeneralError},
        {&Base::PyExc_FC_ExpressionError, "Base.ExpressionError", &Base::PyExc_FC_GeneralError},
        {&Base::PyExc_FC_ParserError, "Base.ParserError", &Base::PyExc_FC_GeneralError},
        {&Base::PyExc_FC_CADKernelError, "Base.CADKernelError", &Base::PyExc_FC_GeneralError},
    };
    for (auto &e : derived) {
        *e.slot = PyErr_NewException(e.name, *e.base, nullptr);
        if (*e.slot) {
            Py_INCREF(*e.slot);
            PyModule_AddObject(module, strchr(e.name, '.') + 1, *e.slot);
        }
    }
}

static PyObject *init_freecad_module()
{
    PyObject *module = PyModule_Create(&FreeCADModuleDef);
    if (!module)
        return nullptr;
    init_exception_types(module);
    add_type(module, "Vector", &Base::VectorPy::Type);
    add_type(module, "Rotation", &Base::RotationPy::Type);
    add_type(module, "Placement", &Base::PlacementPy::Type);
    add_type(module, "Matrix", &Base::MatrixPy::Type);
    add_type(module, "BoundBox", &Base::BoundBoxPy::Type);

    PyObject *units = PyModule_Create(&UnitsModuleDef);
    if (units) {
        add_type(units, "Quantity", &Base::QuantityPy::Type);
        add_type(units, "Unit", &Base::UnitPy::Type);
        // the unit constants (Units.Radian, Units.Metre, ...) as the
        // host's Units module carries them (UnitsApiPy.cpp): Draft
        // multiplies angles by App.Units.Radian
        for (const auto &info : Base::Quantity::unitInfo())
            PyModule_AddObject(units, info.name,
                               new Base::QuantityPy(new Base::Quantity(info.quantity)));
        // and the unit types (Units.Length, Units.Area, ...): BIM builds
        // `Quantity(value, Units.Length)` for its user-facing strings
        for (const auto &v : Base::Unit::unitTypes())
            PyModule_AddObject(units, v.second, new Base::UnitPy(new Base::Unit(v.first)));
        PyModule_AddObject(module, "Units", units);
        PyObject *modules = PyImport_GetModuleDict();
        PyDict_SetItemString(modules, "FreeCAD.Units", units);
    }
    return module;
}


PyObject *initFreeCADModule()
{
    return init_freecad_module();
}

int initEvalGlobals()
{
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
    // What workbench Python reads off FreeCAD before doing geometry:
    // GuiUp (0 until the host's boot request sets it to the host's
    // own, docs/Sandbox.md 7.9 G2b) and a Console whose
    // Print* go to the guest's stderr, which the host logs.  Not a
    // facade: nothing crosses.
    {
        PyObject *dict = PyModule_GetDict(fc);  // borrowed
        PyObject *r = PyRun_String(
            "GuiUp = 0\n"
            "class _Console:\n"
            "    def _w(self, *args):\n"
            "        import sys\n"
            "        sys.stderr.write(''.join(str(a) for a in args))\n"
            "    PrintError = PrintWarning = PrintMessage = PrintLog = _w\n"
            "    PrintDeveloperError = PrintDeveloperWarning = _w\n"
            "Console = _Console()\n"
            "del _Console\n"
            // FreeCAD.Base as workbench code imports it (`from FreeCAD
            // import Base`; Base.Vector): the same value classes and
            // exception types the module itself carries.
            "import sys as _sys, types as _types\n"
            "Base = _types.ModuleType('FreeCAD.Base')\n"
            "for _n in ('Vector', 'Rotation', 'Placement', 'Matrix', 'BoundBox',\n"
            "           'FreeCADError', 'FreeCADAbort', 'XMLBaseException',\n"
            "           'UnknownProgramOption', 'PropertyError', 'XMLParseException',\n"
            "           'XMLAttributeError', 'BadFormatError', 'BadGraphError',\n"
            "           'ExpressionError', 'ParserError', 'CADKernelError'):\n"
            "    if _n in globals():\n"
            "        setattr(Base, _n, globals()[_n])\n"
            // Base.TypeId as data: a type NAME (Draft's snapper asks the
            // main window for the views of `TypeId.fromName('Gui::
            // View3DInventor')`, and the guest's main window has none,
            // G4); no host type registry is consulted.
            "class TypeId:\n"
            "    __slots__ = ('Name',)\n"
            "    def __init__(self, name=''):\n"
            "        self.Name = str(name)\n"
            "    @staticmethod\n"
            "    def fromName(name):\n"
            "        return TypeId(name)\n"
            "    def isBad(self):\n"
            "        return not self.Name\n"
            "    def isDerivedFrom(self, other):\n"
            "        return self.Name == getattr(other, 'Name', other)\n"
            "    def __eq__(self, other):\n"
            "        return isinstance(other, TypeId) and other.Name == self.Name\n"
            "    def __hash__(self):\n"
            "        return hash(self.Name)\n"
            "    def __repr__(self):\n"
            "        return '<TypeId %s>' % self.Name\n"
            "Base.TypeId = TypeId\n"
            "_sys.modules['FreeCAD.Base'] = Base\n"
            "del _sys, _types, _n\n"
            // FreeCAD.ParamGet: every Get* is a prefs.read through the
            // freecad.prefs module facade (BIM's ArchSchedule reads the
            // store at import); a Set*/Rem* is a prefs.write through the
            // same facade -- ALLOW for the session and addons (a task
            // panel storing what the user chose, G3a), DENY for a
            // document, where it raises PermissionError.
            "class _ParamGrp:\n"
            "    __slots__ = ('_path',)\n"
            "    def __init__(self, path):\n"
            "        self._path = path\n"
            "    def __repr__(self):\n"
            "        return '<sandbox parameter group %s>' % self._path\n"
            "    def _get(self, kind, name, default):\n"
            "        from freecad import prefs\n"
            "        return prefs.read(self._path, name, kind, default)\n"
            "    def GetBool(self, name, default=False):\n"
            "        return self._get('Bool', name, default)\n"
            "    def GetInt(self, name, default=0):\n"
            "        return self._get('Int', name, default)\n"
            "    def GetUnsigned(self, name, default=0):\n"
            "        return self._get('Unsigned', name, default)\n"
            "    def GetFloat(self, name, default=0.0):\n"
            "        return self._get('Float', name, default)\n"
            "    def GetString(self, name, default=''):\n"
            "        return self._get('String', name, default)\n"
            "    def _names(self, kind):\n"
            "        from freecad import prefs\n"
            "        return prefs.names(self._path, kind)\n"
            "    def GetBools(self):\n"
            "        return self._names('Bool')\n"
            "    def GetInts(self):\n"
            "        return self._names('Int')\n"
            "    def GetUnsigneds(self):\n"
            "        return self._names('Unsigned')\n"
            "    def GetFloats(self):\n"
            "        return self._names('Float')\n"
            "    def GetStrings(self):\n"
            "        return self._names('String')\n"
            "    def GetGroup(self, name):\n"
            "        return _ParamGrp(self._path + '/' + name)\n"
            "    def HasGroup(self, name):\n"
            "        from freecad import prefs\n"
            "        return prefs.has_group(self._path, name)\n"
            // A write crosses under prefs.write.  The one recompute-time
            // write workbench code used to make (Arch areas and hatches
            // toggling TechDraw's allowCrazyEdge around a projection) is
            // gone: findShapeOutline and makeGeomHatch take
            // allowCrazyEdge=True as a keyword, scoped to the call
            // (2026-09-05), so no document object writes.
            "    def _put(self, kind, name, value):\n"
            "        from freecad import prefs\n"
            "        return prefs.write(self._path, name, kind, value)\n"
            "    def SetBool(self, name, value):\n"
            "        self._put('Bool', name, bool(value))\n"
            "    def SetInt(self, name, value):\n"
            "        self._put('Int', name, int(value))\n"
            "    def SetUnsigned(self, name, value):\n"
            "        self._put('Unsigned', name, int(value))\n"
            "    def SetFloat(self, name, value):\n"
            "        self._put('Float', name, float(value))\n"
            "    def SetString(self, name, value):\n"
            "        self._put('String', name, str(value))\n"
            "    def _rem(self, kind, name):\n"
            "        from freecad import prefs\n"
            "        return prefs.remove(self._path, name, kind)\n"
            "    def RemBool(self, name):\n"
            "        self._rem('Bool', name)\n"
            "    def RemInt(self, name):\n"
            "        self._rem('Int', name)\n"
            "    def RemUnsigned(self, name):\n"
            "        self._rem('Unsigned', name)\n"
            "    def RemFloat(self, name):\n"
            "        self._rem('Float', name)\n"
            "    def RemString(self, name):\n"
            "        self._rem('String', name)\n"
            "    def RemGroup(self, name):\n"
            "        from freecad import prefs\n"
            "        return prefs.remove(self._path, name, 'Group')\n"
            "    def _ignore(self, *args, **kw):\n"
            "        pass\n"
            "    Clear = Notify = NotifyAll = _ignore\n"
            "def ParamGet(path):\n"
            "    return _ParamGrp(path)\n"
            // FreeCAD.Qt: the translation helpers workbench modules
            // bind at import (Arch.py: `QT_TRANSLATE_NOOP =
            // FreeCAD.Qt.QT_TRANSLATE_NOOP`); no translation here.
            "class _Qt:\n"
            "    @staticmethod\n"
            "    def translate(context, text, disambiguation=None, n=-1):\n"
            "        return text\n"
            "    @staticmethod\n"
            "    def QT_TRANSLATE_NOOP(context, text):\n"
            "        return text\n"
            "Qt = _Qt()\n"
            "del _Qt\n"
            // FreeCAD.getResourceDir: a bundled wheel's data rides under
            // fcx_resources/ in site-packages (the fcx_bim wheel puts
            // BIM's Presets at fcx_resources/Mod/BIM/Presets), so the
            // resource paths workbench code builds resolve unchanged.
            // The user data directory is a path that does not exist:
            // code that looks for user files there finds none.
            // (no sysconfig: the WASI stdlib slice does not carry it;
            // site-packages is found on sys.path, and the reference
            // image, which has no wheels, gets a path nothing is under)
            "import sys as _sys\n"
            "_resource_dir = '/fcx_resources/'\n"
            "for _p in _sys.path:\n"
            "    if _p.endswith('site-packages'):\n"
            "        _resource_dir = _p + '/fcx_resources/'\n"
            "        break\n"
            "def getResourceDir():\n"
            "    return _resource_dir\n"
            "def getUserAppDataDir():\n"
            "    return '/fcx/userdata/'\n"
            "def getUserMacroDir(actual=False):\n"
            "    return '/fcx/userdata/Macro/'\n"
            "del _sys, _p\n"
            // Document observers: registered here only, never fired --
            // the guest sees no document events (ArchSchedule and
            // ArchReport register one to refresh themselves).
            "_observers = []\n"
            "def addDocumentObserver(observer):\n"
            "    _observers.append(observer)\n"
            "def removeDocumentObserver(observer):\n"
            "    if observer in _observers:\n"
            "        _observers.remove(observer)\n"
            // FreeCAD.ActiveDocument (FcxWire OpActiveDoc): for a document
            // principal the document of the object whose hook is running
            // (the owner's Document as read_prop reads it), None outside
            // a hook; for the session and an addon -- a command's
            // Activated -- the host's LIVE active document, re-read on
            // every access (docs/Sandbox.md 7.13, S1).  A module
            // property, so the module's class is swapped.
            "import sys as _sys, types as _types\n"
            "class _FcModule(_types.ModuleType):\n"
            "    @property\n"
            "    def ActiveDocument(self):\n"
            "        import _fcx\n"
            "        return _fcx.op('active_doc', 0)\n"
            "_sys.modules['FreeCAD'].__class__ = _FcModule\n"
            "del _sys, _types, _FcModule\n"
            // The application's document set (S1): every document the
            // principal reaches, not the transaction's one.  listDocuments
            // and getDocument under app.query; newDocument, closeDocument
            // and setActiveDocument under app.write, the workbench's
            // (DENY for a document, not promptable).  The writes run the
            // host's own FreeCAD functions, so the GUI follows them.
            "def activeDocument():\n"
            "    import _fcx\n"
            "    return _fcx.op('active_doc', 0)\n"
            "def listDocuments():\n"
            "    import _fcx\n"
            "    return _fcx.op('app.docs', 0)\n"
            "def getDocument(name):\n"
            "    import _fcx\n"
            "    return _fcx.op('app.doc', 0, str(name))\n"
            // by keyword: the host's newDocument takes no None for a
            // name or label it was not given
            "def newDocument(name=None, label=None, hidden=False, temp=False):\n"
            "    import _fcx\n"
            "    kw = {}\n"
            "    if name is not None:\n"
            "        kw['name'] = str(name)\n"
            "    if label is not None:\n"
            "        kw['label'] = str(label)\n"
            "    if hidden:\n"
            "        kw['hidden'] = True\n"
            "    if temp:\n"
            "        kw['temp'] = True\n"
            "    return _fcx.op('app.new_doc', 0, [], kw)\n"
            "def closeDocument(name):\n"
            "    import _fcx\n"
            "    return _fcx.op('app.close_doc', 0, str(name))\n"
            "def setActiveDocument(name):\n"
            "    import _fcx\n"
            "    return _fcx.op('app.set_active_doc', 0, str(name))\n",
            Py_file_input, dict, dict);
        if (!r) {
            PyErr_Print();
            Py_DECREF(fc);
            return 7;
        }
        Py_DECREF(r);
    }
    Py_DECREF(fc);
    // the module facades (Part) go into sys.modules now, so an exec'd
    // module's `import Part` finds them
    if (!FcxImage::installModuleFacades()) {
        PyErr_Print();
        return 6;
    }
    return 0;
}

PyObject *evalGlobals()
{
    return eval_globals;
}

/// Current Python error -> {"ok":false,"exc":<type>,"msg":<text>}.
std::string FcxImage::missingImportOffer(const std::string &module)
{
    // keep whatever error is pending: the caller is in the middle of
    // reporting it
    PyObject *ptype = nullptr, *pvalue = nullptr, *ptrace = nullptr;
    PyErr_Fetch(&ptype, &pvalue, &ptrace);
    std::string offer;
    PyObject *fcx = PyImport_ImportModule("_fcx");
    PyObject *answer = fcx ? PyObject_CallMethod(fcx, "op", "sKs", "pkg.missing",
                                                 (unsigned long long)0, module.c_str())
                           : nullptr;
    if (answer && PyUnicode_Check(answer)) {
        if (const char *text = PyUnicode_AsUTF8(answer))
            offer = text;
    }
    Py_XDECREF(answer);
    Py_XDECREF(fcx);
    PyErr_Clear();
    PyErr_Restore(ptype, pvalue, ptrace);
    return offer;
}

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
    // A module the guest could not import and that ENDED the work (the
    // error is leaving the guest uncaught) is the one to ask the host
    // about: `pkg.missing <name>` answers with the offer the host has
    // recorded ("FreeCAD can install ...") or "installed, next
    // evaluation", and that becomes the message.  Asking here, not in
    // a meta_path finder, means an import a workload catches itself
    // (`try: import regex`, uuid's `_uuid`) never becomes a question
    // -- and `regex` IS in pyodide's lock, so the finder turned lark's
    // optional accelerator into an install prompt (2026-09-05).
    if (type && value && PyErr_GivenExceptionMatches(type, PyExc_ModuleNotFoundError)) {
        PyObject *name = PyObject_GetAttrString(value, "name");
        if (name && PyUnicode_Check(name)) {
            std::string offer = FcxImage::missingImportOffer(PyUnicode_AsUTF8(name));
            if (!offer.empty())
                r["msg"] = offer;
        }
        Py_XDECREF(name);
        PyErr_Clear();
    }
    // The formatted traceback rides along as "tb": the host prints it
    // with a failed hook the way native FreeCAD prints a failed
    // execute()'s.  Best effort -- a stdlib slice without `traceback`
    // (the WASI image) sends none.
    if (trace) {
        PyObject *tbmod = PyImport_ImportModule("traceback");
        if (tbmod) {
            PyObject *lines = PyObject_CallMethod(tbmod, "format_exception", "OOO",
                                                  type ? type : Py_None,
                                                  value ? value : Py_None, trace);
            if (lines) {
                PyObject *empty = PyUnicode_FromString("");
                PyObject *joined = empty ? PyUnicode_Join(empty, lines) : nullptr;
                const char *tbText = joined ? PyUnicode_AsUTF8(joined) : nullptr;
                if (tbText)
                    r["tb"] = tbText;
                Py_XDECREF(joined);
                Py_XDECREF(empty);
                Py_DECREF(lines);
            }
            Py_DECREF(tbmod);
        }
        PyErr_Clear();
    }
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

/** The core-carve eval path (docs/ExpressionImage.md): parse src with
 * the real ExpressionParser and evaluate the AST walker against the S1
 * adapter world -- identifiers resolve from the shipped bindings pack,
 * the owner's Python face is the exported host handle's proxy, and
 * anything neither covers fails in-image.
 * Request: {op:"eval", lang:"expr", src, ctx:{doc,obj}, owner_h?,
 *           opts?, bindings:{identifier-string: wire value}}.
 *
 * `opts` is the host's App::Expression::EvalOption mask.  It is not
 * decoration: OptionCallFrame is what makes a statement legal (without
 * a frame the walker throws "can only be used inside 'eval' or 'func'")
 * and OptionPythonMode changes BOTH the lexer start state and the
 * name-binding rule, so it has to reach the parse as well as the walk.
 */
static json dispatchEvalExpr(const json &req, const std::string &src)
{
    std::string docName;
    std::string objName;
    auto ctx = req.find("ctx");
    if (ctx != req.end() && ctx->is_object()) {
        docName = ctx->value("doc", "");
        objName = ctx->value("obj", "");
    }
    int options = 0;
    auto op = req.find("opts");
    if (op != req.end() && op->is_number_integer())
        options = op->get<int>();

    uint64_t ownerHandle = 0;
    auto oh = req.find("owner_h");
    if (oh != req.end() && oh->is_number_unsigned())
        ownerHandle = oh->get<uint64_t>();
    std::string ownerFacade;
    auto ofc = req.find("owner_fc");
    if (ofc != req.end() && ofc->is_string())
        ownerFacade = ofc->get_ref<const std::string &>();

    Fcx::EvalTransaction tx(docName, objName, ownerHandle, ownerFacade);

    auto bindings = req.find("bindings");
    if (bindings != req.end() && bindings->is_object()) {
        for (auto it = bindings->begin(); it != bindings->end(); ++it) {
            PyObject *obj = FcxImage::decodeValue(it.value());
            if (!obj)
                return errorReply();
            tx.addBinding(it.key(), obj);
            Py_DECREF(obj);
        }
    }
    // Identifiers the host could not resolve INTO A FOREIGN DOCUMENT.
    // We have no foreign documents, so our own answer would name the
    // document as missing when the host knows the real reason.
    auto bindErrs = req.find("binderrs");
    if (bindErrs != req.end() && bindErrs->is_object()) {
        for (auto it = bindErrs->begin(); it != bindErrs->end(); ++it) {
            if (!it.value().is_object())
                continue;
            tx.addBindingError(it.key(),
                               it.value().value("exc", "RuntimeError"),
                               it.value().value("msg", ""));
        }
    }

    try {
        auto expr = App::Expression::parse(
                tx.owner(), src.c_str(), src.size(), false,
                (options & App::Expression::OptionPythonMode) != 0);
        if (!expr)
            return protocolError("expression parse produced nothing");
        Py::Object result = expr->getPyValue(options);

        json reply;
        json value;
        std::string err;
        if (!FcxImage::encodeValue(result.ptr(), value, err)) {
            reply["ok"] = false;
            reply["exc"] = "MarshalError";
            reply["msg"] = err;
            return reply;
        }
        reply["ok"] = true;
        reply["val"] = std::move(value);
        return reply;
    }
    catch (Py::Exception &) {
        return errorReply();
    }
    catch (Base::Exception &e) {
        // Route through the exception's own Python face so the wire
        // carries the same type name the host evaluator would raise.
        e.setPyException();
        return errorReply();
    }
    catch (std::exception &e) {
        json r;
        r["ok"] = false;
        r["exc"] = "RuntimeError";
        r["msg"] = e.what();
        return r;
    }
}

static json dispatchEval(const json &req)
{
    auto src = req.find("src");
    if (src == req.end() || !src->is_string())
        return protocolError("eval without src");

    if (req.value("lang", "py") == "expr")
        return dispatchEvalExpr(req, src->get_ref<const std::string &>());

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


/** {op:"exec", src, module?}: statements, not an expression.  With
 * `module` the source runs as a module of that name -- created, put in
 * sys.modules BEFORE it runs (as import does, so the source may refer
 * to itself) and bound to its parent package when the name is dotted;
 * removed again on failure.  This is how the host pushes workbench
 * Python into the guest while there is no package loader for it
 * (tests today, G1's Draft loader later).  Without `module` the source
 * runs once in a copy of the eval globals and its names are discarded;
 * what it put in sys.modules stays.  Reply {ok:true} or the error.
 */
static json dispatchExec(const json &req)
{
    auto src = req.find("src");
    if (src == req.end() || !src->is_string())
        return protocolError("exec without src");
    const std::string module = req.value("module", "");
    PyObject *modules = PyImport_GetModuleDict();
    PyObject *globals = nullptr;
    PyObject *mod = nullptr;
    if (!module.empty()) {
        mod = PyModule_New(module.c_str());
        if (!mod)
            return errorReply();
        globals = PyModule_GetDict(mod);  // borrowed
        Py_INCREF(globals);
        if (PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0
                || PyDict_SetItemString(modules, module.c_str(), mod) < 0) {
            Py_DECREF(globals);
            Py_DECREF(mod);
            return errorReply();
        }
    }
    else {
        globals = PyDict_Copy(eval_globals);
        if (!globals)
            return errorReply();
    }
    PyObject *result = PyRun_String(src->get_ref<const std::string &>().c_str(),
                                    Py_file_input, globals, globals);
    Py_DECREF(globals);
    if (!result) {
        json err = errorReply();
        if (mod) {
            if (PyDict_DelItemString(modules, module.c_str()) < 0)
                PyErr_Clear();
            Py_DECREF(mod);
        }
        return err;
    }
    Py_DECREF(result);
    if (mod) {
        size_t dot = module.rfind('.');
        if (dot != std::string::npos) {
            PyObject *parent = PyDict_GetItemString(modules, module.substr(0, dot).c_str());
            if (parent && PyObject_SetAttrString(parent, module.c_str() + dot + 1, mod) < 0)
                PyErr_Clear();
        }
        Py_DECREF(mod);  // sys.modules holds it
    }
    json reply;
    reply["ok"] = true;
    return reply;
}

// ---- rung 2: guest-resident Proxies (FcxWire OpProxyNew / OpProxyCall,
// ---- docs/Sandbox.md 7.6 G1c), through the prelude's registry ----

/// The wire array under `key` as a tuple of decoded values (handles
/// become proxies, exactly as eval bindings do); an empty tuple when
/// absent; nullptr with a Python error on a malformed value.
static PyObject *decodeArgs(const json &req, const char *key)
{
    auto a = req.find(key);
    if (a == req.end() || !a->is_array())
        return PyTuple_New(0);
    PyObject *tuple = PyTuple_New((Py_ssize_t)a->size());
    if (!tuple)
        return nullptr;
    Py_ssize_t i = 0;
    for (const auto &item : *a) {
        PyObject *obj = FcxImage::decodeValue(item);
        if (!obj) {
            Py_DECREF(tuple);
            return nullptr;
        }
        PyTuple_SET_ITEM(tuple, i++, obj);
    }
    return tuple;
}

/// A hook's result by value, or a MarshalError reply.
static json valueReply(PyObject *result)
{
    json reply;
    json value;
    std::string err;
    if (!FcxImage::encodeValue(result, value, err)) {
        reply["ok"] = false;
        reply["exc"] = "MarshalError";
        reply["msg"] = err;
        return reply;
    }
    reply["ok"] = true;
    reply["val"] = std::move(value);
    return reply;
}

/// The "k" kwargs map decoded, an empty dict when absent; nullptr with
/// a Python error on a malformed value.
static PyObject *decodeKwargs(const json &req)
{
    auto k = req.find("k");
    if (k != req.end() && k->is_object())
        return FcxImage::decodeValue(*k);
    return PyDict_New();
}

static json dispatchProxyNew(const json &req)
{
    const std::string mod = req.value("mod", "");
    const std::string cls = req.value("cls", "");
    if (mod.empty() || cls.empty())
        return protocolError("proxy_new without mod/cls");
    PyObject *fn = FcxImage::preludeFunction("_proxy_new");
    if (!fn)
        return errorReply();
    PyObject *args = decodeArgs(req, "a");
    if (!args)
        return errorReply();
    PyObject *kwargs = decodeKwargs(req);
    if (!kwargs) {
        Py_DECREF(args);
        return errorReply();
    }
    PyObject *result = PyObject_CallFunction(fn, "ssOOO", mod.c_str(), cls.c_str(), args, kwargs,
                                             req.value("alloc", false) ? Py_True : Py_False);
    Py_DECREF(args);
    Py_DECREF(kwargs);
    if (!result)
        return errorReply();
    json reply = valueReply(result);
    Py_DECREF(result);
    return reply;
}

/// proxy_get / proxy_set: a host read or write of a proxy attribute.
static json dispatchProxyAttr(const json &req, bool set)
{
    auto id = req.find("id");
    const std::string name = req.value("n", "");
    if (id == req.end() || !id->is_number_integer() || name.empty())
        return protocolError(set ? "proxy_set without id/n" : "proxy_get without id/n");
    PyObject *fn = FcxImage::preludeFunction(set ? "_proxy_setattr" : "_proxy_attr");
    if (!fn)
        return errorReply();
    PyObject *result = nullptr;
    if (set) {
        auto v = req.find("v");
        PyObject *value = nullptr;
        if (v != req.end())
            value = FcxImage::decodeValue(*v);
        else {
            value = Py_None;
            Py_INCREF(value);
        }
        if (!value)
            return errorReply();
        result = PyObject_CallFunction(fn, "KsO", (unsigned long long)id->get<uint64_t>(),
                                       name.c_str(), value);
        Py_DECREF(value);
    }
    else
        result = PyObject_CallFunction(fn, "Ks", (unsigned long long)id->get<uint64_t>(),
                                       name.c_str());
    if (!result)
        return errorReply();
    json reply = valueReply(result);
    Py_DECREF(result);
    return reply;
}

static json dispatchProxyCall(const json &req)
{
    auto id = req.find("id");
    const std::string member = req.value("m", "");
    if (id == req.end() || !id->is_number_integer() || member.empty())
        return protocolError("proxy_call without id/m");
    PyObject *fn = FcxImage::preludeFunction("_proxy_call");
    if (!fn)
        return errorReply();
    PyObject *args = decodeArgs(req, "a");
    if (!args)
        return errorReply();
    PyObject *kwargs = decodeKwargs(req);
    if (!kwargs) {
        Py_DECREF(args);
        return errorReply();
    }
    PyObject *result = PyObject_CallFunction(fn, "KsOO", (unsigned long long)id->get<uint64_t>(),
                                             member.c_str(), args, kwargs);
    Py_DECREF(args);
    Py_DECREF(kwargs);
    if (!result)
        return errorReply();
    json reply = valueReply(result);
    Py_DECREF(result);
    return reply;
}

/// "pd": proxies whose host stand-in died since the last request.
static void applyProxyDrops(const json &req)
{
    auto pd = req.find("pd");
    if (pd == req.end() || !pd->is_array() || pd->empty())
        return;
    PyObject *fn = FcxImage::preludeFunction("_proxy_drop");
    PyObject *ids = fn ? FcxImage::decodeValue(*pd) : nullptr;
    PyObject *r = ids ? PyObject_CallFunction(fn, "O", ids) : nullptr;
    Py_XDECREF(ids);
    Py_XDECREF(r);
    if (PyErr_Occurred())
        PyErr_Clear();
}

json dispatch(const json &req)
{
    json reply;
    applyProxyDrops(req);
    auto op = req.find("op");
    if (op == req.end() || !op->is_string())
        reply = protocolError("request without op");
    else if (op->get_ref<const std::string &>() == FcxWire::OpEval)
        reply = dispatchEval(req);
    else if (op->get_ref<const std::string &>() == FcxWire::OpExec)
        reply = dispatchExec(req);
    else if (op->get_ref<const std::string &>() == FcxWire::OpProxyNew)
        reply = dispatchProxyNew(req);
    else if (op->get_ref<const std::string &>() == FcxWire::OpProxyCall)
        reply = dispatchProxyCall(req);
    else if (op->get_ref<const std::string &>() == FcxWire::OpProxyGet)
        reply = dispatchProxyAttr(req, false);
    else if (op->get_ref<const std::string &>() == FcxWire::OpProxySet)
        reply = dispatchProxyAttr(req, true);
    else
        reply = protocolError("unknown op");
    if (PyErr_Occurred())
        PyErr_Clear();
    // What the request queued on the guest's QTimer shim runs now, in
    // order, before the reply goes back (docs/Sandbox.md 7.11, G3c).
    if (PyObject* drain = FcxImage::preludeFunction("_drain_timers")) {
        PyObject* r = PyObject_CallNoArgs(drain);
        if (!r)
            PyErr_Print();
        Py_XDECREF(r);
    }
    // The proxies the evaluation let go (its globals died with it):
    // their releases ride the reply, not a hop each.
    json released = FcxImage::takePendingReleases();
    if (!released.empty())
        reply["r"] = std::move(released);
    return reply;
}

std::vector<uint8_t> dispatchCbor(const uint8_t *req_bytes, size_t len)
{
    json reply;
    try {
        json req = json::from_cbor(req_bytes, req_bytes + len);
        reply = dispatch(req);
    }
    catch (const json::exception &e) {
        reply = protocolError(e.what());
    }
    if (PyErr_Occurred())
        PyErr_Clear();
    return json::to_cbor(reply);
}

}  // namespace FcxImage

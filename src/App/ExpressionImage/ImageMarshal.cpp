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
 * A handle to a DocumentObject or Document carries its durable key in
 * _k ((document, name) or (document,), FcxWire OpResolve): an id lives
 * one transaction, but a Proxy keeps document objects on itself across
 * hooks natively (ArchReport's Result sheet), so every op goes through
 * _hop, which on a stale id re-resolves the key once and retries, and
 * two handles compare and hash by key when both have one.
 */
static const char ProxyPrelude[] =
    "import _fcx\n"
    "def _hop(self, op, *args):\n"
    "    try:\n"
    "        return _fcx.op(op, self._id, *args)\n"
    "    except ReferenceError:\n"
    "        if self._k is None:\n"
    "            raise\n"
    "        object.__setattr__(self, '_id', _fcx.op('resolve', 0, list(self._k)))\n"
    "        return _fcx.op(op, self._id, *args)\n"
    "class HostHandle:\n"
    "    __slots__ = ('_id', '_ty', '_fc', '_k')\n"
    "    def __repr__(self):\n"
    "        return '<HostHandle %s #%d>' % (self._ty, self._id)\n"
    // identity is the host object's: the host mints one id per object
    // per transaction, so `obj in o.Hosts` and `a == b` hold as they
    // do natively; across transactions a document object's key is
    // what stays the same
    "    def __eq__(self, other):\n"
    "        if not isinstance(other, HostHandle):\n"
    "            return False\n"
    "        if self._k is not None and other._k is not None:\n"
    "            return other._k == self._k\n"
    "        return other._id == self._id\n"
    "    def __ne__(self, other):\n"
    "        return not self.__eq__(other)\n"
    "    def __hash__(self):\n"
    "        return hash(self._k) if self._k is not None else hash(self._id)\n"
    // A value read off a handle (read_prop here, get_attr in _attr) is
    // stamped as that attribute of the proxy: a nested write --
    // `obj.Placement.Base = v`, ArchFrame's `profile.Placement.Rotation
    // = rot` -- then writes the whole value back through __setattr__,
    // the write-back native FreeCAD performs for every value an
    // attribute hands out.
    "    def __getattr__(self, name):\n"
    "        if name.startswith('_'):\n"
    "            raise AttributeError(name)\n"
    "        return _fcx.track(_hop(self, 'read_prop', name), self, name)\n"
    "    def __setattr__(self, name, value):\n"
    "        if name in HostHandle.__slots__ or name == '__class__':\n"
    "            object.__setattr__(self, name, value)\n"
    "        else:\n"
    // `obj.Proxy = self` (DraftObject.__init__): the instance stays
    // here, registered; what crosses is its descriptor, from which
    // the host builds the stand-in (FcxWire TagGuestProxy).
    "            if name == 'Proxy' and value is not None and not isinstance(value, HostHandle):\n"
    "                value = _proxy_register(value)\n"
    "            _hop(self, 'write_prop', name, value)\n"
    "    def __bool__(self):\n"
    "        return _hop(self, 'bool')\n"
    "    def __str__(self):\n"
    "        return _hop(self, 'str')\n"
    "    def __getitem__(self, key):\n"
    "        return _hop(self, 'get_item', key)\n"
    "    def __len__(self):\n"
    "        return _hop(self, 'len')\n"
    "    def __del__(self):\n"
    "        try:\n"
    "            _fcx.release_later(self._id)\n"
    "        except Exception:\n"
    "            pass\n"
    "def _attr(name):\n"
    "    def get(self):\n"
    "        return _fcx.track(_hop(self, 'get_attr', name), self, name)\n"
    "    return property(get)\n"
    "def _method(name):\n"
    "    if name == 'addExtension':\n"
    // the object's extensions changed under the proxy: recompose
    // its class from the facades the host now reports (FcxWire OpExt)
    "        def call(self, *args, **kw):\n"
    "            r = _hop(self, 'call', name, args, kw)\n"
    "            self.__class__ = _composed(self._fc, tuple(_hop(self, 'ext')))\n"
    "            return r\n"
    "    else:\n"
    "        def call(self, *args, **kw):\n"
    "            return _hop(self, 'call', name, args, kw)\n"
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
    "        sys.modules[modname] = m\n"
    "        if modname == 'Part':\n"
    "            m.Precision = _Precision\n"
    // Part.Precision: OCCT's Precision class is compile-time constants
    // (Precision.hxx), so it lives here rather than crossing.
    "class _Precision:\n"
    "    @staticmethod\n"
    "    def confusion():\n"
    "        return 1e-7\n"
    "    @staticmethod\n"
    "    def squareConfusion():\n"
    "        return 1e-14\n"
    "    @staticmethod\n"
    "    def angular():\n"
    "        return 1e-12\n"
    "    @staticmethod\n"
    "    def intersection():\n"
    "        return 1e-9\n"
    "    @staticmethod\n"
    "    def approximation():\n"
    "        return 1e-6\n"
    "    @staticmethod\n"
    "    def pConfusion():\n"
    "        return 1e-9\n"
    "    @staticmethod\n"
    "    def pIntersection():\n"
    "        return 1e-11\n"
    "    @staticmethod\n"
    "    def pApproximation():\n"
    "        return 1e-8\n"
    "    @staticmethod\n"
    "    def infinite():\n"
    "        return 2e100\n"
    "    @staticmethod\n"
    "    def isInfinite(value):\n"
    "        return abs(value) >= 1e100\n"
    "    @staticmethod\n"
    "    def isPositiveInfinite(value):\n"
    "        return value >= 1e100\n"
    "    @staticmethod\n"
    "    def isNegativeInfinite(value):\n"
    "        return value <= -1e100\n"
    // A handle whose object carries extensions (Part::AttachExtension
    // on a Draft Wire): the extension methods are injected per
    // instance on the host, so the proxy class is composed from the
    // type's facade plus each extension's, once per combination.
    "COMPOSED = {}\n"
    "def _composed(fc, ext):\n"
    "    key = (fc, ext)\n"
    "    cls = COMPOSED.get(key)\n"
    "    if cls is None:\n"
    "        bases = []\n"
    "        base = FACADES.get(fc) if fc else None\n"
    "        if base is not None:\n"
    "            bases.append(base)\n"
    "        for e in ext:\n"
    "            b = FACADES.get(e)\n"
    "            if b is not None and b not in bases:\n"
    "                bases.append(b)\n"
    "        if not bases:\n"
    "            bases.append(HostHandle)\n"
    "        cls = type((fc or 'HostHandle') + '+' + '+'.join(ext), tuple(bases), {'__slots__': ()})\n"
    "        COMPOSED[key] = cls\n"
    "    return cls\n"
    // Rung 2 (docs/Sandbox.md 7.6, G1c): guest-resident Proxies.  The
    // registry maps a proxy id to the live instance; the id is minted
    // here and travels in the descriptor the host keeps in its
    // stand-in, so a stand-in outliving THIS guest (a reset) finds
    // its proxy gone -- ReferenceError, never a silent new instance.
    "PROXIES = {}\n"
    "PROXY_IDS = {}\n"
    "_next_proxy = [1]\n"
    "HOOKS = ('execute', 'mustExecute', 'skipRecompute', 'onBeforeChange',\n"
    "         'onBeforeChangeLabel', 'onChanged', 'onDocumentRestored',\n"
    "         'unsetupObject', 'getViewProviderName', 'getSubObject',\n"
    "         'getSubObjects', 'getLinkedObject', 'canLinkProperties',\n"
    "         'allowDuplicateLabel', 'redirectSubName', 'canLoadPartial',\n"
    "         'hasChildElement', 'isElementVisible', 'isElementVisibleEx',\n"
    "         'setElementVisible', 'getElementMapVersion', 'editProperty')\n"
    // dumps/loads are always offered: PropertyPythonObject persists a
    // Proxy through them, and for a class without its own the guest
    // answers as the host would natively (__getstate__/__setstate__
    // when the class defines them, else the instance __dict__).
    "def _defines(cls, name):\n"
    "    return any(name in vars(c) for c in cls.__mro__ if c is not object)\n"
    // The GUI objects a workbench registers (docs/Sandbox.md 7.9, G2):
    // the host polls and triggers a command through these names, and
    // drives a workbench handler through those.  They are stand-ins
    // too, registered with their own hook list -- no dumps/loads, the
    // host never persists them.
    "CMD_HOOKS = ('GetResources', 'Activated', 'IsActive', 'GetCommands',\n"
    "             'GetDefaultCommand', 'OnActionInit', 'CmdHelpURL')\n"
    "WB_HOOKS = ('Initialize', 'Activated', 'Deactivated', 'ContextMenu', 'GetClassName')\n"
    // the guest's Jupyter comm manager (the `comm` shim of the
    // fcx_widgets wheel, docs/Sandbox.md 7.3): the host's widget manager
    // delivers a model's messages through these
    "COMM_HOOKS = ('host_msg', 'host_close', 'host_open')\n"
    // a task panel shown from the guest (docs/Sandbox.md 7.11, G3a):
    // what the host's TaskDialogPython reads off a panel object
    "PANEL_HOOKS = ('accept', 'reject', 'clicked', 'open', 'getStandardButtons',\n"
    "               'modifyStandardButtons', 'needsFullSpace', 'isAllowedAlterDocument',\n"
    "               'isAllowedAlterView', 'isAllowedAlterSelection', 'helpRequested',\n"
    "               'shouldShow')\n"
    "def _proxy_register(inst, hooks=None):\n"
    "    pid = PROXY_IDS.get(id(inst))\n"
    "    if pid is None:\n"
    "        pid = _next_proxy[0]\n"
    "        _next_proxy[0] += 1\n"
    "        PROXIES[pid] = inst\n"
    "        PROXY_IDS[id(inst)] = pid\n"
    "    cls = type(inst)\n"
    "    names = [h for h in (HOOKS if hooks is None else hooks) if callable(getattr(cls, h, None))]\n"
    "    if hooks is None:\n"
    "        names.extend(('dumps', 'loads'))\n"
    "    return {'t': 'gproxy', 'id': pid, 'mod': cls.__module__, 'cls': cls.__qualname__,\n"
    "            'hooks': names}\n"
    "def _proxy_dumps(inst):\n"
    "    if _defines(type(inst), '__getstate__'):\n"
    "        return inst.__getstate__()\n"
    "    return getattr(inst, '__dict__', None)\n"
    "def _proxy_loads(inst, state):\n"
    "    if _defines(type(inst), '__setstate__'):\n"
    "        inst.__setstate__(state)\n"
    "    elif state is not None:\n"
    "        inst.__dict__ = state\n"
    "def _proxy_get(pid):\n"
    "    inst = PROXIES.get(pid)\n"
    "    if inst is None:\n"
    "        raise ReferenceError('guest proxy %d is gone' % pid)\n"
    "    return inst\n"
    "def _proxy_drop(pids):\n"
    "    for pid in pids:\n"
    "        inst = PROXIES.pop(pid, None)\n"
    "        if inst is not None:\n"
    "            PROXY_IDS.pop(id(inst), None)\n"
    "def _proxy_new(mod, cls, args, kw, alloc):\n"
    // no importlib: the WASI stdlib slice does not carry it
    "    klass = __import__(mod)\n"
    "    for part in mod.split('.')[1:]:\n"
    "        klass = getattr(klass, part)\n"
    "    for part in cls.split('.'):\n"
    "        klass = getattr(klass, part)\n"
    "    if alloc:\n"
    "        return _proxy_register(klass.__new__(klass))\n"
    // registered whether or not __init__ did `obj.Proxy = self`: the
    // instance is the VALUE of `cls(...)` on the host, and Draft's
    // `Array(None)` is installed later by addObject(..., attach=True)
    "    return _proxy_register(klass(*args, **kw))\n"
    // A host read of a Proxy attribute (proxy_get): data by value, a
    // method as a descriptor the host binds into a forwarder.  A class
    // object is data (Draft never reads one, but `callable` says yes).
    "def _proxy_attr(pid, name):\n"
    "    value = getattr(_proxy_get(pid), name)\n"
    "    if callable(value) and not isinstance(value, type):\n"
    "        return {'t': 'gmethod', 'id': pid, 'n': name}\n"
    "    return value\n"
    "def _proxy_setattr(pid, name, value):\n"
    "    setattr(_proxy_get(pid), name, value)\n"
    "def _proxy_call(pid, m, args, kw):\n"
    "    inst = _proxy_get(pid)\n"
    "    fn = getattr(inst, m, None)\n"
    "    if fn is None and m == 'dumps':\n"
    "        return _proxy_dumps(inst)\n"
    "    if fn is None and m == 'loads':\n"
    "        return _proxy_loads(inst, *args)\n"
    "    return fn(*args, **kw)\n"
    // ---- FreeCADGui as the guest sees it (docs/Sandbox.md 7.9, G2a):
    // U1 registration.  A command or workbench handler the guest
    // registers is a guest proxy with the GUI hook list; the host builds
    // the stand-in the command manager holds (gui.cmd.add) or wraps
    // (gui.wb.add), and drives it back here through proxy_call.  A
    // workbench's toolbar/menu calls are one op on its registered name:
    // the guest never holds the host's __Workbench__.  Every gui.* op is
    // the catalog's `gui` permission on the host (DENY document, ALLOW
    // session and addons).  FreeCAD.GuiUp stays 0: there is no GUI in
    // the guest, and the App side's `if App.GuiUp:` keeps its meaning.
    "import sys as _sys, types as _types\n"
    "_gui = _types.ModuleType('FreeCADGui')\n"
    "_gui.__doc__ = 'FreeCADGui in the sandbox guest: registration only (docs/Sandbox.md 7.9)'\n"
    "_gui_commands = {}\n"
    "_gui_workbenches = {}\n"
    "_WB_METHODS = ('appendToolbar', 'removeToolbar', 'listToolbars', 'getToolbarItems',\n"
    "               'appendCommandbar', 'removeCommandbar', 'listCommandbars',\n"
    "               'appendMenu', 'removeMenu', 'listMenus', 'appendContextMenu',\n"
    "               'removeContextMenu', 'reloadActive', 'name')\n"
    "class _GuiWorkbench:\n"
    "    MenuText = ''\n"
    "    ToolTip = ''\n"
    "    Icon = None\n"
    "    _fcx_name = None\n"
    "    def Initialize(self):\n"
    "        import FreeCAD\n"
    "        FreeCAD.Console.PrintWarning(str(self) + ': Workbench.Initialize() not implemented in subclass!')\n"
    "    def ContextMenu(self, recipient):\n"
    "        pass\n"
    "    def GetClassName(self):\n"
    "        return 'Gui::PythonWorkbench'\n"
    "    def _fcx_wb(self, m, *args):\n"
    "        return _fcx.op('gui.wb', 0, [self._fcx_name or type(self).__name__, m, list(args)])\n"
    "def _wb_method(m):\n"
    "    def method(self, *args):\n"
    "        return self._fcx_wb(m, *args)\n"
    "    method.__name__ = m\n"
    "    return method\n"
    "for _m in _WB_METHODS:\n"
    "    setattr(_GuiWorkbench, _m, _wb_method(_m))\n"
    "_GuiWorkbench.__name__ = _GuiWorkbench.__qualname__ = 'Workbench'\n"
    "_GuiWorkbench.__module__ = 'FreeCADGui'\n"
    "def _gui_add_command(name, obj, activation=None):\n"
    "    if not isinstance(name, str):\n"
    "        raise TypeError('addCommand(name, object[, activation]): name must be a str')\n"
    "    desc = _proxy_register(obj, CMD_HOOKS)\n"
    "    group = getattr(_gui, '_fcx_group', None) or type(obj).__module__.split('.')[0]\n"
    "    _fcx.op('gui.cmd.add', 0, [name, desc, group, activation])\n"
    "    _gui_commands[name] = obj\n"
    "def _gui_add_workbench(wb):\n"
    "    if isinstance(wb, type) and issubclass(wb, _GuiWorkbench):\n"
    "        name = wb.__name__\n"
    "        wb = wb()\n"
    "    elif isinstance(wb, _GuiWorkbench):\n"
    "        name = type(wb).__name__\n"
    "    else:\n"
    "        raise TypeError('arg must be a subclass or an instance of a subclass of Workbench')\n"
    "    wb._fcx_name = name\n"
    "    desc = _proxy_register(wb, WB_HOOKS)\n"
    "    icon = wb.Icon if isinstance(wb.Icon, str) else None\n"
    "    _fcx.op('gui.wb.add', 0, [name, desc, str(wb.MenuText), str(wb.ToolTip), icon])\n"
    "    _gui_workbenches[name] = wb\n"
    "def _gui_remove_workbench(name):\n"
    "    _fcx.op('gui.wb.remove', 0, name)\n"
    "    _gui_workbenches.pop(name, None)\n"
    "def _gui_get_workbench(name):\n"
    "    wb = _gui_workbenches.get(name)\n"
    "    if wb is None:\n"
    "        raise KeyError(\"workbench '%s' is not registered from the sandbox\" % name)\n"
    "    return wb\n"
    "def _gui_active_workbench():\n"
    "    return _gui_workbenches.get(_fcx.op('gui.wb.active', 0))\n"
    "def _gui_list_workbenches():\n"
    "    return {n: _gui_workbenches.get(n) for n in _fcx.op('gui.wb.list', 0)}\n"
    "def _gui_list_commands():\n"
    "    return _fcx.op('gui.cmd.list', 0)\n"
    "def _gui_add_icon_path(path):\n"
    "    _fcx.op('gui.icon_path', 0, path)\n"
    "def _gui_add_language_path(path):\n"
    "    _fcx.op('gui.lang_path', 0, path)\n"
    "def _gui_add_preference_page(page, group):\n"
    "    if not isinstance(page, str):\n"
    "        raise TypeError('a preference page class cannot be registered from the sandbox;'\n"
    "                        ' give the .ui file (forms are not in the guest yet)')\n"
    "    _fcx.op('gui.pref_page', 0, [page, group])\n"
    "_gui.Workbench = _GuiWorkbench\n"
    "_gui.addCommand = _gui_add_command\n"
    "_gui.addWorkbench = _gui_add_workbench\n"
    "_gui.removeWorkbench = _gui_remove_workbench\n"
    "_gui.getWorkbench = _gui_get_workbench\n"
    "_gui.activeWorkbench = _gui_active_workbench\n"
    "_gui.listWorkbenches = _gui_list_workbenches\n"
    "_gui.listCommands = _gui_list_commands\n"
    "_gui.addIconPath = _gui_add_icon_path\n"
    "_gui.addLanguagePath = _gui_add_language_path\n"
    "_gui.addPreferencePage = _gui_add_preference_page\n"
    "_gui.updateLocale = lambda: None\n"
    // Draft's Initialize self-test compares the host's Coin with pivy's
    "_gui.getSoDBVersion = lambda: _fcx.op('gui.sodb_version', 0)\n"
    // U3 (docs/Sandbox.md 7.3): the guest's ipywidgets models cross as
    // Jupyter comm traffic; the comm shim registers its manager as a
    // guest proxy once, and a widget is shown on the host by its model
    // id (a task panel, or a window), IPython.display.display's route.
    "def _gui_register_comm_manager(manager):\n"
    "    _fcx.op('gui.comm.manager', 0, _proxy_register(manager, COMM_HOOKS))\n"
    "def _gui_show_widget(widget, title=None, where='panel'):\n"
    "    model_id = widget if isinstance(widget, str) else widget.model_id\n"
    "    return _fcx.op('gui.widget.show', 0, [model_id, title, where])\n"
    "def _gui_hide_widget(widget):\n"
    "    model_id = widget if isinstance(widget, str) else widget.model_id\n"
    "    return _fcx.op('gui.widget.hide', 0, model_id)\n"
    "_gui._register_comm_manager = _gui_register_comm_manager\n"
    "_gui.showWidget = _gui_show_widget\n"
    "_gui.hideWidget = _gui_hide_widget\n"
    // G3a (docs/Sandbox.md 7.11): the forms.  Control, PySideUic and
    // UiLoader live in the fcx_widgets wheel (freecad.widgets.gui) and
    // load on first use; a panel shown through Control registers as a
    // guest proxy with the panel hooks and crosses with its form ids.
    "def _gui_show_panel(panel, form_ids):\n"
    "    _fcx.op('gui.control.show', 0, [_proxy_register(panel, PANEL_HOOKS), list(form_ids)])\n"
    "_gui._show_panel = _gui_show_panel\n"
    "def _gui_getattr(name):\n"
    "    if name in ('Control', 'PySideUic', 'UiLoader'):\n"
    "        from freecad.widgets import gui as _forms\n"
    "        return getattr(_forms, name)\n"
    // the panels' finish(): `Gui.ActiveDocument.resetEdit()` -- the
    // first U4 op, the GUI document of the active document, or None
    "    if name == 'ActiveDocument':\n"
    "        from freecad.widgets import gui as _forms\n"
    "        return _forms.active_document()\n"
    "    raise AttributeError('FreeCADGui.%s is not in the sandbox (docs/Sandbox.md 7)' % name)\n"
    "_gui.__getattr__ = _gui_getattr\n"
    "_sys.modules['FreeCADGui'] = _gui\n"
    "del _sys, _types, _m\n";

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
        auto ext = v.find("ext");
        const char* fcKey = fc != v.end() && fc->is_string()
            ? fc->get_ref<const std::string&>().c_str() : nullptr;
        PyObject* type = nullptr;
        PyObject* composed = nullptr;
        if (ext != v.end() && ext->is_array() && !ext->empty()) {
            // extensions on the object: the class composed from the
            // type's facade and theirs (prelude _composed, cached)
            PyObject* keys = PyTuple_New((Py_ssize_t)ext->size());
            if (!keys)
                return nullptr;
            Py_ssize_t i = 0;
            for (const auto& e : *ext)
                PyTuple_SET_ITEM(keys, i++, PyUnicode_FromString(
                    e.is_string() ? e.get_ref<const std::string&>().c_str() : ""));
            PyObject* fn = preludeFunction("_composed");
            composed = fn ? PyObject_CallFunction(fn, "sO", fcKey, keys) : nullptr;
            Py_DECREF(keys);
            if (!composed)
                return nullptr;
            type = composed;
        }
        else
            type = facadeClass(fcKey);
        if (type && id != v.end() && id->is_number_integer() && ty != v.end()
                && ty->is_string()) {
            PyObject* inst = PyObject_CallNoArgs(type);
            Py_XDECREF(composed);
            if (!inst)
                return nullptr;
            PyObject* pid = PyLong_FromUnsignedLongLong(id->get<uint64_t>());
            PyObject* pty = PyUnicode_FromString(
                ty->get_ref<const std::string&>().c_str());
            PyObject* pfc = fcKey ? PyUnicode_FromString(fcKey) : (Py_INCREF(Py_None), Py_None);
            // the durable key of a document object ("k"): a tuple of
            // its names, None for a value object
            PyObject* pk = nullptr;
            auto key = v.find("k");
            if (key != v.end() && key->is_array() && !key->empty()) {
                pk = PyTuple_New((Py_ssize_t)key->size());
                Py_ssize_t i = 0;
                for (const auto& part : *key)
                    if (pk)
                        PyTuple_SET_ITEM(pk, i++, PyUnicode_FromString(
                            part.is_string() ? part.get_ref<const std::string&>().c_str() : ""));
            }
            else {
                Py_INCREF(Py_None);
                pk = Py_None;
            }
            int rc = (pid && pty && pfc && pk) ? PyObject_SetAttrString(inst, "_id", pid)
                                               : -1;
            if (rc == 0)
                rc = PyObject_SetAttrString(inst, "_ty", pty);
            if (rc == 0)
                rc = PyObject_SetAttrString(inst, "_fc", pfc);
            if (rc == 0)
                rc = PyObject_SetAttrString(inst, "_k", pk);
            Py_XDECREF(pid);
            Py_XDECREF(pty);
            Py_XDECREF(pfc);
            Py_XDECREF(pk);
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
        Py_XDECREF(composed);
    }
    else if (t == FcxWire::TagGuestProxy) {
        // a Proxy that lives here, named by the host's stand-in
        auto id = v.find("id");
        PyObject* fn = preludeFunction("_proxy_get");
        if (fn && id != v.end() && id->is_number_integer())
            return PyObject_CallFunction(fn, "K", (unsigned long long)id->get<uint64_t>());
    }
bad:
    PyErr_SetString(PyExc_ValueError, "malformed typed wire value");
    return nullptr;
}

PyObject* preludeFunction(const char* name)
{
    PyObject* ns = proxyNamespace();
    PyObject* fn = ns ? PyDict_GetItemString(ns, name) : nullptr;  // borrowed
    if (!fn)
        PyErr_Format(PyExc_RuntimeError, "sandbox prelude has no '%s'", name);
    return fn;
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
        if (ok) {
            out = {{FcxWire::TagKey, FcxWire::TagHandle},
                   {"id", (uint64_t)PyLong_AsUnsignedLongLong(pid)},
                   {"ty", std::string(PyUnicode_AsUTF8(pty))}};
            // the durable key rides along, so a stale id re-resolves
            // on the host while decoding (FcxWire OpResolve)
            PyObject* pk = PyObject_GetAttrString(obj, "_k");
            if (pk && PyTuple_Check(pk)) {
                json key = json::array();
                for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(pk); ++i) {
                    PyObject* part = PyTuple_GET_ITEM(pk, i);
                    key.push_back(std::string(PyUnicode_Check(part) ? PyUnicode_AsUTF8(part) : ""));
                }
                out["k"] = std::move(key);
            }
            Py_XDECREF(pk);
            if (!pk)
                PyErr_Clear();
        }
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

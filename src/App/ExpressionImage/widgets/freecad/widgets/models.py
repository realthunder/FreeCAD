# SPDX-License-Identifier: LGPL-2.1-or-later
"""The Qt widget classes of the sandbox guest, as widget-protocol models
(docs/Sandbox.md 7.11).

Each class is an ``ipywidgets.Widget`` (not a DOMWidget: one comm per
widget, no layout or style models) named after the Qt class it stands
for, ``_model_module = "freecad.widgets"``, ``_model_name = "<Qt
class>Model"``.  Its synced traits are the Qt properties, named
``q_<property>`` (``q_text``, ``q_checked``, ``q_value``, ...: the
prefix keeps the trait apart from the Qt getter of the same name,
``text()``, ``checked()``; the host strips it and calls Qt's own
``setProperty``); its methods are the Qt accessors the workbenches call
(``setText``/``text``, ``setChecked``/``isChecked``, ``setValue``/
``value``, ...).  Signals are ``Signal`` descriptors: a value signal
(``stateChanged``, ``valueChanged``, ``currentIndexChanged``,
``textChanged``) is emitted from the trait's observer, so a
programmatic set fires it exactly as Qt does; an event signal
(``clicked``, ``pressed``, ``returnPressed``, ``editingFinished``,
``textEdited``) arrives as a custom message ``{"event": name, "args":
[...]}`` from the host's view.

``_touched`` lists the properties the guest SET -- through a setter or
a constructor argument.  The rest of the state is the class default,
or the value a ``.ui`` file gave (the loader writes those silently):
the host applies only the touched properties to a widget it binds or
builds, and leaves the rest to uic (translated) or to Qt.
"""

import inspect

import ipywidgets
from traitlets import Bool, Dict, Float, Int, List, Unicode, observe
from ipywidgets import widget_serialization

from . import qtdata

MODULE = "freecad.widgets"
MODULE_VERSION = "0.1"
PREFIX = "q_"


# -- signals ------------------------------------------------------------


def _arity(fn):
    """How many positional arguments `fn` takes (None: any number)."""
    try:
        sig = inspect.signature(fn)
    except (TypeError, ValueError):
        return None
    n = 0
    for p in sig.parameters.values():
        if p.kind == p.VAR_POSITIONAL:
            return None
        if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD):
            n += 1
    return n


class BoundSignal:
    """A signal on one object: `connect`, `disconnect`, `emit`.  A slot
    taking fewer arguments than the signal carries gets the first ones,
    as Qt does."""

    __slots__ = ("owner", "name", "_slots")

    def __init__(self, owner, name):
        self.owner = owner
        self.name = name
        self._slots = []

    def connect(self, slot):
        if not callable(slot):
            raise TypeError("%s.connect: %r is not callable" % (self.name, slot))
        self._slots.append((slot, _arity(slot)))

    def disconnect(self, slot=None):
        if slot is None:
            self._slots = []
            return
        self._slots = [(s, n) for (s, n) in self._slots if s != slot]

    def emit(self, *args):
        for slot, n in list(self._slots):
            slot(*(args if n is None else args[:n]))

    def __call__(self, *args):
        self.emit(*args)

    def __repr__(self):
        return "<Signal %s of %r>" % (self.name, self.owner)


class Signal:
    """The class-level descriptor (PySide's `QtCore.Signal`)."""

    def __init__(self, *types, **kwargs):
        self.types = types
        self.name = kwargs.get("name")

    def __set_name__(self, owner, name):
        if self.name is None:
            self.name = name

    def __get__(self, obj, owner=None):
        if obj is None:
            return self
        try:
            signals = obj._fcx_signals
        except AttributeError:
            signals = {}
            object.__setattr__(obj, "_fcx_signals", signals)
        s = signals.get(self.name)
        if s is None:
            s = signals[self.name] = BoundSignal(obj, self.name)
        return s


def SIGNAL(spec):
    """Old-style `QtCore.SIGNAL("clicked()")`: the signal's name."""
    return spec.split("(", 1)[0].strip()


def SLOT(spec):
    return spec.split("(", 1)[0].strip()


class QObject:
    """The old-style static `connect` (285 uses in Draft and BIM) and a
    plain object base for what is not a widget."""

    @staticmethod
    def connect(sender, signal, slot=None, *rest):
        if slot is None:
            raise TypeError("QObject.connect(sender, SIGNAL, slot)")
        if isinstance(signal, str):
            signal = getattr(sender, SIGNAL(signal))
        signal.connect(slot)
        return True

    @staticmethod
    def disconnect(sender, signal, slot=None, *rest):
        if isinstance(signal, str):
            signal = getattr(sender, SIGNAL(signal))
        signal.disconnect(slot)
        return True

    def __init__(self, parent=None):
        self._parent = parent
        self._objectName = ""

    def parent(self):
        return self._parent

    def objectName(self):
        return self._objectName

    def setObjectName(self, name):
        self._objectName = name

    def deleteLater(self):
        pass

    def eventFilter(self, obj, event):
        """The base: nothing filtered (a subclass installed on a widget
        sees the events the host relays, docs/Sandbox.md 7.11 G3c)."""
        return False

    def emit(self, signal, *args):
        """Old-style `self.emit(QtCore.SIGNAL("escaped()"))`: the signal
        of that name, made on the object if it declares none."""
        name = SIGNAL(signal) if isinstance(signal, str) else signal.name
        sig = getattr(self, name, None)
        if not isinstance(sig, BoundSignal):
            try:
                signals = self._fcx_signals
            except AttributeError:
                signals = {}
                object.__setattr__(self, "_fcx_signals", signals)
            sig = signals.get(name)
            if sig is None:
                sig = signals[name] = BoundSignal(self, name)
        sig.emit(*args)

    def __getattr__(self, name):
        # a signal connected by name before it was ever emitted
        # (`QtCore.QObject.connect(w, SIGNAL("escaped()"), fn)`)
        signals = self.__dict__.get("_fcx_signals")
        if signals is not None and name in signals:
            return signals[name]
        raise AttributeError("%r has no attribute %r" % (type(self).__name__, name))


# -- the widget base ---------------------------------------------------------


def _q(name):
    return PREFIX + name


def _ref(widget):
    """A widget named in a request: its model ref, the host resolves it."""
    return None if widget is None else "IPY_MODEL_" + widget.model_id


# every model by id: what a ref in a host event resolves to
_REGISTRY = {}


def _deref(value):
    """A model ref (`IPY_MODEL_<id>`) in a host event's arguments, or a
    list of them, as the model; anything else as it is."""
    if isinstance(value, str) and value.startswith("IPY_MODEL_"):
        return _REGISTRY.get(value[10:])
    if isinstance(value, list):
        return [_deref(v) for v in value]
    return value


_MAIN_WINDOW_ATTR = "_fcx_mainwindow"


class QWidget(ipywidgets.Widget):
    """The base: identity, visibility, enablement, tooltip, the window
    properties of a root, and the children tree (guest side)."""

    _model_module = Unicode(MODULE).tag(sync=True)
    _model_module_version = Unicode(MODULE_VERSION).tag(sync=True)
    _model_name = Unicode("QWidgetModel").tag(sync=True)
    _view_module = Unicode(MODULE).tag(sync=True)
    _view_module_version = Unicode(MODULE_VERSION).tag(sync=True)
    _view_name = Unicode("QWidgetView").tag(sync=True)

    qtClass = Unicode("QWidget").tag(sync=True)
    _touched = List(Unicode()).tag(sync=True)

    q_objectName = Unicode("").tag(sync=True)
    q_visible = Bool(True).tag(sync=True)
    q_enabled = Bool(True).tag(sync=True)
    q_toolTip = Unicode("").tag(sync=True)
    q_statusTip = Unicode("").tag(sync=True)
    q_whatsThis = Unicode("").tag(sync=True)
    q_windowTitle = Unicode("").tag(sync=True)
    q_windowIcon = Unicode("").tag(sync=True)
    q_styleSheet = Unicode("").tag(sync=True)
    q_minimumWidth = Int(0).tag(sync=True)
    q_minimumHeight = Int(0).tag(sync=True)
    q_maximumWidth = Int(16777215).tag(sync=True)
    q_maximumHeight = Int(16777215).tag(sync=True)
    q_prefEntry = Unicode("").tag(sync=True)
    q_prefPath = Unicode("").tag(sync=True)
    # G3c (docs/Sandbox.md 7.11): a font as data, the actions the widget
    # carries, the QEvent types the host relays, the focus it reports
    q_font = Dict().tag(sync=True)
    q_actions = List().tag(sync=True)
    q_watchEvents = List(Int()).tag(sync=True)
    q_focus = Bool(False).tag(sync=True)
    # a code-built layout tree, whole, re-synced on every mutation (the
    # host builds the real layouts from it when it realizes the widget)
    layoutSpec = Dict(allow_none=True, default_value=None).tag(sync=True)

    destroyed = Signal()

    # the Qt class name a plain constructor stands for (a subclass's
    # own; `Gui::PrefCheckBox` when the loader made it)
    qt_class = "QWidget"

    def __init__(self, *args, **kwargs):
        parent = kwargs.pop("parent", None)
        args = list(args)
        for a in list(args):
            if isinstance(a, QWidget) or a is None:
                parent = a
                args.remove(a)
        kwargs.setdefault("qtClass", self.qt_class)
        self._parent = None
        self._children = []
        self._layout = None
        self._data = {}
        self._filters = []
        self._fcx_init_args(args, kwargs)
        # a constructor argument is a set: QLabel("text") shows "text"
        kwargs["_touched"] = [k[len(PREFIX):] for k in kwargs if k.startswith(PREFIX)]
        super().__init__(**kwargs)
        _REGISTRY[self.model_id] = self
        if parent is not None:
            if getattr(parent, _MAIN_WINDOW_ATTR, False):
                parent = None
            self._attach(parent)
        # a subclass handling key presses itself asks for them
        if type(self).keyPressEvent is not QWidget.keyPressEvent:
            self._watch(qtdata.QEvent.KeyPress)

    def _fcx_init_args(self, args, kwargs):
        """A subclass reads its Qt-style positional arguments here."""
        if args:
            raise TypeError("%s(): unexpected positional arguments %r"
                            % (type(self).__name__, tuple(args)))

    # -- what the host sends: state (traitlets) and events

    def _handle_custom_msg(self, content, buffers):
        if isinstance(content, dict) and "event" in content:
            args = _deref(content.get("args") or [])
            if content["event"] == "qevent":
                # a relayed QEvent: answered inside it, so the host's
                # filter knows whether to let the widget have it
                self._event("eventDone", bool(self._dispatch_qevent(args)))
                return
            signal = getattr(self, content["event"], None)
            if isinstance(signal, BoundSignal):
                signal.emit(*args)
                return
        super()._handle_custom_msg(content, buffers)

    # -- the event stream (G3c): the QEvent types the widget asked for

    def _watch(self, *types):
        want = list(self.q_watchEvents)
        new = [int(t) for t in types if int(t) not in want]
        if new:
            self._set(watchEvents=want + new)

    def _dispatch_qevent(self, args):
        """The event through the installed filters, then the widget's
        own handler; True when one of them ate it."""
        if not args:
            return False
        ev = qtdata.QEvent.make(args)
        for f in list(self._filters):
            try:
                if f.eventFilter(self, ev):
                    return True
            except Exception:
                import traceback

                traceback.print_exc()
        if ev.type() == qtdata.QEvent.KeyPress \
                and type(self).keyPressEvent is not QWidget.keyPressEvent:
            ev._to_host = False
            self.keyPressEvent(ev)
            return not ev._to_host
        return False

    def keyPressEvent(self, event):
        """The base handler: the real widget takes the key."""
        event._to_host = True

    emit = QObject.emit

    def eventFilter(self, obj, event):
        return False

    def installEventFilter(self, obj):
        if obj not in self._filters:
            self._filters.append(obj)
        self._watch(qtdata.QEvent.KeyPress, qtdata.QEvent.MouseButtonDblClick)

    def removeEventFilter(self, obj):
        if obj in self._filters:
            self._filters.remove(obj)

    # -- actions (G3c): `addAction` on any widget, positioned on a line edit

    def addAction(self, *args):
        """`addAction(action)`, `addAction(icon, position)` (a line edit's
        trailing icon), `addAction(text)`: the action, carried as state."""
        action = None
        position = -1
        for a in args:
            if isinstance(a, QAction):
                action = a
            elif isinstance(a, (qtdata.QIcon, qtdata.QPixmap)):
                action = QAction(a, "", self)
            elif isinstance(a, str) and action is None:
                action = QAction(a, self)
            elif isinstance(a, int):
                position = a
        if action is None:
            raise TypeError("addAction: an action, an icon or a text")
        entries = [dict(e) for e in self.q_actions]
        entries.append({"action": _ref(action), "position": position})
        self._set(actions=entries)
        return action

    def removeAction(self, action):
        ref = _ref(action)
        self._set(actions=[dict(e) for e in self.q_actions if e.get("action") != ref])

    def actions(self):
        return [w for w in (_deref(e.get("action")) for e in self.q_actions) if w is not None]

    def insertAction(self, before, action):
        return self.addAction(action)

    def _set(self, **props):
        """A Qt setter of the properties `props` (Qt names): the keys
        join `_touched` in the same message as the values."""
        touched = self._touched
        new = [k for k in props if k not in touched]
        with self.hold_sync():
            if new:
                self._touched = list(touched) + new
            for k, v in props.items():
                setattr(self, _q(k), v)

    def _event(self, name, *args):
        """Ask the host to do something to the widget (focus, select)."""
        self.send({"event": name, "args": list(args)})

    def _has(self, prop):
        return self.has_trait(_q(prop))

    # -- tree

    def _attach(self, parent):
        if self._parent is not None and self in self._parent._children:
            self._parent._children.remove(self)
        self._parent = parent
        if parent is not None:
            parent._children.append(self)

    def setParent(self, parent):
        """Re-parent: the guest tree, and the host's widget (None makes
        it a hidden top-level, as Qt does; a layout's addWidget brings
        it back).  The main window shim as a parent is a top-level too
        (a tool bar the main window owns already)."""
        if parent is not None and getattr(parent, _MAIN_WINDOW_ATTR, False):
            parent = None
        if parent is self._parent:
            return
        self._attach(parent)
        if self.comm is not None:
            self._event("setParent", parent.model_id if parent is not None else None)

    def parent(self):
        return self._parent

    def children(self):
        return list(self._children)

    def findChild(self, cls=None, name=None):
        for w in self.findChildren(cls, name):
            return w
        return None

    def findChildren(self, cls=None, name=None):
        out = []
        if cls is not None and not isinstance(cls, type):
            cls = tuple(cls)
        for w in self._descendants():
            if name is not None and w.objectName() != name:
                continue
            if cls is not None and not isinstance(w, cls):
                continue
            out.append(w)
        return out

    def _descendants(self):
        """Child widgets and layouts, depth first (a layout's items are
        its widget's children already; the layouts are yielded for
        `findChild(QtWidgets.QGridLayout, name)`)."""
        if self._layout is not None:
            for lay in self._layout._nested():
                yield lay
        for c in self._children:
            yield c
            for d in c._descendants():
                yield d

    def layout(self):
        return self._layout

    def setLayout(self, layout):
        self._layout = layout
        if layout is not None:
            layout._owner = self
            for item in layout._items:
                if item._widget is not None and item._widget._parent is not self:
                    item._widget.setParent(self)
        self._sync_layout()

    def _sync_layout(self):
        """A code-built layout crosses whole as `layoutSpec` (a `.ui`
        file's layouts are uic's on the host and cross as ops only)."""
        lay = self._layout
        if lay is None or lay._from_ui or self.comm is None:
            return
        self.layoutSpec = lay._spec()

    # -- identity

    def objectName(self):
        return self.q_objectName

    def setObjectName(self, name):
        self._set(objectName=str(name))

    def metaObject(self):
        return _MetaObject(self.qtClass)

    def inherits(self, name):
        return name == self.qtClass or any(name == getattr(c, "qt_class", None)
                                            for c in type(self).__mro__)

    # -- visibility, enablement

    def show(self):
        self._set(visible=True)

    def hide(self):
        self._set(visible=False)

    def setVisible(self, on):
        self._set(visible=bool(on))

    def setHidden(self, on):
        self._set(visible=not on)

    def isVisible(self):
        return self.q_visible

    def isHidden(self):
        return not self.q_visible

    def setEnabled(self, on):
        self._set(enabled=bool(on))

    def setDisabled(self, on):
        self._set(enabled=not on)

    def isEnabled(self):
        return self.q_enabled

    # -- tips and titles

    def setToolTip(self, text):
        self._set(toolTip=str(text))

    def toolTip(self):
        return self.q_toolTip

    def setStatusTip(self, text):
        self._set(statusTip=str(text))

    def setWhatsThis(self, text):
        self._set(whatsThis=str(text))

    def setWindowTitle(self, text):
        self._set(windowTitle=str(text))

    def windowTitle(self):
        return self.q_windowTitle

    def setWindowIcon(self, icon):
        self._set(windowIcon=_icon_path(icon))

    def windowIcon(self):
        return qtdata.QIcon(self.q_windowIcon)

    def setStyleSheet(self, css):
        self._set(styleSheet=str(css))

    def styleSheet(self):
        return self.q_styleSheet

    # -- geometry hints: accepted, the sizes honored by the host

    def setMinimumWidth(self, w):
        self._set(minimumWidth=int(w))

    def setMinimumHeight(self, h):
        self._set(minimumHeight=int(h))

    def setMaximumWidth(self, w):
        self._set(maximumWidth=int(w))

    def setMaximumHeight(self, h):
        self._set(maximumHeight=int(h))

    def setFixedWidth(self, w):
        self._set(minimumWidth=int(w), maximumWidth=int(w))

    def setFixedHeight(self, h):
        self._set(minimumHeight=int(h), maximumHeight=int(h))

    def setFixedSize(self, *args):
        w, h = (args[0].w, args[0].h) if len(args) == 1 else args
        self.setFixedWidth(w)
        self.setFixedHeight(h)

    def setMinimumSize(self, *args):
        w, h = (args[0].w, args[0].h) if len(args) == 1 else args
        self._set(minimumWidth=int(w), minimumHeight=int(h))

    def setMaximumSize(self, *args):
        w, h = (args[0].w, args[0].h) if len(args) == 1 else args
        self._set(maximumWidth=int(w), maximumHeight=int(h))

    def minimumWidth(self):
        return self.q_minimumWidth

    def maximumWidth(self):
        return self.q_maximumWidth

    def setSizePolicy(self, *args):
        pass

    def sizeHint(self):
        """An estimate (nothing is laid out here): a text's width at
        about seven pixels a character, a widget's 100 x 24."""
        text = getattr(self, "q_text", None)
        if isinstance(text, str):
            return qtdata.QSize(qtdata.QFontMetrics(self.font()).width(text) + 8, 24)
        return qtdata.QSize(100, 24)

    def width(self):
        return self.q_minimumWidth or 100

    def height(self):
        return self.q_minimumHeight or 24

    def setFont(self, font):
        self._set(font=font.toDict() if isinstance(font, qtdata.QFont) else dict(font))

    def font(self):
        return qtdata.QFont.fromDict(self.q_font)

    def setContentsMargins(self, *args):
        pass

    def setFocusPolicy(self, policy):
        pass

    def setAttribute(self, *args):
        pass

    def setContextMenuPolicy(self, policy):
        pass

    def adjustSize(self):
        pass

    def update(self):
        pass

    def repaint(self):
        pass

    def setUpdatesEnabled(self, on):
        pass

    # -- focus: a request to the host

    def setFocus(self, reason=None):
        self._event("setFocus")

    def hasFocus(self):
        """What the host reported (a widget that asked for focus events;
        every text input does)."""
        return self.q_focus

    def clearFocus(self):
        pass

    # -- Qt's dynamic properties: the traits by Qt name

    def setProperty(self, name, value):
        if self._has(name):
            self._set(**{name: value})
            return True
        self._data[name] = value
        return False

    def property(self, name):
        if self._has(name):
            return getattr(self, _q(name))
        return self._data.get(name)

    def dynamicPropertyNames(self):
        return list(self._data)

    def __repr__(self):
        return "<%s %r>" % (self.qtClass, self.q_objectName)


class _MetaObject:
    def __init__(self, name):
        self._name = name

    def className(self):
        return self._name


def _icon_path(icon):
    if isinstance(icon, (qtdata.QIcon, qtdata.QPixmap)):
        return icon.path
    return str(icon or "")


# -- layouts -------------------------------------------------------------------
#
# A layout is not a model: it is a guest object on its widget, holding
# the items in order, and every mutation crosses as a layout op on the
# OWNING widget's comm (`{"layout": name, "op": ..., ...}`), which the
# host applies to the real layout of that name (a .ui file names its
# layouts; uic keeps the names).  A layout with no name reaches no
# host layout: code-built layouts are G3c (docs/Sandbox.md 7.11).


_LAYOUT_SEQ = [0]


def _layout_name():
    _LAYOUT_SEQ[0] += 1
    return "_fcx_layout_%d" % _LAYOUT_SEQ[0]


class QSpacerItem:
    def __init__(self, w=0, h=0, hPolicy=None, vPolicy=None):
        self.w, self.h = w, h
        self.hPolicy, self.vPolicy = hPolicy, vPolicy

    def _policies(self):
        def value(p, default):
            if p is None:
                return default
            return int(getattr(p, "value", p))
        return [value(self.hPolicy, 1), value(self.vPolicy, 1)]

    def widget(self):
        return None

    def layout(self):
        return None

    def spacerItem(self):
        return self


class QLayoutItem:
    """What `itemAt`/`takeAt` return: a widget, a layout or a spacer."""

    __slots__ = ("_widget", "_layout", "_spacer")

    def __init__(self, widget=None, layout=None, spacer=None):
        self._widget, self._layout, self._spacer = widget, layout, spacer

    def widget(self):
        return self._widget

    def layout(self):
        return self._layout

    def spacerItem(self):
        return self._spacer

    def isEmpty(self):
        return self._widget is None and self._layout is None


class QLayout(QObject):
    kind = "layout"
    qt_class = "QBoxLayout"

    def __init__(self, parent=None):
        QObject.__init__(self, None)
        self._items = []  # QLayoutItem, with a position tuple each
        self._positions = []
        self._owner = None
        self._parent_layout = None
        # a code-built layout carries a generated name: the host names
        # the real one after it, so the ops that follow find it
        self._objectName = _layout_name()
        self._from_ui = False
        self._margins = None
        self._spacing = None
        if parent is not None:
            if isinstance(parent, QWidget):
                parent.setLayout(self)
            elif isinstance(parent, QLayout):
                parent.addLayout(self)

    def _spec(self):
        """The whole tree as data: what the host builds the real layouts
        from (docs/Sandbox.md 7.11, G3c)."""
        items = []
        for item, pos in zip(self._items, self._positions):
            if item._widget is not None:
                entry = {"widget": _ref(item._widget)}
            elif item._layout is not None:
                entry = {"layout": item._layout._spec()}
            elif isinstance(item, _ActionItem):
                entry = {"action": _ref(item._action)}
            elif isinstance(item, _SeparatorItem):
                entry = {"separator": True}
            elif pos[:1] == ("stretch",):
                entry = {"stretch": pos[1]}
            elif pos[:1] == ("spacing",):
                entry = {"spacing": pos[1]}
            elif item._spacer is not None:
                sp = item._spacer
                entry = {"spacer": [sp.w, sp.h] + sp._policies()}
            else:
                continue
            if pos and isinstance(pos[0], int):
                entry["pos"] = list(pos)
            items.append(entry)
        spec = {"class": self.qt_class, "name": self._objectName, "items": items}
        if self._margins is not None:
            spec["margins"] = list(self._margins)
        if self._spacing is not None:
            spec["spacing"] = self._spacing
        return spec

    # -- the owner: the widget this layout (or its parent layout) sits on

    def parentWidget(self):
        lay = self
        while lay._owner is None and lay._parent_layout is not None:
            lay = lay._parent_layout
        return lay._owner

    def _notify(self, op, **fields):
        owner = self.parentWidget()
        if owner is None or not self._objectName or owner.comm is None:
            return
        # the whole tree first (a widget not yet realized is built from
        # it), then the op (a realized one applies it to the real layout)
        owner._sync_layout()
        msg = {"layout": self._objectName, "op": op}
        msg.update(fields)
        owner.send(msg)

    def _nested(self):
        yield self
        for item in self._items:
            if item._layout is not None:
                for lay in item._layout._nested():
                    yield lay

    # -- Qt's API

    def count(self):
        return len(self._items)

    def itemAt(self, index):
        return self._items[index] if 0 <= index < len(self._items) else None

    def takeAt(self, index):
        if not 0 <= index < len(self._items):
            return None
        item = self._items.pop(index)
        self._positions.pop(index)
        if item._layout is not None:
            item._layout._parent_layout = None
        self._notify("takeAt", index=index)
        return item

    def indexOf(self, widget):
        for i, item in enumerate(self._items):
            if item._widget is widget or item._layout is widget:
                return i
        return -1

    def removeWidget(self, widget):
        i = self.indexOf(widget)
        if i >= 0:
            self._items.pop(i)
            self._positions.pop(i)
            self._notify("removeWidget", widget=widget.model_id)

    def removeItem(self, item):
        if item in self._items:
            i = self._items.index(item)
            self.takeAt(i)

    def _add_widget(self, widget, position, index=None):
        if not isinstance(widget, QWidget):
            raise TypeError("addWidget: %r is not a widget" % (widget,))
        owner = self.parentWidget()
        if owner is not None and widget._parent is not owner:
            # Qt reparents the widget to the layout's widget
            widget.setParent(owner)
        item = QLayoutItem(widget=widget)
        if index is None:
            self._items.append(item)
            self._positions.append(position)
        else:
            self._items.insert(index, item)
            self._positions.insert(index, position)
        self._notify("insertWidget" if index is not None else "addWidget",
                     widget=widget.model_id, args=list(position),
                     **({"index": index} if index is not None else {}))

    def _add_layout(self, layout, position):
        if not isinstance(layout, QLayout):
            raise TypeError("addLayout: %r is not a layout" % (layout,))
        layout._parent_layout = self
        self._items.append(QLayoutItem(layout=layout))
        self._positions.append(position)
        self._notify("addLayout", sublayout=layout._objectName, args=list(position))

    def addWidget(self, widget, *args):
        self._add_widget(widget, tuple(args))

    def addLayout(self, layout, *args):
        self._add_layout(layout, tuple(args))

    def addItem(self, item, *args):
        if isinstance(item, QSpacerItem):
            self._items.append(QLayoutItem(spacer=item))
            self._positions.append(tuple(args))
            self._notify("addSpacing", args=[item.w, item.h])
        elif isinstance(item, QLayoutItem):
            self._items.append(item)
            self._positions.append(tuple(args))

    def addStretch(self, stretch=0):
        self._items.append(QLayoutItem(spacer=QSpacerItem()))
        self._positions.append(("stretch", stretch))
        self._notify("addStretch", args=[stretch])

    def addSpacing(self, size):
        self._items.append(QLayoutItem(spacer=QSpacerItem(size, size)))
        self._positions.append(("spacing", size))
        self._notify("addSpacing", args=[size])

    def insertWidget(self, index, widget, *args):
        self._add_widget(widget, tuple(args), index=index)

    def setContentsMargins(self, *args):
        if len(args) == 4:
            self._margins = [int(a) for a in args]
        self._notify("setContentsMargins", args=list(args))

    def setSpacing(self, n):
        self._spacing = int(n)
        self._notify("setSpacing", args=[n])

    def contentsMargins(self):
        return qtdata.QMargins(*(self._margins or [0, 0, 0, 0]))

    def spacing(self):
        return self._spacing if self._spacing is not None else -1

    def parent(self):
        return self._owner if self._owner is not None else self._parent_layout

    def setAlignment(self, *args):
        pass

    def setSizeConstraint(self, c):
        pass

    def setStretch(self, index, stretch):
        pass

    def setColumnStretch(self, col, stretch):
        pass

    def setRowStretch(self, row, stretch):
        pass

    def setColumnMinimumWidth(self, col, w):
        pass

    def setHorizontalSpacing(self, n):
        pass

    def setVerticalSpacing(self, n):
        pass

    def setFieldGrowthPolicy(self, p):
        pass

    def setLabelAlignment(self, a):
        pass

    def invalidate(self):
        pass

    def update(self):
        pass


class QBoxLayout(QLayout):
    LeftToRight = 0
    RightToLeft = 1
    TopToBottom = 2
    BottomToTop = 3

    def __init__(self, direction=None, parent=None):
        if isinstance(direction, (QWidget, QLayout)) and parent is None:
            direction, parent = None, direction
        QLayout.__init__(self, parent)
        self.direction = direction

    def setDirection(self, d):
        self.direction = d


class QVBoxLayout(QBoxLayout):
    kind = "vbox"
    qt_class = "QVBoxLayout"

    def __init__(self, parent=None):
        QBoxLayout.__init__(self, QBoxLayout.TopToBottom, parent)


class QHBoxLayout(QBoxLayout):
    kind = "hbox"
    qt_class = "QHBoxLayout"

    def __init__(self, parent=None):
        QBoxLayout.__init__(self, QBoxLayout.LeftToRight, parent)


class QGridLayout(QLayout):
    kind = "grid"
    qt_class = "QGridLayout"

    def rowCount(self):
        rows = 0
        for pos in self._positions:
            if len(pos) >= 2 and isinstance(pos[0], int):
                rows = max(rows, pos[0] + (pos[2] if len(pos) > 2 else 1))
        return rows

    def columnCount(self):
        cols = 0
        for pos in self._positions:
            if len(pos) >= 2 and isinstance(pos[1], int):
                cols = max(cols, pos[1] + (pos[3] if len(pos) > 3 else 1))
        return cols

    def itemAtPosition(self, row, col):
        for item, pos in zip(self._items, self._positions):
            if len(pos) >= 2 and pos[0] == row and pos[1] == col:
                return item
        return None


class QFormLayout(QLayout):
    kind = "form"
    qt_class = "QFormLayout"

    def addRow(self, label, field=None):
        if field is None:
            if isinstance(label, QLayout):
                self._add_layout(label, ())
            else:
                self._add_widget(label, ())
            return
        if isinstance(label, str):
            label = QLabel(label)
        row = self.rowCount()
        self._add_widget(label, (row, 0))
        if isinstance(field, QLayout):
            self._add_layout(field, (row, 1))
        else:
            self._add_widget(field, (row, 1))

    def rowCount(self):
        rows = 0
        for pos in self._positions:
            if len(pos) >= 2 and isinstance(pos[0], int):
                rows = max(rows, pos[0] + 1)
        return rows

    def setWidget(self, row, role, widget):
        self._add_widget(widget, (row, role))

    def setLayout(self, row, role, layout):
        self._add_layout(layout, (row, role))


class _ActionItem(QLayoutItem):
    """An action on a bar."""

    __slots__ = ("_action",)

    def __init__(self, action):
        QLayoutItem.__init__(self)
        self._action = action

    def action(self):
        return self._action


class _SeparatorItem(QLayoutItem):
    __slots__ = ()


class _Bar(QLayout):
    """A tool bar's or a menu's content: widgets, actions and separators
    in order, the layout of that widget on both sides (the host fills
    the real bar from it, no QLayout involved)."""

    kind = "bar"
    qt_class = "_bar"

    def __init__(self, owner):
        QLayout.__init__(self, None)
        self._objectName = "_fcx_bar"
        owner._layout = self
        self._owner = owner

    def _add_action(self, action, index=None):
        if not isinstance(action, QAction):
            raise TypeError("addAction: %r is not an action" % (action,))
        item = _ActionItem(action)
        if index is None:
            self._items.append(item)
            self._positions.append(())
            self._notify("addAction", action=_ref(action))
        else:
            self._items.insert(index, item)
            self._positions.insert(index, ())
            self._notify("insertAction", action=_ref(action), index=index)

    def _remove_action(self, action):
        for i, item in enumerate(self._items):
            if isinstance(item, _ActionItem) and item._action is action:
                self._items.pop(i)
                self._positions.pop(i)
                self._notify("removeAction", action=_ref(action))
                return

    def _add_separator(self):
        self._items.append(_SeparatorItem())
        self._positions.append(())
        self._notify("addSeparator")

    def _clear(self):
        self._items = []
        self._positions = []
        self._notify("clear")

    def _actions(self):
        return [item._action for item in self._items if isinstance(item, _ActionItem)]


LAYOUTS = {
    "QVBoxLayout": QVBoxLayout,
    "QHBoxLayout": QHBoxLayout,
    "QGridLayout": QGridLayout,
    "QFormLayout": QFormLayout,
    "QBoxLayout": QBoxLayout,
}


# -- labels and buttons ---------------------------------------------------


class QLabel(QWidget):
    _model_name = Unicode("QLabelModel").tag(sync=True)
    qt_class = "QLabel"
    q_text = Unicode("").tag(sync=True)
    q_wordWrap = Bool(False).tag(sync=True)
    q_alignment = Int(0).tag(sync=True)
    q_openExternalLinks = Bool(False).tag(sync=True)
    q_pixmap = Unicode("").tag(sync=True)

    linkActivated = Signal(str)

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], str):
            kwargs["q_text"] = args.pop(0)
        QWidget._fcx_init_args(self, args, kwargs)

    def setText(self, text):
        self._set(text=str(text))

    def text(self):
        return self.q_text

    def setWordWrap(self, on):
        self._set(wordWrap=bool(on))

    def setAlignment(self, a):
        self._set(alignment=int(a))

    def setOpenExternalLinks(self, on):
        self._set(openExternalLinks=bool(on))

    def setPixmap(self, pixmap):
        self._set(pixmap=_icon_path(pixmap))

    def setTextFormat(self, fmt):
        pass

    def setTextInteractionFlags(self, flags):
        pass

    def setIndent(self, n):
        pass

    def clear(self):
        self._set(text="")


class QAbstractButton(QWidget):
    # the button's popup menu (docs/Sandbox.md 7.15): a QMenu model the
    # host sets on the real button
    q_menu = Unicode("", allow_none=True).tag(sync=True)

    def setMenu(self, menu):
        if menu is not None:
            menu._attach(self)
        self._button_menu = menu
        self._set(menu=_ref(menu))

    def menu(self):
        return getattr(self, "_button_menu", None)

    q_text = Unicode("").tag(sync=True)
    q_checkable = Bool(False).tag(sync=True)
    q_checked = Bool(False).tag(sync=True)
    q_icon = Unicode("").tag(sync=True)
    q_autoExclusive = Bool(False).tag(sync=True)

    clicked = Signal(bool)
    pressed = Signal()
    released = Signal()
    toggled = Signal(bool)

    def _fcx_init_args(self, args, kwargs):
        for a in list(args):
            if isinstance(a, str):
                kwargs["q_text"] = a
                args.remove(a)
            elif isinstance(a, qtdata.QIcon):
                kwargs["q_icon"] = a.path
                args.remove(a)
        QWidget._fcx_init_args(self, args, kwargs)

    def setText(self, text):
        self._set(text=str(text))

    def text(self):
        return self.q_text

    def setIcon(self, icon):
        self._set(icon=_icon_path(icon))

    def icon(self):
        return qtdata.QIcon(self.q_icon)

    def setCheckable(self, on):
        self._set(checkable=bool(on))

    def isCheckable(self):
        return self.q_checkable

    def setChecked(self, on):
        on = bool(on)
        if on and self.q_autoExclusive and self._parent is not None:
            for sib in self._parent._children:
                if sib is not self and isinstance(sib, QAbstractButton) \
                        and sib.q_autoExclusive and sib.q_checked:
                    sib._set(checked=False)
        self._set(checked=on)

    def isChecked(self):
        return self.q_checked

    def toggle(self):
        self.setChecked(not self.q_checked)

    def click(self):
        """A programmatic click: toggles a checkable, fires `clicked`."""
        if self.q_checkable:
            self.setChecked(not self.q_checked)
        self.clicked.emit(self.q_checked)

    def animateClick(self, *args):
        self.click()

    def setAutoExclusive(self, on):
        self._set(autoExclusive=bool(on))

    def setShortcut(self, key):
        pass

    def setIconSize(self, size):
        pass

    def setDown(self, on):
        pass

    @observe("q_checked")
    def _fcx_checked(self, change):
        self.toggled.emit(bool(change["new"]))


class QPushButton(QAbstractButton):
    _model_name = Unicode("QPushButtonModel").tag(sync=True)
    qt_class = "QPushButton"
    q_flat = Bool(False).tag(sync=True)
    q_default = Bool(False).tag(sync=True)

    def setFlat(self, on):
        self._set(flat=bool(on))

    def setDefault(self, on):
        self._set(default=bool(on))

    def setAutoDefault(self, on):
        pass

    def showMenu(self):
        pass


class QToolButton(QAbstractButton):
    _model_name = Unicode("QToolButtonModel").tag(sync=True)
    qt_class = "QToolButton"
    q_autoRaise = Bool(False).tag(sync=True)
    # the tool bar button made for an action (`QToolBar.widgetForAction`,
    # docs/Sandbox.md 7.15): the host binds this model to the real one
    q_forAction = Unicode("", allow_none=True).tag(sync=True)
    q_defaultAction = Unicode("", allow_none=True).tag(sync=True)

    DelayedPopup = 0
    MenuButtonPopup = 1
    InstantPopup = 2

    def setAutoRaise(self, on):
        self._set(autoRaise=bool(on))

    def setToolButtonStyle(self, style):
        pass

    def setDefaultAction(self, action):
        self._default_action = action
        self._set(defaultAction=_ref(action))

    def defaultAction(self):
        return getattr(self, "_default_action", None)

    def setPopupMode(self, mode):
        pass


class QCheckBox(QAbstractButton):
    _model_name = Unicode("QCheckBoxModel").tag(sync=True)
    qt_class = "QCheckBox"
    q_tristate = Bool(False).tag(sync=True)
    q_checkable = Bool(True).tag(sync=True)

    stateChanged = Signal(int)
    checkStateChanged = Signal(int)

    def setTristate(self, on=True):
        self._set(tristate=bool(on))

    def checkState(self):
        return qtdata.Qt.Checked if self.q_checked else qtdata.Qt.Unchecked

    def setCheckState(self, state):
        self.setChecked(int(state) == qtdata.Qt.Checked)

    @observe("q_checked")
    def _fcx_state(self, change):
        state = qtdata.Qt.Checked if change["new"] else qtdata.Qt.Unchecked
        self.stateChanged.emit(state)
        self.checkStateChanged.emit(state)


class QRadioButton(QAbstractButton):
    _model_name = Unicode("QRadioButtonModel").tag(sync=True)
    qt_class = "QRadioButton"
    q_checkable = Bool(True).tag(sync=True)
    q_autoExclusive = Bool(True).tag(sync=True)


class QGroupBox(QWidget):
    _model_name = Unicode("QGroupBoxModel").tag(sync=True)
    qt_class = "QGroupBox"
    q_title = Unicode("").tag(sync=True)
    q_checkable = Bool(False).tag(sync=True)
    q_checked = Bool(True).tag(sync=True)
    q_flat = Bool(False).tag(sync=True)

    toggled = Signal(bool)
    clicked = Signal(bool)

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], str):
            kwargs["q_title"] = args.pop(0)
        QWidget._fcx_init_args(self, args, kwargs)

    def setTitle(self, text):
        self._set(title=str(text))

    def title(self):
        return self.q_title

    def setCheckable(self, on):
        self._set(checkable=bool(on))

    def isCheckable(self):
        return self.q_checkable

    def setChecked(self, on):
        self._set(checked=bool(on))

    def isChecked(self):
        return self.q_checked

    def setFlat(self, on):
        self._set(flat=bool(on))

    @observe("q_checked")
    def _fcx_checked(self, change):
        self.toggled.emit(bool(change["new"]))


class QFrame(QWidget):
    _model_name = Unicode("QFrameModel").tag(sync=True)
    qt_class = "QFrame"
    NoFrame = 0
    Box = 1
    Panel = 2
    StyledPanel = 6
    HLine = 4
    VLine = 5
    Plain = 16
    Raised = 32
    Sunken = 48

    def setFrameShape(self, shape):
        pass

    def setFrameShadow(self, shadow):
        pass

    def setFrameStyle(self, style):
        pass

    def setLineWidth(self, w):
        pass


# -- text inputs -------------------------------------------------------


class QLineEdit(QWidget):
    _model_name = Unicode("QLineEditModel").tag(sync=True)
    qt_class = "QLineEdit"
    q_text = Unicode("").tag(sync=True)
    q_placeholderText = Unicode("").tag(sync=True)
    q_readOnly = Bool(False).tag(sync=True)
    q_maxLength = Int(32767).tag(sync=True)
    q_echoMode = Int(0).tag(sync=True)
    q_clearButtonEnabled = Bool(False).tag(sync=True)

    Normal = 0
    NoEcho = 1
    Password = 2
    LeadingPosition = 0
    TrailingPosition = 1

    textChanged = Signal(str)
    textEdited = Signal(str)
    returnPressed = Signal()
    editingFinished = Signal()
    selectionChanged = Signal()

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], str):
            kwargs["q_text"] = args.pop(0)
        QWidget._fcx_init_args(self, args, kwargs)

    def setText(self, text):
        self._set(text=str(text))

    def text(self):
        return self.q_text

    def clear(self):
        self._set(text="")

    def setPlaceholderText(self, text):
        self._set(placeholderText=str(text))

    def placeholderText(self):
        return self.q_placeholderText

    def setReadOnly(self, on):
        self._set(readOnly=bool(on))

    def isReadOnly(self):
        return self.q_readOnly

    def setMaxLength(self, n):
        self._set(maxLength=int(n))

    def setEchoMode(self, mode):
        self._set(echoMode=int(mode))

    def setClearButtonEnabled(self, on):
        self._set(clearButtonEnabled=bool(on))

    def selectAll(self):
        self._event("selectAll")

    def setSelection(self, start, length):
        self._event("setSelection", int(start), int(length))

    def setCursorPosition(self, pos):
        self._event("setCursorPosition", int(pos))

    def setValidator(self, validator):
        pass

    def setCompleter(self, completer):
        pass

    def setAlignment(self, a):
        pass

    def setFrame(self, on):
        pass

    def setInputMask(self, mask):
        pass

    def hasAcceptableInput(self):
        return True

    def __init__(self, *args, **kwargs):
        QWidget.__init__(self, *args, **kwargs)
        # a text input's focus is state the corpus reads (`hasFocus()`)
        self._watch(qtdata.QEvent.FocusIn, qtdata.QEvent.FocusOut)

    @observe("q_text")
    def _fcx_text(self, change):
        self.textChanged.emit(change["new"])


class QTextEdit(QWidget):
    _model_name = Unicode("QTextEditModel").tag(sync=True)
    qt_class = "QTextEdit"
    q_plainText = Unicode("").tag(sync=True)
    q_html = Unicode("").tag(sync=True)
    q_readOnly = Bool(False).tag(sync=True)
    q_placeholderText = Unicode("").tag(sync=True)

    textChanged = Signal()

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], str):
            kwargs["q_plainText"] = args.pop(0)
        QWidget._fcx_init_args(self, args, kwargs)

    def setPlainText(self, text):
        self._set(plainText=str(text), html="")

    def setText(self, text):
        self.setPlainText(text)

    def setHtml(self, html):
        self._set(html=str(html))

    def toPlainText(self):
        return self.q_plainText

    def toHtml(self):
        return self.q_html

    def insertPlainText(self, text):
        self._set(plainText=self.q_plainText + str(text), html="")

    def append(self, text):
        sep = "\n" if self.q_plainText else ""
        self._set(plainText=self.q_plainText + sep + str(text), html="")

    def clear(self):
        self._set(plainText="", html="")

    def setReadOnly(self, on):
        self._set(readOnly=bool(on))

    def setPlaceholderText(self, text):
        self._set(placeholderText=str(text))

    def setTabChangesFocus(self, on):
        pass

    def setAcceptRichText(self, on):
        pass

    def setLineWrapMode(self, mode):
        pass

    def selectAll(self):
        self._event("selectAll")

    def moveCursor(self, *args):
        pass

    def document(self):
        raise AttributeError("QTextEdit.document: the rich text document is not in the"
                             " sandbox's subset")

    @observe("q_plainText", "q_html")
    def _fcx_text(self, change):
        self.textChanged.emit()


class QPlainTextEdit(QTextEdit):
    _model_name = Unicode("QPlainTextEditModel").tag(sync=True)
    qt_class = "QPlainTextEdit"


class QTextBrowser(QTextEdit):
    _model_name = Unicode("QTextBrowserModel").tag(sync=True)
    qt_class = "QTextBrowser"
    q_openExternalLinks = Bool(False).tag(sync=True)

    anchorClicked = Signal(object)

    def setOpenExternalLinks(self, on):
        self._set(openExternalLinks=bool(on))

    def setOpenLinks(self, on):
        pass

    def setSource(self, url):
        pass


# -- numbers ------------------------------------------------------------------


class QAbstractSpinBox(QWidget):
    q_minimum = Float(0).tag(sync=True)
    q_maximum = Float(99).tag(sync=True)
    q_singleStep = Float(1).tag(sync=True)
    q_prefix = Unicode("").tag(sync=True)
    q_suffix = Unicode("").tag(sync=True)
    q_readOnly = Bool(False).tag(sync=True)

    editingFinished = Signal()

    def setMinimum(self, v):
        self._set(minimum=self._num(v))
        self._clamp()

    def minimum(self):
        return self.q_minimum

    def setMaximum(self, v):
        self._set(maximum=self._num(v))
        self._clamp()

    def maximum(self):
        return self.q_maximum

    def setRange(self, lo, hi):
        self._set(minimum=self._num(lo), maximum=self._num(hi))
        self._clamp()

    def setSingleStep(self, v):
        self._set(singleStep=self._num(v))

    def singleStep(self):
        return self.q_singleStep

    def setPrefix(self, s):
        self._set(prefix=str(s))

    def setSuffix(self, s):
        self._set(suffix=str(s))

    def setReadOnly(self, on):
        self._set(readOnly=bool(on))

    def setAlignment(self, a):
        pass

    def setKeyboardTracking(self, on):
        pass

    def setButtonSymbols(self, s):
        pass

    def setSpecialValueText(self, s):
        pass

    def selectAll(self):
        self._event("selectAll")

    def value(self):
        return self.q_value

    def _num(self, v):
        return v

    def _clamp(self):
        v = min(max(self.q_value, self.q_minimum), self.q_maximum)
        if v != self.q_value:
            self._set(value=self._num(v))


class QSpinBox(QAbstractSpinBox):
    _model_name = Unicode("QSpinBoxModel").tag(sync=True)
    qt_class = "QSpinBox"
    q_value = Int(0).tag(sync=True)
    q_minimum = Int(0).tag(sync=True)
    q_maximum = Int(99).tag(sync=True)
    q_singleStep = Int(1).tag(sync=True)

    valueChanged = Signal(int)

    def _num(self, v):
        return int(v)

    def setValue(self, v):
        v = int(v)
        self._set(value=min(max(v, self.q_minimum), self.q_maximum))

    @observe("q_value")
    def _fcx_value(self, change):
        self.valueChanged.emit(change["new"])


class QDoubleSpinBox(QAbstractSpinBox):
    _model_name = Unicode("QDoubleSpinBoxModel").tag(sync=True)
    qt_class = "QDoubleSpinBox"
    q_value = Float(0.0).tag(sync=True)
    q_decimals = Int(2).tag(sync=True)

    valueChanged = Signal(float)

    def _num(self, v):
        return float(v)

    def setValue(self, v):
        v = float(v)
        self._set(value=min(max(v, self.q_minimum), self.q_maximum))

    def setDecimals(self, n):
        self._set(decimals=int(n))

    def decimals(self):
        return self.q_decimals

    @observe("q_value")
    def _fcx_value(self, change):
        self.valueChanged.emit(change["new"])


class QSlider(QWidget):
    _model_name = Unicode("QSliderModel").tag(sync=True)
    qt_class = "QSlider"
    q_value = Int(0).tag(sync=True)
    q_minimum = Int(0).tag(sync=True)
    q_maximum = Int(99).tag(sync=True)
    q_singleStep = Int(1).tag(sync=True)
    q_orientation = Int(1).tag(sync=True)

    valueChanged = Signal(int)
    sliderReleased = Signal()

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], int):
            kwargs["q_orientation"] = args.pop(0)
        QWidget._fcx_init_args(self, args, kwargs)

    def setValue(self, v):
        self._set(value=min(max(int(v), self.q_minimum), self.q_maximum))

    def value(self):
        return self.q_value

    def setMinimum(self, v):
        self._set(minimum=int(v))

    def setMaximum(self, v):
        self._set(maximum=int(v))

    def setRange(self, lo, hi):
        self._set(minimum=int(lo), maximum=int(hi))

    def setSingleStep(self, v):
        self._set(singleStep=int(v))

    def setOrientation(self, o):
        self._set(orientation=int(o))

    def setTickPosition(self, p):
        pass

    def setTickInterval(self, i):
        pass

    @observe("q_value")
    def _fcx_value(self, change):
        self.valueChanged.emit(change["new"])


class QProgressBar(QWidget):
    _model_name = Unicode("QProgressBarModel").tag(sync=True)
    qt_class = "QProgressBar"
    q_value = Int(0).tag(sync=True)
    q_minimum = Int(0).tag(sync=True)
    q_maximum = Int(100).tag(sync=True)
    q_format = Unicode("%p%").tag(sync=True)

    def setValue(self, v):
        self._set(value=int(v))

    def value(self):
        return self.q_value

    def setMinimum(self, v):
        self._set(minimum=int(v))

    def setMaximum(self, v):
        self._set(maximum=int(v))

    def setRange(self, lo, hi):
        self._set(minimum=int(lo), maximum=int(hi))

    def setFormat(self, f):
        self._set(format=str(f))

    def reset(self):
        self._set(value=self.q_minimum)


# -- choices -------------------------------------------------------------


class QComboBox(QWidget):
    _model_name = Unicode("QComboBoxModel").tag(sync=True)
    qt_class = "QComboBox"
    q_items = List(Unicode()).tag(sync=True)
    q_itemIcons = List(Unicode()).tag(sync=True)
    q_currentIndex = Int(-1).tag(sync=True)
    q_editable = Bool(False).tag(sync=True)
    q_editText = Unicode("").tag(sync=True)

    AdjustToContents = 0
    AdjustToContentsOnFirstShow = 1
    AdjustToMinimumContentsLengthWithIcon = 3
    NoInsert = 0
    InsertAtTop = 1
    InsertAtBottom = 3

    currentIndexChanged = Signal(int)
    currentTextChanged = Signal(str)
    activated = Signal(int)
    editTextChanged = Signal(str)

    def _fcx_init_args(self, args, kwargs):
        QWidget._fcx_init_args(self, args, kwargs)
        self._item_data = []

    def _fcx_set_items(self, items, icons, data, current):
        self._item_data = list(data)
        if current >= len(items):
            current = len(items) - 1
        if current < 0 and items:
            current = 0
        self._set(items=list(items), itemIcons=list(icons), currentIndex=current)

    def _fcx_item_args(self, args):
        icon = ""
        text = ""
        data = None
        rest = list(args)
        if rest and isinstance(rest[0], qtdata.QIcon):
            icon = rest.pop(0).path
        if rest:
            text = str(rest.pop(0))
        if rest:
            data = rest.pop(0)
        return icon, text, data

    def addItem(self, *args):
        icon, text, data = self._fcx_item_args(args)
        self._fcx_set_items(self.q_items + [text], self.q_itemIcons + [icon],
                            self._item_data + [data], self.q_currentIndex)

    def addItems(self, texts):
        texts = [str(t) for t in texts]
        self._fcx_set_items(self.q_items + texts, self.q_itemIcons + [""] * len(texts),
                            self._item_data + [None] * len(texts), self.q_currentIndex)

    def insertItem(self, index, *args):
        icon, text, data = self._fcx_item_args(args)
        items, icons, datas = list(self.q_items), list(self.q_itemIcons), list(self._item_data)
        items.insert(index, text)
        icons.insert(index, icon)
        datas.insert(index, data)
        current = self.q_currentIndex
        if current >= index:
            current += 1
        self._fcx_set_items(items, icons, datas, current)

    def removeItem(self, index):
        if 0 <= index < len(self.q_items):
            items, icons, datas = list(self.q_items), list(self.q_itemIcons), list(self._item_data)
            del items[index], icons[index], datas[index]
            current = self.q_currentIndex
            if current > index or current >= len(items):
                current -= 1
            self._fcx_set_items(items, icons, datas, current)

    def clear(self):
        self._item_data = []
        self._set(items=[], itemIcons=[], currentIndex=-1)

    def count(self):
        return len(self.q_items)

    def itemText(self, index):
        return self.q_items[index] if 0 <= index < len(self.q_items) else ""

    def setItemText(self, index, text):
        items = list(self.q_items)
        items[index] = str(text)
        self._set(items=items)

    def itemData(self, index, role=None):
        return self._item_data[index] if 0 <= index < len(self._item_data) else None

    def setItemData(self, index, value, role=None):
        self._item_data[index] = value

    def setItemIcon(self, index, icon):
        icons = list(self.q_itemIcons)
        icons[index] = _icon_path(icon)
        self._set(itemIcons=icons)

    def currentIndex(self):
        return self.q_currentIndex

    def currentText(self):
        if self.q_editable and self.q_editText:
            return self.q_editText
        return self.itemText(self.q_currentIndex)

    def currentData(self, role=None):
        return self.itemData(self.q_currentIndex)

    def setCurrentIndex(self, index):
        index = int(index)
        if index >= len(self.q_items):
            index = -1
        self._set(currentIndex=index)

    def setCurrentText(self, text):
        i = self.findText(text)
        if i >= 0:
            self.setCurrentIndex(i)
        elif self.q_editable:
            self._set(editText=str(text))

    def setEditText(self, text):
        self._set(editText=str(text))

    def findText(self, text, flags=None):
        try:
            return self.q_items.index(str(text))
        except ValueError:
            return -1

    def findData(self, data, role=None):
        try:
            return self._item_data.index(data)
        except ValueError:
            return -1

    def setEditable(self, on):
        self._set(editable=bool(on))

    def isEditable(self):
        return self.q_editable

    def setSizeAdjustPolicy(self, p):
        pass

    def setInsertPolicy(self, p):
        pass

    def setMaxVisibleItems(self, n):
        pass

    def setIconSize(self, s):
        pass

    def setMinimumContentsLength(self, n):
        pass

    def setCompleter(self, c):
        pass

    def lineEdit(self):
        raise AttributeError("QComboBox.lineEdit is not in the sandbox's subset")

    def view(self):
        raise AttributeError("QComboBox.view is not in the sandbox's subset")

    def showPopup(self):
        pass

    @observe("q_currentIndex")
    def _fcx_index(self, change):
        self.currentIndexChanged.emit(change["new"])
        self.currentTextChanged.emit(self.currentText())

    @observe("q_editText")
    def _fcx_edit(self, change):
        self.editTextChanged.emit(change["new"])


class QFontComboBox(QComboBox):
    _model_name = Unicode("QFontComboBoxModel").tag(sync=True)
    qt_class = "QFontComboBox"

    currentFontChanged = Signal(object)

    def currentFont(self):
        return qtdata.QFont(self.currentText())

    def setCurrentFont(self, font):
        self.setCurrentText(font.family or "")


# -- FreeCAD's own -------------------------------------------------------------


class InputField(QLineEdit):
    """`Gui::InputField`: a quantity input.  `rawValue` is the value in
    internal units (mm, deg), `text` the string as shown; the host
    formats the text from the value in the user's unit schema."""

    _model_name = Unicode("InputFieldModel").tag(sync=True)
    qt_class = "Gui::InputField"
    q_rawValue = Float(0.0).tag(sync=True)
    q_unit = Unicode("").tag(sync=True)
    q_minimum = Float(-1e12).tag(sync=True)
    q_maximum = Float(1e12).tag(sync=True)
    q_singleStep = Float(1.0).tag(sync=True)
    q_decimals = Int(2).tag(sync=True)
    q_precision = Int(2).tag(sync=True)
    q_historySize = Int(5).tag(sync=True)
    q_format = Unicode("g").tag(sync=True)
    q_quantityString = Unicode("").tag(sync=True)

    valueChanged = Signal(object)
    parseError = Signal(str)

    def setValue(self, value):
        v = _raw(value)
        self._set(rawValue=v, text=self._fcx_format(v))

    def setRawValue(self, value):
        self.setValue(value)

    def rawValue(self):
        return self.q_rawValue

    def setProperty(self, name, value):
        # the corpus writes the value through the dynamic property
        if name == "rawValue":
            self.setValue(value)
            return True
        return QLineEdit.setProperty(self, name, value)

    def _fcx_format(self, value):
        """The text for a value the guest set: the unit schema's string
        at the user's decimals (the host reformats its own widget; a
        host edit sends both)."""
        try:
            import FreeCAD

            if self.q_unit:
                q = FreeCAD.Units.Quantity(value, self.q_unit)
            else:
                q = FreeCAD.Units.Quantity(value)
            try:
                q.Format = {"NumberFormat": "f", "Precision": _decimals(), "Denominator": 8}
            except Exception:
                pass
            return q.UserString
        except Exception:
            return ("%g %s" % (value, self.q_unit)).strip()

    def setUnitText(self, unit):
        self._set(unit=str(unit))

    def getUnitText(self):
        return self.q_unit

    def setMinimum(self, v):
        self._set(minimum=float(v))

    def setMaximum(self, v):
        self._set(maximum=float(v))

    def setRange(self, lo, hi):
        self._set(minimum=float(lo), maximum=float(hi))

    def setSingleStep(self, v):
        self._set(singleStep=float(v))

    def setDecimals(self, n):
        self._set(decimals=int(n))

    def setPrecision(self, n):
        self._set(precision=int(n))

    def setHistorySize(self, n):
        self._set(historySize=int(n))

    def setFormat(self, f):
        self._set(format=str(f))

    def setQuantityString(self, s):
        self._set(quantityString=str(s))

    def setParamGrpPath(self, path):
        self._set(prefPath=str(path))

    def getQuantity(self):
        import FreeCAD

        if self.q_unit:
            return FreeCAD.Units.Quantity(self.q_rawValue, self.q_unit)
        return FreeCAD.Units.Quantity(self.q_rawValue)

    def pushToHistory(self, *args):
        pass

    def setToLastUsedValue(self):
        pass

    @observe("q_rawValue")
    def _fcx_raw(self, change):
        # `valueChanged(const Base::Quantity&)` is the overload PySide
        # connects (DraftGui reads `d.Value`); the double is the other
        self.valueChanged.emit(self._fcx_quantity(change["new"]))

    def _fcx_quantity(self, value):
        try:
            import FreeCAD

            if self.q_unit:
                return FreeCAD.Units.Quantity(value, self.q_unit)
            return FreeCAD.Units.Quantity(value)
        except Exception:
            return value


_DECIMALS = []


def _decimals():
    """The user's Units/Decimals preference, read once (prefs.read)."""
    if not _DECIMALS:
        try:
            import FreeCAD

            grp = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Units")
            _DECIMALS.append(int(grp.GetInt("Decimals", 2)))
        except Exception:
            _DECIMALS.append(2)
    return _DECIMALS[0]


def _raw(value):
    """A number, or a Quantity's value in internal units."""
    try:
        return float(value)
    except TypeError:
        return float(getattr(value, "Value"))


class QuantitySpinBox(InputField):
    _model_name = Unicode("QuantitySpinBoxModel").tag(sync=True)
    qt_class = "Gui::QuantitySpinBox"
    q_displayUnit = Unicode("").tag(sync=True)

    def setDisplayUnit(self, unit):
        self._set(displayUnit=str(unit))

    def value(self):
        return self.getQuantity()


class ColorButton(QPushButton):
    """`Gui::ColorButton`: a color as four floats, `changed` when picked."""

    _model_name = Unicode("ColorButtonModel").tag(sync=True)
    qt_class = "Gui::ColorButton"
    q_color = List(Float(), default_value=[0.0, 0.0, 0.0, 1.0]).tag(sync=True)
    q_allowTransparency = Bool(False).tag(sync=True)
    q_allowChangeColor = Bool(True).tag(sync=True)
    q_drawFrame = Bool(True).tag(sync=True)

    changed = Signal()

    def setColor(self, color):
        if not isinstance(color, qtdata.QColor):
            color = qtdata.QColor(color)
        self._set(color=list(color.getRgbF()))

    def color(self):
        return qtdata.QColor(list(self.q_color))

    def setAllowTransparency(self, on):
        self._set(allowTransparency=bool(on))

    def setAllowChangeColor(self, on):
        self._set(allowChangeColor=bool(on))

    def setDrawFrame(self, on):
        self._set(drawFrame=bool(on))

    @observe("q_color")
    def _fcx_color(self, change):
        self.changed.emit()


# -- the form loaded from a .ui file ----------------------------------------



# -- dialogs (G3b) ------------------------------------------------------------


# -- actions, tool bars, menus (G3c, docs/Sandbox.md 7.11) ---------------


class QAction(QWidget):
    """A `QAction`: not a widget, but a model like one (text, icon,
    checkable, checked, enabled, visible, tool tip, shortcut); the host
    makes the real action on whatever carries it -- a widget's
    `addAction`, a tool bar, a menu."""

    _model_name = Unicode("QActionModel").tag(sync=True)
    qt_class = "QAction"
    q_text = Unicode("").tag(sync=True)
    q_icon = Unicode("").tag(sync=True)
    q_checkable = Bool(False).tag(sync=True)
    q_checked = Bool(False).tag(sync=True)
    q_shortcut = Unicode("").tag(sync=True)
    q_separator = Bool(False).tag(sync=True)
    # a host command's own action (docs/Sandbox.md 7.15): the host binds
    # the model to `Command::getAction()` (the group member `commandIndex`,
    # 0 the group's own) instead of making an action
    q_command = Unicode("").tag(sync=True)
    q_commandIndex = Int(0).tag(sync=True)
    # a group as the tool bar mirror streams it (docs/Sandbox.md 7.18):
    # the members (refs, in order), the one the button shows, exclusive,
    # the drop-down face, a member's own command; the guest never sets
    # them (its QActionGroup stays guest-only)
    q_members = List().tag(sync=True)
    q_defaultAction = Int(-1).tag(sync=True)
    q_exclusive = Bool(False).tag(sync=True)
    q_dropDown = Bool(False).tag(sync=True)
    q_memberCommand = Unicode("").tag(sync=True)

    triggered = Signal(bool)
    toggled = Signal(bool)
    hovered = Signal()
    changed = Signal()

    def _fcx_init_args(self, args, kwargs):
        self._group = None
        for a in list(args):
            if isinstance(a, str):
                kwargs["q_text"] = a
                args.remove(a)
            elif isinstance(a, (qtdata.QIcon, qtdata.QPixmap)):
                kwargs["q_icon"] = _icon_path(a)
                args.remove(a)
            elif isinstance(a, QActionGroup):
                # QAction(group): the group is the parent and the action
                # joins it, as Qt has it
                self._group = a
                args.remove(a)
        self._data_value = None
        QWidget._fcx_init_args(self, args, kwargs)

    def __init__(self, *args, **kwargs):
        QWidget.__init__(self, *args, **kwargs)
        if self._group is not None:
            self._group.addAction(self)

    def parent(self):
        return self._group if self._group is not None else QWidget.parent(self)

    def actionGroup(self):
        return self._group

    def setActionGroup(self, group):
        if group is not None:
            group.addAction(self)
        elif self._group is not None:
            self._group.removeAction(self)

    @classmethod
    def _fcx_host(cls, command, index=0):
        """A host command's action, by name (`Command.getAction()`)."""
        a = cls()
        a._set(command=str(command), commandIndex=int(index))
        return a

    def setText(self, text):
        self._set(text=str(text))

    def text(self):
        return self.q_text

    def setIcon(self, icon):
        self._set(icon=_icon_path(icon))

    def icon(self):
        return qtdata.QIcon(self.q_icon)

    def setCheckable(self, on):
        self._set(checkable=bool(on))

    def isCheckable(self):
        return self.q_checkable

    def setChecked(self, on):
        self._set(checked=bool(on))

    def isChecked(self):
        return self.q_checked

    def toggle(self):
        self.setChecked(not self.q_checked)

    def setShortcut(self, keys):
        self._set(shortcut=str(getattr(keys, "toString", lambda: keys)()))

    def shortcut(self):
        return self.q_shortcut

    def setSeparator(self, on):
        self._set(separator=bool(on))

    def isSeparator(self):
        return self.q_separator

    def setData(self, value):
        self._data_value = value

    def data(self):
        return self._data_value

    def setMenu(self, menu):
        self._menu = menu

    def menu(self):
        return getattr(self, "_menu", None)

    def trigger(self):
        """A programmatic trigger: toggles a checkable, fires
        `triggered` here and asks the host to trigger the real one."""
        if self.q_checkable:
            self.setChecked(not self.q_checked)
        self.triggered.emit(self.q_checked)

    def activate(self, *args):
        self._event("trigger")

    def setIconText(self, text):
        pass

    def setPriority(self, p):
        pass

    def setMenuRole(self, role):
        pass

    @observe("q_checked")
    def _fcx_checked(self, change):
        self.toggled.emit(bool(change["new"]))


class QActionGroup(QObject):
    """A `QActionGroup`, guest-only (docs/Sandbox.md 7.15): membership,
    exclusivity and the aggregated `triggered(action)` -- each member
    action crosses on its own, the group never does."""

    triggered = Signal(object)
    hovered = Signal(object)

    def __init__(self, parent=None):
        QObject.__init__(self, parent)
        self._actions = []
        self._exclusive = True

    def addAction(self, *args):
        action = _make_action(args, None)
        if action not in self._actions:
            self._actions.append(action)
            action._group = self
            action.triggered.connect(lambda checked=False, a=action: self._on_triggered(a))
        return action

    def removeAction(self, action):
        if action in self._actions:
            self._actions.remove(action)
            action._group = None

    def actions(self):
        return list(self._actions)

    def _on_triggered(self, action):
        if self._exclusive and action.isCheckable() and action.isChecked():
            for other in self._actions:
                if other is not action and other.isChecked():
                    other.setChecked(False)
        self.triggered.emit(action)

    def setExclusive(self, on):
        self._exclusive = bool(on)

    def isExclusive(self):
        return self._exclusive

    def setExclusionPolicy(self, policy):
        self._exclusive = int(policy) != 0

    def checkedAction(self):
        for a in self._actions:
            if a.isChecked():
                return a
        return None

    def setEnabled(self, on):
        for a in self._actions:
            a.setEnabled(on)

    def isEnabled(self):
        return all(a.isEnabled() for a in self._actions)

    def setVisible(self, on):
        for a in self._actions:
            a.setVisible(on)

    def isVisible(self):
        return any(a.isVisible() for a in self._actions)


def _make_action(args, parent):
    """The action `addAction(...)` names: an action, or one made from
    `(text)`, `(icon, text)`, with an optional callable to connect."""
    action = None
    icon = None
    text = None
    slot = None
    for a in args:
        if isinstance(a, QAction):
            action = a
        elif isinstance(a, (qtdata.QIcon, qtdata.QPixmap)):
            icon = a
        elif isinstance(a, str):
            text = a
        elif callable(a):
            slot = a
    if action is None:
        action = QAction(text or "", parent)
        if icon is not None:
            action.setIcon(icon)
    if slot is not None:
        action.triggered.connect(slot)
    return action


class QToolBar(QWidget):
    """A tool bar: its content is a bar (widgets, actions, separators in
    order); `getMainWindow().addToolBar(bar)` realizes it on the host."""

    _model_name = Unicode("QToolBarModel").tag(sync=True)
    qt_class = "QToolBar"
    q_iconSize = Int(0).tag(sync=True)
    q_toolButtonStyle = Int(0).tag(sync=True)
    q_movable = Bool(True).tag(sync=True)
    q_floatable = Bool(True).tag(sync=True)
    q_orientation = Int(1).tag(sync=True)
    q_toggleViewAction = Unicode("", allow_none=True).tag(sync=True)
    # where the desktop shows a mirrored bar (docs/Sandbox.md 7.18); ""
    # for a bar of the guest's own
    q_area = Unicode("").tag(sync=True)

    actionTriggered = Signal(object)
    visibilityChanged = Signal(bool)

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], str):
            kwargs["q_windowTitle"] = args.pop(0)
        self._toggle = None
        QWidget._fcx_init_args(self, args, kwargs)

    def __init__(self, *args, **kwargs):
        QWidget.__init__(self, *args, **kwargs)
        _Bar(self)

    def addWidget(self, widget):
        self._layout._add_widget(widget, ())
        return None

    def insertWidget(self, before, widget):
        i = self._layout.indexOf(before) if not isinstance(before, int) else before
        self._layout._add_widget(widget, (), index=i if i >= 0 else None)
        return None

    def removeWidget(self, widget):
        self._layout.removeWidget(widget)

    def addAction(self, *args):
        action = _make_action(args, self)
        self._layout._add_action(action)
        return action

    def insertAction(self, before, action):
        i = -1
        for k, item in enumerate(self._layout._items):
            if isinstance(item, _ActionItem) and item._action is before:
                i = k
        self._layout._add_action(action, index=i if i >= 0 else None)

    def removeAction(self, action):
        self._layout._remove_action(action)

    def addSeparator(self):
        self._layout._add_separator()
        return None

    def clear(self):
        self._layout._clear()

    def actions(self):
        return self._layout._actions()

    def toggleViewAction(self):
        """The action that shows and hides the bar (the host binds it to
        the real bar's own)."""
        if self._toggle is None:
            self._toggle = QAction(self.q_windowTitle)
            self._toggle.setCheckable(True)
            self._set(toggleViewAction=_ref(self._toggle))
        return self._toggle

    def setIconSize(self, size):
        self._set(iconSize=int(size.width() if hasattr(size, "width") else size))

    def setToolButtonStyle(self, style):
        self._set(toolButtonStyle=int(style))

    def setMovable(self, on):
        self._set(movable=bool(on))

    def setFloatable(self, on):
        self._set(floatable=bool(on))

    def setOrientation(self, o):
        self._set(orientation=int(o))

    def setAllowedAreas(self, areas):
        pass

    def widgetForAction(self, action):
        """The button the real bar made for `action` (docs/Sandbox.md
        7.15): a QToolButton model the host binds to it, one per action."""
        if not isinstance(action, QAction):
            return None
        buttons = self.__dict__.setdefault("_action_buttons", {})
        b = buttons.get(action.model_id)
        if b is None:
            b = QToolButton(self)
            # the host binds once it knows the bar (a constructor parent
            # does not cross by itself) and the action
            b._event("setParent", self.model_id)
            b._set(forAction=_ref(action))
            buttons[action.model_id] = b
        return b

    def children(self):
        """The bar's content in order, an action as the button the bar
        made for it (Draft reads `children()[-1]` after `addAction`)."""
        out = []
        for item in self._layout._items:
            if isinstance(item, _ActionItem):
                out.append(self.widgetForAction(item._action))
            elif getattr(item, "_widget", None) is not None:
                out.append(item._widget)
        return out

    @observe("q_visible")
    def _fcx_visible(self, change):
        self.visibilityChanged.emit(bool(change["new"]))


class QMenu(QWidget):
    """A menu: its content is a bar (actions, separators, sub-menus);
    `exec_()` is one synchronous op (the host pops the real menu at the
    cursor and runs a nested loop), the chosen action comes back as
    `triggered` and as the result."""

    _model_name = Unicode("QMenuModel").tag(sync=True)
    qt_class = "QMenu"
    q_title = Unicode("").tag(sync=True)
    q_icon = Unicode("").tag(sync=True)
    q_tearOffEnabled = Bool(False).tag(sync=True)

    triggered = Signal(object)
    aboutToShow = Signal()
    aboutToHide = Signal()

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], str):
            kwargs["q_title"] = args.pop(0)
        QWidget._fcx_init_args(self, args, kwargs)

    def __init__(self, *args, **kwargs):
        QWidget.__init__(self, *args, **kwargs)
        _Bar(self)

    def addAction(self, *args):
        action = _make_action(args, self)
        self._layout._add_action(action)
        return action

    def insertAction(self, before, action):
        self._layout._add_action(action)

    def removeAction(self, action):
        self._layout._remove_action(action)

    def addSeparator(self):
        self._layout._add_separator()
        return None

    def addMenu(self, *args):
        menu = args[0] if args and isinstance(args[0], QMenu) else QMenu(*args)
        self._layout._add_widget(menu, ())
        return menu

    def clear(self):
        self._layout._clear()

    def actions(self):
        return self._layout._actions()

    def setTitle(self, title):
        self._set(title=str(title))

    def title(self):
        return self.q_title

    def setIcon(self, icon):
        self._set(icon=_icon_path(icon))

    def setTearOffEnabled(self, on):
        self._set(tearOffEnabled=bool(on))

    def isEmpty(self):
        return not self._layout._items

    def exec_(self, pos=None, *args):
        import _fcx

        chosen = _fcx.op("gui.menu.exec", 0, self.model_id)
        return _REGISTRY.get(chosen) if chosen else None

    exec = exec_

    def popup(self, pos=None, *args):
        self.exec_(pos)

    def menuAction(self):
        return None


class _CloseEvent:
    """What a dock's `closeEvent(event)` receives."""

    def __init__(self):
        self._accepted = True

    def accept(self):
        self._accepted = True

    def ignore(self):
        self._accepted = False

    def isAccepted(self):
        return self._accepted

    def type(self):
        return qtdata.QEvent.Close


class QDockWidget(QWidget):
    """A dock widget (docs/Sandbox.md 7.15): `setWidget(form)` names the
    content, `getMainWindow().addDockWidget(area, dock)` makes the real
    dock through the host's dock manager (a first-class panel).  The
    host writes back `area`, `floating`, `visible` and the geometry; a
    close reaches `closeEvent` (an instance attribute override runs, as
    BimViews assigns one)."""

    _model_name = Unicode("QDockWidgetModel").tag(sync=True)
    qt_class = "QDockWidget"
    q_widget = Unicode("", allow_none=True).tag(sync=True)
    q_floating = Bool(False).tag(sync=True)
    q_area = Int(0).tag(sync=True)
    q_x = Int(0).tag(sync=True)
    q_y = Int(0).tag(sync=True)
    q_width = Int(0).tag(sync=True)
    q_height = Int(0).tag(sync=True)
    q_toggleViewAction = Unicode("", allow_none=True).tag(sync=True)

    DockWidgetClosable = 1
    DockWidgetMovable = 2
    DockWidgetFloatable = 4
    AllDockWidgetFeatures = 7
    NoDockWidgetFeatures = 0

    dockLocationChanged = Signal(int)
    visibilityChanged = Signal(bool)
    topLevelChanged = Signal(bool)
    featuresChanged = Signal(int)

    def _fcx_init_args(self, args, kwargs):
        if args and isinstance(args[0], str):
            kwargs["q_windowTitle"] = args.pop(0)
        args[:] = [a for a in args if not isinstance(a, int)]
        self._content = None
        self._toggle = None
        QWidget._fcx_init_args(self, args, kwargs)

    def _handle_custom_msg(self, content, buffers):
        if isinstance(content, dict) and content.get("event") == "close":
            self.closeEvent(_CloseEvent())
            return
        QWidget._handle_custom_msg(self, content, buffers)

    def closeEvent(self, event):
        event.accept()

    def setWidget(self, widget):
        if widget is not None:
            widget._attach(self)
        self._content = widget
        self._set(widget=_ref(widget))

    def widget(self):
        return self._content

    def setFloating(self, on):
        self._set(floating=bool(on))

    def isFloating(self):
        return self.q_floating

    def setGeometry(self, *args):
        if len(args) == 1:
            r = args[0]
            args = (r.x(), r.y(), r.width(), r.height())
        x, y, w, h = (int(v) for v in args)
        self._set(x=x, y=y, width=w, height=h)
        self._event("setGeometry", x, y, w, h)

    def move(self, *args):
        pt = args[0] if len(args) == 1 else qtdata.QPoint(*args)
        self._event("move", int(pt.x()), int(pt.y()))

    def resize(self, *args):
        sz = args[0] if len(args) == 1 else qtdata.QSize(*args)
        self._event("resize", int(sz.width()), int(sz.height()))

    def x(self):
        return self.q_x

    def y(self):
        return self.q_y

    def width(self):
        return self.q_width

    def height(self):
        return self.q_height

    def pos(self):
        return qtdata.QPoint(self.q_x, self.q_y)

    def size(self):
        return qtdata.QSize(self.q_width, self.q_height)

    def rect(self):
        return qtdata.QRect(0, 0, self.q_width, self.q_height)

    def geometry(self):
        return qtdata.QRect(self.q_x, self.q_y, self.q_width, self.q_height)

    frameGeometry = geometry

    def close(self):
        self._event("close")
        return True

    def toggleViewAction(self):
        if self._toggle is None:
            self._toggle = QAction(self.q_windowTitle)
            self._toggle.setCheckable(True)
            self._set(toggleViewAction=_ref(self._toggle))
        return self._toggle

    def setAllowedAreas(self, areas):
        pass

    def setFeatures(self, features):
        pass

    def features(self):
        return self.AllDockWidgetFeatures

    def setTitleBarWidget(self, widget):
        pass

    def raise_(self):
        self._event("raise")


class QDialog(QWidget):
    """A dialog: `exec_()` is one synchronous op (the host runs a nested
    event loop; the guest's slots run nested in it), `accept`/`reject`/
    `done` are requests, `accepted`/`rejected`/`finished` come back as
    events, `result` as state.  A `.ui` file whose root is a QDialog
    loads as a UiForm, which IS one of these."""

    _model_name = Unicode("QDialogModel").tag(sync=True)
    qt_class = "QDialog"
    q_modal = Bool(False).tag(sync=True)
    q_result = Int(0).tag(sync=True)
    q_width = Int(0).tag(sync=True)
    q_height = Int(0).tag(sync=True)

    Rejected = 0
    Accepted = 1

    accepted = Signal()
    rejected = Signal()
    finished = Signal(int)

    def _fcx_init_args(self, args, kwargs):
        # QDialog(parent, flags): the flags are not a subset concern
        args[:] = [a for a in args if not isinstance(a, int)]
        QWidget._fcx_init_args(self, args, kwargs)

    def exec_(self):
        import _fcx

        return int(_fcx.op("gui.dialog.exec", 0, self.model_id) or 0)

    exec = exec_

    # NOT `open`: ipywidgets' Widget.open() makes the comm, and Qt's
    # modeless QDialog.open() would shadow it (no form would have one)

    def accept(self):
        self._event("accept")

    def reject(self):
        self._event("reject")

    def done(self, r):
        self._event("done", int(r))

    def close(self):
        """Qt's: the window closes (rejected if it was open); the object
        lives on.  ipywidgets' close (the comm's) is `__del__`'s."""
        self._event("close")
        return True

    def __del__(self):
        try:
            ipywidgets.Widget.close(self)
        except Exception:
            pass

    def result(self):
        return self.q_result

    def setResult(self, r):
        self._set(result=int(r))

    def setModal(self, on):
        self._set(modal=bool(on))

    def isModal(self):
        return self.q_modal

    def setWindowModality(self, m):
        self._set(modal=int(m) != 0)

    def setWindowFlags(self, flags):
        pass

    def setWindowFlag(self, flag, on=True):
        pass

    def windowFlags(self):
        return 0

    def setSizeGripEnabled(self, on):
        pass

    def move(self, *args):
        pt = args[0] if len(args) == 1 else qtdata.QPoint(*args)
        self._event("move", int(pt.x()), int(pt.y()))

    def resize(self, *args):
        sz = args[0] if len(args) == 1 else qtdata.QSize(*args)
        self._event("resize", int(sz.width()), int(sz.height()))

    def raise_(self):
        self._event("raise")

    def activateWindow(self):
        self._event("activateWindow")

    def adjustSize(self):
        self._event("adjustSize")

    def width(self):
        return self.q_width

    def height(self):
        return self.q_height

    def rect(self):
        return qtdata.QRect(0, 0, self.q_width, self.q_height)

    def geometry(self):
        return self.rect()

    def frameGeometry(self):
        return self.rect()

    def size(self):
        return qtdata.QSize(self.q_width, self.q_height)

    def pos(self):
        return qtdata.QPoint(0, 0)


class _ButtonBoxButton:
    """`buttonBox.button(QDialogButtonBox.Ok)`: a handle on one of the
    box's buttons; each call a request the host applies to the real
    button."""

    def __init__(self, box, which):
        self._box = box
        self._which = int(which)

    def _call(self, method, *args):
        self._box._event("button", self._which, method, list(args))

    def setEnabled(self, on):
        self._call("setEnabled", bool(on))

    def setDisabled(self, on):
        self._call("setEnabled", not on)

    def setText(self, text):
        self._call("setText", str(text))

    def setDefault(self, on):
        self._call("setDefault", bool(on))

    def setAutoDefault(self, on):
        self._call("setAutoDefault", bool(on))

    def setFocus(self, *args):
        self._call("setFocus")

    def setIcon(self, icon):
        self._call("setIcon", _icon_path(icon))

    def setToolTip(self, text):
        self._call("setToolTip", str(text))

    def setVisible(self, on):
        self._call("setVisible", bool(on))

    def hide(self):
        self.setVisible(False)

    def show(self):
        self.setVisible(True)

    def click(self):
        self._call("click")

    def animateClick(self, *args):
        self._call("click")

    @property
    def clicked(self):
        return self._box._button_signal(self._which)


class QDialogButtonBox(QWidget):
    """The standard buttons of a dialog: which ones (Qt's flag values,
    the enum the guest's getStandardButtons already returns), their
    orientation; `accepted`/`rejected`/`clicked`/`helpRequested` from
    the host."""

    _model_name = Unicode("QDialogButtonBoxModel").tag(sync=True)
    qt_class = "QDialogButtonBox"
    q_standardButtons = Int(0).tag(sync=True)
    q_orientation = Int(1).tag(sync=True)
    q_centerButtons = Bool(False).tag(sync=True)

    AcceptRole = 0
    RejectRole = 1
    DestructiveRole = 2
    ActionRole = 3
    HelpRole = 4
    YesRole = 5
    NoRole = 6
    ResetRole = 7
    ApplyRole = 8

    accepted = Signal()
    rejected = Signal()
    helpRequested = Signal()
    clicked = Signal(object)

    def _fcx_init_args(self, args, kwargs):
        rest = []
        for a in args:
            if isinstance(a, int):
                if a in (1, 2):
                    kwargs["q_orientation"] = a
                else:
                    kwargs["q_standardButtons"] = a
            else:
                rest.append(a)
        args[:] = rest
        QWidget._fcx_init_args(self, args, kwargs)
        self._button_signals = {}

    def setStandardButtons(self, buttons):
        self._set(standardButtons=int(buttons))

    def standardButtons(self):
        return self.q_standardButtons

    def setOrientation(self, o):
        self._set(orientation=int(o))

    def orientation(self):
        return self.q_orientation

    def setCenterButtons(self, on):
        self._set(centerButtons=bool(on))

    def button(self, which):
        return _ButtonBoxButton(self, which)

    def _button_signal(self, which):
        s = self._button_signals.get(which)
        if s is None:
            s = self._button_signals[which] = BoundSignal(self, "clicked")
        return s

    def addButton(self, *args):
        if len(args) == 1 and isinstance(args[0], int):
            self._set(standardButtons=self.q_standardButtons | int(args[0]))
            return self.button(args[0])
        raise TypeError("QDialogButtonBox.addButton(widget, role) is not in the sandbox's"
                        " subset yet (G3c)")

    def removeButton(self, button):
        if isinstance(button, _ButtonBoxButton):
            self._set(standardButtons=self.q_standardButtons & ~button._which)

    def standardButton(self, button):
        return button._which if isinstance(button, _ButtonBoxButton) else 0

    def buttons(self):
        return [self.button(b) for b in _standard_button_values() if self.q_standardButtons & b]

    def _handle_custom_msg(self, content, buffers):
        if isinstance(content, dict) and content.get("event") == "clicked":
            which = (content.get("args") or [0])[0]
            handle = self.button(which)
            self.clicked.emit(handle)
            sig = self._button_signals.get(int(which))
            if sig is not None:
                sig.emit(False)
            return
        QWidget._handle_custom_msg(self, content, buffers)


def _standard_button_values():
    return [v for k, v in vars(qtdata.QDialogButtonBoxButtons).items()
            if isinstance(v, int) and not k.startswith("_")]


for _k, _v in vars(qtdata.QDialogButtonBoxButtons).items():
    if isinstance(_v, int) and not _k.startswith("_"):
        setattr(QDialogButtonBox, _k, _v)
del _k, _v
QDialogButtonBox.StandardButton = qtdata.QDialogButtonBoxButtons


# -- containers (G3b) --------------------------------------------------------


class QTabWidget(QWidget):
    """Pages with titles: the pages are child widgets, the titles the
    `tabs` state, the current one an index."""

    _model_name = Unicode("QTabWidgetModel").tag(sync=True)
    qt_class = "QTabWidget"
    q_tabs = List(Unicode()).tag(sync=True)
    q_currentIndex = Int(-1).tag(sync=True)
    q_tabsClosable = Bool(False).tag(sync=True)
    q_documentMode = Bool(False).tag(sync=True)
    q_tabPosition = Int(0).tag(sync=True)

    North = 0
    South = 1
    West = 2
    East = 3
    Rounded = 0
    Triangular = 1

    currentChanged = Signal(int)
    tabCloseRequested = Signal(int)
    tabBarClicked = Signal(int)

    def _fcx_init_args(self, args, kwargs):
        QWidget._fcx_init_args(self, args, kwargs)
        self._pages = []

    def addTab(self, widget, *args):
        return self.insertTab(len(self._pages), widget, *args)

    def insertTab(self, index, widget, *args):
        title = ""
        icon = None
        for a in args:
            if isinstance(a, str):
                title = a
            elif isinstance(a, qtdata.QIcon):
                icon = a
        widget._attach(self)
        self._pages.insert(index, widget)
        tabs = list(self.q_tabs)
        tabs.insert(index, title)
        current = self.q_currentIndex if self.q_currentIndex >= 0 else 0
        self._set(tabs=tabs, currentIndex=current)
        self._event("insertTab", index, _ref(widget), title, _icon_path(icon) if icon else "")
        return index

    def removeTab(self, index):
        if 0 <= index < len(self._pages):
            self._pages.pop(index)
            tabs = list(self.q_tabs)
            tabs.pop(index)
            current = min(self.q_currentIndex, len(tabs) - 1)
            self._set(tabs=tabs, currentIndex=current)
            self._event("removeTab", index)

    def clear(self):
        while self._pages:
            self.removeTab(0)

    def count(self):
        return len(self.q_tabs)

    def widget(self, index):
        return self._pages[index] if 0 <= index < len(self._pages) else None

    def indexOf(self, widget):
        try:
            return self._pages.index(widget)
        except ValueError:
            return -1

    def currentIndex(self):
        return self.q_currentIndex

    def setCurrentIndex(self, index):
        self._set(currentIndex=int(index))

    def currentWidget(self):
        return self.widget(self.q_currentIndex)

    def setCurrentWidget(self, widget):
        i = self.indexOf(widget)
        if i >= 0:
            self.setCurrentIndex(i)

    def tabText(self, index):
        return self.q_tabs[index] if 0 <= index < len(self.q_tabs) else ""

    def setTabText(self, index, text):
        tabs = list(self.q_tabs)
        if 0 <= index < len(tabs):
            tabs[index] = str(text)
            self._set(tabs=tabs)

    def setTabEnabled(self, index, on):
        self._event("setTabEnabled", int(index), bool(on))

    def isTabEnabled(self, index):
        return True

    def setTabVisible(self, index, on):
        self._event("setTabVisible", int(index), bool(on))

    def setTabToolTip(self, index, text):
        self._event("setTabToolTip", int(index), str(text))

    def setTabIcon(self, index, icon):
        self._event("setTabIcon", int(index), _icon_path(icon))

    def setTabsClosable(self, on):
        self._set(tabsClosable=bool(on))

    def setDocumentMode(self, on):
        self._set(documentMode=bool(on))

    def setTabPosition(self, p):
        self._set(tabPosition=int(p))

    def setTabShape(self, s):
        pass

    def setMovable(self, on):
        pass

    def setUsesScrollButtons(self, on):
        pass

    def setElideMode(self, m):
        pass

    def tabBar(self):
        return self

    def _fcx_page(self, widget, title):
        """The .ui loader: a page uic already placed."""
        self._pages.append(widget)

    @observe("q_currentIndex")
    def _fcx_index(self, change):
        self.currentChanged.emit(change["new"])


class QStackedWidget(QWidget):
    _model_name = Unicode("QStackedWidgetModel").tag(sync=True)
    qt_class = "QStackedWidget"
    q_currentIndex = Int(-1).tag(sync=True)

    currentChanged = Signal(int)
    widgetRemoved = Signal(int)

    def _fcx_init_args(self, args, kwargs):
        QWidget._fcx_init_args(self, args, kwargs)
        self._pages = []

    def addWidget(self, widget):
        return self.insertWidget(len(self._pages), widget)

    def insertWidget(self, index, widget):
        widget._attach(self)
        self._pages.insert(index, widget)
        if self.q_currentIndex < 0:
            self._set(currentIndex=0)
        self._event("insertWidget", index, _ref(widget))
        return index

    def removeWidget(self, widget):
        if widget in self._pages:
            i = self._pages.index(widget)
            self._pages.remove(widget)
            self._event("removeWidget", _ref(widget))
            self.widgetRemoved.emit(i)

    def count(self):
        return len(self._pages)

    def widget(self, index):
        return self._pages[index] if 0 <= index < len(self._pages) else None

    def indexOf(self, widget):
        try:
            return self._pages.index(widget)
        except ValueError:
            return -1

    def currentIndex(self):
        return self.q_currentIndex

    def setCurrentIndex(self, index):
        self._set(currentIndex=int(index))

    def currentWidget(self):
        return self.widget(self.q_currentIndex)

    def setCurrentWidget(self, widget):
        i = self.indexOf(widget)
        if i >= 0:
            self.setCurrentIndex(i)

    def _fcx_page(self, widget, title=""):
        self._pages.append(widget)

    @observe("q_currentIndex")
    def _fcx_index(self, change):
        self.currentChanged.emit(change["new"])


class QScrollArea(QWidget):
    _model_name = Unicode("QScrollAreaModel").tag(sync=True)
    qt_class = "QScrollArea"
    q_widgetResizable = Bool(False).tag(sync=True)

    def _fcx_init_args(self, args, kwargs):
        QWidget._fcx_init_args(self, args, kwargs)
        self._widget = None

    def setWidget(self, widget):
        widget._attach(self)
        self._widget = widget
        self._event("setWidget", _ref(widget))

    def widget(self):
        return self._widget

    def takeWidget(self):
        w = self._widget
        self._widget = None
        if w is not None:
            self._event("setWidget", None)
        return w

    def setWidgetResizable(self, on):
        self._set(widgetResizable=bool(on))

    def widgetResizable(self):
        return self.q_widgetResizable

    def setVerticalScrollBarPolicy(self, p):
        pass

    def setHorizontalScrollBarPolicy(self, p):
        pass

    def setFrameShape(self, s):
        pass

    def setFrameStyle(self, s):
        pass

    def ensureWidgetVisible(self, *args):
        pass

    def verticalScrollBar(self):
        return _ScrollBarStub()

    def horizontalScrollBar(self):
        return _ScrollBarStub()

    def _fcx_page(self, widget, title=""):
        self._widget = widget


class _ScrollBarStub:
    def setValue(self, v):
        pass

    def value(self):
        return 0

    def maximum(self):
        return 0

    def minimum(self):
        return 0


class QSplitter(QWidget):
    _model_name = Unicode("QSplitterModel").tag(sync=True)
    qt_class = "QSplitter"
    q_orientation = Int(1).tag(sync=True)
    q_childrenCollapsible = Bool(True).tag(sync=True)
    q_sizes = List(Int()).tag(sync=True)

    splitterMoved = Signal(int, int)

    def _fcx_init_args(self, args, kwargs):
        rest = []
        for a in args:
            if isinstance(a, int):
                kwargs["q_orientation"] = a
            else:
                rest.append(a)
        args[:] = rest
        QWidget._fcx_init_args(self, args, kwargs)
        self._panes = []

    def addWidget(self, widget):
        self.insertWidget(len(self._panes), widget)

    def insertWidget(self, index, widget):
        widget._attach(self)
        self._panes.insert(index, widget)
        self._event("insertWidget", index, _ref(widget))

    def widget(self, index):
        return self._panes[index] if 0 <= index < len(self._panes) else None

    def count(self):
        return len(self._panes)

    def indexOf(self, widget):
        try:
            return self._panes.index(widget)
        except ValueError:
            return -1

    def setOrientation(self, o):
        self._set(orientation=int(o))

    def orientation(self):
        return self.q_orientation

    def setChildrenCollapsible(self, on):
        self._set(childrenCollapsible=bool(on))

    def setCollapsible(self, index, on):
        self._event("setCollapsible", int(index), bool(on))

    def setStretchFactor(self, index, factor):
        self._event("setStretchFactor", int(index), int(factor))

    def setSizes(self, sizes):
        self._set(sizes=[int(x) for x in sizes])

    def sizes(self):
        return list(self.q_sizes)

    def setHandleWidth(self, w):
        pass

    def setOpaqueResize(self, on):
        pass

    def saveState(self):
        return b""

    def restoreState(self, state):
        return False

    def _fcx_page(self, widget, title=""):
        self._panes.append(widget)


class FileChooser(QWidget):
    """`Gui::FileChooser`: a path and a browse button.  The path is data
    the HOST consumes (a font file for a ShapeString); the guest never
    reads it, so it needs no file permission (the catalog offers none)."""

    _model_name = Unicode("FileChooserModel").tag(sync=True)
    qt_class = "Gui::FileChooser"
    q_fileName = Unicode("").tag(sync=True)
    q_mode = Int(0).tag(sync=True)
    q_acceptMode = Int(0).tag(sync=True)
    q_filter = Unicode("").tag(sync=True)
    q_buttonText = Unicode("").tag(sync=True)

    File = 0
    Directory = 1
    AcceptOpen = 0
    AcceptSave = 1

    fileNameChanged = Signal(str)
    fileNameSelected = Signal(str)

    def fileName(self):
        return self.q_fileName

    def setFileName(self, name):
        self._set(fileName=str(name))

    def setMode(self, mode):
        self._set(mode=int(mode))

    def mode(self):
        return self.q_mode

    def setAcceptMode(self, mode):
        self._set(acceptMode=int(mode))

    def setFilter(self, text):
        self._set(filter=str(text))

    def filter(self):
        return self.q_filter

    def setButtonText(self, text):
        self._set(buttonText=str(text))

    def buttonText(self):
        return self.q_buttonText

    @observe("q_fileName")
    def _fcx_name(self, change):
        self.fileNameChanged.emit(change["new"])


class UiForm(QDialog):
    """The root `loadUi` returns: the file, and the named widgets in it
    by name (each an attribute too, as uic makes them).  A QDialog too,
    for the files whose root is one (`exec_`, `accept`, `rejected`);
    a QWidget root never calls that half."""

    _model_name = Unicode("UiFormModel").tag(sync=True)
    uiFile = Unicode("").tag(sync=True)
    widgets = Dict().tag(sync=True, **widget_serialization)


from .items import *  # noqa: E402,F401,F403  (the item views, G3b)
from . import items as _items  # noqa: E402

# The Qt class name -> the model class, for the .ui loader and
# `UiLoader().createWidget`.  A `Gui::Pref*` is its base class here
# ---- the U2 dialogs (docs/Sandbox.md 7.11, G3d): the stock Qt dialogs
# as one synchronous op each -- the host runs the nested loop under
# its main window and answers with the value.  A `parent` argument is
# accepted and ignored (the host parents them itself).


def _dialog_op(name, arg):
    import _fcx

    return _fcx.op(name, 0, arg)


class QMessageBox:
    """The statics (`question`, `information`, `warning`, `critical`,
    `about`) and the instance shape (`setText`, `setStandardButtons`,
    `exec_`); the button pressed as Qt's StandardButton value."""

    NoIcon = 0
    Information = 1
    Warning = 2
    Critical = 3
    Question = 4

    class Icon(qtdata._Enum):
        _name = "QMessageBox.Icon"
        NoIcon = 0
        Information = 1
        Warning = 2
        Critical = 3
        Question = 4

    StandardButton = qtdata.QDialogButtonBoxButtons
    NoButton = 0
    Ok = qtdata.QDialogButtonBoxButtons.Ok
    Save = qtdata.QDialogButtonBoxButtons.Save
    SaveAll = qtdata.QDialogButtonBoxButtons.SaveAll
    Open = qtdata.QDialogButtonBoxButtons.Open
    Yes = qtdata.QDialogButtonBoxButtons.Yes
    YesToAll = qtdata.QDialogButtonBoxButtons.YesToAll
    No = qtdata.QDialogButtonBoxButtons.No
    NoToAll = qtdata.QDialogButtonBoxButtons.NoToAll
    Abort = qtdata.QDialogButtonBoxButtons.Abort
    Retry = qtdata.QDialogButtonBoxButtons.Retry
    Ignore = qtdata.QDialogButtonBoxButtons.Ignore
    Close = qtdata.QDialogButtonBoxButtons.Close
    Cancel = qtdata.QDialogButtonBoxButtons.Cancel
    Discard = qtdata.QDialogButtonBoxButtons.Discard
    Help = qtdata.QDialogButtonBoxButtons.Help
    Apply = qtdata.QDialogButtonBoxButtons.Apply
    Reset = qtdata.QDialogButtonBoxButtons.Reset
    RestoreDefaults = qtdata.QDialogButtonBoxButtons.RestoreDefaults

    @staticmethod
    def _show(icon, title, text, buttons, default, informative="", detailed=""):
        return int(_dialog_op("gui.dialog.message", {
            "icon": int(icon), "title": str(title), "text": str(text),
            "informative": str(informative), "detailed": str(detailed),
            "buttons": int(buttons), "default": int(default)}))

    @classmethod
    def question(cls, parent, title, text, buttons=None, defaultButton=0):
        if buttons is None:
            buttons = cls.Yes | cls.No
        return cls._show(cls.Question, title, text, buttons, defaultButton)

    @classmethod
    def information(cls, parent, title, text, buttons=None, defaultButton=0):
        return cls._show(cls.Information, title, text, buttons or cls.Ok, defaultButton)

    @classmethod
    def warning(cls, parent, title, text, buttons=None, defaultButton=0):
        return cls._show(cls.Warning, title, text, buttons or cls.Ok, defaultButton)

    @classmethod
    def critical(cls, parent, title, text, buttons=None, defaultButton=0):
        return cls._show(cls.Critical, title, text, buttons or cls.Ok, defaultButton)

    @classmethod
    def about(cls, parent, title, text):
        cls._show(cls.NoIcon, title, text, cls.Ok, 0)

    def __init__(self, *args, **kw):
        # QMessageBox(parent) or QMessageBox(icon, title, text, buttons, parent)
        self._icon = self.NoIcon
        self._title = ""
        self._text = ""
        self._informative = ""
        self._detailed = ""
        self._buttons = self.NoButton
        self._default = 0
        if args and isinstance(args[0], int):
            self._icon = args[0]
            if len(args) > 1:
                self._title = str(args[1])
            if len(args) > 2:
                self._text = str(args[2])
            if len(args) > 3:
                self._buttons = int(args[3])

    def setIcon(self, icon):
        self._icon = int(icon)

    def setWindowTitle(self, title):
        self._title = str(title)

    def setText(self, text):
        self._text = str(text)

    def setInformativeText(self, text):
        self._informative = str(text)

    def setDetailedText(self, text):
        self._detailed = str(text)

    def setStandardButtons(self, buttons):
        self._buttons = int(buttons)

    def setDefaultButton(self, button):
        self._default = int(button)

    def setEscapeButton(self, button):
        pass

    def setTextFormat(self, fmt):
        pass

    def setWindowFlags(self, flags):
        pass

    def setModal(self, modal):
        pass

    def exec_(self):
        return self._show(self._icon, self._title, self._text, self._buttons, self._default,
                          self._informative, self._detailed)

    exec = exec_

    def show(self):
        self.exec_()

    def open(self):
        self.exec_()


class QInputDialog:
    """The statics: `(value, ok)` as PySide answers them."""

    Normal = 0
    NoEcho = 1
    Password = 2
    PasswordEchoOnEdit = 3
    TextInput = 0
    IntInput = 1
    DoubleInput = 2

    @staticmethod
    def _ask(arg):
        value, ok = _dialog_op("gui.dialog.input", arg)
        return value, bool(ok)

    @classmethod
    def getText(cls, parent, title, label, echo=0, text="", *args, **kw):
        return cls._ask({"kind": "text", "title": str(title), "label": str(label),
                         "echo": int(echo), "value": str(text)})

    @classmethod
    def getMultiLineText(cls, parent, title, label, text="", *args, **kw):
        return cls._ask({"kind": "multiline", "title": str(title), "label": str(label),
                         "value": str(text)})

    @classmethod
    def getInt(cls, parent, title, label, value=0, minValue=-2147483647,
               maxValue=2147483647, step=1, *args, **kw):
        return cls._ask({"kind": "int", "title": str(title), "label": str(label),
                         "value": int(value), "min": int(minValue), "max": int(maxValue),
                         "step": int(step)})

    getInteger = getInt

    @classmethod
    def getDouble(cls, parent, title, label, value=0.0, minValue=-2147483647.0,
                  maxValue=2147483647.0, decimals=1, *args, **kw):
        return cls._ask({"kind": "double", "title": str(title), "label": str(label),
                         "value": float(value), "min": float(minValue),
                         "max": float(maxValue), "decimals": int(decimals)})

    @classmethod
    def getItem(cls, parent, title, label, items, current=0, editable=True, *args, **kw):
        return cls._ask({"kind": "item", "title": str(title), "label": str(label),
                         "items": [str(i) for i in items], "current": int(current),
                         "editable": bool(editable)})


class QFileDialog:
    """The statics: `(path, selected filter)` as PySide answers them.
    The path is data; reading or writing it is the file-system
    grant's business (docs/Sandbox.md 7.1, U2)."""

    ShowDirsOnly = 0x1
    DontResolveSymlinks = 0x2
    DontConfirmOverwrite = 0x4
    DontUseNativeDialog = 0x10
    ReadOnly = 0x20
    HideNameFilterDetails = 0x40
    Option = int
    Options = int
    AnyFile = 0
    ExistingFile = 1
    Directory = 2
    ExistingFiles = 3
    AcceptOpen = 0
    AcceptSave = 1

    @staticmethod
    def _ask(mode, caption, directory, filter, selected, options):
        return _dialog_op("gui.dialog.file", {
            "mode": mode, "caption": str(caption), "dir": str(directory),
            "filter": str(filter), "selected": str(selected or ""), "options": int(options or 0)})

    @classmethod
    def getOpenFileName(cls, parent=None, caption="", dir="", filter="", selectedFilter="",
                        options=0, **kw):
        path, chosen = cls._ask("open", caption, dir, filter, selectedFilter, options)
        return path, chosen

    @classmethod
    def getOpenFileNames(cls, parent=None, caption="", dir="", filter="", selectedFilter="",
                         options=0, **kw):
        paths, chosen = cls._ask("opens", caption, dir, filter, selectedFilter, options)
        return list(paths), chosen

    @classmethod
    def getSaveFileName(cls, parent=None, caption="", dir="", filter="", selectedFilter="",
                        options=0, **kw):
        path, chosen = cls._ask("save", caption, dir, filter, selectedFilter, options)
        return path, chosen

    @classmethod
    def getExistingDirectory(cls, parent=None, caption="", dir="", options=0, **kw):
        path, _ = cls._ask("dir", caption, dir, "", "", options)
        return path


class QColorDialog:
    """`getColor`: a QColor, invalid when canceled."""

    ShowAlphaChannel = 0x1
    NoButtons = 0x2
    DontUseNativeDialog = 0x4

    @staticmethod
    def getColor(initial=None, parent=None, title="", options=0):
        if initial is None:
            initial = qtdata.QColor(255, 255, 255)
        elif not isinstance(initial, qtdata.QColor):
            initial = qtdata.QColor(initial)
        rgba = _dialog_op("gui.dialog.color", {
            "initial": list(initial.getRgbF()), "title": str(title), "options": int(options)})
        if rgba is None:
            return qtdata.QColor.invalid()
        return qtdata.QColor.fromRgbF(*rgba)


# (the host makes the real one); an unknown class is a QWidget.
CLASSES = {
    "QDialog": QDialog,
    "QDialogButtonBox": QDialogButtonBox,
    "QTabWidget": QTabWidget,
    "QStackedWidget": QStackedWidget,
    "QScrollArea": QScrollArea,
    "QSplitter": QSplitter,
    "Gui::FileChooser": FileChooser,
    "Gui::PrefFileChooser": FileChooser,
    "QTreeWidget": _items.QTreeWidget,
    "QListWidget": _items.QListWidget,
    "QTableWidget": _items.QTableWidget,
    "QTreeView": _items.QTreeView,
    "QListView": _items.QListView,
    "QTableView": _items.QTableView,
    "QColumnView": _items.QColumnView,
    "QWidget": QWidget,
    "QToolBar": QToolBar,
    "Gui::ToolBar": QToolBar,
    "QMenu": QMenu,
    "QAction": QAction,
    "QDockWidget": QDockWidget,
    "QLabel": QLabel,
    "QPushButton": QPushButton,
    "QToolButton": QToolButton,
    "QCheckBox": QCheckBox,
    "QRadioButton": QRadioButton,
    "QGroupBox": QGroupBox,
    "QFrame": QFrame,
    "QLineEdit": QLineEdit,
    "QTextEdit": QTextEdit,
    "QPlainTextEdit": QPlainTextEdit,
    "QTextBrowser": QTextBrowser,
    "QSpinBox": QSpinBox,
    "QDoubleSpinBox": QDoubleSpinBox,
    "QSlider": QSlider,
    "QProgressBar": QProgressBar,
    "QComboBox": QComboBox,
    "QFontComboBox": QFontComboBox,
    "Gui::InputField": InputField,
    "Gui::QuantitySpinBox": QuantitySpinBox,
    "Gui::PrefQuantitySpinBox": QuantitySpinBox,
    "Gui::PrefUnitSpinBox": QuantitySpinBox,
    "Gui::ColorButton": ColorButton,
    "Gui::PrefColorButton": ColorButton,
    "Gui::PrefCheckBox": QCheckBox,
    "Gui::PrefRadioButton": QRadioButton,
    "Gui::PrefLineEdit": QLineEdit,
    "Gui::PrefTextEdit": QTextEdit,
    "Gui::PrefComboBox": QComboBox,
    "Gui::PrefSpinBox": QSpinBox,
    "Gui::PrefDoubleSpinBox": QDoubleSpinBox,
    "Gui::PrefSlider": QSlider,
    "Gui::PrefCheckableGroupBox": QGroupBox,
    "Gui::PrefFontBox": QFontComboBox,
}


def make(qt_class, *args, **kwargs):
    """A widget of the Qt class `qt_class` (`Gui::PrefCheckBox` is a
    QCheckBox model the host builds as the real thing)."""
    cls = CLASSES.get(qt_class, QWidget)
    kwargs["qtClass"] = qt_class
    return cls(*args, **kwargs)


__all__ = [n for n in list(globals())
           if n[:1] == "Q" or n in ("Signal", "SIGNAL", "SLOT", "InputField", "QuantitySpinBox",
                                    "ColorButton", "FileChooser", "UiForm", "CLASSES", "LAYOUTS",
                                    "make")]

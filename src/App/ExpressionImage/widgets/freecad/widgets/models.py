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


# -- the widget base ---------------------------------------------------------


def _q(name):
    return PREFIX + name


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
        self._fcx_init_args(args, kwargs)
        # a constructor argument is a set: QLabel("text") shows "text"
        kwargs["_touched"] = [k[len(PREFIX):] for k in kwargs if k.startswith(PREFIX)]
        super().__init__(**kwargs)
        if parent is not None:
            self._attach(parent)

    def _fcx_init_args(self, args, kwargs):
        """A subclass reads its Qt-style positional arguments here."""
        if args:
            raise TypeError("%s(): unexpected positional arguments %r"
                            % (type(self).__name__, tuple(args)))

    # -- what the host sends: state (traitlets) and events

    def _handle_custom_msg(self, content, buffers):
        if isinstance(content, dict) and "event" in content:
            signal = getattr(self, content["event"], None)
            if isinstance(signal, BoundSignal):
                signal.emit(*(content.get("args") or []))
                return
        super()._handle_custom_msg(content, buffers)

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
        it back)."""
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
        return qtdata.QSize(100, 24)

    def width(self):
        return self.q_minimumWidth or 100

    def height(self):
        return self.q_minimumHeight or 24

    def setFont(self, font):
        pass

    def font(self):
        return qtdata.QFont()

    def setContentsMargins(self, *args):
        pass

    def setFocusPolicy(self, policy):
        pass

    def setAttribute(self, *args):
        pass

    def setContextMenuPolicy(self, policy):
        pass

    def installEventFilter(self, obj):
        pass

    def removeEventFilter(self, obj):
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
        return False

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


class QSpacerItem:
    def __init__(self, w=0, h=0, hPolicy=None, vPolicy=None):
        self.w, self.h = w, h
        self.hPolicy, self.vPolicy = hPolicy, vPolicy

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

    def __init__(self, parent=None):
        QObject.__init__(self, None)
        self._items = []  # QLayoutItem, with a position tuple each
        self._positions = []
        self._owner = None
        self._parent_layout = None
        self._objectName = ""
        if parent is not None:
            if isinstance(parent, QWidget):
                parent.setLayout(self)
            elif isinstance(parent, QLayout):
                parent.addLayout(self)

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
        self._notify("setContentsMargins", args=list(args))

    def setSpacing(self, n):
        self._notify("setSpacing", args=[n])

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

    def __init__(self, parent=None):
        QBoxLayout.__init__(self, QBoxLayout.TopToBottom, parent)


class QHBoxLayout(QBoxLayout):
    kind = "hbox"

    def __init__(self, parent=None):
        QBoxLayout.__init__(self, QBoxLayout.LeftToRight, parent)


class QGridLayout(QLayout):
    kind = "grid"

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

    def setMenu(self, menu):
        raise TypeError("QPushButton.setMenu is not in the sandbox's subset yet (G3c)")


class QToolButton(QAbstractButton):
    _model_name = Unicode("QToolButtonModel").tag(sync=True)
    qt_class = "QToolButton"
    q_autoRaise = Bool(False).tag(sync=True)

    def setAutoRaise(self, on):
        self._set(autoRaise=bool(on))

    def setToolButtonStyle(self, style):
        pass

    def setDefaultAction(self, action):
        raise TypeError("QToolButton.setDefaultAction is not in the sandbox's subset yet (G3c)")

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

    def addAction(self, *args):
        pass

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

    valueChanged = Signal(float)
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
        self.valueChanged.emit(change["new"])


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


class UiForm(QWidget):
    """The root `loadUi` returns: the file, and the named widgets in it
    by name (each an attribute too, as uic makes them)."""

    _model_name = Unicode("UiFormModel").tag(sync=True)
    uiFile = Unicode("").tag(sync=True)
    widgets = Dict().tag(sync=True, **widget_serialization)


# The Qt class name -> the model class, for the .ui loader and
# `UiLoader().createWidget`.  A `Gui::Pref*` is its base class here
# (the host makes the real one); an unknown class is a QWidget.
CLASSES = {
    "QWidget": QWidget,
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
                                    "ColorButton", "UiForm", "CLASSES", "LAYOUTS", "make")]

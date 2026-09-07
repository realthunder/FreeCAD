# SPDX-License-Identifier: LGPL-2.1-or-later
"""The item views for the sandbox guest (docs/Sandbox.md 7.11, G3b):
QListWidget, QTreeWidget, QTableWidget, QTreeView with QStandardItemModel,
their typed items, QModelIndex, the selection model and the header.

One representation serves all four: a view holds ROWS, each a list of
cells (text, icon, tool tip, check state, flags, colors) and, for a
tree, child rows.  The rows are NOT a synced trait -- a tree of a
thousand items would resend itself on every setText -- but cross as
item ops on the view's comm (`{"item": "insert" | "set" | "row" |
"remove" | "clear", ...}`), one small message per mutation, the way a
layout op crosses.  The host keeps the same row tree on its model of
the view and renders it through whichever Qt view class the .ui file
named.  What the user does comes back as state (`selection`, the
current row) and events with row ids (`itemClicked`, `itemEdited`,
`itemExpanded`, ...); the view maps the ids to its item objects and
fires the Qt-named signals with Qt's arguments.

A QStandardItemModel is a plain object on the guest, not a comm: its
rows cross on the comm of every view it is set on, so a DOM tier sees
one kind of thing.  User-role data stays on the guest (it holds
FreeCAD objects the wire could not carry); the host never needs it.
"""

import itertools

from traitlets import Any, Bool, Int, List, Unicode, observe

from . import qtdata
from .models import QObject, QWidget, Signal, _icon_path, _ref

_ids = itertools.count(1)

# the roles a cell carries on the wire, by Qt's role number
_ROLE_KEYS = {
    qtdata.Qt.DisplayRole: "text",
    qtdata.Qt.EditRole: "text",
    qtdata.Qt.DecorationRole: "icon",
    qtdata.Qt.ToolTipRole: "toolTip",
    qtdata.Qt.CheckStateRole: "check",
    qtdata.Qt.StatusTipRole: "statusTip",
    qtdata.Qt.WhatsThisRole: "whatsThis",
    qtdata.Qt.TextAlignmentRole: "align",
    qtdata.Qt.ForegroundRole: "fg",
    qtdata.Qt.BackgroundRole: "bg",
}

# Qt's default item flags, per class
FLAGS_TREE = 61  # Selectable | UserCheckable | Enabled | DragEnabled | DropEnabled
FLAGS_LIST = 53  # Selectable | UserCheckable | Enabled | DragEnabled
FLAGS_TABLE = 63  # + Editable
FLAGS_STANDARD = 47  # Selectable | Editable | Enabled | DragEnabled | DropEnabled


def _color(value):
    """A QColor / QBrush / (r, g, b[, a]) as the four floats the wire
    carries; None for nothing."""
    if value is None:
        return None
    if hasattr(value, "color"):
        value = value.color()
    if isinstance(value, qtdata.QColor):
        return list(value.getRgbF())
    if isinstance(value, (tuple, list)):
        vals = [float(v) for v in value]
        if any(v > 1.0 for v in vals):
            vals = [v / 255.0 for v in vals]
        while len(vals) < 4:
            vals.append(1.0)
        return vals[:4]
    return None


def _wire_cell(cell):
    """The cell without its defaults and its guest-only data."""
    out = {}
    for k, v in cell.items():
        if k == "_data":
            continue
        if v is None or v == "" or v is False or v == 0:
            continue
        out[k] = v
    return out


def _new_cell(text=""):
    return {"text": text, "icon": "", "toolTip": "", "check": None, "flags": None,
            "fg": None, "bg": None, "bold": False, "align": 0, "statusTip": "",
            "whatsThis": "", "_data": {}}


class _Row:
    """One row of a view: its cells, its children (a tree), the state
    a row has (expanded, hidden).  `container` is the view or the model
    it is attached to; None while an item is being built."""

    __slots__ = ("id", "cells", "children", "parent", "expanded", "hidden", "flags",
                 "container", "items")

    def __init__(self):
        self.id = next(_ids)
        self.cells = []
        self.children = []
        self.parent = None
        self.expanded = False
        self.hidden = False
        self.flags = None
        self.container = None
        # the item objects standing on this row (a tree item: one for the
        # row; a table or standard item: one per cell)
        self.items = {}

    def cell(self, col):
        while len(self.cells) <= col:
            self.cells.append(_new_cell())
        return self.cells[col]

    def to_wire(self):
        d = {"id": self.id, "cells": [_wire_cell(c) for c in self.cells]}
        if self.children:
            d["children"] = [c.to_wire() for c in self.children]
        if self.expanded:
            d["expanded"] = True
        if self.hidden:
            d["hidden"] = True
        if self.flags is not None:
            d["flags"] = self.flags
        return d

    def attach(self, container):
        self.container = container
        if container is not None:
            container._by_id[self.id] = self
        for c in self.children:
            c.attach(container)

    def detach(self):
        c = self.container
        if c is not None:
            c._by_id.pop(self.id, None)
        self.container = None
        for ch in self.children:
            ch.detach()

    def op(self, op):
        if self.container is not None:
            self.container._item_op(op)

    def set_cell(self, col, **partial):
        cell = self.cell(col)
        cell.update(partial)
        self.op({"item": "set", "id": self.id, "col": col, "cell": _wire_cell(partial)})
        c = self.container
        if c is not None:
            c._cell_changed(self, col)

    def set_row(self, **partial):
        for k, v in partial.items():
            setattr(self, k, v)
        self.op({"item": "row", "id": self.id, "row": partial})

    def insert_children(self, index, rows):
        for r in rows:
            r.parent = self
        self.children[index:index] = rows
        if self.container is not None:
            for r in rows:
                r.attach(self.container)
            self.op({"item": "insert", "parent": self.id, "index": index,
                     "rows": [r.to_wire() for r in rows]})

    def remove_child(self, row):
        if row not in self.children:
            return
        self.children.remove(row)
        row.parent = None
        if row.container is not None:
            row.op({"item": "remove", "id": row.id})
            row.detach()

    def is_top(self):
        return self.parent is None or self.parent.id == 0

    def index_in_parent(self):
        siblings = self.parent.children if self.parent is not None else \
            (self.container._top if self.container is not None else [])
        try:
            return siblings.index(self)
        except ValueError:
            return -1

    def depth_first(self):
        yield self
        for c in self.children:
            for d in c.depth_first():
                yield d


class _Container:
    """What holds top-level rows: a widget view or a QStandardItemModel."""

    def _init_rows(self):
        self._top = []
        self._by_id = {}

    def _insert_top(self, index, rows):
        for r in rows:
            r.parent = None
        self._top[index:index] = rows
        for r in rows:
            r.attach(self)
        self._item_op({"item": "insert", "parent": 0, "index": index,
                       "rows": [r.to_wire() for r in rows]})

    def _remove_top(self, row):
        if row not in self._top:
            return
        self._top.remove(row)
        self._item_op({"item": "remove", "id": row.id})
        row.detach()

    def _clear_rows(self):
        for r in self._top:
            r.detach()
        self._top = []
        self._by_id = {}
        self._item_op({"item": "clear"})

    def _row_of(self, row_id):
        return self._by_id.get(row_id)

    def _all_rows(self):
        for r in self._top:
            for d in r.depth_first():
                yield d

    def _flat(self, visible_only=False):
        """The rows in display order (a collapsed subtree hidden when
        `visible_only`)."""
        out = []

        def walk(rows):
            for r in rows:
                if visible_only and r.hidden:
                    continue
                out.append(r)
                if not visible_only or r.expanded:
                    walk(r.children)

        walk(self._top)
        return out

    def _item_op(self, op):
        raise NotImplementedError

    def _cell_changed(self, row, col):
        pass


# -- the items --------------------------------------------------------------


class _ItemBase:
    """The cell accessors an item shares, on `self._row` and a column."""

    _default_flags = FLAGS_TREE

    def _cell(self, col):
        return self._row.cell(col)

    def _set(self, col, **partial):
        self._row.set_cell(col, **partial)

    # roles
    def _data(self, col, role):
        key = _ROLE_KEYS.get(role)
        cell = self._cell(col)
        if key == "check":
            return cell["check"]
        if key == "fg" or key == "bg":
            v = cell[key]
            return qtdata.QColor(*[int(x * 255) for x in v]) if v else None
        if key == "icon":
            return qtdata.QIcon(cell["icon"])
        if key is not None:
            return cell[key]
        if role == qtdata.Qt.FontRole:
            f = qtdata.QFont()
            f.bold = cell["bold"]
            return f
        return cell["_data"].get(role)

    def _set_data(self, col, role, value):
        key = _ROLE_KEYS.get(role)
        if key == "check":
            self._set(col, check=int(value) if value is not None else None)
        elif key == "icon":
            self._set(col, icon=_icon_path(value))
        elif key in ("fg", "bg"):
            self._set(col, **{key: _color(value)})
        elif key == "align":
            self._set(col, align=int(value))
        elif key is not None:
            self._set(col, **{key: "" if value is None else str(value)})
        elif role == qtdata.Qt.FontRole:
            self._set(col, bold=bool(getattr(value, "bold", False)))
        else:
            # user data: the guest's own (a FreeCAD object, a tuple), not
            # on the wire
            self._cell(col)["_data"][role] = value

    def _flags(self, col):
        f = self._cell(col)["flags"]
        return self._default_flags if f is None else f

    def _set_flags(self, col, flags):
        self._set(col, flags=int(flags))


class QTreeWidgetItem(_ItemBase):
    """A row of a QTreeWidget: cells by column, child items."""

    UserType = 1000
    Type = 0
    _default_flags = FLAGS_TREE

    def __init__(self, *args):
        self._row = _Row()
        self._row.items[0] = self
        self._type = 0
        args = list(args)
        parent = None
        if args and isinstance(args[0], (QTreeWidgetItem, QTreeWidget)):
            parent = args.pop(0)
        elif args and args[0] is None:
            args.pop(0)
        if args and isinstance(args[0], (list, tuple)):
            for i, text in enumerate(args.pop(0)):
                self._row.cell(i)["text"] = str(text)
        elif args and isinstance(args[0], str):
            self._row.cell(0)["text"] = args.pop(0)
        if args and isinstance(args[0], int):
            self._type = args.pop(0)
        if isinstance(parent, QTreeWidgetItem):
            parent.addChild(self)
        elif isinstance(parent, QTreeWidget):
            parent.addTopLevelItem(self)

    @staticmethod
    def _of(row):
        return row.items.get(0) if row is not None else None

    def type(self):
        return self._type

    # -- cells
    def text(self, col=0):
        return self._cell(col)["text"]

    def setText(self, col, text):
        self._set(col, text=str(text))

    def icon(self, col=0):
        return qtdata.QIcon(self._cell(col)["icon"])

    def setIcon(self, col, icon):
        self._set(col, icon=_icon_path(icon))

    def toolTip(self, col=0):
        return self._cell(col)["toolTip"]

    def setToolTip(self, col, text):
        self._set(col, toolTip=str(text))

    def setStatusTip(self, col, text):
        self._set(col, statusTip=str(text))

    def setWhatsThis(self, col, text):
        self._set(col, whatsThis=str(text))

    def data(self, col, role):
        return self._data(col, role)

    def setData(self, col, role, value):
        self._set_data(col, role, value)

    def checkState(self, col=0):
        c = self._cell(col)["check"]
        return qtdata.Qt.Unchecked if c is None else c

    def setCheckState(self, col, state):
        self._set(col, check=int(state))

    def flags(self):
        f = self._row.flags
        return FLAGS_TREE if f is None else f

    def setFlags(self, flags):
        self._row.set_row(flags=int(flags))

    def setDisabled(self, on):
        f = self.flags()
        self.setFlags((f & ~qtdata.Qt.ItemIsEnabled) if on else (f | qtdata.Qt.ItemIsEnabled))

    def isDisabled(self):
        return not (self.flags() & qtdata.Qt.ItemIsEnabled)

    def foreground(self, col=0):
        return self._data(col, qtdata.Qt.ForegroundRole)

    def setForeground(self, col, brush):
        self._set(col, fg=_color(brush))

    def background(self, col=0):
        return self._data(col, qtdata.Qt.BackgroundRole)

    def setBackground(self, col, brush):
        self._set(col, bg=_color(brush))

    def setFont(self, col, font):
        self._set(col, bold=bool(getattr(font, "bold", False)))

    def font(self, col=0):
        return self._data(col, qtdata.Qt.FontRole)

    def setTextAlignment(self, col, align):
        self._set(col, align=int(align))

    def setSizeHint(self, col, size):
        pass

    def columnCount(self):
        return len(self._row.cells)

    # -- the tree
    def addChild(self, item):
        self.insertChild(len(self._row.children), item)

    def addChildren(self, items):
        for it in items:
            self.addChild(it)

    def insertChild(self, index, item):
        if item._row.parent is not None:
            item._row.parent.remove_child(item._row)
        elif item._row.container is not None:
            item._row.container._remove_top(item._row)
        self._row.insert_children(index, [item._row])

    def insertChildren(self, index, items):
        for i, it in enumerate(items):
            self.insertChild(index + i, it)

    def removeChild(self, item):
        self._row.remove_child(item._row)

    def takeChild(self, index):
        if 0 <= index < len(self._row.children):
            row = self._row.children[index]
            self._row.remove_child(row)
            return self._of(row)
        return None

    def takeChildren(self):
        out = []
        while self._row.children:
            out.append(self.takeChild(0))
        return out

    def child(self, index):
        if 0 <= index < len(self._row.children):
            return self._of(self._row.children[index])
        return None

    def childCount(self):
        return len(self._row.children)

    def indexOfChild(self, item):
        try:
            return self._row.children.index(item._row)
        except ValueError:
            return -1

    def parent(self):
        return None if self._row.is_top() else self._of(self._row.parent)

    def treeWidget(self):
        c = self._row.container
        return c if isinstance(c, QTreeWidget) else None

    def isExpanded(self):
        return self._row.expanded

    def setExpanded(self, on):
        self._row.set_row(expanded=bool(on))

    def isHidden(self):
        return self._row.hidden

    def setHidden(self, on):
        self._row.set_row(hidden=bool(on))

    def isSelected(self):
        v = self.treeWidget()
        return v is not None and self._row.id in v.q_selection

    def setSelected(self, on):
        v = self.treeWidget()
        if v is not None:
            v._select_rows([self._row], bool(on))

    def sortChildren(self, col, order):
        self._row.op({"item": "sort", "id": self._row.id, "col": col, "order": int(order)})

    def setFirstColumnSpanned(self, on):
        self._row.op({"item": "row", "id": self._row.id, "row": {"spanned": bool(on)}})

    def __lt__(self, other):
        return self.text(0) < other.text(0)


class QListWidgetItem(_ItemBase):
    """A row of a QListWidget: one cell."""

    _default_flags = FLAGS_LIST
    UserType = 1000

    def __init__(self, *args):
        self._row = _Row()
        self._row.items[0] = self
        args = list(args)
        parent = None
        if args and isinstance(args[0], qtdata.QIcon):
            self._row.cell(0)["icon"] = args.pop(0).path
        if args and isinstance(args[0], str):
            self._row.cell(0)["text"] = args.pop(0)
        if args and isinstance(args[0], QListWidget):
            parent = args.pop(0)
        elif args and args[0] is None:
            args.pop(0)
        self._row.cell(0)
        if parent is not None:
            parent.addItem(self)

    @staticmethod
    def _of(row):
        return row.items.get(0) if row is not None else None

    def text(self):
        return self._cell(0)["text"]

    def setText(self, text):
        self._set(0, text=str(text))

    def icon(self):
        return qtdata.QIcon(self._cell(0)["icon"])

    def setIcon(self, icon):
        self._set(0, icon=_icon_path(icon))

    def toolTip(self):
        return self._cell(0)["toolTip"]

    def setToolTip(self, text):
        self._set(0, toolTip=str(text))

    def setStatusTip(self, text):
        self._set(0, statusTip=str(text))

    def data(self, role):
        return self._data(0, role)

    def setData(self, role, value):
        self._set_data(0, role, value)

    def checkState(self):
        c = self._cell(0)["check"]
        return qtdata.Qt.Unchecked if c is None else c

    def setCheckState(self, state):
        self._set(0, check=int(state))

    def flags(self):
        return self._flags(0)

    def setFlags(self, flags):
        self._set_flags(0, flags)

    def setForeground(self, brush):
        self._set(0, fg=_color(brush))

    def setBackground(self, brush):
        self._set(0, bg=_color(brush))

    def setFont(self, font):
        self._set(0, bold=bool(getattr(font, "bold", False)))

    def setTextAlignment(self, align):
        self._set(0, align=int(align))

    def setSizeHint(self, size):
        pass

    def listWidget(self):
        return self._row.container

    def isHidden(self):
        return self._row.hidden

    def setHidden(self, on):
        self._row.set_row(hidden=bool(on))

    def isSelected(self):
        v = self._row.container
        return v is not None and self._row.id in v.q_selection

    def setSelected(self, on):
        v = self._row.container
        if v is not None:
            v._select_rows([self._row], bool(on))

    def __lt__(self, other):
        return self.text() < other.text()


class QTableWidgetItem(_ItemBase):
    """A cell of a QTableWidget: a row and a column once placed; its
    own cell dict until then."""

    _default_flags = FLAGS_TABLE
    UserType = 1000

    def __init__(self, *args):
        self._row = None
        self._col = None
        self._pending = _new_cell()
        args = list(args)
        if args and isinstance(args[0], qtdata.QIcon):
            self._pending["icon"] = args.pop(0).path
        if args and isinstance(args[0], str):
            self._pending["text"] = args.pop(0)

    def _cell(self, col=0):
        if self._row is None:
            return self._pending
        return self._row.cell(self._col)

    def _set(self, col=0, **partial):
        if self._row is None:
            self._pending.update(partial)
        else:
            self._row.set_cell(self._col, **partial)

    def _place(self, row, col):
        self._row, self._col = row, col
        while len(row.cells) <= col:
            row.cells.append(_new_cell())
        row.cells[col] = self._pending
        row.items[col] = self

    def text(self):
        return self._cell()["text"]

    def setText(self, text):
        self._set(text=str(text))

    def icon(self):
        return qtdata.QIcon(self._cell()["icon"])

    def setIcon(self, icon):
        self._set(icon=_icon_path(icon))

    def toolTip(self):
        return self._cell()["toolTip"]

    def setToolTip(self, text):
        self._set(toolTip=str(text))

    def data(self, role):
        return self._data(0, role)

    def setData(self, role, value):
        self._set_data(0, role, value)

    def checkState(self):
        c = self._cell()["check"]
        return qtdata.Qt.Unchecked if c is None else c

    def setCheckState(self, state):
        self._set(check=int(state))

    def flags(self):
        return self._flags(0)

    def setFlags(self, flags):
        self._set(flags=int(flags))

    def setForeground(self, brush):
        self._set(fg=_color(brush))

    def setBackground(self, brush):
        self._set(bg=_color(brush))

    def setFont(self, font):
        self._set(bold=bool(getattr(font, "bold", False)))

    def setTextAlignment(self, align):
        self._set(align=int(align))

    def setSizeHint(self, size):
        pass

    def row(self):
        return self._row.index_in_parent() if self._row is not None else -1

    def column(self):
        return self._col if self._col is not None else -1

    def tableWidget(self):
        return self._row.container if self._row is not None else None

    def isSelected(self):
        v = self.tableWidget()
        return v is not None and self._row.id in v.q_selection

    def setSelected(self, on):
        v = self.tableWidget()
        if v is not None:
            v._select_rows([self._row], bool(on))

    def __lt__(self, other):
        return self.text() < other.text()


class QStandardItem(_ItemBase):
    """An item of a QStandardItemModel: a cell of a row once placed
    (`appendRow`), with the row's children as its own (Qt hangs a row's
    children on its column-0 item; here they hang on the row)."""

    _default_flags = FLAGS_STANDARD
    UserType = 1000

    def __init__(self, *args):
        self._row = None
        self._col = None
        self._pending = _new_cell()
        self._pending_children = []
        args = list(args)
        if args and isinstance(args[0], qtdata.QIcon):
            self._pending["icon"] = args.pop(0).path
        if args and isinstance(args[0], str):
            self._pending["text"] = args.pop(0)

    def _cell(self, col=0):
        if self._row is None:
            return self._pending
        return self._row.cell(self._col)

    def _set(self, col=0, **partial):
        if self._row is None:
            self._pending.update(partial)
        else:
            self._row.set_cell(self._col, **partial)

    def _place(self, row, col):
        self._row, self._col = row, col
        while len(row.cells) <= col:
            row.cells.append(_new_cell())
        row.cells[col] = self._pending
        row.items[col] = self
        if self._pending_children:
            rows = self._pending_children
            self._pending_children = []
            row.insert_children(len(row.children), rows)

    @staticmethod
    def _of(row, col=0):
        if row is None:
            return None
        it = row.items.get(col)
        if it is None and col < len(row.cells):
            it = QStandardItem()
            it._place(row, col)
        return it

    # -- cells
    def text(self):
        return self._cell()["text"]

    def setText(self, text):
        self._set(text=str(text))

    def icon(self):
        return qtdata.QIcon(self._cell()["icon"])

    def setIcon(self, icon):
        self._set(icon=_icon_path(icon))

    def toolTip(self):
        return self._cell()["toolTip"]

    def setToolTip(self, text):
        self._set(toolTip=str(text))

    def setStatusTip(self, text):
        self._set(statusTip=str(text))

    def setWhatsThis(self, text):
        self._set(whatsThis=str(text))

    def data(self, role=qtdata.Qt.UserRole + 1):
        return self._data(0, role)

    def setData(self, value, role=qtdata.Qt.UserRole + 1):
        self._set_data(0, role, value)

    def checkState(self):
        c = self._cell()["check"]
        return qtdata.Qt.Unchecked if c is None else c

    def setCheckState(self, state):
        self._set(check=int(state))

    def flags(self):
        return self._flags(0)

    def setFlags(self, flags):
        self._set(flags=int(flags))

    def _flag(self, bit, on):
        f = self.flags()
        self.setFlags((f | bit) if on else (f & ~bit))

    def setCheckable(self, on):
        self._flag(qtdata.Qt.ItemIsUserCheckable, on)
        if on and self._cell()["check"] is None:
            self._set(check=qtdata.Qt.Unchecked)

    def isCheckable(self):
        return bool(self.flags() & qtdata.Qt.ItemIsUserCheckable)

    def setEditable(self, on):
        self._flag(qtdata.Qt.ItemIsEditable, on)

    def isEditable(self):
        return bool(self.flags() & qtdata.Qt.ItemIsEditable)

    def setEnabled(self, on):
        self._flag(qtdata.Qt.ItemIsEnabled, on)

    def isEnabled(self):
        return bool(self.flags() & qtdata.Qt.ItemIsEnabled)

    def setSelectable(self, on):
        self._flag(qtdata.Qt.ItemIsSelectable, on)

    def isSelectable(self):
        return bool(self.flags() & qtdata.Qt.ItemIsSelectable)

    def setDragEnabled(self, on):
        self._flag(qtdata.Qt.ItemIsDragEnabled, on)

    def setDropEnabled(self, on):
        self._flag(qtdata.Qt.ItemIsDropEnabled, on)

    def setUserTristate(self, on):
        pass

    def setAutoTristate(self, on):
        pass

    def setForeground(self, brush):
        self._set(fg=_color(brush))

    def foreground(self):
        return self._data(0, qtdata.Qt.ForegroundRole)

    def setBackground(self, brush):
        self._set(bg=_color(brush))

    def background(self):
        return self._data(0, qtdata.Qt.BackgroundRole)

    def setFont(self, font):
        self._set(bold=bool(getattr(font, "bold", False)))

    def font(self):
        return self._data(0, qtdata.Qt.FontRole)

    def setTextAlignment(self, align):
        self._set(align=int(align))

    def setSizeHint(self, size):
        pass

    def clone(self):
        c = QStandardItem()
        c._pending = dict(self._cell())
        c._pending["_data"] = dict(self._cell()["_data"])
        return c

    def emitDataChanged(self):
        if self._row is not None:
            self._row.set_cell(self._col)

    # -- the tree (the row's children)
    def _children_rows(self):
        return self._row.children if self._row is not None else self._pending_children

    def _make_row(self, items):
        if not isinstance(items, (list, tuple)):
            items = [items]
        row = _Row()
        for i, it in enumerate(items):
            if it is None:
                row.cell(i)
                continue
            it._place(row, i)
        return row

    def appendRow(self, items):
        self.insertRow(len(self._children_rows()), items)

    def appendRows(self, items):
        for it in items:
            self.appendRow(it)

    def insertRow(self, index, items):
        row = self._make_row(items)
        if self._row is None:
            row.parent = None
            self._pending_children.insert(index, row)
        else:
            self._row.insert_children(index, [row])

    def insertRows(self, index, items):
        for i, it in enumerate(items):
            self.insertRow(index + i, it)

    def removeRow(self, index):
        rows = self._children_rows()
        if 0 <= index < len(rows):
            if self._row is None:
                rows.pop(index)
            else:
                self._row.remove_child(rows[index])

    def removeRows(self, index, count):
        for _ in range(count):
            self.removeRow(index)

    def takeRow(self, index):
        rows = self._children_rows()
        if not 0 <= index < len(rows):
            return []
        row = rows[index]
        out = [row.items.get(c) for c in range(len(row.cells))]
        self.removeRow(index)
        return out

    def takeChild(self, row, col=0):
        rows = self._children_rows()
        if 0 <= row < len(rows):
            return rows[row].items.get(col)
        return None

    def child(self, row, col=0):
        rows = self._children_rows()
        if 0 <= row < len(rows):
            return QStandardItem._of(rows[row], col)
        return None

    def setChild(self, row, col, item=None):
        if item is None:
            item, col = col, 0
        rows = self._children_rows()
        while len(rows) <= row:
            self.appendRow([])
        item._place(rows[row], col)
        if rows[row].container is not None:
            rows[row].set_cell(col, **{k: v for k, v in item._cell().items() if k != "_data"})

    def rowCount(self):
        return len(self._children_rows())

    def columnCount(self):
        rows = self._children_rows()
        return max((len(r.cells) for r in rows), default=0)

    def hasChildren(self):
        return bool(self._children_rows())

    def setRowCount(self, n):
        while self.rowCount() > n:
            self.removeRow(self.rowCount() - 1)
        while self.rowCount() < n:
            self.appendRow([])

    def setColumnCount(self, n):
        pass

    def sortChildren(self, col, order=qtdata.Qt.AscendingOrder):
        if self._row is not None:
            self._row.op({"item": "sort", "id": self._row.id, "col": col, "order": int(order)})

    def parent(self):
        if self._row is None or self._row.is_top():
            return None
        return QStandardItem._of(self._row.parent, 0)

    def model(self):
        c = self._row.container if self._row is not None else None
        return c if isinstance(c, QStandardItemModel) else None

    def row(self):
        return self._row.index_in_parent() if self._row is not None else -1

    def column(self):
        return self._col if self._col is not None else -1

    def index(self):
        if self._row is None or self._row.container is None:
            return QModelIndex()
        return QModelIndex(self._row, self._col, self._row.container)

    def __lt__(self, other):
        return self.text() < other.text()


# -- indexes, selection, headers ------------------------------------------------


class QModelIndex:
    """A (row, column) of a model: valid when it stands on a row."""

    __slots__ = ("_row", "_col", "_model")

    def __init__(self, row=None, col=0, model=None):
        self._row = row
        self._col = col
        self._model = model

    def isValid(self):
        return self._row is not None

    def row(self):
        return self._row.index_in_parent() if self._row is not None else -1

    def column(self):
        return self._col if self._row is not None else -1

    def parent(self):
        if self._row is None or self._row.is_top():
            return QModelIndex()
        return QModelIndex(self._row.parent, 0, self._model)

    @staticmethod
    def _probe(col):
        """An index of that column on no row, for a delegate's createEditor."""
        return QModelIndex(_Row(), col, None)

    def sibling(self, row, col):
        if self._row is None:
            return QModelIndex()
        siblings = self._row.parent.children if self._row.parent is not None else \
            self._model._top
        if 0 <= row < len(siblings):
            return QModelIndex(siblings[row], col, self._model)
        return QModelIndex()

    def siblingAtColumn(self, col):
        return QModelIndex(self._row, col, self._model) if self._row is not None \
            else QModelIndex()

    def child(self, row, col):
        if self._row is None or not 0 <= row < len(self._row.children):
            return QModelIndex()
        return QModelIndex(self._row.children[row], col, self._model)

    def model(self):
        return self._model

    def data(self, role=qtdata.Qt.DisplayRole):
        if self._row is None:
            return None
        item = self._model._item_at(self._row, self._col) if self._model is not None else None
        if item is not None:
            return item._data(0 if not isinstance(item, QTreeWidgetItem) else self._col, role)
        return None

    def flags(self):
        if self._row is None:
            return 0
        f = self._row.cell(self._col)["flags"]
        if f is None:
            f = self._row.flags
        return FLAGS_STANDARD if f is None else f

    def internalPointer(self):
        return self._row

    def internalId(self):
        return self._row.id if self._row is not None else 0

    def __eq__(self, other):
        return isinstance(other, QModelIndex) and self._row is other._row \
            and (self._row is None or self._col == other._col)

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        return hash((id(self._row), self._col))

    def __repr__(self):
        return "<QModelIndex %d,%d>" % (self.row(), self.column())


class QPersistentModelIndex(QModelIndex):
    def __init__(self, index=None):
        if index is None:
            QModelIndex.__init__(self)
        else:
            QModelIndex.__init__(self, index._row, index._col, index._model)


class QItemSelection(list):
    """The selected indexes, as the ranges' indexes."""

    def indexes(self):
        return list(self)


class QItemSelectionModel(QObject):
    """The selection of one view, over its rows."""

    NoUpdate = 0
    Clear = 1
    Select = 2
    Deselect = 4
    Toggle = 8
    Current = 16
    Rows = 32
    Columns = 64
    ClearAndSelect = 3
    SelectCurrent = 18

    selectionChanged = Signal(object, object)
    currentChanged = Signal(object, object)
    currentRowChanged = Signal(object, object)

    def __init__(self, view):
        QObject.__init__(self)
        self._view = view

    def selectedIndexes(self):
        return self._view.selectedIndexes()

    def selectedRows(self, col=0):
        return [i.siblingAtColumn(col) for i in self._view.selectedIndexes() if i.column() == 0]

    def hasSelection(self):
        return bool(self._view.q_selection)

    def isSelected(self, index):
        return index.isValid() and index._row.id in self._view.q_selection

    def isRowSelected(self, row, parent=QModelIndex()):
        idx = parent.child(row, 0) if parent.isValid() else self._view.model().index(row, 0)
        return self.isSelected(idx)

    def select(self, index, flags):
        if isinstance(index, QItemSelection):
            rows = [i._row for i in index if i.isValid()]
        else:
            rows = [index._row] if index.isValid() else []
        if flags & self.Clear:
            self._view._set(selection=[])
        if flags & self.Toggle:
            for r in rows:
                self._view._select_rows([r], r.id not in self._view.q_selection)
        elif flags & self.Deselect:
            self._view._select_rows(rows, False)
        elif flags & self.Select:
            self._view._select_rows(rows, True)
        if flags & self.Current and rows:
            self._view._set(currentId=rows[0].id)

    def setCurrentIndex(self, index, flags):
        if index.isValid():
            self._view._set(currentId=index._row.id, currentColumn=index.column())
        else:
            self._view._set(currentId=0)
        self.select(index, flags & ~self.Current)

    def currentIndex(self):
        return self._view.currentIndex()

    def clearSelection(self):
        self._view._set(selection=[])

    def clear(self):
        self._view._set(selection=[], currentId=0)

    def reset(self):
        self.clear()

    def model(self):
        return self._view.model()


class QHeaderView(QObject):
    """A view's header: every call a request to the host's; the sizes
    read back from the view's `columnWidths`."""

    Interactive = 0
    Stretch = 1
    Fixed = 2
    ResizeToContents = 3

    sectionClicked = Signal(int)
    sectionResized = Signal(int, int, int)
    sectionDoubleClicked = Signal(int)

    def __init__(self, view, which):
        QObject.__init__(self)
        self._view = view
        self._which = which
        self._hidden = False

    def _call(self, method, *args):
        self._view._event("header", self._which, method, list(args))

    def setSectionResizeMode(self, *args):
        self._call("setSectionResizeMode", *args)

    def setResizeMode(self, *args):
        self._call("setSectionResizeMode", *args)

    def setStretchLastSection(self, on):
        self._call("setStretchLastSection", bool(on))

    def setDefaultSectionSize(self, size):
        self._call("setDefaultSectionSize", int(size))

    def setMinimumSectionSize(self, size):
        self._call("setMinimumSectionSize", int(size))

    def setMaximumSectionSize(self, size):
        self._call("setMaximumSectionSize", int(size))

    def resizeSection(self, i, size):
        self._call("resizeSection", int(i), int(size))

    def resizeSections(self, mode=None):
        self._call("resizeSections", *([int(mode)] if mode is not None else []))

    def setSortIndicator(self, i, order):
        self._call("setSortIndicator", int(i), int(order))

    def setSortIndicatorShown(self, on):
        self._call("setSortIndicatorShown", bool(on))

    def setSectionsClickable(self, on):
        self._call("setSectionsClickable", bool(on))

    def setSectionsMovable(self, on):
        self._call("setSectionsMovable", bool(on))

    def setDefaultAlignment(self, a):
        self._call("setDefaultAlignment", int(a))

    def setHighlightSections(self, on):
        self._call("setHighlightSections", bool(on))

    def setCascadingSectionResizes(self, on):
        pass

    def setVisible(self, on):
        self._hidden = not on
        self._call("setVisible", bool(on))

    def hide(self):
        self.setVisible(False)

    def show(self):
        self.setVisible(True)

    def isVisible(self):
        return not self._hidden

    def isHidden(self):
        return self._hidden

    def hideSection(self, i):
        self._call("hideSection", int(i))

    def showSection(self, i):
        self._call("showSection", int(i))

    def moveSection(self, a, b):
        self._call("moveSection", int(a), int(b))

    def count(self):
        return self._view.columnCount() if self._which == "h" else self._view.rowCount()

    def sectionSize(self, i):
        widths = self._view.q_columnWidths
        return widths[i] if 0 <= i < len(widths) else 0

    def length(self):
        return sum(self._view.q_columnWidths)

    def height(self):
        return 24

    def width(self):
        return self.length()


class QStyledItemDelegate(QObject):
    """A delegate is a cell TYPE here (docs/Sandbox.md 7.11, G3b): the
    view asks the delegate for an editor per column once, on the guest,
    and tells the host what kind of editor each column takes.  The
    delegate's own paint and geometry never run."""

    def __init__(self, parent=None, *args):
        QObject.__init__(self, parent)

    def createEditor(self, parent, option, index):
        return None

    def setEditorData(self, editor, index):
        pass

    def setModelData(self, editor, model, index):
        pass

    def updateEditorGeometry(self, editor, option, index):
        pass

    def paint(self, painter, option, index):
        pass

    def sizeHint(self, option, index):
        return qtdata.QSize(0, 0)

    def displayText(self, value, locale=None):
        return str(value)

    def initStyleOption(self, option, index):
        pass


QItemDelegate = QStyledItemDelegate
QAbstractItemDelegate = QStyledItemDelegate


def _cell_type_of(editor):
    """The kind of editor a delegate made, as the host takes it."""
    from . import models

    if editor is None or not isinstance(editor, QWidget):
        return None
    try:
        if isinstance(editor, models.QComboBox):
            return {"type": "combo", "items": list(editor.q_items)}
        if isinstance(editor, models.QSpinBox):
            return {"type": "int", "minimum": editor.q_minimum, "maximum": editor.q_maximum}
        if isinstance(editor, models.QDoubleSpinBox):
            return {"type": "double", "minimum": editor.q_minimum, "maximum": editor.q_maximum,
                    "decimals": editor.q_decimals}
        if isinstance(editor, models.QCheckBox):
            return {"type": "check"}
        if isinstance(editor, models.QLineEdit):
            return {"type": "text"}
        return None
    finally:
        try:
            editor.close()
        except Exception:
            pass


# -- the views --------------------------------------------------------------


class QAbstractItemView(QWidget, _Container):
    """What the four views share: the rows (item ops), the selection and
    the current row (state), the header and the delegate, the events
    with row ids mapped to items."""

    NoSelection = 0
    SingleSelection = 1
    MultiSelection = 2
    ExtendedSelection = 3
    ContiguousSelection = 4
    SelectItems = 0
    SelectRows = 1
    SelectColumns = 2
    NoEditTriggers = 0
    CurrentChanged = 1
    DoubleClicked = 2
    SelectedClicked = 4
    EditKeyPressed = 8
    AnyKeyPressed = 16
    AllEditTriggers = 31
    NoDragDrop = 0
    DragOnly = 1
    DropOnly = 2
    DragDrop = 3
    InternalMove = 4
    ScrollPerItem = 0
    ScrollPerPixel = 1
    EnsureVisible = 0
    PositionAtTop = 1
    PositionAtBottom = 2
    PositionAtCenter = 3

    class State:
        """`QAbstractItemView.State`: `state()` answers NoState here -- an
        edit in progress is the host's, and a guest that asks (BimViews
        before it refills its trees) reads "not editing"."""

        NoState = 0
        DraggingState = 1
        DragSelectingState = 2
        EditingState = 3
        ExpandingState = 4
        CollapsingState = 5
        AnimatingState = 6

    NoState = 0
    EditingState = 3

    def state(self):
        return self.State.NoState

    q_columns = List(Unicode()).tag(sync=True)
    q_columnCount = Int(1).tag(sync=True)
    q_rowLabels = List(Unicode()).tag(sync=True)
    q_selection = List(Int()).tag(sync=True)
    q_currentId = Int(0).tag(sync=True)
    q_currentColumn = Int(0).tag(sync=True)
    q_selectionMode = Int(1).tag(sync=True)
    q_selectionBehavior = Int(0).tag(sync=True)
    q_editTriggers = Int(0).tag(sync=True)
    q_dragDropMode = Int(0).tag(sync=True)
    q_sortingEnabled = Bool(False).tag(sync=True)
    q_alternatingRowColors = Bool(False).tag(sync=True)
    q_headerHidden = Bool(False).tag(sync=True)
    q_rootIsDecorated = Bool(True).tag(sync=True)
    q_uniformRowHeights = Bool(False).tag(sync=True)
    q_itemsExpandable = Bool(True).tag(sync=True)
    q_indentation = Int(20).tag(sync=True)
    q_showGrid = Bool(True).tag(sync=True)
    q_wordWrap = Bool(True).tag(sync=True)
    q_columnWidths = List(Int()).tag(sync=True)
    q_cellTypes = List(Any()).tag(sync=True)

    itemSelectionChanged = Signal()
    customContextMenuRequested = Signal(object)

    def __init__(self, *args, **kwargs):
        QWidget.__init__(self, *args, **kwargs)
        # rows a constructor made (QTableWidget(rows, cols)) predate the comm
        if self._top:
            self._item_op({"item": "insert", "parent": 0, "index": 0,
                           "rows": [r.to_wire() for r in self._top]})

    def _fcx_init_args(self, args, kwargs):
        QWidget._fcx_init_args(self, args, kwargs)
        self._init_rows()
        self._delegate = None
        self._column_delegates = {}
        self._selection_model = None
        self._header = None
        self._vheader = None
        self._prev_current = 0
        self._prev_selection = []

    # -- the wire
    def _item_op(self, op):
        if self.comm is not None:
            self.send(op)

    def _cell_changed(self, row, col):
        pass

    def _handle_custom_msg(self, content, buffers):
        if isinstance(content, dict) and "event" in content:
            handler = getattr(self, "_fcx_on_" + content["event"], None)
            if handler is not None:
                handler(*(content.get("args") or []))
                return
        QWidget._handle_custom_msg(self, content, buffers)

    def _item_of(self, row_id, col=0):
        """The item object standing on a row id (None if gone)."""
        row = self._row_of(row_id)
        return self._item_at(row, col) if row is not None else None

    def _item_at(self, row, col):
        return row.items.get(0)

    def _fcx_on_itemEdited(self, row_id, col, cell):
        row = self._row_of(row_id)
        if row is None:
            return
        row.cell(col).update(cell)
        self._edited(row, col)

    def _edited(self, row, col):
        pass

    # -- selection and current, as state
    def _select_rows(self, rows, on):
        sel = list(self.q_selection)
        for r in rows:
            if on and r.id not in sel:
                sel.append(r.id)
            elif not on and r.id in sel:
                sel.remove(r.id)
        if on and self.q_selectionMode == self.SingleSelection:
            sel = [rows[-1].id] if rows else sel
        self._set(selection=sel)

    def selectionModel(self):
        if self._selection_model is None:
            self._selection_model = QItemSelectionModel(self)
        return self._selection_model

    def selectedIndexes(self):
        out = []
        for rid in self.q_selection:
            row = self._row_of(rid)
            if row is None:
                continue
            for c in range(max(1, len(row.cells))):
                out.append(QModelIndex(row, c, self._index_model()))
        return out

    def _index_model(self):
        return self

    def currentIndex(self):
        row = self._row_of(self.q_currentId)
        if row is None:
            return QModelIndex()
        return QModelIndex(row, self.q_currentColumn, self._index_model())

    def setCurrentIndex(self, index):
        if index.isValid():
            self._set(currentId=index._row.id, currentColumn=index.column())
            self._select_rows([index._row], True)
        else:
            self._set(currentId=0)

    def clearSelection(self):
        self._set(selection=[])

    def selectAll(self):
        self._set(selection=[r.id for r in self._all_rows()])

    @observe("q_selection")
    def _fcx_selection(self, change):
        self.itemSelectionChanged.emit()
        if self._selection_model is not None:
            sel = QItemSelection(i for i in self.selectedIndexes())
            self._selection_model.selectionChanged.emit(sel, QItemSelection())

    @observe("q_currentId")
    def _fcx_current(self, change):
        prev = self._prev_current
        self._prev_current = change["new"]
        self._current_changed(change["new"], prev)

    def _current_changed(self, cur, prev):
        if self._selection_model is not None:
            c = self.currentIndex()
            p = QModelIndex(self._row_of(prev), 0, self._index_model()) \
                if self._row_of(prev) is not None else QModelIndex()
            self._selection_model.currentChanged.emit(c, p)
            self._selection_model.currentRowChanged.emit(c, p)

    # -- the header, the delegate
    def header(self):
        if self._header is None:
            self._header = QHeaderView(self, "h")
        return self._header

    horizontalHeader = header

    def verticalHeader(self):
        if self._vheader is None:
            self._vheader = QHeaderView(self, "v")
        return self._vheader

    def setItemDelegate(self, delegate):
        self._delegate = delegate
        self._push_cell_types()

    def itemDelegate(self):
        return self._delegate

    def setItemDelegateForColumn(self, col, delegate):
        self._column_delegates[int(col)] = delegate
        self._push_cell_types()

    def _push_cell_types(self):
        types = []
        n = max(self.columnCount(), max(self._column_delegates, default=-1) + 1, 1)
        for c in range(n):
            d = self._column_delegates.get(c, self._delegate)
            t = None
            if d is not None and type(d).createEditor is not QStyledItemDelegate.createEditor:
                try:
                    editor = d.createEditor(None, None, QModelIndex._probe(c))
                    t = _cell_type_of(editor)
                except Exception:
                    t = None
            types.append(t)
        self._set(cellTypes=types)

    # -- the widget's properties
    def setSelectionMode(self, mode):
        self._set(selectionMode=int(mode))

    def selectionMode(self):
        return self.q_selectionMode

    def setSelectionBehavior(self, b):
        self._set(selectionBehavior=int(b))

    def setEditTriggers(self, t):
        self._set(editTriggers=int(t))

    def setDragDropMode(self, m):
        self._set(dragDropMode=int(m))

    def setDragEnabled(self, on):
        pass

    def setAcceptDrops(self, on):
        pass

    def setDropIndicatorShown(self, on):
        pass

    def setDefaultDropAction(self, a):
        pass

    def setSortingEnabled(self, on):
        self._set(sortingEnabled=bool(on))

    def isSortingEnabled(self):
        return bool(self.q_sortingEnabled)

    def setAlternatingRowColors(self, on):
        self._set(alternatingRowColors=bool(on))

    def setHeaderHidden(self, on):
        self._set(headerHidden=bool(on))

    def setRootIsDecorated(self, on):
        self._set(rootIsDecorated=bool(on))

    def setUniformRowHeights(self, on):
        self._set(uniformRowHeights=bool(on))

    def setItemsExpandable(self, on):
        self._set(itemsExpandable=bool(on))

    def setIndentation(self, n):
        self._set(indentation=int(n))

    def setShowGrid(self, on):
        self._set(showGrid=bool(on))

    def setWordWrap(self, on):
        self._set(wordWrap=bool(on))

    def setAnimated(self, on):
        pass

    def setExpandsOnDoubleClick(self, on):
        pass

    def setIconSize(self, size):
        pass

    def setAutoScroll(self, on):
        pass

    def setVerticalScrollMode(self, m):
        pass

    def setHorizontalScrollMode(self, m):
        pass

    def setTextElideMode(self, m):
        pass

    def setMouseTracking(self, on):
        pass

    def setAllColumnsShowFocus(self, on):
        pass

    def viewport(self):
        return self

    def columnCount(self):
        return max(self.q_columnCount, len(self.q_columns))

    def setColumnCount(self, n):
        self._set(columnCount=int(n))

    def columnWidth(self, c):
        w = self.q_columnWidths
        return w[c] if 0 <= c < len(w) else 100

    def setColumnWidth(self, c, w):
        widths = list(self.q_columnWidths)
        while len(widths) <= c:
            widths.append(100)
        widths[c] = int(w)
        self._set(columnWidths=widths)

    def resizeColumnToContents(self, c):
        self._event("resizeColumnToContents", int(c))

    def resizeColumnsToContents(self):
        self._event("resizeColumnsToContents")

    def resizeRowsToContents(self):
        self._event("resizeRowsToContents")

    def setColumnHidden(self, c, on):
        self._event("setColumnHidden", int(c), bool(on))

    def hideColumn(self, c):
        self.setColumnHidden(c, True)

    def showColumn(self, c):
        self.setColumnHidden(c, False)

    def setRowHidden(self, row, *args):
        r = self._flat()[row] if 0 <= row < len(self._flat()) else None
        if r is not None:
            r.set_row(hidden=bool(args[-1]))

    def sortByColumn(self, c, order=qtdata.Qt.AscendingOrder):
        self._event("sortByColumn", int(c), int(order))

    def sortItems(self, c, order=qtdata.Qt.AscendingOrder):
        self._event("sortByColumn", int(c), int(order))

    def scrollToItem(self, item, hint=0):
        if item is not None and item._row is not None:
            self._event("scrollTo", item._row.id)

    def scrollTo(self, index, hint=0):
        if index.isValid():
            self._event("scrollTo", index._row.id)

    def scrollToTop(self):
        self._event("scrollToTop")

    def scrollToBottom(self):
        self._event("scrollToBottom")

    def edit(self, index, *args):
        if index.isValid():
            self._event("edit", index._row.id, index.column())

    def expandAll(self):
        for r in self._all_rows():
            if r.children:
                r.expanded = True
        self._event("expandAll")

    def collapseAll(self):
        for r in self._all_rows():
            r.expanded = False
        self._event("collapseAll")

    def expand(self, index):
        if index.isValid():
            index._row.set_row(expanded=True)

    def collapse(self, index):
        if index.isValid():
            index._row.set_row(expanded=False)

    def expandToDepth(self, depth):
        self._event("expandToDepth", int(depth))

    def isExpanded(self, index):
        return index.isValid() and index._row.expanded

    def setRootIndex(self, index):
        pass

    def rootIndex(self):
        return QModelIndex()

    def indexAt(self, pos):
        return QModelIndex()

    def itemAt(self, *args):
        return None

    def mapToGlobal(self, p):
        return p

    def model(self):
        return self

    # the model interface a widget-based view answers itself
    def rowCount(self, parent=QModelIndex()):
        return len(parent._row.children) if parent.isValid() else len(self._top)

    def index(self, row, col=0, parent=QModelIndex()):
        rows = parent._row.children if parent.isValid() else self._top
        if 0 <= row < len(rows):
            return QModelIndex(rows[row], col, self._index_model())
        return QModelIndex()

    def data(self, index, role=qtdata.Qt.DisplayRole):
        return index.data(role)

    def setData(self, index, value, role=qtdata.Qt.EditRole):
        if not index.isValid():
            return False
        item = self._item_at(index._row, index.column())
        if item is None:
            return False
        item._set_data(index.column() if isinstance(item, QTreeWidgetItem) else 0, role, value)
        return True

    def _fcx_on_sectionClicked(self, which, i):
        h = self._header if which == "h" else self._vheader
        if h is not None:
            h.sectionClicked.emit(i)


class QTreeWidget(QAbstractItemView):
    _model_name = Unicode("QTreeWidgetModel").tag(sync=True)
    qt_class = "QTreeWidget"

    itemClicked = Signal(object, int)
    itemDoubleClicked = Signal(object, int)
    itemActivated = Signal(object, int)
    itemPressed = Signal(object, int)
    itemEntered = Signal(object, int)
    itemChanged = Signal(object, int)
    itemExpanded = Signal(object)
    itemCollapsed = Signal(object)
    currentItemChanged = Signal(object, object)

    def _item_at(self, row, col):
        return QTreeWidgetItem._of(row)

    def _edited(self, row, col):
        self.itemChanged.emit(QTreeWidgetItem._of(row), col)

    def _cell_changed(self, row, col):
        self.itemChanged.emit(QTreeWidgetItem._of(row), col)

    def _fcx_on_itemClicked(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemClicked.emit(it, col)

    def _fcx_on_itemDoubleClicked(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemDoubleClicked.emit(it, col)

    def _fcx_on_itemActivated(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemActivated.emit(it, col)

    def _fcx_on_itemPressed(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemPressed.emit(it, col)

    def _fcx_on_itemExpanded(self, rid):
        row = self._row_of(rid)
        if row is not None:
            row.expanded = True
            self.itemExpanded.emit(QTreeWidgetItem._of(row))

    def _fcx_on_itemCollapsed(self, rid):
        row = self._row_of(rid)
        if row is not None:
            row.expanded = False
            self.itemCollapsed.emit(QTreeWidgetItem._of(row))

    def _current_changed(self, cur, prev):
        QAbstractItemView._current_changed(self, cur, prev)
        self.currentItemChanged.emit(self._item_of(cur), self._item_of(prev))

    # -- items
    def addTopLevelItem(self, item):
        self.insertTopLevelItem(len(self._top), item)

    def addTopLevelItems(self, items):
        for it in items:
            self.addTopLevelItem(it)

    def insertTopLevelItem(self, index, item):
        if item._row.parent is not None:
            item._row.parent.remove_child(item._row)
        elif item._row.container is not None:
            item._row.container._remove_top(item._row)
        self._insert_top(index, [item._row])

    def insertTopLevelItems(self, index, items):
        for i, it in enumerate(items):
            self.insertTopLevelItem(index + i, it)

    def takeTopLevelItem(self, index):
        if 0 <= index < len(self._top):
            row = self._top[index]
            self._remove_top(row)
            return QTreeWidgetItem._of(row)
        return None

    def topLevelItem(self, index):
        if 0 <= index < len(self._top):
            return QTreeWidgetItem._of(self._top[index])
        return None

    def topLevelItemCount(self):
        return len(self._top)

    def indexOfTopLevelItem(self, item):
        try:
            return self._top.index(item._row)
        except ValueError:
            return -1

    def invisibleRootItem(self):
        return _InvisibleRoot(self)

    def clear(self):
        self._clear_rows()
        self._set(selection=[], currentId=0)

    def currentItem(self):
        return self._item_of(self.q_currentId)

    def setCurrentItem(self, item, col=0):
        if item is None or item._row is None:
            self._set(currentId=0)
            return
        self._set(currentId=item._row.id, currentColumn=int(col))
        self._select_rows([item._row], True)

    def currentColumn(self):
        return self.q_currentColumn

    def selectedItems(self):
        out = []
        for rid in self.q_selection:
            it = self._item_of(rid)
            if it is not None:
                out.append(it)
        return out

    def findItems(self, text, flags=qtdata.Qt.MatchExactly, column=0):
        out = []
        recursive = bool(flags & qtdata.Qt.MatchRecursive)
        rows = self._all_rows() if recursive else self._top
        for r in rows:
            t = r.cell(column)["text"]
            mode = flags & 15
            if (mode == qtdata.Qt.MatchContains and text in t) \
                    or (mode == qtdata.Qt.MatchStartsWith and t.startswith(text)) \
                    or (mode == qtdata.Qt.MatchEndsWith and t.endswith(text)) \
                    or (mode == qtdata.Qt.MatchExactly and t == text):
                out.append(QTreeWidgetItem._of(r))
        return out

    def itemAbove(self, item):
        flat = self._flat(True)
        try:
            i = flat.index(item._row)
        except ValueError:
            return None
        return QTreeWidgetItem._of(flat[i - 1]) if i > 0 else None

    def itemBelow(self, item):
        flat = self._flat(True)
        try:
            i = flat.index(item._row)
        except ValueError:
            return None
        return QTreeWidgetItem._of(flat[i + 1]) if i + 1 < len(flat) else None

    def itemFromIndex(self, index):
        return QTreeWidgetItem._of(index._row) if index.isValid() else None

    def indexFromItem(self, item, col=0):
        return QModelIndex(item._row, col, self) if item is not None else QModelIndex()

    def editItem(self, item, col=0):
        if item._row is not None:
            self._event("edit", item._row.id, int(col))

    def openPersistentEditor(self, item, col=0):
        pass

    def closePersistentEditor(self, item, col=0):
        pass

    def setItemWidget(self, item, col, widget):
        widget._attach(self)
        self._event("setItemWidget", item._row.id, int(col), _ref(widget))

    def itemWidget(self, item, col):
        return None

    def removeItemWidget(self, item, col):
        self._event("setItemWidget", item._row.id, int(col), None)

    def setHeaderLabels(self, labels):
        labels = [str(x) for x in labels]
        self._set(columns=labels, columnCount=max(len(labels), self.q_columnCount))

    def setHeaderLabel(self, label):
        self.setHeaderLabels([label])

    def setHeaderItem(self, item):
        self.setHeaderLabels([item.text(c) for c in range(item.columnCount())])

    def headerItem(self):
        it = QTreeWidgetItem(list(self.q_columns))
        return it

    def isItemExpanded(self, item):
        return item.isExpanded()

    def setItemExpanded(self, item, on):
        item.setExpanded(on)

    def isItemHidden(self, item):
        return item.isHidden()

    def setItemHidden(self, item, on):
        item.setHidden(on)

    def isItemSelected(self, item):
        return item.isSelected()

    def setItemSelected(self, item, on):
        item.setSelected(on)

    def setFirstColumnSpanned(self, row, parent, on):
        rows = parent._row.children if parent.isValid() else self._top
        if 0 <= row < len(rows):
            rows[row].op({"item": "row", "id": rows[row].id, "row": {"spanned": bool(on)}})

    def setColumnCount(self, n):
        self._set(columnCount=int(n))


class _InvisibleRoot(QTreeWidgetItem):
    """`treeWidget.invisibleRootItem()`: the top level as an item."""

    def __init__(self, view):
        self._view = view
        self._row = _Row()
        self._row.children = view._top
        self._row.container = view
        self._type = 0

    def addChild(self, item):
        self._view.addTopLevelItem(item)

    def insertChild(self, index, item):
        self._view.insertTopLevelItem(index, item)

    def removeChild(self, item):
        self._view._remove_top(item._row)

    def takeChild(self, index):
        return self._view.takeTopLevelItem(index)

    def child(self, index):
        return self._view.topLevelItem(index)

    def childCount(self):
        return self._view.topLevelItemCount()

    def indexOfChild(self, item):
        return self._view.indexOfTopLevelItem(item)


class QListWidget(QAbstractItemView):
    _model_name = Unicode("QListWidgetModel").tag(sync=True)
    qt_class = "QListWidget"
    q_rootIsDecorated = Bool(False).tag(sync=True)

    IconMode = 1
    ListMode = 0
    Static = 0
    Free = 1
    Snap = 2
    TopToBottom = 0
    LeftToRight = 1

    itemClicked = Signal(object)
    itemDoubleClicked = Signal(object)
    itemActivated = Signal(object)
    itemPressed = Signal(object)
    itemEntered = Signal(object)
    itemChanged = Signal(object)
    currentItemChanged = Signal(object, object)
    currentRowChanged = Signal(int)
    currentTextChanged = Signal(str)

    def _item_at(self, row, col):
        return QListWidgetItem._of(row)

    def _edited(self, row, col):
        self.itemChanged.emit(QListWidgetItem._of(row))

    def _cell_changed(self, row, col):
        self.itemChanged.emit(QListWidgetItem._of(row))

    def _fcx_on_itemClicked(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemClicked.emit(it)

    def _fcx_on_itemDoubleClicked(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemDoubleClicked.emit(it)

    def _fcx_on_itemActivated(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemActivated.emit(it)

    def _fcx_on_itemPressed(self, rid, col):
        it = self._item_of(rid)
        if it is not None:
            self.itemPressed.emit(it)

    def _current_changed(self, cur, prev):
        QAbstractItemView._current_changed(self, cur, prev)
        c, p = self._item_of(cur), self._item_of(prev)
        self.currentItemChanged.emit(c, p)
        self.currentRowChanged.emit(self.currentRow())
        self.currentTextChanged.emit(c.text() if c is not None else "")

    def addItem(self, item):
        if isinstance(item, str):
            item = QListWidgetItem(item)
        self.insertItem(len(self._top), item)

    def addItems(self, texts):
        for t in texts:
            self.addItem(t)

    def insertItem(self, index, item):
        if isinstance(item, str):
            item = QListWidgetItem(item)
        if item._row.container is not None:
            item._row.container._remove_top(item._row)
        self._insert_top(index, [item._row])

    def insertItems(self, index, texts):
        for i, t in enumerate(texts):
            self.insertItem(index + i, t)

    def takeItem(self, index):
        if 0 <= index < len(self._top):
            row = self._top[index]
            self._remove_top(row)
            sel = [x for x in self.q_selection if x != row.id]
            if len(sel) != len(self.q_selection):
                self._set(selection=sel)
            return QListWidgetItem._of(row)
        return None

    def removeItemWidget(self, item):
        pass

    def item(self, index):
        if 0 <= index < len(self._top):
            return QListWidgetItem._of(self._top[index])
        return None

    def count(self):
        return len(self._top)

    def row(self, item):
        try:
            return self._top.index(item._row)
        except ValueError:
            return -1

    def clear(self):
        self._clear_rows()
        self._set(selection=[], currentId=0)

    def currentItem(self):
        return self._item_of(self.q_currentId)

    def setCurrentItem(self, item, *args):
        if item is None or item._row is None:
            self._set(currentId=0)
            return
        self._set(currentId=item._row.id)
        self._select_rows([item._row], True)

    def currentRow(self):
        row = self._row_of(self.q_currentId)
        return row.index_in_parent() if row is not None else -1

    def setCurrentRow(self, index, *args):
        if 0 <= index < len(self._top):
            self.setCurrentItem(QListWidgetItem._of(self._top[index]))
        else:
            self._set(currentId=0, selection=[])

    def selectedItems(self):
        return [it for it in (self._item_of(r) for r in self.q_selection) if it is not None]

    def findItems(self, text, flags=qtdata.Qt.MatchExactly):
        out = []
        for r in self._top:
            t = r.cell(0)["text"]
            mode = flags & 15
            if (mode == qtdata.Qt.MatchContains and text in t) \
                    or (mode == qtdata.Qt.MatchStartsWith and t.startswith(text)) \
                    or (mode == qtdata.Qt.MatchEndsWith and t.endswith(text)) \
                    or (mode == qtdata.Qt.MatchExactly and t == text):
                out.append(QListWidgetItem._of(r))
        return out

    def itemFromIndex(self, index):
        return QListWidgetItem._of(index._row) if index.isValid() else None

    def indexFromItem(self, item):
        return QModelIndex(item._row, 0, self) if item is not None else QModelIndex()

    def editItem(self, item):
        if item._row is not None:
            self._event("edit", item._row.id, 0)

    def setItemWidget(self, item, widget):
        widget._attach(self)
        self._event("setItemWidget", item._row.id, 0, _ref(widget))

    def itemWidget(self, item):
        return None

    def setViewMode(self, m):
        pass

    def setFlow(self, f):
        pass

    def setMovement(self, m):
        pass

    def setResizeMode(self, m):
        pass

    def setSpacing(self, n):
        pass

    def setWrapping(self, on):
        pass

    def setGridSize(self, s):
        pass

    def isItemSelected(self, item):
        return item.isSelected()

    def setItemSelected(self, item, on):
        item.setSelected(on)

    def isItemHidden(self, item):
        return item.isHidden()

    def setItemHidden(self, item, on):
        item.setHidden(on)


class QTableWidget(QAbstractItemView):
    _model_name = Unicode("QTableWidgetModel").tag(sync=True)
    qt_class = "QTableWidget"
    q_rootIsDecorated = Bool(False).tag(sync=True)
    q_columnCount = Int(0).tag(sync=True)

    cellClicked = Signal(int, int)
    cellDoubleClicked = Signal(int, int)
    cellActivated = Signal(int, int)
    cellPressed = Signal(int, int)
    cellChanged = Signal(int, int)
    cellEntered = Signal(int, int)
    currentCellChanged = Signal(int, int, int, int)
    itemClicked = Signal(object)
    itemDoubleClicked = Signal(object)
    itemActivated = Signal(object)
    itemPressed = Signal(object)
    itemChanged = Signal(object)
    currentItemChanged = Signal(object, object)

    def _item_at(self, row, col):
        it = row.items.get(col)
        if it is None and col < len(row.cells):
            it = QTableWidgetItem()
            it._place(row, col)
        return it

    def _edited(self, row, col):
        self.cellChanged.emit(row.index_in_parent(), col)
        it = row.items.get(col)
        if it is not None:
            self.itemChanged.emit(it)

    def _cell_changed(self, row, col):
        self.cellChanged.emit(row.index_in_parent(), col)
        it = row.items.get(col)
        if it is not None:
            self.itemChanged.emit(it)

    def _fcx_on_itemClicked(self, rid, col):
        row = self._row_of(rid)
        if row is not None:
            self.cellClicked.emit(row.index_in_parent(), col)
            it = row.items.get(col)
            if it is not None:
                self.itemClicked.emit(it)

    def _fcx_on_itemDoubleClicked(self, rid, col):
        row = self._row_of(rid)
        if row is not None:
            self.cellDoubleClicked.emit(row.index_in_parent(), col)
            it = row.items.get(col)
            if it is not None:
                self.itemDoubleClicked.emit(it)

    def _fcx_on_itemActivated(self, rid, col):
        row = self._row_of(rid)
        if row is not None:
            self.cellActivated.emit(row.index_in_parent(), col)

    def _fcx_on_itemPressed(self, rid, col):
        row = self._row_of(rid)
        if row is not None:
            self.cellPressed.emit(row.index_in_parent(), col)

    def _current_changed(self, cur, prev):
        QAbstractItemView._current_changed(self, cur, prev)
        c, p = self._row_of(cur), self._row_of(prev)
        self.currentCellChanged.emit(c.index_in_parent() if c else -1, self.q_currentColumn,
                                     p.index_in_parent() if p else -1, 0)
        self.currentItemChanged.emit(self.currentItem(),
                                     p.items.get(0) if p is not None else None)

    # -- the grid
    def _fcx_init_args(self, args, kwargs):
        rows = cols = 0
        if len(args) >= 2 and isinstance(args[0], int) and isinstance(args[1], int):
            rows, cols = args[0], args[1]
            args[:2] = []
        QAbstractItemView._fcx_init_args(self, args, kwargs)
        if cols:
            kwargs["q_columnCount"] = cols
        for _ in range(rows):
            row = _Row()
            for c in range(cols):
                row.cell(c)
            row.attach(self)
            self._top.append(row)

    def _new_row(self):
        row = _Row()
        for c in range(self.columnCount()):
            row.cell(c)
        return row

    def rowCount(self, *args):
        return len(self._top)

    def setRowCount(self, n):
        n = int(n)
        while len(self._top) > n:
            self._remove_top(self._top[-1])
        if len(self._top) < n:
            rows = [self._new_row() for _ in range(n - len(self._top))]
            self._insert_top(len(self._top), rows)

    def columnCount(self):
        return max(self.q_columnCount, len(self.q_columns))

    def setColumnCount(self, n):
        self._set(columnCount=int(n))
        for r in self._top:
            for c in range(int(n)):
                r.cell(c)

    def insertRow(self, index):
        self._insert_top(index, [self._new_row()])

    def removeRow(self, index):
        if 0 <= index < len(self._top):
            self._remove_top(self._top[index])

    def insertColumn(self, index):
        self.setColumnCount(self.columnCount() + 1)

    def removeColumn(self, index):
        pass

    def clear(self):
        self._clear_rows()
        self._set(selection=[], currentId=0, rowLabels=[])

    def clearContents(self):
        rows = list(self._top)
        self._clear_rows()
        self._set(selection=[], currentId=0)
        if rows:
            self._insert_top(0, [self._new_row() for _ in rows])

    def setItem(self, r, c, item):
        while len(self._top) <= r:
            self.insertRow(len(self._top))
        row = self._top[r]
        item._place(row, c)
        if row.container is not None:
            row.set_cell(c, **{k: v for k, v in item._cell().items() if k != "_data"})

    def item(self, r, c):
        if 0 <= r < len(self._top):
            row = self._top[r]
            it = row.items.get(c)
            if it is None and c < len(row.cells) and (row.cells[c]["text"]
                                                     or row.cells[c]["check"] is not None):
                it = QTableWidgetItem()
                it._place(row, c)
            return it
        return None

    def takeItem(self, r, c):
        it = self.item(r, c)
        if it is not None:
            row = it._row
            row.items.pop(c, None)
            it._pending = row.cells[c]
            it._row, it._col = None, None
            row.cells[c] = _new_cell()
            row.set_cell(c, text="", icon="", toolTip="", check=None, flags=None)
        return it

    def currentRow(self):
        row = self._row_of(self.q_currentId)
        return row.index_in_parent() if row is not None else -1

    def currentColumn(self):
        return self.q_currentColumn if self.q_currentId else -1

    def setCurrentCell(self, r, c, *args):
        if 0 <= r < len(self._top):
            self._set(currentId=self._top[r].id, currentColumn=int(c))
            self._select_rows([self._top[r]], True)
        else:
            self._set(currentId=0)

    def currentItem(self):
        row = self._row_of(self.q_currentId)
        return self._item_at(row, self.q_currentColumn) if row is not None else None

    def setCurrentItem(self, item, *args):
        if item is None or item._row is None:
            self._set(currentId=0)
        else:
            self._set(currentId=item._row.id, currentColumn=item._col)
            self._select_rows([item._row], True)

    def selectedItems(self):
        out = []
        for rid in self.q_selection:
            row = self._row_of(rid)
            if row is None:
                continue
            for c in range(len(row.cells)):
                it = self._item_at(row, c)
                if it is not None:
                    out.append(it)
        return out

    def selectRow(self, r):
        if 0 <= r < len(self._top):
            self._select_rows([self._top[r]], True)

    def selectColumn(self, c):
        pass

    def selectedRanges(self):
        return []

    def row(self, item):
        return item.row()

    def column(self, item):
        return item.column()

    def setHorizontalHeaderLabels(self, labels):
        labels = [str(x) for x in labels]
        self._set(columns=labels, columnCount=max(len(labels), self.q_columnCount))

    def setVerticalHeaderLabels(self, labels):
        self._set(rowLabels=[str(x) for x in labels])

    def setHorizontalHeaderItem(self, c, item):
        labels = list(self.q_columns)
        while len(labels) <= c:
            labels.append("")
        labels[c] = item.text()
        self.setHorizontalHeaderLabels(labels)

    def horizontalHeaderItem(self, c):
        labels = self.q_columns
        return QTableWidgetItem(labels[c]) if 0 <= c < len(labels) else None

    def setVerticalHeaderItem(self, r, item):
        labels = list(self.q_rowLabels)
        while len(labels) <= r:
            labels.append("")
        labels[r] = item.text()
        self._set(rowLabels=labels)

    def setRowHeight(self, r, h):
        self._event("setRowHeight", int(r), int(h))

    def setSpan(self, r, c, rs, cs):
        self._event("setSpan", int(r), int(c), int(rs), int(cs))

    def setCellWidget(self, r, c, widget):
        if 0 <= r < len(self._top):
            widget._attach(self)
            self._event("setItemWidget", self._top[r].id, int(c), _ref(widget))

    def cellWidget(self, r, c):
        return None

    def removeCellWidget(self, r, c):
        if 0 <= r < len(self._top):
            self._event("setItemWidget", self._top[r].id, int(c), None)

    def setCornerButtonEnabled(self, on):
        pass

    def editItem(self, item):
        if item._row is not None:
            self._event("edit", item._row.id, item._col)

    def itemFromIndex(self, index):
        return self._item_at(index._row, index.column()) if index.isValid() else None

    def indexFromItem(self, item):
        return QModelIndex(item._row, item._col, self) if item._row is not None \
            else QModelIndex()

    def findItems(self, text, flags=qtdata.Qt.MatchExactly):
        out = []
        for r in self._top:
            for c, cell in enumerate(r.cells):
                t = cell["text"]
                mode = flags & 15
                if (mode == qtdata.Qt.MatchContains and text in t) \
                        or (mode == qtdata.Qt.MatchStartsWith and t.startswith(text)) \
                        or (mode == qtdata.Qt.MatchEndsWith and t.endswith(text)) \
                        or (mode == qtdata.Qt.MatchExactly and t == text):
                    out.append(self._item_at(r, c))
        return out


class QStandardItemModel(QObject, _Container):
    """An item model of its own: rows of QStandardItems.  Its rows cross
    on the comm of every view it is set on."""

    itemChanged = Signal(object)
    dataChanged = Signal(object, object, object)
    rowsInserted = Signal(object, int, int)
    rowsRemoved = Signal(object, int, int)
    modelReset = Signal()
    layoutChanged = Signal()

    def __init__(self, *args):
        QObject.__init__(self)
        self._init_rows()
        self._views = []
        self._columns = []
        self._column_count = 0
        self._root = QStandardItem()
        self._root._row = _Row()
        self._root._row.id = 0
        self._root._row.container = self
        self._root._row.children = self._top
        self._root._col = 0
        args = [a for a in args if not isinstance(a, QObject)]
        if len(args) >= 2 and isinstance(args[0], int):
            self._column_count = int(args[1])
            for _ in range(int(args[0])):
                self.appendRow([])

    # -- the wire: through the views
    def _item_op(self, op):
        for v in self._views:
            v._item_op(op)

    def _cell_changed(self, row, col):
        item = QStandardItem._of(row, col)
        self.itemChanged.emit(item)
        idx = QModelIndex(row, col, self)
        self.dataChanged.emit(idx, idx, [])

    def _attach_view(self, view):
        if view not in self._views:
            self._views.append(view)
        view._by_id = self._by_id
        view._top = self._top
        view._set(columns=list(self._columns),
                  columnCount=max(self._column_count, len(self._columns), 1))
        view._item_op({"item": "clear"})
        if self._top:
            view._item_op({"item": "insert", "parent": 0, "index": 0,
                           "rows": [r.to_wire() for r in self._top]})

    def _detach_view(self, view):
        if view in self._views:
            self._views.remove(view)
        view._init_rows()

    def _item_at(self, row, col):
        return QStandardItem._of(row, col)

    # -- rows
    def appendRow(self, items):
        self._root.appendRow(items)

    def appendRows(self, items):
        self._root.appendRows(items)

    def insertRow(self, index, items=None):
        if items is None:
            items = []
        self._root.insertRow(index, items)
        return True

    def insertRows(self, index, count, parent=QModelIndex()):
        target = QStandardItem._of(parent._row, 0) if parent.isValid() else self._root
        for _ in range(count):
            target.insertRow(index, [])
        return True

    def removeRow(self, index, parent=QModelIndex()):
        target = QStandardItem._of(parent._row, 0) if parent.isValid() else self._root
        target.removeRow(index)
        return True

    def removeRows(self, index, count, parent=QModelIndex()):
        for _ in range(count):
            self.removeRow(index, parent)
        return True

    def takeRow(self, index):
        return self._root.takeRow(index)

    def takeItem(self, r, c=0):
        row = self._top[r] if 0 <= r < len(self._top) else None
        return row.items.get(c) if row is not None else None

    def rowCount(self, parent=QModelIndex()):
        return len(parent._row.children) if parent.isValid() else len(self._top)

    def columnCount(self, parent=QModelIndex()):
        return max(self._column_count, len(self._columns), self._root.columnCount())

    def setRowCount(self, n):
        self._root.setRowCount(n)

    def setColumnCount(self, n):
        self._column_count = int(n)
        for v in self._views:
            v._set(columnCount=int(n))

    def hasChildren(self, parent=QModelIndex()):
        return self.rowCount(parent) > 0

    def clear(self):
        self._clear_rows()
        self._root._row.children = self._top
        self._columns = []
        for v in self._views:
            v._top = self._top
            v._by_id = self._by_id
            v._set(selection=[], currentId=0)
        self.modelReset.emit()

    def item(self, r, c=0):
        if 0 <= r < len(self._top):
            return QStandardItem._of(self._top[r], c)
        return None

    def setItem(self, r, c, item=None):
        self._root.setChild(r, c, item)

    def invisibleRootItem(self):
        return self._root

    def itemFromIndex(self, index):
        if not index.isValid():
            return None
        return QStandardItem._of(index._row, index.column())

    def indexFromItem(self, item):
        return item.index()

    def index(self, row, col=0, parent=QModelIndex()):
        rows = parent._row.children if parent.isValid() else self._top
        if 0 <= row < len(rows):
            return QModelIndex(rows[row], col, self)
        return QModelIndex()

    def parent(self, index=None):
        if index is None:
            return None
        return index.parent()

    def data(self, index, role=qtdata.Qt.DisplayRole):
        return index.data(role) if index.isValid() else None

    def setData(self, index, value, role=qtdata.Qt.EditRole):
        if not index.isValid():
            return False
        QStandardItem._of(index._row, index.column())._set_data(0, role, value)
        return True

    def flags(self, index):
        return index.flags()

    def setHorizontalHeaderLabels(self, labels):
        self._columns = [str(x) for x in labels]
        for v in self._views:
            v._set(columns=list(self._columns),
                   columnCount=max(self._column_count, len(self._columns)))

    def setHorizontalHeaderItem(self, c, item):
        labels = list(self._columns)
        while len(labels) <= c:
            labels.append("")
        labels[c] = item.text()
        self.setHorizontalHeaderLabels(labels)

    def horizontalHeaderItem(self, c):
        return QStandardItem(self._columns[c]) if 0 <= c < len(self._columns) else None

    def headerData(self, section, orientation, role=qtdata.Qt.DisplayRole):
        if orientation == qtdata.Qt.Horizontal and 0 <= section < len(self._columns):
            return self._columns[section]
        return None

    def setHeaderData(self, section, orientation, value, role=qtdata.Qt.EditRole):
        if orientation == qtdata.Qt.Horizontal:
            labels = list(self._columns)
            while len(labels) <= section:
                labels.append("")
            labels[section] = str(value)
            self.setHorizontalHeaderLabels(labels)
        return True

    def setVerticalHeaderLabels(self, labels):
        for v in self._views:
            v._set(rowLabels=[str(x) for x in labels])

    def findItems(self, text, flags=qtdata.Qt.MatchExactly, column=0):
        out = []
        recursive = bool(flags & qtdata.Qt.MatchRecursive)
        rows = self._all_rows() if recursive else self._top
        for r in rows:
            t = r.cell(column)["text"]
            mode = flags & 15
            if (mode == qtdata.Qt.MatchContains and text in t) \
                    or (mode == qtdata.Qt.MatchStartsWith and t.startswith(text)) \
                    or (mode == qtdata.Qt.MatchEndsWith and t.endswith(text)) \
                    or (mode == qtdata.Qt.MatchExactly and t == text):
                out.append(QStandardItem._of(r, column))
        return out

    def sort(self, col, order=qtdata.Qt.AscendingOrder):
        self._item_op({"item": "sort", "id": 0, "col": col, "order": int(order)})

    def setSortRole(self, role):
        pass

    def setItemPrototype(self, item):
        pass

    def beginResetModel(self):
        pass

    def endResetModel(self):
        self.modelReset.emit()


class QTreeView(QAbstractItemView):
    """A view over a QStandardItemModel (index-based signals)."""

    _model_name = Unicode("QTreeViewModel").tag(sync=True)
    qt_class = "QTreeView"

    clicked = Signal(object)
    doubleClicked = Signal(object)
    activated = Signal(object)
    pressed = Signal(object)
    entered = Signal(object)
    expanded = Signal(object)
    collapsed = Signal(object)

    def _fcx_init_args(self, args, kwargs):
        QAbstractItemView._fcx_init_args(self, args, kwargs)
        self._model = None

    def setModel(self, model):
        if self._model is not None:
            self._model._detach_view(self)
        self._model = model
        if model is not None:
            model._attach_view(self)

    def model(self):
        return self._model

    def _index_model(self):
        return self._model if self._model is not None else self

    def _item_at(self, row, col):
        return QStandardItem._of(row, col)

    def _edited(self, row, col):
        if self._model is not None:
            self._model._cell_changed(row, col)

    def _idx(self, rid, col=0):
        row = self._row_of(rid)
        return QModelIndex(row, col, self._index_model()) if row is not None else QModelIndex()

    def _fcx_on_itemClicked(self, rid, col):
        self.clicked.emit(self._idx(rid, col))

    def _fcx_on_itemDoubleClicked(self, rid, col):
        self.doubleClicked.emit(self._idx(rid, col))

    def _fcx_on_itemActivated(self, rid, col):
        self.activated.emit(self._idx(rid, col))

    def _fcx_on_itemPressed(self, rid, col):
        self.pressed.emit(self._idx(rid, col))

    def _fcx_on_itemExpanded(self, rid):
        row = self._row_of(rid)
        if row is not None:
            row.expanded = True
            self.expanded.emit(self._idx(rid))

    def _fcx_on_itemCollapsed(self, rid):
        row = self._row_of(rid)
        if row is not None:
            row.expanded = False
            self.collapsed.emit(self._idx(rid))

    def setFirstColumnSpanned(self, row, parent, on):
        rows = parent._row.children if parent.isValid() else self._top
        if 0 <= row < len(rows):
            rows[row].op({"item": "row", "id": rows[row].id, "row": {"spanned": bool(on)}})

    def isFirstColumnSpanned(self, row, parent):
        return False

    def reset(self):
        pass

    def clear(self):
        if self._model is not None:
            self._model.clear()


class QListView(QTreeView):
    _model_name = Unicode("QListViewModel").tag(sync=True)
    qt_class = "QListView"
    q_rootIsDecorated = Bool(False).tag(sync=True)


class QTableView(QTreeView):
    _model_name = Unicode("QTableViewModel").tag(sync=True)
    qt_class = "QTableView"
    q_rootIsDecorated = Bool(False).tag(sync=True)


class QColumnView(QTreeView):
    qt_class = "QColumnView"


__all__ = [
    "QTreeWidgetItem", "QListWidgetItem", "QTableWidgetItem", "QStandardItem",
    "QStandardItemModel", "QModelIndex", "QPersistentModelIndex", "QItemSelection",
    "QItemSelectionModel", "QHeaderView", "QStyledItemDelegate", "QItemDelegate",
    "QAbstractItemDelegate", "QAbstractItemView", "QTreeWidget", "QListWidget",
    "QTableWidget", "QTreeView", "QListView", "QTableView", "QColumnView",
]

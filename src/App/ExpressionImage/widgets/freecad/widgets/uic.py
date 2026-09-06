# SPDX-License-Identifier: LGPL-2.1-or-later
"""`loadUi` for the sandbox guest: a `.ui` file parsed into models.

The file's text comes from the host (`gui.ui.read`: the Qt resource
system or a file under the module roots -- data, not code).  Every
`<widget class="C" name="N">` becomes the model of class C (an unknown
class a QWidget), its `<property>`s the model's initial state, written
SILENTLY (not `_touched`) so the host applies them from the file itself
(uic, translated) and only what the guest sets afterwards.  The root is a
`UiForm` carrying the file and the named widgets, each an attribute of
the root as uic makes them (docs/Sandbox.md 7.11).
"""

import xml.etree.ElementTree as ET

from . import models
from .models import PREFIX


def read_ui(path):
    """The .ui text through the host."""
    import _fcx

    return _fcx.op("gui.ui.read", 0, str(path))


def loadUi(path, base=None):
    """`FreeCADGui.PySideUic.loadUi(path[, baseinstance])`."""
    text = read_ui(path)
    return parse_ui(text, path, base)


def parse_ui(text, path="", base=None):
    root = ET.fromstring(text)
    top = root.find("widget")
    if top is None:
        raise ValueError("loadUi(%r): no top-level widget" % (path,))
    if base is not None:
        raise TypeError("loadUi(path, baseinstance) is not in the sandbox's subset")
    form = models.UiForm(qtClass=top.get("class", "QWidget"), uiFile=str(path))
    named = {}
    _apply_properties(form, top)
    _build_children(top, form, named)
    form.widgets = {name: w for name, w in named.items()}
    return form


def _build_children(elem, widget, named):
    """The widgets and layouts directly under `elem`, onto `widget`."""
    for child in elem:
        if child.tag == "widget":
            _build(child, widget, named)
        elif child.tag == "layout":
            widget.setLayout(_build_layout(child, widget, named))


def _build_layout(elem, widget, named):
    cls = models.LAYOUTS.get(elem.get("class", ""), models.QVBoxLayout)
    layout = cls()
    layout.setObjectName(elem.get("name", ""))
    for item in elem:
        if item.tag != "item":
            continue
        pos = _item_position(item)
        for sub in item:
            if sub.tag == "widget":
                w = _build(sub, widget, named)
                layout._items.append(models.QLayoutItem(widget=w))
                layout._positions.append(pos)
            elif sub.tag == "layout":
                lay = _build_layout(sub, widget, named)
                lay._parent_layout = layout
                layout._items.append(models.QLayoutItem(layout=lay))
                layout._positions.append(pos)
            elif sub.tag == "spacer":
                layout._items.append(models.QLayoutItem(spacer=models.QSpacerItem()))
                layout._positions.append(pos)
    return layout


def _item_position(item):
    """A grid item's (row, column[, rowspan, colspan]): a span given
    without the other means the other is 1, as uic reads it."""
    if item.get("row") is None or item.get("column") is None:
        return ()
    pos = [int(item.get("row")), int(item.get("column"))]
    if item.get("rowspan") is not None or item.get("colspan") is not None:
        pos.append(int(item.get("rowspan") or 1))
        pos.append(int(item.get("colspan") or 1))
    return tuple(pos)


def _child_widgets(elem):
    """The `<widget>` elements directly under `elem` or under its
    layouts, in document order (layouts nest; spacers are skipped)."""
    for child in elem:
        if child.tag == "widget":
            yield child
        elif child.tag == "layout":
            for item in child:
                if item.tag == "item":
                    for w in _child_widgets(item):
                        yield w
                elif item.tag == "widget":
                    yield item
        elif child.tag == "item":
            for w in _child_widgets(child):
                yield w


def _build(elem, parent, named):
    cls = elem.get("class", "QWidget")
    name = elem.get("name", "")
    props = _properties(elem)
    props["objectName"] = name
    items = _items(elem)
    widget = models.make(cls, parent)
    _set_initial(widget, props, items)
    if name:
        named[name] = widget
        root = parent
        while root._parent is not None:
            root = root._parent
        setattr(root, name, widget)
    _build_children(elem, widget, named)
    return widget


def _apply_properties(widget, elem):
    _set_initial(widget, _properties(elem), _items(elem))


def _set_initial(widget, props, items):
    """The file's values as the model's initial state, written straight
    (a setter would mark them touched; uic gives the host these)."""
    with widget.hold_sync():
        for name, value in props.items():
            trait = PREFIX + name
            if not widget.has_trait(trait):
                continue
            try:
                setattr(widget, trait, value)
            except Exception:
                continue
        if items and isinstance(widget, models.QComboBox):
            widget._item_data = [None] * len(items)
            widget.q_items = [t for t, _ in items]
            widget.q_itemIcons = [i for _, i in items]
            if widget.q_currentIndex < 0:
                widget.q_currentIndex = 0
        widget._touched = []


# a `<size>` property as the two keys it stands for
_SIZES = {
    "minimumSize": ("minimumWidth", "minimumHeight"),
    "maximumSize": ("maximumWidth", "maximumHeight"),
}


def _properties(elem):
    out = {}
    for prop in elem.findall("property"):
        name = prop.get("name")
        value = _value(prop)
        if name is None or value is None:
            continue
        if isinstance(value, dict):
            keys = _SIZES.get(name)
            if keys:
                out[keys[0]] = value["width"]
                out[keys[1]] = value["height"]
            continue
        out[name] = value
    return out


def _items(elem):
    """A combo's `<item>` entries: (text, icon path)."""
    out = []
    for item in elem.findall("item"):
        props = _properties(item)
        out.append((str(props.get("text", "")), str(props.get("icon", ""))))
    return out


def _value(prop):
    """A property's value: the one child element, typed."""
    for v in prop:
        tag = v.tag
        text = (v.text or "").strip()
        if tag == "string":
            return v.text or ""
        if tag == "cstring":
            return text
        if tag == "bool":
            return text == "true"
        if tag == "number":
            try:
                return int(text)
            except ValueError:
                return float(text)
        if tag == "double":
            return float(text)
        if tag == "enum":
            return _enum(text)
        if tag == "set":
            return sum(_enum(part.strip()) for part in text.split("|") if part.strip())
        if tag == "iconset":
            norm = v.find("normaloff")
            return (norm.text or "").strip() if norm is not None else text
        if tag == "color":
            r, g, b = (int((v.findtext(k) or "0").strip()) for k in ("red", "green", "blue"))
            a = int((v.findtext("alpha") or "255").strip())
            return [r / 255.0, g / 255.0, b / 255.0, a / 255.0]
        if tag == "size":
            return {"width": int((v.findtext("width") or "0").strip()),
                    "height": int((v.findtext("height") or "0").strip())}
        if tag in ("rect", "font", "sizepolicy", "url", "pixmap", "date", "locale",
                   "stringlist", "char", "cursor", "cursorShape", "brush", "point"):
            return None
        return text or None
    return None


def _enum(text):
    """An enum value the model traits use as an int; others by name."""
    name = text.rsplit("::", 1)[-1].rsplit(".", 1)[-1]
    from . import qtdata

    value = getattr(type(qtdata.Qt), name, None)
    if isinstance(value, int):
        return value
    if name == "Checked":
        return 2
    if name == "Unchecked":
        return 0
    return 0

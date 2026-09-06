#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Project Association                        *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""fwuic: a .ui file as a C++ form of host widget models (docs/Sandbox.md 7.12).

Where uic emits `Ui_X::setupUi(QWidget*)` building Qt widgets, this
emits `Ui_X::setupUi(Gui::Fw::UiForm*)` building the models of
src/Gui/Fw/FwWidgets.h with the file's names, layouts and values --
the values written silently (`setInitial`: the file's, not something
the dialog set), the strings untranslated (the Qt backend binds the
same file through uic and reads its translations back into the bag).
A ported dialog keeps `ui->lengthEdit` as a typed member and its
`connect` calls compile unchanged; the translation unit never includes
QtWidgets.

    fwuic.py TaskPanel_OrthoArray.ui -o ui_TaskPanel_OrthoArray.h \\
        [--ui-path :/ui/TaskPanel_OrthoArray.ui] [--class-name Ui_X]

`--ui-path` is the path the backend loads the same file from at run
time (a Qt resource, by default `:/ui/<basename>`).  The parse rules
are the sandbox guest's (src/App/ExpressionImage/widgets/freecad/
widgets/uic.py), so both sides read a file the same way.
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
QTDATA = os.path.join(HERE, "..", "App", "ExpressionImage", "widgets", "freecad", "widgets")

# Qt class name -> Fw class (the model); a `Gui::Pref*` is its base class.
# Kept in step with the factory table in src/Gui/Fw/FwWidgets.cpp.
CLASSES = {
    "QWidget": "Widget",
    "QLabel": "QLabel",
    "QPushButton": "QPushButton",
    "QToolButton": "QToolButton",
    "QCheckBox": "QCheckBox",
    "QRadioButton": "QRadioButton",
    "QGroupBox": "QGroupBox",
    "QFrame": "QFrame",
    "QLineEdit": "QLineEdit",
    "QTextEdit": "QTextEdit",
    "QPlainTextEdit": "QPlainTextEdit",
    "QTextBrowser": "QTextBrowser",
    "QSpinBox": "QSpinBox",
    "QDoubleSpinBox": "QDoubleSpinBox",
    "QSlider": "QSlider",
    "QProgressBar": "QProgressBar",
    "QComboBox": "QComboBox",
    "QFontComboBox": "QFontComboBox",
    "Gui::InputField": "InputField",
    "Gui::QuantitySpinBox": "QuantitySpinBox",
    "Gui::PrefQuantitySpinBox": "QuantitySpinBox",
    "Gui::PrefUnitSpinBox": "QuantitySpinBox",
    "Gui::ColorButton": "ColorButton",
    "Gui::PrefColorButton": "ColorButton",
    "Gui::PrefCheckBox": "QCheckBox",
    "Gui::PrefRadioButton": "QRadioButton",
    "Gui::PrefLineEdit": "QLineEdit",
    "Gui::PrefTextEdit": "QTextEdit",
    "Gui::PrefComboBox": "QComboBox",
    "Gui::PrefSpinBox": "QSpinBox",
    "Gui::PrefDoubleSpinBox": "QDoubleSpinBox",
    "Gui::PrefSlider": "QSlider",
    "Gui::PrefCheckableGroupBox": "QGroupBox",
    "Gui::PrefFontBox": "QFontComboBox",
}

# a .ui property whose bag key differs
RENAMED = {"quantity": "rawValue"}

LAYOUTS = ("QVBoxLayout", "QHBoxLayout", "QGridLayout", "QFormLayout", "QBoxLayout")


def _qt_enums():
    sys.path.insert(0, QTDATA)
    try:
        import qtdata  # the guest's Qt namespace values, no imports of its own

        return type(qtdata.Qt)
    finally:
        sys.path.pop(0)


QT = _qt_enums()


def enum_value(text):
    name = text.rsplit("::", 1)[-1].rsplit(".", 1)[-1]
    value = getattr(QT, name, None)
    if isinstance(value, int):
        return value
    return {"Checked": 2, "Unchecked": 0}.get(name, 0)


def value_of(prop):
    """A <property>'s value as a Python value, or None when not carried."""
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
            return enum_value(text)
        if tag == "set":
            return sum(enum_value(p.strip()) for p in text.split("|") if p.strip())
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
        return None
    return None


# a `<size>` property as the two bag keys it stands for
SIZES = {"minimumSize": ("minimumWidth", "minimumHeight"),
         "maximumSize": ("maximumWidth", "maximumHeight")}


def cpp_string(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif 32 <= o < 127:
            out.append(ch)
        else:
            out.append("\\u%04x" % o if o <= 0xFFFF else "\\U%08x" % o)
    return 'QStringLiteral("%s")' % "".join(out)


def cpp_value(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, str):
        return cpp_string(value)
    if isinstance(value, list):
        return "QVariantList {%s}" % ", ".join(cpp_value(v) for v in value)
    raise TypeError(value)


class Emitter:
    def __init__(self, ui_path):
        self.ui_path = ui_path
        self.members = []  # (type, name)
        self.body = []
        self.names = set()
        self.counter = 0

    def unique(self, name):
        base = name or "widget"
        base = re.sub(r"\W", "_", base)
        if base not in self.names:
            self.names.add(base)
            return base
        i = 2
        while "%s_%d" % (base, i) in self.names:
            i += 1
        out = "%s_%d" % (base, i)
        self.names.add(out)
        return out

    def line(self, s):
        self.body.append("        " + s)

    def properties(self, var, elem):
        for prop in elem.findall("property"):
            name = prop.get("name")
            value = value_of(prop)
            if name is None or value is None or name == "objectName":
                continue
            if isinstance(value, dict):
                keys = SIZES.get(name)
                if keys:
                    self.line("%s->setInitial(%s, %d);"
                              % (var, cpp_string(keys[0]), value["width"]))
                    self.line("%s->setInitial(%s, %d);"
                              % (var, cpp_string(keys[1]), value["height"]))
                continue
            name = RENAMED.get(name, name)
            self.line("%s->setInitial(%s, %s);" % (var, cpp_string(name), cpp_value(value)))

    def widget(self, elem, parent_var, form_var, top=False):
        cls = elem.get("class", "QWidget")
        name = elem.get("name", "")
        if top:
            var = form_var
            self.line("%s->setQtClass(%s);" % (var, cpp_string(cls)))
        else:
            fw = CLASSES.get(cls)
            var = self.unique(name)
            if fw is None:
                self.members.append(("Gui::Fw::Widget*", var))
                self.line("%s = Gui::Fw::createWidget(%s, %s);"
                          % (var, cpp_string(cls), parent_var))
            elif cls.startswith("Gui::") and cls != "Gui::InputField" \
                    and cls != "Gui::QuantitySpinBox" and cls != "Gui::ColorButton":
                self.members.append(("Gui::Fw::%s*" % fw, var))
                self.line("%s = static_cast<Gui::Fw::%s*>(Gui::Fw::createWidget(%s, %s));"
                          % (var, fw, cpp_string(cls), parent_var))
            else:
                self.members.append(("Gui::Fw::%s*" % fw, var))
                self.line("%s = new Gui::Fw::%s(%s);" % (var, fw, parent_var))
        if name:
            self.line("%s->setInitial(QStringLiteral(\"objectName\"), %s);"
                      % (var, cpp_string(name)))
            if not top:
                self.line("%s->addNamed(%s, %s);" % (form_var, cpp_string(name), var))
        self.properties(var, elem)
        items = self.items(elem)
        if items and cls in ("QComboBox", "QFontComboBox", "Gui::PrefComboBox", "Gui::PrefFontBox"):
            texts = [cpp_string(t) for t, _ in items]
            icons = [cpp_string(i) for _, i in items]
            self.line("%s->setInitial(QStringLiteral(\"items\"), QStringList {%s});"
                      % (var, ", ".join(texts)))
            if any(i for _, i in items):
                self.line("%s->setInitial(QStringLiteral(\"itemIcons\"), QStringList {%s});"
                          % (var, ", ".join(icons)))
            self.line("%s->setInitial(QStringLiteral(\"currentIndex\"), 0);" % var)
        self.children(elem, var, form_var)
        return var

    @staticmethod
    def items(elem):
        out = []
        for item in elem.findall("item"):
            props = {p.get("name"): value_of(p) for p in item.findall("property")}
            out.append((str(props.get("text") or ""), str(props.get("icon") or "")))
        return out

    def children(self, elem, var, form_var):
        for child in elem:
            if child.tag == "widget":
                self.widget(child, var, form_var)
            elif child.tag == "layout":
                # the owner argument sets it as the widget's layout
                self.layout(child, var, form_var, owner=var)

    def layout(self, elem, widget_var, form_var, owner=None):
        cls = elem.get("class", "QVBoxLayout")
        if cls not in LAYOUTS:
            cls = "QVBoxLayout"
        name = elem.get("name", "")
        var = self.unique(name or "layout")
        self.members.append(("Gui::Fw::Layout*", var))
        self.line("%s = Gui::Fw::createLayout(%s, %s);"
                  % (var, cpp_string(cls), owner if owner else "nullptr"))
        if name:
            self.line("%s->setObjectName(%s);" % (var, cpp_string(name)))
        for prop in elem.findall("property"):
            pname = prop.get("name")
            value = value_of(prop)
            if pname == "spacing" and isinstance(value, int):
                self.line("%s->setSpacing(%d);" % (var, value))
        margins = {}
        for prop in elem.findall("property"):
            pname = prop.get("name")
            value = value_of(prop)
            if pname in ("leftMargin", "topMargin", "rightMargin", "bottomMargin") \
                    and isinstance(value, int):
                margins[pname] = value
        if margins:
            self.line("%s->setContentsMargins(%d, %d, %d, %d);" % (
                var, margins.get("leftMargin", 0), margins.get("topMargin", 0),
                margins.get("rightMargin", 0), margins.get("bottomMargin", 0)))
        for item in elem:
            if item.tag != "item":
                continue
            pos = self.position(item)
            for sub in item:
                if sub.tag == "widget":
                    w = self.widget(sub, widget_var, form_var)
                    self.line("%s->addWidget(%s%s);" % (var, w, self.pos_args(pos)))
                elif sub.tag == "layout":
                    lay = self.layout(sub, widget_var, form_var)
                    self.line("%s->addLayout(%s%s);" % (var, lay, self.pos_args(pos)))
                elif sub.tag == "spacer":
                    w, h = 0, 0
                    for prop in sub.findall("property"):
                        if prop.get("name") == "sizeHint":
                            size = prop.find("size")
                            if size is not None:
                                w = int((size.findtext("width") or "0").strip())
                                h = int((size.findtext("height") or "0").strip())
                    self.line("%s->addSpacer(%d, %d%s);" % (var, w, h, self.pos_args(pos)))
        return var

    @staticmethod
    def position(item):
        """A grid item's [row, column[, rowspan, colspan]]: a span given
        without the other means the other is 1, as uic reads it."""
        if item.get("row") is None or item.get("column") is None:
            return []
        pos = [int(item.get("row")), int(item.get("column"))]
        if item.get("rowspan") is not None or item.get("colspan") is not None:
            pos.append(int(item.get("rowspan") or 1))
            pos.append(int(item.get("colspan") or 1))
        return pos

    @staticmethod
    def pos_args(pos):
        if not pos:
            return ""
        return ", QVariantList {%s}" % ", ".join(str(p) for p in pos)


def generate(source, ui_path, class_name=None):
    root = ET.fromstring(source)
    top = root.find("widget")
    if top is None:
        raise ValueError("no top-level widget")
    # uic names the class from <class>: `Gui::TaskOrientation` becomes
    # `Gui::Ui_TaskOrientation`, plus `Gui::Ui::TaskOrientation`
    full = (root.findtext("class") or top.get("name", "Form")).strip()
    parts = [p for p in full.split("::") if p]
    namespaces, short = parts[:-1], parts[-1] if parts else "Form"
    if class_name:
        namespaces = []
    else:
        class_name = "Ui_" + re.sub(r"\W", "_", short)
    em = Emitter(ui_path)
    em.widget(top, None, "form", top=True)
    guard = "FWUIC_%s_H" % re.sub(r"\W", "_", "_".join(namespaces + [class_name])).upper()
    out = []
    out.append("// Generated by src/Tools/fwuic.py from %s -- do not edit."
               % os.path.basename(ui_path))
    out.append("// The form as host widget models (docs/Sandbox.md 7.12).")
    out.append("#ifndef %s" % guard)
    out.append("#define %s" % guard)
    out.append("")
    out.append("#include <QStringList>")
    out.append("#include <QVariantList>")
    out.append("#include <Gui/Fw/FwWidgets.h>")
    out.append("")
    for ns in namespaces:
        out.append("namespace %s {" % ns)
    if namespaces:
        out.append("")
    out.append("class %s" % class_name)
    out.append("{")
    out.append("public:")
    for typ, name in em.members:
        out.append("    %s %s = nullptr;" % (typ, name))
    out.append("")
    out.append("    void setupUi(Gui::Fw::UiForm* form)")
    out.append("    {")
    out.append("        form->setUiFile(%s);" % cpp_string(ui_path))
    out.extend(em.body)
    out.append("    }")
    out.append("};")
    if class_name.startswith("Ui_"):
        out.append("")
        out.append("namespace Ui {")
        out.append("class %s : public %s {};" % (class_name[3:], class_name))
        out.append("}  // namespace Ui")
    if namespaces:
        out.append("")
        for ns in reversed(namespaces):
            out.append("}  // namespace %s" % ns)
    out.append("")
    out.append("#endif  // %s" % guard)
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("ui", help="the .ui file")
    ap.add_argument("-o", "--output", help="the header to write (default: stdout)")
    ap.add_argument("--ui-path", help="the path the backend loads the file from at run time"
                    " (default :/ui/<basename>)")
    ap.add_argument("--class-name", help="the class name (default Ui_<top widget name>)")
    args = ap.parse_args(argv)
    with open(args.ui, encoding="utf-8") as f:
        source = f.read()
    ui_path = args.ui_path or ":/ui/" + os.path.basename(args.ui)
    text = generate(source, ui_path, args.class_name)
    if args.output:
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())

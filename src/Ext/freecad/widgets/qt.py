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

"""The Qt widget manager: views for the widget models of freecad.widgets.

One `View` per (model, rendering): it builds a QWidget from the model's
state, applies state diffs the guest sends (`update`), and reports what
the user does as state the guest's model follows (`send`) or as a
custom event (a button's click).  A view applying the guest's state
sets `applying`, so the Qt signals that fires do not echo it back.
Model names map to view classes in VIEWS; a name not there renders as a
labeled placeholder, never as nothing (docs/Sandbox.md 7.3).  The core
ipywidgets models covered: the sliders and number inputs, Text and
Textarea, Button, Checkbox and ToggleButton, Dropdown, RadioButtons and
Select, Label and HTML, the boxes; Layout and the style models are data
(a few Layout keys are honored).
"""

import FreeCAD
from PySide import QtCore, QtGui, QtWidgets

from . import MODEL_REF

Qt = QtCore.Qt


def _alive(widget):
    """Whether the C++ object behind a wrapper still exists (a task
    dialog deletes its forms; a form's children go with it)."""
    try:
        import shiboken6

        return shiboken6.isValid(widget)
    except ImportError:
        return True


class View:
    """A rendering of one model."""

    def __init__(self, manager, model, parent=None, widget=None):
        self.manager = manager
        self.model = model
        self.applying = False
        self.bound = widget is not None
        self.widget = widget if widget is not None else self.build(parent)
        self.bind()
        model.views.append(self)
        self.applying = True
        try:
            self.update(self.initial_keys())
        finally:
            self.applying = False

    # -- what a subclass fills in

    def build(self, parent):
        raise NotImplementedError

    def bind(self):
        """Connect the widget's signals (built or adopted)."""

    def initial_keys(self):
        """The state keys applied at construction: all of them."""
        return set(self.model.state)

    def apply(self, keys):
        """Apply the state under `keys` to the widget."""

    def custom(self, content, buffers):
        """A custom message from the guest's model."""

    # -- the protocol side

    def update(self, keys):
        was = self.applying
        self.applying = True
        try:
            self.apply(keys)
            if "layout" in keys or "_fcx_layout" in keys:
                self.apply_layout()
        finally:
            self.applying = was

    def send(self, **state):
        if not self.applying:
            self.manager.send_update(self.model, state)

    def send_custom(self, content, buffers=None):
        if not self.applying:
            self.manager.send_custom(self.model, content, buffers)

    def close(self, delete=True):
        """Detach from the model; `delete` the widget too, unless a
        container that owns it (a task dialog) is about to."""
        if self in self.model.views:
            self.model.views.remove(self)
        widget, self.widget = self.widget, None
        if widget is not None and delete and _alive(widget):
            widget.setParent(None)
            widget.deleteLater()

    def get(self, key, default=None):
        return self.model.state.get(key, default)

    def layout_state(self):
        layout = self.manager.resolve(self.get("layout"))
        return layout.state if layout is not None else {}

    def apply_layout(self):
        """The few Layout keys a native widget honors: px sizes and
        visibility."""
        st = self.layout_state()
        w = self.widget
        for key, setter in (("width", w.setFixedWidth), ("height", w.setFixedHeight)):
            value = st.get(key)
            if isinstance(value, str) and value.endswith("px"):
                try:
                    setter(int(float(value[:-2])))
                except ValueError:
                    pass
        hidden = st.get("display") == "none" or st.get("visibility") == "hidden"
        w.setVisible(not hidden)


class Placeholder(View):
    """A model name no view exists for: labeled, so the gap is visible."""

    def build(self, parent):
        label = QtWidgets.QLabel(parent)
        label.setFrameShape(QtWidgets.QFrame.StyledPanel)
        return label

    def apply(self, keys):
        self.widget.setText("[%s]" % (self.get("_model_name") or "widget"))
        self.widget.setToolTip("no native view for this widget model")


class Described(View):
    """A control with ipywidgets' description label in front of it."""

    def build(self, parent):
        box = QtWidgets.QWidget(parent)
        row = QtWidgets.QHBoxLayout(box)
        row.setContentsMargins(0, 0, 0, 0)
        self.label = QtWidgets.QLabel(box)
        row.addWidget(self.label)
        self.control = self.build_control(box)
        row.addWidget(self.control, 1)
        return box

    def build_control(self, parent):
        raise NotImplementedError

    def apply(self, keys):
        if "description" in keys:
            text = self.get("description", "")
            self.label.setText(text)
            self.label.setVisible(bool(text))
        if "tooltip" in keys or "description_tooltip" in keys:
            self.control.setToolTip(self.get("tooltip") or self.get("description_tooltip") or "")
        if "disabled" in keys:
            self.control.setEnabled(not self.get("disabled", False))


class SliderBase(Described):
    """IntSlider and FloatSlider: a QSlider with an optional readout."""

    is_float = False

    def build_control(self, parent):
        box = QtWidgets.QWidget(parent)
        row = QtWidgets.QHBoxLayout(box)
        row.setContentsMargins(0, 0, 0, 0)
        self.slider = QtWidgets.QSlider(Qt.Horizontal, box)
        self.readout = QtWidgets.QLabel(box)
        row.addWidget(self.slider, 1)
        row.addWidget(self.readout)
        self.slider.valueChanged.connect(self._changed)
        self.slider.sliderReleased.connect(self._released)
        return box

    def _step(self):
        step = self.get("step")
        if not step:
            step = 0.1 if self.is_float else 1
        return step

    def _to_slider(self, value):
        return int(round((value - self.get("min", 0)) / self._step()))

    def _from_slider(self, pos):
        value = self.get("min", 0) + pos * self._step()
        return value if self.is_float else int(round(value))

    def apply(self, keys):
        Described.apply(self, keys)
        if keys & {"min", "max", "step"}:
            self.slider.setRange(0, self._to_slider(self.get("max", 100)))
        if keys & {"value", "min", "max", "step"}:
            self.slider.setValue(self._to_slider(self.get("value", 0)))
        if "orientation" in keys:
            self.slider.setOrientation(
                Qt.Vertical if self.get("orientation") == "vertical" else Qt.Horizontal)
        if keys & {"readout", "value", "readout_format"}:
            self.readout.setVisible(self.get("readout", True))
            self._show_readout(self.get("value", 0))

    def _show_readout(self, value):
        fmt = self.get("readout_format") or (".2f" if self.is_float else "d")
        try:
            self.readout.setText(format(value, fmt))
        except (ValueError, TypeError):
            self.readout.setText(str(value))

    def _changed(self, pos):
        value = self._from_slider(pos)
        self._show_readout(value)
        if self.applying:
            return
        if self.get("continuous_update", True) or not self.slider.isSliderDown():
            self.send(value=value)

    def _released(self):
        if not self.get("continuous_update", True):
            self.send(value=self._from_slider(self.slider.value()))


class IntSliderView(SliderBase):
    pass


class FloatSliderView(SliderBase):
    is_float = True


class IntTextView(Described):
    bounded = False

    def build_control(self, parent):
        spin = QtWidgets.QSpinBox(parent)
        spin.setRange(-(2**31), 2**31 - 1)
        spin.valueChanged.connect(self._changed)
        spin.editingFinished.connect(self._finished)
        return spin

    def apply(self, keys):
        Described.apply(self, keys)
        if self.bounded and keys & {"min", "max"}:
            self.control.setRange(self.get("min", 0), self.get("max", 100))
        if "step" in keys:
            self.control.setSingleStep(self.get("step") or 1)
        if "value" in keys:
            self.control.setValue(self.get("value", 0))

    def _changed(self, value):
        if self.get("continuous_update", False):
            self.send(value=value)

    def _finished(self):
        if not self.get("continuous_update", False):
            self.send(value=self.control.value())


class BoundedIntTextView(IntTextView):
    bounded = True


class FloatTextView(Described):
    bounded = False

    def build_control(self, parent):
        spin = QtWidgets.QDoubleSpinBox(parent)
        spin.setRange(-1e18, 1e18)
        spin.setDecimals(3)
        spin.valueChanged.connect(self._changed)
        spin.editingFinished.connect(self._finished)
        return spin

    def apply(self, keys):
        Described.apply(self, keys)
        if self.bounded and keys & {"min", "max"}:
            self.control.setRange(self.get("min", 0.0), self.get("max", 100.0))
        if "step" in keys:
            step = self.get("step")
            if step:
                self.control.setSingleStep(step)
                digits = 0
                while digits < 10 and abs(round(step, digits) - step) > 1e-12:
                    digits += 1
                self.control.setDecimals(max(digits, 1))
        if "value" in keys:
            self.control.setValue(float(self.get("value", 0.0)))

    def _changed(self, value):
        if self.get("continuous_update", False):
            self.send(value=value)

    def _finished(self):
        if not self.get("continuous_update", False):
            self.send(value=self.control.value())


class BoundedFloatTextView(FloatTextView):
    bounded = True


class TextView(Described):
    def build_control(self, parent):
        edit = QtWidgets.QLineEdit(parent)
        edit.textEdited.connect(self._edited)
        edit.editingFinished.connect(self._finished)
        return edit

    def apply(self, keys):
        Described.apply(self, keys)
        if "value" in keys and self.control.text() != self.get("value", ""):
            self.control.setText(self.get("value", ""))
        if "placeholder" in keys:
            self.control.setPlaceholderText(self.get("placeholder", "").replace("\u200b", ""))

    def _edited(self, text):
        if self.get("continuous_update", True):
            self.send(value=text)

    def _finished(self):
        if not self.get("continuous_update", True):
            self.send(value=self.control.text())
        elif not self.applying:
            self.send_custom({"event": "submit"})


class TextareaView(Described):
    def build_control(self, parent):
        edit = QtWidgets.QPlainTextEdit(parent)
        edit.textChanged.connect(self._changed)
        return edit

    def apply(self, keys):
        Described.apply(self, keys)
        if "value" in keys and self.control.toPlainText() != self.get("value", ""):
            self.control.setPlainText(self.get("value", ""))
        if "placeholder" in keys:
            self.control.setPlaceholderText(self.get("placeholder", "").replace("\u200b", ""))
        if "rows" in keys and self.get("rows"):
            fm = self.control.fontMetrics()
            self.control.setFixedHeight(fm.lineSpacing() * int(self.get("rows")) + 12)

    def _changed(self):
        self.send(value=self.control.toPlainText())


class ButtonView(View):
    def build(self, parent):
        button = QtWidgets.QPushButton(parent)
        button.clicked.connect(lambda: self.send_custom({"event": "click"}))
        return button

    def apply(self, keys):
        if "description" in keys:
            self.widget.setText(self.get("description", ""))
        if "tooltip" in keys:
            self.widget.setToolTip(self.get("tooltip") or "")
        if "disabled" in keys:
            self.widget.setEnabled(not self.get("disabled", False))
        if "icon" in keys:
            name = self.get("icon") or ""
            self.widget.setIcon(QtGui.QIcon.fromTheme(name) if name else QtGui.QIcon())


class CheckboxView(View):
    def build(self, parent):
        box = QtWidgets.QCheckBox(parent)
        box.toggled.connect(lambda on: self.send(value=bool(on)))
        return box

    def apply(self, keys):
        if "description" in keys:
            self.widget.setText(self.get("description", ""))
        if "value" in keys:
            self.widget.setChecked(bool(self.get("value", False)))
        if "disabled" in keys:
            self.widget.setEnabled(not self.get("disabled", False))


class ToggleButtonView(View):
    def build(self, parent):
        button = QtWidgets.QPushButton(parent)
        button.setCheckable(True)
        button.toggled.connect(lambda on: self.send(value=bool(on)))
        return button

    def apply(self, keys):
        if "description" in keys:
            self.widget.setText(self.get("description", ""))
        if "value" in keys:
            self.widget.setChecked(bool(self.get("value", False)))
        if "disabled" in keys:
            self.widget.setEnabled(not self.get("disabled", False))
        if "tooltip" in keys:
            self.widget.setToolTip(self.get("tooltip") or "")


class DropdownView(Described):
    def build_control(self, parent):
        combo = QtWidgets.QComboBox(parent)
        combo.currentIndexChanged.connect(self._changed)
        return combo

    def apply(self, keys):
        Described.apply(self, keys)
        if "_options_labels" in keys:
            self.control.clear()
            self.control.addItems([str(x) for x in self.get("_options_labels", [])])
        if keys & {"index", "_options_labels"}:
            index = self.get("index")
            self.control.setCurrentIndex(-1 if index is None else int(index))

    def _changed(self, index):
        self.send(index=None if index < 0 else index)


class RadioButtonsView(Described):
    def build_control(self, parent):
        box = QtWidgets.QWidget(parent)
        self.column = QtWidgets.QVBoxLayout(box)
        self.column.setContentsMargins(0, 0, 0, 0)
        self.group = QtWidgets.QButtonGroup(box)
        self.group.idToggled.connect(self._toggled)
        return box

    def apply(self, keys):
        Described.apply(self, keys)
        if "_options_labels" in keys:
            for button in self.group.buttons():
                self.group.removeButton(button)
                button.deleteLater()
            for i, text in enumerate(self.get("_options_labels", [])):
                button = QtWidgets.QRadioButton(str(text), self.control)
                self.group.addButton(button, i)
                self.column.addWidget(button)
        if keys & {"index", "_options_labels"}:
            index = self.get("index")
            for button in self.group.buttons():
                button.setChecked(index is not None and self.group.id(button) == index)

    def _toggled(self, index, on):
        if on:
            self.send(index=index)


class SelectView(Described):
    def build_control(self, parent):
        lst = QtWidgets.QListWidget(parent)
        lst.currentRowChanged.connect(lambda row: self.send(index=None if row < 0 else row))
        return lst

    def apply(self, keys):
        Described.apply(self, keys)
        if "_options_labels" in keys:
            self.control.clear()
            self.control.addItems([str(x) for x in self.get("_options_labels", [])])
        if keys & {"index", "_options_labels"}:
            index = self.get("index")
            self.control.setCurrentRow(-1 if index is None else int(index))
        if "rows" in keys and self.get("rows"):
            fm = self.control.fontMetrics()
            self.control.setFixedHeight(fm.lineSpacing() * int(self.get("rows")) + 8)


class LabelView(View):
    rich = False

    def build(self, parent):
        label = QtWidgets.QLabel(parent)
        label.setTextFormat(Qt.RichText if self.rich else Qt.PlainText)
        return label

    def apply(self, keys):
        if "value" in keys:
            self.widget.setText(self.get("value", ""))
        if "description" in keys and self.get("description"):
            self.widget.setToolTip(self.get("description"))


class HTMLView(LabelView):
    rich = True


class BoxView(View):
    """VBox, HBox and Box: the children models rendered inside."""

    direction = QtWidgets.QBoxLayout.TopToBottom

    def build(self, parent):
        box = QtWidgets.QWidget(parent)
        self.column = QtWidgets.QBoxLayout(self.direction, box)
        self.column.setContentsMargins(0, 0, 0, 0)
        self.children = []
        return box

    def apply(self, keys):
        if "children" in keys:
            for child in self.children:
                child.close()
            self.children = []
            for ref in self.get("children", []):
                model = self.manager.resolve(ref)
                if model is None:
                    FreeCAD.Console.PrintWarning("freecad.widgets: unknown child %r\n" % (ref,))
                    continue
                view = make_view(self.manager, model, self.widget)
                self.children.append(view)
                self.column.addWidget(view.widget)
            self.column.addStretch(1)

    def close(self, delete=True):
        # the children are the box widget's: one delete for the tree
        for child in self.children:
            child.close(delete=False)
        self.children = []
        View.close(self, delete)


class VBoxView(BoxView):
    pass


class HBoxView(BoxView):
    direction = QtWidgets.QBoxLayout.LeftToRight


VIEWS = {
    "IntSliderModel": IntSliderView,
    "FloatSliderModel": FloatSliderView,
    "IntTextModel": IntTextView,
    "BoundedIntTextModel": BoundedIntTextView,
    "FloatTextModel": FloatTextView,
    "BoundedFloatTextModel": BoundedFloatTextView,
    "TextModel": TextView,
    "TextareaModel": TextareaView,
    "ButtonModel": ButtonView,
    "CheckboxModel": CheckboxView,
    "ToggleButtonModel": ToggleButtonView,
    "DropdownModel": DropdownView,
    "RadioButtonsModel": RadioButtonsView,
    "SelectModel": SelectView,
    "LabelModel": LabelView,
    "HTMLModel": HTMLView,
    "BoxModel": VBoxView,
    "VBoxModel": VBoxView,
    "HBoxModel": HBoxView,
    "GridBoxModel": VBoxView,
}


def make_view(manager, model, parent=None, widget=None):
    """The view of `model`: built under `parent`, or bound to `widget`
    (something else made it)."""
    cls = VIEWS.get(model.name, Placeholder)
    if widget is not None:
        return cls(manager, model, widget=widget)
    return cls(manager, model, parent)


class TaskPanel:
    """The task panel a shown root lives in; OK and Cancel reach the
    guest as custom events on the root model."""

    def __init__(self, manager, model, view, title):
        self.manager = manager
        self.model = model
        self.view = view
        self.form = view.widget
        self.form.setWindowTitle(title or model.get("description") or "Sandbox")

    def accept(self):
        self.manager.send_custom(self.model, {"event": "accept"})
        hide(self.manager, self.model, closing=True)
        return True

    def reject(self):
        self.manager.send_custom(self.model, {"event": "reject"})
        hide(self.manager, self.model, closing=True)
        return True


def show(manager, model, title=None, where="panel"):
    if model.shown is not None:
        return model.shown[1].form if model.shown[0] == "panel" else model.shown[1]
    view = make_view(manager, model)
    if where == "panel":
        import FreeCADGui

        panel = TaskPanel(manager, model, view, title)
        model.shown = ("panel", panel, view)
        FreeCADGui.Control.showDialog(panel)
        return panel.form
    if where == "window":
        view.widget.setWindowTitle(title or model.get("description") or "Sandbox")
        model.shown = ("window", view.widget, view)
        view.widget.show()
        return view.widget
    view.close()
    raise ValueError("showWidget: where must be 'panel' or 'window', not %r" % (where,))


def hide(manager, model, closing=False):
    """Take the shown root down.  A task panel owns its form: closing
    the dialog deletes the widgets (`closing`: the dialog is doing so
    itself, from accept/reject), so the views only detach."""
    shown = model.shown
    if shown is None:
        return
    model.shown = None
    kind, obj, view = shown
    if kind == "panel":
        import FreeCADGui

        if not closing and FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        view.close(delete=False)
    else:
        view.close()


# -- the Qt-shaped models of freecad.widgets ---------------------------------
#
# Rendered here in G3a (docs/Sandbox.md 7.11); since H0 (7.12) they are
# the C++ host widget layer's: src/Gui/SandboxGui.cpp routes a comm of
# the `freecad.widgets` module to Fw::Store, and the C++ Qt view
# (src/Gui/Fw/FwQtView.cpp) renders it.  None of them reaches this
# manager any more; only the plain ipywidgets models above do.


class GuestTaskPanel:
    """The task panel object the host's Control shows for a panel the
    guest built from plain ipywidgets roots (a form of freecad.widgets
    models is the C++ PanelDialog's, src/Gui/Fw/FwQtPanel.cpp): `form`
    is the rendered forms' widgets, and every hook
    TaskDialogPython reads forwards to the guest's panel when it has
    it, else answers as the C++ default would.  When the dialog goes,
    its widgets go with it (TaskDialogPython deletes the forms), so the
    views detach on `destroyed`."""

    STANDARD = 0x00000400 | 0x00400000  # Ok | Cancel, TaskDialog's default

    def __init__(self, manager, standin, models, hooks):
        self.manager = manager
        self.standin = standin
        self.hooks = set(hooks)
        self.views = [make_view(manager, m) for m in models]
        self.form = [v.widget for v in self.views]
        for v in self.views:
            v.widget.destroyed.connect(lambda *a, v=v: v.close(delete=False))

    def _hook(self, name, *args):
        return getattr(self.standin, name)(*args)

    def _detach(self):
        for v in self.views:
            v.close(delete=False)

    def accept(self):
        ok = bool(self._hook("accept")) if "accept" in self.hooks else True
        if ok:
            self._detach()
        return ok

    def reject(self):
        ok = bool(self._hook("reject")) if "reject" in self.hooks else True
        if ok:
            self._detach()
        return ok

    def clicked(self, index):
        if "clicked" in self.hooks:
            self._hook("clicked", int(index))

    def open(self):
        if "open" in self.hooks:
            self._hook("open")

    def helpRequested(self):
        if "helpRequested" in self.hooks:
            self._hook("helpRequested")

    def getStandardButtons(self):
        if "getStandardButtons" in self.hooks:
            return int(self._hook("getStandardButtons"))
        return self.STANDARD

    def needsFullSpace(self):
        return bool(self._hook("needsFullSpace")) if "needsFullSpace" in self.hooks else False

    def shouldShow(self):
        return bool(self._hook("shouldShow")) if "shouldShow" in self.hooks else True

    def isAllowedAlterDocument(self):
        if "isAllowedAlterDocument" in self.hooks:
            return bool(self._hook("isAllowedAlterDocument"))
        return True

    def isAllowedAlterView(self):
        if "isAllowedAlterView" in self.hooks:
            return bool(self._hook("isAllowedAlterView"))
        return True

    def isAllowedAlterSelection(self):
        if "isAllowedAlterSelection" in self.hooks:
            return bool(self._hook("isAllowedAlterSelection"))
        return True

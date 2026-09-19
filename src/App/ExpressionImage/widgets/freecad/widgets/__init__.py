# SPDX-License-Identifier: LGPL-2.1-or-later
"""FreeCAD's widget module for the sandbox guest (docs/Sandbox.md 7.11, G3).

The forms a workbench writes speak Qt: ``QtWidgets.QCheckBox(parent)``,
``self.form.spinbox.setValue(3)``, ``button.clicked.connect(fn)``.  This
package is that subset as ipywidgets MODELS -- one ``ipywidgets.Widget``
subclass per Qt class, ``_model_module = "freecad.widgets"``, whose
synced traits are the Qt properties (``text``, ``checked``, ``value``,
``enabled``, ``visible``, ...) and whose methods are the Qt accessors.
The host (``freecad.widgets`` on the FreeCAD side, the same module name
as the Jupyter convention has it) renders a model as the real Qt
widget of that class and sends what the user does back as state and
events.  Nothing here draws.

``models``   the widget classes and ``Signal``
``qtdata``   the value types (``QColor``, ``QIcon``, the ``Qt`` enums)
``uic``      ``loadUi``: a ``.ui`` file parsed into models
``gui``      ``FreeCADGui.Control``, ``UiLoader`` and ``PySideUic``
"""

__version__ = "0.1"

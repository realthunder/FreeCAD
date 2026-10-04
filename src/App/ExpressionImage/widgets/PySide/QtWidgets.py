# SPDX-License-Identifier: LGPL-2.1-or-later
"""`PySide.QtWidgets` for the sandbox guest: the widget, dialog and item
classes are the models of `freecad.widgets.models`, loaded on the first
class asked for (docs/Sandbox.md 7.11).  `QSizePolicy` is a value type.

`QDialogButtonBox` is a MODEL, not a value type: `qtdata` carries only
its button flags (`QDialogButtonBoxButtons`), so it resolves through the
models below like every other widget class.
"""

from freecad.widgets.qtdata import QSizePolicy  # noqa: F401


def __getattr__(name):
    from freecad.widgets import models

    try:
        return getattr(models, name)
    except AttributeError:
        raise AttributeError("PySide.QtWidgets.%s is not in the sandbox's subset" % name)

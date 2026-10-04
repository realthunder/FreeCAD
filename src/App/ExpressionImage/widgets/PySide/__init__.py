# SPDX-License-Identifier: LGPL-2.1-or-later
"""`PySide` as the sandbox guest sees it (docs/Sandbox.md 7.3, 7.11).

The guest's forms ARE the models of `freecad.widgets`: a guest
`PySide.QtWidgets.QLabel` IS `freecad.widgets.models.QLabel`, and the
value types (`QColor`, `QIcon`, the `Qt` enums) are
`freecad.widgets.qtdata`.  This package is only that spelling -- the
module name Qt-shaped code reaches the layer through -- plus the guest
half of the host-timer protocol in `QtCore` (7.15), which has no other
home: the image's prelude drains it by name
(`sys.modules['PySide.QtCore']._drain`).

Nothing here draws, and nothing here is Qt: the rule of sec 7 stands --
the guest sees FreeCAD's API, and this is the widget layer wearing Qt's
names.  What is deliberately absent is the COMPATIBILITY subset U7
carried for installed workbench code (translate, `QLocale`, `Slot`,
`QUrl`, `QDesktopServices`, ...): U7 is retired with Proxy routing
(ruling 2026-09-18, docs/Sandbox.md 7.31), because installed workbench
code no longer runs in the guest.
"""

__all__ = ["QtCore", "QtGui", "QtWidgets"]

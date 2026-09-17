# SPDX-License-Identifier: LGPL-2.1-or-later
"""`PySide.QtGui` for the sandbox guest: the value types (`QColor`,
`QIcon`, `QPixmap`, `QFont`) are data from `freecad.widgets.qtdata`, and
a widget or item class reached through `QtGui` is the model of
`freecad.widgets.models` (docs/Sandbox.md 7.11).  Nothing is drawn here.
"""

from freecad.widgets.qtdata import (  # noqa: F401
    QColor, QIcon, QPixmap, QFont, QFontMetrics, QFontMetricsF, QImage, QPainter, QPen,
    QBrush, QKeyEvent, QMouseEvent, QFocusEvent, QCursor, QKeySequence,
)


def __getattr__(name):
    from freecad.widgets import models

    try:
        return getattr(models, name)
    except AttributeError:
        raise AttributeError("PySide.QtGui.%s is not in the sandbox's subset" % name)

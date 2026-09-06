"""PySide.QtGui for the sandbox guest: the value types (QColor, QIcon,
QPixmap, QFont) are data, and the widget classes old PySide code
reaches through QtGui are the models of freecad.widgets
(docs/Sandbox.md 7.11).  Nothing is drawn here."""

from freecad.widgets.qtdata import (  # noqa: F401
    QColor, QIcon, QPixmap, QFont, QFontMetrics, QFontMetricsF, QImage, QPainter, QPen,
    QBrush, QKeyEvent, QMouseEvent, QFocusEvent, QCursor, QKeySequence,
)


def qAlpha(rgb):
    return (rgb >> 24) & 255


def qRed(rgb):
    return (rgb >> 16) & 255


def qGreen(rgb):
    return (rgb >> 8) & 255


def qBlue(rgb):
    return rgb & 255


def qGray(r, g=None, b=None):
    if g is None:
        r, g, b = qRed(r), qGreen(r), qBlue(r)
    return (r * 11 + g * 16 + b * 5) // 32


class QDesktopServices:
    """Named at import by the hyperlink tool and BIM's reports; opening
    a URL from the guest is not in the sandbox (a network decision,
    docs/Sandbox.md sec 6)."""

    @staticmethod
    def openUrl(url):
        raise RuntimeError("QDesktopServices.openUrl is not in the sandbox (docs/Sandbox.md 6)")


class QFileSystemModel:
    """A base class BIM's library browser derives from at import; a
    file system model is not in the sandbox (no file system to show)."""

    def __init__(self, *args, **kw):
        raise RuntimeError("QFileSystemModel is not in the sandbox's subset")


def __getattr__(name):
    from freecad.widgets import models

    try:
        return getattr(models, name)
    except AttributeError:
        raise AttributeError("PySide.QtGui.%s is not in the sandbox's subset" % name)

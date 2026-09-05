"""PySide.QtCore for the sandbox guest: translation is the identity,
a zero-delay single shot runs now (there is no event loop to defer to),
the locale is C, and the Qt enums, signals and value types are data
(freecad.widgets.qtdata).  A widget class asked of this module comes
from freecad.widgets.models, loaded then (docs/Sandbox.md 7.11)."""

from freecad.widgets.qtdata import Qt, QSize, QPoint, QPointF  # noqa: F401


def QT_TRANSLATE_NOOP(context, text):
    return text


class QCoreApplication:
    UnicodeUTF8 = 0

    @staticmethod
    def translate(context, text, disambiguation=None, n=-1):
        return text

    @staticmethod
    def processEvents(*args):
        pass


class QTimer:
    @staticmethod
    def singleShot(msec, callback):
        callback()


class QLocale:
    def decimalPoint(self):
        return "."


class QUrl:
    def __init__(self, url=""):
        self._url = str(url)

    @staticmethod
    def fromLocalFile(path):
        return QUrl("file://" + str(path))

    def toString(self):
        return self._url

    def toLocalFile(self):
        return self._url[7:] if self._url.startswith("file://") else self._url


class QRegularExpression:
    CaseInsensitiveOption = 1

    def __init__(self, pattern="", options=0):
        self.pattern = pattern
        self.options = options


_MODELS = ("Signal", "SIGNAL", "SLOT", "QObject")


def __getattr__(name):
    if name in _MODELS:
        from freecad.widgets import models

        return getattr(models, name)
    raise AttributeError("PySide.QtCore.%s is not in the sandbox's subset" % name)

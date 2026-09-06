"""PySide.QtCore for the sandbox guest: translation is the identity,
a single shot runs when the current request returns (there is no event
loop to defer to; the image drains the queue), the locale is C, and the
Qt enums, signals and value types are data (freecad.widgets.qtdata).  A
widget class asked of this module comes from freecad.widgets.models,
loaded then (docs/Sandbox.md 7.11)."""

from freecad.widgets.qtdata import (  # noqa: F401
    Qt, QSize, QPoint, QPointF, QRect, QMargins, QEvent,
)


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


# There is no event loop in the guest: a single shot runs when the
# request that scheduled it returns to the host (the image drains the
# queue at the end of every dispatched exec, evaluate and hook), which
# is the order Qt's zero-delay timer gives Draft's `todo.delay` -- the
# call that scheduled it finishes first (G3c, docs/Sandbox.md 7.11).
_pending = []


def _drain():
    """Run what `QTimer.singleShot` queued, in order, until nothing is
    left (a callback may queue more)."""
    while _pending:
        fn = _pending.pop(0)
        try:
            fn()
        except Exception:
            import traceback

            traceback.print_exc()


class QTimer:
    def __init__(self, parent=None):
        self.timeout = _Timeout(self)
        self._active = False
        self._single = False
        self._interval = 0

    @staticmethod
    def singleShot(msec, callback):
        _pending.append(callback)

    def start(self, msec=None):
        if msec is not None:
            self._interval = msec
        self._active = True
        # one shot at the next drain: a repeating timer would spin a
        # guest without a loop; the corpus uses these for a delayed call
        _pending.append(self._fire)

    def stop(self):
        self._active = False

    def setInterval(self, msec):
        self._interval = msec

    def setSingleShot(self, on):
        self._single = bool(on)

    def isActive(self):
        return self._active

    def _fire(self):
        if self._active:
            self._active = False
            self.timeout.emit()


class _Timeout:
    def __init__(self, timer):
        self._slots = []

    def connect(self, slot):
        self._slots.append(slot)

    def disconnect(self, slot=None):
        self._slots = [] if slot is None else [s for s in self._slots if s != slot]

    def emit(self):
        for slot in list(self._slots):
            slot()


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

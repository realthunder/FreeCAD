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
# delayed callbacks a document guest queued (no host timer for it): they
# wait for the NEXT drain, so a callback that re-arms itself cannot
# spin the one it is in (docs/Sandbox.md 7.15)
_deferred = []


def _drain():
    """Run what `QTimer.singleShot(0, ...)` queued, in order, until
    nothing is left (a callback may queue more); then move what was
    deferred into the next drain's queue."""
    while _pending:
        fn = _pending.pop(0)
        try:
            fn()
        except Exception:
            import traceback

            traceback.print_exc()
    if _deferred:
        _pending.extend(_deferred)
        del _deferred[:]


# ---- host timers (docs/Sandbox.md 7.15) -------------------------------
#
# A delayed callback runs when the HOST's timer fires, not when the drain
# after some request happens to reach it: Draft shows its status bar
# widgets 500 ms after activation, BimViews re-arms its update every 2 s.
# One hook call per firing (`fire(id)` on the dispatcher below).  Where
# the op is refused (a document principal) the callback is deferred to
# the next drain instead.

_timers = {}
_next_timer = [1]
_dispatcher = [None]


class _TimerDispatcher:
    """The guest-side end of the host's timers: `fire(id)` runs the
    callback of that timer (a repeating one stays registered)."""

    def fire(self, timer_id):
        entry = _timers.get(int(timer_id))
        if entry is None:
            return None
        callback, repeat = entry
        if not repeat:
            _timers.pop(int(timer_id), None)
        try:
            callback()
        except Exception:
            import traceback

            traceback.print_exc()
        _drain()
        return None


def _host_timer(callback, msec, repeat):
    """Register `callback` with the host's timer; the timer id, or None
    when the host refused (the caller defers instead)."""
    try:
        import _fcx
        import FreeCADGui
    except ImportError:
        return None
    timer_id = _next_timer[0]
    _next_timer[0] += 1
    _timers[timer_id] = (callback, bool(repeat))
    try:
        if _dispatcher[0] is None:
            _dispatcher[0] = _TimerDispatcher()
            desc = FreeCADGui._proxy_register(_dispatcher[0], ("fire",))
            _fcx.op("gui.timer", 0, ["dispatcher", desc])
        _fcx.op("gui.timer", 0, ["start", timer_id, int(msec), bool(repeat)])
    except Exception:
        _timers.pop(timer_id, None)
        _dispatcher[0] = None
        return None
    return timer_id


def _stop_host_timer(timer_id):
    if timer_id is None or _timers.pop(timer_id, None) is None:
        return
    try:
        import _fcx

        _fcx.op("gui.timer", 0, ["stop", int(timer_id)])
    except Exception:
        pass


class QTimer:
    def __init__(self, parent=None):
        self.timeout = _Timeout(self)
        self._active = False
        self._single = False
        self._interval = 0
        self._host_id = None

    @staticmethod
    def singleShot(msec, callback):
        """A 0 ms shot runs in the current drain (todo's order contract);
        a delayed one on the host's timer, or in the next drain where
        the guest may not have one."""
        if int(msec) <= 0:
            _pending.append(callback)
            return
        if _host_timer(callback, msec, False) is None:
            _deferred.append(callback)

    def start(self, msec=None):
        if msec is not None:
            self._interval = int(msec)
        self.stop()
        self._active = True
        if self._interval <= 0:
            _pending.append(self._fire)
            return
        self._host_id = _host_timer(self._fire, self._interval, not self._single)
        if self._host_id is None:
            _deferred.append(self._fire)

    def stop(self):
        self._active = False
        host_id, self._host_id = self._host_id, None
        _stop_host_timer(host_id)

    def setInterval(self, msec):
        self._interval = int(msec)

    def interval(self):
        return self._interval

    def setSingleShot(self, on):
        self._single = bool(on)

    def isSingleShot(self):
        return self._single

    def isActive(self):
        return self._active

    def _fire(self):
        if not self._active:
            return
        if self._single or self._host_id is None:
            self._active = False
            self._host_id = None
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


def Slot(*args, **kw):
    """PySide's @Slot(...) decorator factory: the function itself
    (ArchReport decorates at module level under `if FreeCAD.GuiUp`).
    Always a factory -- `@Slot(int)` names a type, not the function."""
    return lambda fn: fn


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

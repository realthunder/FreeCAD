"""PySide.QtWidgets for the sandbox guest: the widget classes are the
models of freecad.widgets (docs/Sandbox.md 7.11), loaded on the first
class asked for; QApplication and QMdiArea are the names App-side code
probes without instantiating (draftutils.translate expects the
AttributeError on QApplication.UnicodeUTF8)."""

from freecad.widgets.qtdata import QSizePolicy, QDialogButtonBox  # noqa: F401


class QApplication:
    @staticmethod
    def translate(context, text, disambiguation=None, n=-1):
        return text

    @staticmethod
    def processEvents(*args):
        pass


class QMdiArea:
    pass


def __getattr__(name):
    from freecad.widgets import models

    try:
        return getattr(models, name)
    except AttributeError:
        raise AttributeError("PySide.QtWidgets.%s is not in the sandbox's subset" % name)

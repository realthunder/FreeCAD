"""PySide.QtWidgets for the sandbox guest: the classes App-side code
names without instantiating (draftutils.translate probes
QApplication.UnicodeUTF8 and expects the AttributeError)."""


class QApplication:
    pass


class QMdiArea:
    pass

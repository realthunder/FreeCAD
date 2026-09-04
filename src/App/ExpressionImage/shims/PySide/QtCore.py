"""PySide.QtCore for the sandbox guest: translation is the identity,
a zero-delay single shot runs now (there is no event loop to defer to),
the locale is C.  Anything else is an AttributeError where it is used."""


def QT_TRANSLATE_NOOP(context, text):
    return text


class QCoreApplication:
    @staticmethod
    def translate(context, text, disambiguation=None, n=-1):
        return text


class QTimer:
    @staticmethod
    def singleShot(msec, callback):
        callback()


class QLocale:
    def decimalPoint(self):
        return "."

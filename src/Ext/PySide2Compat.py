"""PySide2 compatibility namespace for a PySide6 build.

FreeCAD's own answer to the Qt5/Qt6 split is the generated ``PySide``
namespace (see cMake/FreeCAD_Helpers/SetupShibokenAndPyside.cmake): code
writes ``from PySide import QtWidgets`` and it forwards to whichever
PySide is installed. Anything importing ``PySide2`` directly gets
nothing, which is deliberate upstream -- and fatal for the large body of
third-party addons written against PySide2, which simply fail to load on
a Qt6 build ("No module named 'PySide2'").

This module makes ``PySide2`` resolve to the installed PySide6, so those
addons load. It is installed ONLY when building against PySide 6 or
newer, so it can never shadow a real PySide2.

Two things it is careful about:

- **Qt5 layout, not Qt4.** The ``PySide`` shim merges QtGui and QtWidgets
  into one namespace, because that is what the Qt4-era FreeCAD code
  expects. PySide2 is Qt5: the two are separate, and code says
  ``QtWidgets.QWidget`` and ``QtGui.QPainter``. Forwarding module for
  module keeps that contract.
- **It never mutates PySide6.** Each forwarded module is a distinct proxy
  that reads through to the real one, so the compatibility patches below
  cannot leak into ``PySide6.QtWidgets`` and change what code that
  legitimately asked for PySide6 sees. The classes themselves are the
  same objects, so isinstance() and every C++ signature still work
  across the two spellings.
- **It reads through rather than copying.** PySide6 loads its classes
  lazily -- ``PySide6.QtCore.__dict__`` holds 72 names where ``dir()``
  reports 254, and QObject is not among the 72 until something asks for
  it. A proxy built by copying the module dict would therefore come out
  almost empty, so the proxy forwards on attribute access instead and
  publishes ``__all__`` from ``dir()`` so that ``import *`` still works.

What it deliberately does NOT do: invent replacements for Qt5 API that
Qt6 removed outright. ``QtCore.QRegExp`` is gone and no alias to
QRegularExpression would be faithful (different API: exactMatch,
indexIn, cap). Code reaching for those gets an honest AttributeError
naming the attribute, which is a better diagnosis than a silent
mistranslation.

``__version__`` reports the real PySide6 version rather than a fake 5.x,
for the same reason: an addon branching on the version should learn the
truth.
"""

import importlib
import importlib.util
import sys
import types
from importlib.abc import Loader, MetaPathFinder
from importlib.machinery import ModuleSpec

_PREFIX = "PySide2."

# Newest first. The install gate means this list should only ever match
# PySide6 or later, never a real PySide2.
_CANDIDATES = ("PySide6",)

_TARGET = None
for _candidate in _CANDIDATES:
    if importlib.util.find_spec(_candidate) is not None:
        _TARGET = _candidate
        break

if _TARGET is None:
    raise ImportError(
        "the PySide2 compatibility namespace found none of %s to forward to"
        % (", ".join(_CANDIDATES),)
    )

_pyside = importlib.import_module(_TARGET)
__version__ = getattr(_pyside, "__version__", "")
__version_info__ = getattr(_pyside, "__version_info__", ())


def _relocated(target, module_name, names):
    """Names Qt6 moved out of `target` and into the `module_name` module."""
    out = {}
    try:
        source = importlib.import_module(_TARGET + "." + module_name)
    except ImportError:
        return out
    for name in names:
        if not hasattr(target, name) and hasattr(source, name):
            out[name] = getattr(source, name)
    return out


def _widgets_extras(target):
    # Qt6 moved the action and shortcut classes from QtWidgets to QtGui.
    # PySide2 code says QtWidgets.QAction, and that spelling is by far
    # the most common single reason an addon dies on Qt6.
    return _relocated(target, "QtGui", ("QAction", "QActionGroup", "QShortcut"))


def _webengine_extras(target):
    # The same relocation the PySide shim already patches around.
    return _relocated(target, "QtWebEngineCore",
                      ("QWebEnginePage", "QWebEngineSettings",
                       "QWebEngineProfile", "QWebEngineScript"))


_EXTRAS = {
    "QtWidgets": _widgets_extras,
    "QtWebEngineWidgets": _webengine_extras,
}


class _ForwardingModule(types.ModuleType):
    """Reads through to the PySide6 module it stands for.

    Read-through rather than copy, because PySide6 populates its module
    dict lazily; see the note in the module docstring.
    """

    def __getattr__(self, name):
        # Reached only when normal lookup failed, so every name resolved
        # here is cached into the instance dict and costs one lookup.
        extras = self.__dict__.get("__pyside_extras__") or {}
        if name in extras:
            value = extras[name]
        else:
            target = self.__dict__.get("__pyside_target__")
            if target is None:
                raise AttributeError(name)
            try:
                value = getattr(target, name)
            except AttributeError:
                # Named against the PySide2 spelling the caller used --
                # an addon reading the traceback should see its own
                # import, not a PySide6 module it never mentioned.
                raise AttributeError(
                    "module %r has no attribute %r"
                    % (self.__dict__.get("__name__"), name)) from None
        setattr(self, name, value)
        return value

    def __dir__(self):
        target = self.__dict__.get("__pyside_target__")
        extras = self.__dict__.get("__pyside_extras__") or {}
        names = set(self.__dict__) | set(extras)
        if target is not None:
            names |= set(dir(target))
        return sorted(names)


class _ForwardingLoader(Loader):
    """Builds a proxy module that reads as the PySide6 one it stands for."""

    def create_module(self, spec):
        sub = spec.name[len(_PREFIX):]
        target = importlib.import_module(_TARGET + "." + sub)

        extras = _EXTRAS.get(sub)
        extras = extras(target) if extras else {}

        proxy = _ForwardingModule(spec.name)
        proxy.__dict__["__pyside_target__"] = target
        proxy.__dict__["__pyside_extras__"] = extras
        proxy.__package__ = "PySide2"
        proxy.__loader__ = self
        proxy.__spec__ = spec
        proxy.__doc__ = getattr(target, "__doc__", None)
        proxy.__file__ = getattr(target, "__file__", None)
        # `from PySide2.QtWidgets import *` consults __all__ first, and
        # has to, since the dict starts out nearly empty.
        proxy.__all__ = sorted(
            set(n for n in dir(target) if not n.startswith("_")) | set(extras))
        return proxy

    def exec_module(self, module):
        # Everything happened in create_module; the target module has
        # already executed exactly once, under its own name.
        pass


class _ForwardingFinder(MetaPathFinder):
    """Answers for `PySide2.<submodule>` and nothing else."""

    def find_spec(self, fullname, path=None, target=None):
        if not fullname.startswith(_PREFIX):
            return None
        sub = fullname[len(_PREFIX):]
        if not sub or "." in sub:
            # Only the top-level Qt modules are forwarded. Anything
            # deeper is PySide6's own business and would be found under
            # its real name.
            return None
        try:
            if importlib.util.find_spec(_TARGET + "." + sub) is None:
                return None
        except (ImportError, AttributeError, ValueError):
            return None
        return ModuleSpec(fullname, _ForwardingLoader())


# Appended rather than inserted: the normal path-based finder gets first
# refusal, so a real module inside this package would still win.
if not any(isinstance(f, _ForwardingFinder) for f in sys.meta_path):
    sys.meta_path.append(_ForwardingFinder())

"""The `freecad` namespace package in the sandbox guest: carries
freecad.deprecation (this wheel) and freecad.widgets (the fcx_widgets
wheel), merged from wherever each is installed."""

__path__ = __import__("pkgutil").extend_path(__path__, __name__)

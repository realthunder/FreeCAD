# SPDX-License-Identifier: LGPL-2.1-or-later
"""A stand-in for the ``IPython`` package in the sandbox guest.

ipywidgets imports IPython at import time for three things: ``get_ipython``
(to register with a running shell; None here, as outside any shell),
``IPython.display`` (``display`` and ``clear_output``), and the
``InteractiveShell`` class its Output widget mentions.  The real package
is twelve pure wheels and about a second of import in the guest for
nothing a form needs, so this package carries those names and no shell
(docs/Sandbox.md 7.3, Probe B).  ``display(widget)`` is the one that
does something: it shows the widget on the host (FreeCADGui.showWidget).
"""

__version__ = "0.0.0+fcx"
version_info = (0, 0, 0)


def get_ipython():
    """No InteractiveShell runs in the guest."""
    return None


from . import display  # noqa: E402,F401  (IPython.display is a module attribute natively)

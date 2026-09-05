# SPDX-License-Identifier: LGPL-2.1-or-later
"""``IPython.display`` for the guest: a widget shown is a widget on the host."""

WIDGET_MIME = "application/vnd.jupyter.widget-view+json"


def display(*objs, **kwargs):
    """Show each widget through the host (FreeCADGui.showWidget); print
    the repr of anything else, as IPython does outside a rich front end."""
    import FreeCADGui

    for obj in objs:
        bundle = getattr(obj, "_repr_mimebundle_", None)
        data = bundle(**kwargs) if callable(bundle) else None
        if isinstance(data, tuple):
            data = data[0]
        if isinstance(data, dict) and WIDGET_MIME in data:
            FreeCADGui.showWidget(obj)
        else:
            print(repr(obj))


def clear_output(wait=False):
    """No output area in the guest."""


class DisplayHandle:
    """What IPython's display(display_id=...) returns; inert here."""

    def update(self, obj, **kwargs):
        display(obj, **kwargs)

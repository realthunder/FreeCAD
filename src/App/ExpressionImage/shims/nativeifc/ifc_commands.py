# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guest shim of `nativeifc.ifc_commands` (docs/Sandbox.md 7.9, G2b):
BIM's `Initialize()` imports it and asks `get_commands()` for the IFC
tool bar.  The guest carries no ifcopenshell, so no IFC command is
registered from here and the answer is the empty list: BIM's other
tool bars come up, the IFC one is empty."""


def get_commands():
    return []

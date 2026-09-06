# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guest shim of `nativeifc.ifc_observer` (docs/Sandbox.md 7.9, G2b):
BIM's `Activated()` installs the document observer that keeps an IFC
file in step with the document, `Deactivated()` removes it.  The guest
carries no ifcopenshell, so there is no IFC file to keep in step: both
are no-ops, and an IFC-backed object is refused where it is edited
(`ifc_tools`), not observed."""


def add_observer():
    return None


def remove_observer():
    return None

# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guest shim of BIM's `nativeifc` package (docs/Sandbox.md sec 13).

The real package needs ifcopenshell, which the sandbox guest does not
carry.  BIM's App side reaches it from a recompute in exactly one place
that does not depend on the document being IFC-backed: ArchSchedule's
execute() ends with save_ifc_props(), whose edit_pset() returns before
touching IFC when the object has no IFC project.  This shim answers
that path the way the native package does for a plain document, and
raises where the native code would go on to ifcopenshell -- an
IFC-backed object is not edited silently, and not skipped silently.
"""


class IfcUnavailableError(RuntimeError):
    """The sandbox guest carries no ifcopenshell."""


def unavailable(what):
    raise IfcUnavailableError(
        "%s needs nativeifc/ifcopenshell, which the sandbox guest does not carry" % what
    )

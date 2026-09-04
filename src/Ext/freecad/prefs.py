# SPDX-License-Identifier: LGPL-2.1-or-later
"""The sandbox guest's read-only view of the parameter store.

Workbench code calls `FreeCAD.ParamGet(path).GetBool(name, default)`
(BIM's ArchSchedule at import, ArchTessellation and ArchComponent at
execute).  In the sandbox guest `FreeCAD.ParamGet` is a Python shim
whose group object answers every Get* through `read` here, a module
facade under the `prefs.read` permission (docs/Sandbox.md 7.6: read
only, allowed for every principal, the ruling that let a document
object's Wire.__init__ read its MakeFaceMode).  Set* never crosses.
"""

import FreeCAD

KINDS = ("Bool", "Int", "Unsigned", "Float", "String")


def read(path, name, kind, default):
    """`FreeCAD.ParamGet(path).Get<kind>(name, default)`, by value."""
    if kind not in KINDS:
        raise ValueError("unknown parameter kind %r" % (kind,))
    return getattr(FreeCAD.ParamGet(path), "Get" + kind)(name, default)


def names(path, kind):
    """`FreeCAD.ParamGet(path).Get<kind>s()`: the names of the group's
    parameters of that kind."""
    if kind not in KINDS:
        raise ValueError("unknown parameter kind %r" % (kind,))
    return list(getattr(FreeCAD.ParamGet(path), "Get" + kind + "s")())


def has_group(path, name):
    """`FreeCAD.ParamGet(path).HasGroup(name)`."""
    return FreeCAD.ParamGet(path).HasGroup(name)

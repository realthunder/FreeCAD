# SPDX-License-Identifier: LGPL-2.1-or-later
"""The sandbox guest's view of the parameter store.

Workbench code calls `FreeCAD.ParamGet(path).GetBool(name, default)`
(BIM's ArchSchedule at import, ArchTessellation and ArchComponent at
execute).  In the sandbox guest `FreeCAD.ParamGet` is a Python shim
whose group object answers every Get* through `read` here, a module
facade under the `prefs.read` permission (docs/Sandbox.md 7.6: read
only, allowed for every principal, the ruling that let a document
object's Wire.__init__ read its MakeFaceMode), and every Set*/Rem*
through `write`/`remove`, under `prefs.write` (docs/Sandbox.md 7.11:
a task panel storing what the user chose; ALLOW for the session and
addons, DENY for a document).
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


def write(path, name, kind, value):
    """`FreeCAD.ParamGet(path).Set<kind>(name, value)`."""
    if kind not in KINDS:
        raise ValueError("unknown parameter kind %r" % (kind,))
    getattr(FreeCAD.ParamGet(path), "Set" + kind)(name, value)
    return True


def remove(path, name, kind):
    """`FreeCAD.ParamGet(path).Rem<kind>(name)` (`RemGroup` for "Group")."""
    if kind not in KINDS and kind != "Group":
        raise ValueError("unknown parameter kind %r" % (kind,))
    getattr(FreeCAD.ParamGet(path), "Rem" + kind)(name)
    return True

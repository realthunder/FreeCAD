# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/Inspection/Inspection.

They are what the Visual Inspection dialog was last accepted with: the
dialog reads them when it opens and stores them when it is accepted, and
nothing else names them, so no class is generated for them. They are
described to the settings registry when Init.py imports this file
(freecad.params), so that the omni search lists them with the others. Each
default is what the dialog's widget starts on.
"""
import sys

from freecad.params import ParamFloat, register

NameSpace = "Inspection"
ClassName = "InspectionParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/Inspection"

_KEPT = " Stored when the Visual Inspection dialog is accepted, read when it opens."

Params = [
    ParamFloat(
        "InspectionSearchDistance",
        0.05,
        subpath="Inspection",
        param_name="SearchDistance",
        title="Visual inspection: search distance",
        doc="How far from a point of the actual object the nominal one is looked for, "
        "as the dialog was last used." + _KEPT,
    ),
    ParamFloat(
        "InspectionThickness",
        0.0,
        subpath="Inspection",
        param_name="Thickness",
        title="Visual inspection: thickness",
        doc="The thickness the inspection was last given." + _KEPT,
    ),
]

register(sys.modules[__name__])

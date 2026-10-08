# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/Material that only Material's Python code reads.

The ones C++ reads are in App/MaterialParams.py, which a class is generated
from. These have no class: they are described to the settings registry when
Init.py imports this file (freecad.params), so that the omni search lists
them with the others. They belong to the material card editor written in
Python (MaterialEditor.py) and to the card list it builds
(materialtools/cardutils.py), which are left as they are -- each default
here is the one its reader passes.
"""
import sys

from freecad.params import ParamBool, ParamInt, register

NameSpace = "Material"
ClassName = "MaterialPyParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/Material"

_LIST = " Takes effect the next time the card editor fills its list."

Params = [
    ParamBool(
        "CardsDeleteDuplicates",
        True,
        subpath="Cards",
        param_name="DeleteDuplicates",
        title="Delete card duplicates",
        doc="Duplicate cards will be deleted from the displayed material card list: a "
        "card with the same content as one listed already is left out." + _LIST,
    ),
    ParamBool(
        "CardsSortByResources",
        False,
        subpath="Cards",
        param_name="SortByResources",
        title="Sort by resources",
        doc="Material cards appear sorted by their resources (locations). If unchecked, "
        "they will be sorted by their name." + _LIST,
    ),
    ParamInt(
        "MaterialEditorWidth",
        441,
        title="Card editor: width",
        doc="Width, in pixels, the material card editor written in Python was last "
        "left with. Stored when the editor closes, read when it opens.",
    ),
    ParamInt(
        "MaterialEditorHeight",
        626,
        title="Card editor: height",
        doc="Height, in pixels, the material card editor written in Python was last "
        "left with. Stored when the editor closes, read when it opens.",
    ),
]

register(sys.modules[__name__])

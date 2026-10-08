# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/Assembly that only Assembly's Python code reads.

The ones C++ reads are in App/AssemblyParams.py, which a class is generated
from. These have no class: they are described to the settings registry when
Init.py imports this file (freecad.params), so that the omni search lists
them with the others. The commands and dialogues that read them keep their
own code -- each default here is the one its reader passes.

Six of them are what a dialogue was last left with rather than something set
on a preference page: the dialogue stores them when it closes.
"""
import sys

from freecad.params import ParamBool, ParamColor, ParamComboBox, ParamHex, ParamInt, register

NameSpace = "Assembly"
ClassName = "AssemblyPyParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/Assembly"

Params = [
    ParamInt(
        "GroundFirstPart",
        0,
        proxy=ParamComboBox(["Ask", "Always", "Never"]),
        title="Ground first part",
        doc="Whether the first part inserted into an assembly is grounded: asked\n"
        "each time, always, or never. Answering the question with Always or\n"
        "Never stores that answer here. Takes effect at the next insertion.",
    ),
    ParamBool(
        "EnforceOneAssemblyRule",
        True,
        title="One root assembly per document",
        doc="Disables the Create Assembly command while the document has an\n"
        "assembly at its top level and none is active, so that a further\n"
        "assembly is made inside an active one. Takes effect at once.",
    ),
    ParamBool(
        "SolveInJointCreation",
        True,
        title="Solve while editing a joint",
        doc="Solves the assembly whenever a joint is made or its references,\n"
        "offsets, distance or angle change, so that the parts move to where\n"
        "the joint puts them, and after a new part is inserted. Takes effect\n"
        "at the next such change.",
    ),
    ParamHex(
        "AssemblyConstraints",
        0xCC333300,
        proxy=ParamColor(transparency=False),
        title="Grounded part marker colour",
        doc="Colour of the padlock shown on a grounded part. Takes effect for the\n"
        "markers made afterwards: a part grounded or a document opened later.",
    ),
    ParamInt(
        "StepLineThickness",
        3,
        title="Exploded view line width",
        doc="Width, in pixels, of the lines an exploded view draws from where a\n"
        "part was to where it is moved. Takes effect for the exploded views\n"
        "made or opened afterwards.",
    ),
    ParamHex(
        "StepLineColor",
        0xCC333300,
        proxy=ParamColor(transparency=False),
        title="Exploded view line colour",
        doc="Colour of the lines an exploded view draws from where a part was to\n"
        "where it is moved. Takes effect for the exploded views made or opened\n"
        "afterwards.",
    ),
    ParamBool(
        "BOMOnlyParts",
        False,
        title="Bill of materials: only parts",
        doc="A new bill of materials starts with 'Only parts' checked: it lists\n"
        "part containers and sub-assemblies and leaves out solids such as\n"
        "bodies, fasteners and primitives. Stored when the dialogue of a bill\n"
        "of materials closes.",
    ),
    ParamBool(
        "BOMDetailParts",
        True,
        title="Bill of materials: parts children",
        doc="A new bill of materials starts with 'Parts children' checked: it\n"
        "lists what the parts contain as well. Stored when the dialogue of a\n"
        "bill of materials closes.",
    ),
    ParamBool(
        "BOMDetailSubAssemblies",
        True,
        title="Bill of materials: sub-assemblies children",
        doc="A new bill of materials starts with 'Sub-assemblies children'\n"
        "checked: it lists what the sub-assemblies contain as well. Stored\n"
        "when the dialogue of a bill of materials closes.",
    ),
    ParamBool(
        "PartsAsSingleSolid",
        True,
        title="Exploded view: parts as single solid",
        doc="The exploded view dialogue opens with 'Parts as single solid'\n"
        "checked: picking anything of a part moves the whole part, not the\n"
        "one object picked. Stored when the dialogue closes.",
    ),
    ParamBool(
        "InsertShowOnlyParts",
        False,
        title="Insert: show only parts",
        doc="The Insert dialogue opens with 'Show only parts' checked: its list\n"
        "offers parts and assemblies only. Stored when the dialogue closes.",
    ),
    ParamBool(
        "InsertRigidSubAssemblies",
        True,
        title="Insert: rigid sub-assemblies",
        doc="The Insert dialogue opens with 'Rigid sub-assemblies' checked: an\n"
        "inserted sub-assembly is one solid unit in its parent, where a\n"
        "flexible one lets its own joints move. Stored when the dialogue\n"
        "closes.",
    ),
    ParamBool(
        "PartInNewFile",
        True,
        title="New part in a new file",
        doc="The Insert New Part dialogue opens with 'Create part in new file'\n"
        "checked: the part is made in a document of its own and linked into\n"
        "the assembly. Stored when the dialogue closes.",
    ),
]

register(sys.modules[__name__])

# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/OpenSCAD.

OpenSCAD is written in Python and has no generated class: its settings are
described to the settings registry when Init.py imports this file
(freecad.params), so that the omni search lists them with the others.
Eleven are on the module's preference page, and the title, the range and the
items of each are the page's; four more are read by the code and shown
nowhere. The importer, the exporter and the commands that read them are left
as they are -- each default here is the one its reader passes.

Not here: meshmaxarea and meshlocallen, which only a branch that is switched
off reads.
"""
import sys

from freecad.params import (
    ParamBool,
    ParamComboBox,
    ParamFile,
    ParamFloat,
    ParamInt,
    ParamSpinBox,
    ParamString,
    register,
)

NameSpace = "OpenSCAD"
ClassName = "OpenSCADParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/OpenSCAD"

_IMPORT = " Takes effect at the next import."
_EXPORT = " Read when the exporter is first used in a session."

Params = [
    ParamString(
        "openscadexecutable",
        "",
        proxy=ParamFile(),
        title="OpenSCAD executable",
        doc="The path to the OpenSCAD executable. With it, .scad files can be imported "
        "and the workbench has its OpenSCAD commands. Empty, the workbench looks for the "
        "program when it is first activated and stores what it finds here. Read at "
        "start.",
    ),
    ParamBool(
        "printVerbose",
        False,
        title="Print debug information in the Console",
        doc="The importer prints what it reads and builds as it goes. Read when the "
        "importer is first used in a session.",
    ),
    ParamBool(
        "useViewProviderTree",
        False,
        title="Use ViewProvider in Tree View",
        doc="If this is checked, Features will claim their children in the tree view."
        + _IMPORT,
    ),
    ParamBool(
        "useMultmatrixFeature",
        False,
        title="Use Multmatrix Feature",
        doc="If this is checked, Multmatrix Object will be Parametric: a multmatrix "
        "that deforms its shape becomes an object that keeps the matrix, where the "
        "matrix is otherwise applied to a copy of the shape once." + _IMPORT,
    ),
    ParamInt(
        "useMaxFN",
        16,
        proxy=ParamSpinBox(0, 99, 1),
        title="Maximum number of faces for polygons (fn)",
        doc="The maximum number of faces of a polygon, prism or frustum. If fn is "
        "greater than this value the object is considered to be a circular. Set to 0 "
        "for no limit." + _IMPORT,
    ),
    ParamInt(
        "transfermechanism",
        0,
        proxy=ParamComboBox(
            [
                "Standard temp directory",
                "User-specified directory",
                "stdout pipe (requires OpenSCAD >= 2021.1)",
            ]
        ),
        title="Send to OpenSCAD via",
        doc="The transfer mechanism for getting data to and from OpenSCAD. Takes effect "
        "the next time OpenSCAD is run.",
    ),
    ParamString(
        "transferdirectory",
        "",
        title="Transfer directory",
        doc="The path to the directory for transferring files to and from OpenSCAD, "
        "used when the transfer goes through a user-specified directory. Takes effect "
        "the next time OpenSCAD is run.",
    ),
    ParamFloat(
        "exportFa",
        12.0,
        proxy=ParamSpinBox(0.01, 360.0, 1.0, 2),
        title="Maximum fragment size: angle (fa)",
        doc="Minimum angle for a fragment, in degrees: the $fa written with each round "
        "shape of an exported file." + _EXPORT,
    ),
    ParamFloat(
        "exportFs",
        2.0,
        proxy=ParamSpinBox(0.01, 10000.0, 1.0, 2),
        title="Maximum fragment size: size (fs)",
        doc="Minimum size of a fragment, in mm: the $fs written with each round shape "
        "of an exported file." + _EXPORT,
    ),
    ParamInt(
        "exportConvexity",
        10,
        proxy=ParamSpinBox(0, 99, 1),
        title="Convexity",
        doc="The convexity written with each extrusion of an exported file, which "
        "OpenSCAD uses for its preview." + _EXPORT,
    ),
    ParamFloat(
        "meshdeflection",
        0.0,
        proxy=ParamSpinBox(0.0, 1000.0, 1.0, 2),
        title="Mesh fallback: deflection",
        doc="Deflection of the mesh a shape is exported as when it has no OpenSCAD "
        "equivalent and is written as a polyhedron. Takes effect at the next export.",
    ),
    ParamInt(
        "fnForImport",
        32,
        title="Fragments for 2D operations (fn)",
        doc="The $fn OpenSCAD is run with for an operation on 2D objects, such as a "
        "2D hull or Minkowski sum: the number of fragments of a full circle. On no "
        "page. Takes effect at the next such operation.",
    ),
    ParamFloat(
        "meshmaxlength",
        1.0,
        title="Mesh operations: tessellation tolerance",
        doc="Tolerance the shapes are tessellated with before they are handed to "
        "OpenSCAD as meshes, for a mesh boolean, a hull or a Minkowski sum. On no page. "
        "Takes effect at the next such operation.",
    ),
    ParamInt(
        "tempmeshmaxpoints",
        5000,
        title="Mesh operations: most points for a solid result",
        doc="A hull or Minkowski sum of 3D objects is only handed to OpenSCAD when the "
        "mesh of each object has fewer points than this; with more, nothing is made. On "
        "no page. Takes effect at the next such operation.",
    ),
    ParamBool(
        "usePlaceholderForUnsupported",
        False,
        title="Placeholder for unsupported functions",
        doc="An OpenSCAD function the importer cannot build, such as glide, subdiv or "
        "a true projection, becomes a placeholder object that keeps its arguments. "
        "Off, a message box reports it and nothing is made for it. On no page."
        + _IMPORT,
    ),
]

register(sys.modules[__name__])

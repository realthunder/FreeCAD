# SPDX-License-Identifier: LGPL-2.1-or-later
"""Draft's and BIM's settings, described to the settings registry.

`draftutils.params` keeps the table of every setting the two workbenches
read -- name, type and default; some written out there, the others read
from the .ui files of the preference pages. `register()` describes that
table to App::ParamRegistry (`FreeCAD.registerParam`), which is where the
omni search finds a setting. The table is not written a second time: this
module adds what a description needs beyond name, type and default.

  - A title and a short documentation. A setting of a preference page takes
    them from its widget: the text of a check box, or the label beside the
    widget, and the tool tip. `DOCS` has them for the settings no page
    shows, and a documentation for the page settings without a tool tip.
  - The editor: the items of a combo box, the range of a spin box, a colour
    button, a file chooser. Read from the widget as well.

Only the groups that are Draft's and BIM's own are described. The table
also holds settings of other modules that Draft reads (General, Units,
View, Mod/Mesh); describing those is their modules' business.

Many of the settings no page shows are what a task panel was last given --
the width of the last wall, whether Copy was checked. They are listed all
the same: a tool starts with them.

Two kinds of BIM's settings are not in the table at all and are described
from here too: those of a preference page the table does not read
(`EXTRA_PAGES`), and those BIM's code reads with the name written out
(`EXTRA`).
"""

import re

import FreeCAD as App

_PREFIX = "User parameter:BaseApp/Preferences/"
_OWN = ("Mod/Draft", "Mod/Arch", "Mod/BIM")
# the table's "unsigned" is a packed colour; "uint" is for a number written here
_TYPES = {
    "bool": "Bool",
    "int": "Int",
    "float": "Float",
    "string": "String",
    "unsigned": "Hex",
    "uint": "UInt",
}
_OWN_TEXT = ("Gui::PrefCheckBox", "Gui::PrefRadioButton", "Gui::PrefCheckableGroupBox")

_LAST = " The value last entered in its task panel, which stores it."
_TOGGLE = " Stored when toggled."


def _sizes(what, tool, last=_LAST):
    return {
        what + "Length": (what + " length", "Length, in mm, the %s starts with." % tool + last),
        what + "Width": (what + " width", "Width, in mm, the %s starts with." % tool + last),
        what + "Height": (what + " height", "Height, in mm, the %s starts with." % tool + last),
    }


def _shortcut(label, action):
    return (
        "In-command shortcut: " + label,
        "Key that %s while a Draft command waits for a point. Takes effect at the next command."
        % action,
    )


def _precast(name, what):
    return (
        "Precast: " + name,
        "The %s of the precast concrete options of the Structure tool, in mm: the value last "
        "entered there, shown again the next time." % what,
    )


def _legacy_ifc(title, what):
    return (
        "Legacy IFC import: " + title,
        "An option of the legacy IFC importer (importIFClegacy.py), which the IFC preference "
        "pages no longer show: %s." % what,
    )


def _state(title, what):
    return (title, "%s Stored when it closes." % what)


# (title, documentation) or (title, documentation, items of a choice), by group and entry.
# None for a title or a documentation keeps what the preference page's widget gives.
DOCS = {
    "Mod/Draft": {
        "AnnotationStyleEditorHeight": _state(
            "Annotation style editor height",
            "Height, in pixels, of the annotation style editor.",
        ),
        "AnnotationStyleEditorWidth": _state(
            "Annotation style editor width",
            "Width, in pixels, of the annotation style editor.",
        ),
        "CenterPlaneOnView": (
            "Center working plane on view",
            "The check box of the working plane task panel that centres the working plane on "
            "the current view when one of the panel's plane buttons is pressed." + _TOGGLE,
        ),
        "ChainedMode": (
            "Chained mode",
            "The Chained mode check box of the Draft task panel, which the Dimension command "
            "shows: each dimension starts where the one before it ended." + _TOGGLE,
        ),
        "CopyMode": (
            "Copy",
            "The Copy check box of the Move and Rotate task panels, and of BIM's Copy command: "
            "the command works on a copy and keeps the original." + _TOGGLE,
        ),
        "DefaultAnnoDisplayMode": (
            "Annotation display mode",
            "Display mode new texts, dimensions and labels get: World or Screen.",
            ["World", "Screen"],
        ),
        "DefaultDisplayMode": (
            "Shape display mode",
            "Display mode new Draft shapes get, by its place in the list of the Set Style "
            "dialogue, which stores it.",
        ),
        "DefaultDrawStyle": (
            "Line draw style",
            "Draw style of the lines of new Draft shapes and new layers, by its place in the "
            "list of the Set Style dialogue, which stores it.",
        ),
        "DefaultPrintColor": ("Layer print colour", "Line print colour a new layer gets."),
        "DimAutoFlipText": (
            "Flip dimension text automatically",
            "Flips the text of a new dimension when the direction it is drawn in would leave "
            "the text upside down on the working plane.",
        ),
        "Draft_array_fuse": (
            "Array: fuse",
            "The Fuse check box of the array task panels: overlapping elements of the new "
            "array are fused." + _TOGGLE,
        ),
        "Draft_array_Link": (
            "Array: link array",
            "The Link array check box of the array task panels: the new array is made of "
            "links to the original instead of copies." + _TOGGLE,
        ),
        "Draft_array_build_shape": (
            "Array: build shape",
            "Whether a new link array builds a compound shape of its visible elements. Off, "
            "and with Fuse off, the array has no shape of its own, which saves recompute "
            "time. The array task panels store it; the path and point array commands read it.",
        ),
        "FilletChamferMode": (
            "Fillet: create chamfer",
            "The Create chamfer check box of the Fillet task panel." + _TOGGLE,
        ),
        "FilletDeleteMode": (
            "Fillet: delete original objects",
            "The Delete original objects check box of the Fillet task panel." + _TOGGLE,
        ),
        "FilletRadius": (
            "Fillet radius",
            "Radius, in mm, the Fillet task panel starts with: the one last used.",
        ),
        "GlobalMode": (
            "Global coordinates",
            "The Global check box of the Draft task panel: coordinates are entered in the "
            "global coordinate system instead of the working plane's." + _TOGGLE,
        ),
        "GridHideInOtherWorkbenches": (
            "Hide the grid in other workbenches",
            "Hides the Draft grid when a workbench that does not use it is activated. Off, "
            "the grid stays as it was.",
        ),
        "HatchPatternFile": (
            "Hatch pattern file",
            "The PAT file the Hatch dialogue takes its patterns from: the one last chosen "
            "there. BIM's covering tool reads it too.",
        ),
        "HatchPatternName": (
            "Hatch pattern",
            "Name of the pattern the Hatch dialogue starts with: the one last chosen.",
        ),
        "HatchPatternResolution": (
            "SVG pattern resolution",
            "Size, in pixels, of the image an SVG fill pattern of a Draft object is drawn to "
            "for the 3D view.",
        ),
        "HatchPatternRotation": (
            "Hatch rotation",
            "Rotation, in degrees, the Hatch dialogue starts with: the one last used.",
        ),
        "HatchPatternScale": (
            "Hatch scale",
            "Scale the Hatch dialogue starts with: the one last used.",
        ),
        "HatchPatternTranslate": (
            "Hatch: translate",
            "The Translate check box of the Hatch dialogue, which sets the Translate property "
            "of the new hatch. Stored when the dialogue is accepted.",
        ),
        "labeltype": (
            "Label type",
            "The type the Label command starts with: the one last chosen in its task panel.",
        ),
        "LayersManagerHeight": _state(
            "Layers manager height",
            "Height, in pixels, of the layers manager, Draft's and BIM's.",
        ),
        "LayersManagerWidth": _state(
            "Layers manager width",
            "Width, in pixels, of the layers manager, Draft's and BIM's.",
        ),
        "MakeFaceMode": (
            "Make face",
            "The Make face check box of the Draft task panel: a closed shape gets a face. New "
            "Draft objects take their Make Face property from it." + _TOGGLE,
        ),
        "maxSnapEdges": (
            "Maximum edges for snapping",
            "Objects with more edges than this are not searched for snap points. 0 is no limit.",
        ),
        "OffsetCopyMode": (
            "Offset: copy",
            "The Copy check box as the Offset command shows it: the offset is a new object "
            "and the original stays." + _TOGGLE,
        ),
        "Offset_OCC": (
            "OCC-style offset",
            "The OCC-style offset check box of the Offset task panel. Stored when the offset "
            "is made.",
        ),
        "RelativeMode": (
            "Relative coordinates",
            "The Relative check box of the Draft task panel: coordinates are relative to the "
            "last point." + _TOGGLE,
        ),
        "ScaleClone": (
            "Scale: create a clone",
            "The Create a clone check box of the Scale task panel." + _TOGGLE,
        ),
        "ScaleCopy": ("Scale: copy", "The Copy check box of the Scale task panel." + _TOGGLE),
        "ScaleRelative": (
            "Scale: working plane orientation",
            "The Working plane orientation option of the Scale task panel, whose check box is "
            "not shown at present: stored, and read by nothing.",
        ),
        "ScaleUniform": (
            "Scale: uniform scaling",
            "The Uniform scaling check box of the Scale task panel." + _TOGGLE,
        ),
        "ShapeStringFontFile": (
            "Shape string font file",
            "Font file the Shape String task panel starts with: the one last used, at first a "
            "sans serif font found on this computer. The labels of BIM's panel sheets use it "
            "too.",
        ),
        "ShapeStringHeight": (
            "Shape string height",
            "Height, in mm, the Shape String task panel starts with: the one last used.",
        ),
        "ShapeStringText": (
            "Shape string text",
            "Text the Shape String task panel starts with: the one last entered.",
        ),
        "showtray": (
            "Show the Draft tray",
            "Whether the Draft tray is shown when the Draft workbench is activated. Stored "
            "when the tray is shown or hidden.",
        ),
        "snapModes": (
            "Snap modes",
            "Which snap modes are on: one digit for each mode, 1 for on. Stored when a snap "
            "mode is toggled.",
        ),
        "snapRange": (
            "Snap range",
            "Distance, in pixels, within which the cursor snaps to a point. The working plane "
            "task panel and the in-command radius keys change it.",
        ),
        "SubelementMode": (
            "Subelement mode",
            "The subelement mode check box of the Move, Rotate and Scale task panels: the "
            "command works on the selected points and edges instead of whole objects."
            + _TOGGLE,
        ),
        "SvgLinesBlack": (
            "White lines black in SVG",
            "Writes white lines as black where Draft produces SVG for a drawing page, so that "
            "they show on paper.",
        ),
        "useSupport": (
            "Use the support object",
            "A Draft object drawn on a face of another object gets that object as its support.",
        ),
        "inCommandShortcutRelative": _shortcut("Relative", "toggles Relative"),
        "inCommandShortcutGlobal": _shortcut("Global", "toggles Global"),
        "inCommandShortcutLength": _shortcut(
            "Length", "locks the length and moves on to the angle"
        ),
        "inCommandShortcutMakeFace": _shortcut("Make face", "toggles Make face"),
        "inCommandShortcutSelectEdge": _shortcut(
            "Select edge", "starts picking an edge to take a direction from"
        ),
        "inCommandShortcutSubelementMode": _shortcut(
            "Subelement mode", "toggles the subelement mode"
        ),
        "inCommandShortcutCopy": _shortcut("Copy", "toggles Copy"),
        "inCommandShortcutUndo": _shortcut("Undo", "undoes the last segment"),
        "inCommandShortcutWipe": _shortcut("Wipe", "removes the segments drawn so far"),
        "inCommandShortcutClose": _shortcut("Close", "closes the line and finishes"),
        "inCommandShortcutExit": _shortcut("Exit", "finishes the command"),
        "inCommandShortcutContinue": _shortcut("Continue", "toggles Continue"),
        "inCommandShortcutCycleSnap": _shortcut(
            "Cycle snap", "cycles through the snap points at the cursor"
        ),
        "inCommandShortcutAddHold": _shortcut("Add hold", "adds a hold point at the cursor"),
        "inCommandShortcutSetWP": _shortcut("Set working plane", "sets the working plane"),
        "inCommandShortcutSnap": _shortcut("Snap", "toggles snapping"),
        "inCommandShortcutIncreaseRadius": _shortcut("Increase radius", "increases the snap range"),
        "inCommandShortcutDecreaseRadius": _shortcut("Decrease radius", "decreases the snap range"),
        "inCommandShortcutRestrictX": _shortcut("Restrict X", "restricts the movement to X"),
        "inCommandShortcutRestrictY": _shortcut("Restrict Y", "restricts the movement to Y"),
        "inCommandShortcutRestrictZ": _shortcut("Restrict Z", "restricts the movement to Z"),
        "inCommandShortcutRecenter": _shortcut(
            "Recenter", "recentres the working plane on the last point"
        ),
        "DWGConversion": (
            None,
            "How DWG files are converted to DXF. Automatic looks for the converters in the "
            "order of this list. If none is found, choose one and give its path below: the "
            "dwg2dxf utility of LibreDWG, ODAFileConverter of the ODA file converter, or the "
            "dwg2dwg utility of the pro version of QCAD.",
        ),
    },
    "Mod/Draft/OrthoArrayLinearMode": {
        "LinearModeOn": (
            "Orthogonal array: linear mode",
            "The linear mode switch of the orthogonal array task panel: the array runs along "
            "one axis only." + _TOGGLE,
        ),
        "AxisSelected": (
            "Orthogonal array: axis",
            "The axis of an orthogonal array in linear mode, X, Y or Z: the one last chosen.",
        ),
        "XInterval": (
            "Orthogonal array: X interval",
            "Distance between the elements along X, in mm, that the orthogonal array task "
            "panel starts with in linear mode.",
        ),
        "YInterval": (
            "Orthogonal array: Y interval",
            "Distance between the elements along Y, in mm, that the orthogonal array task "
            "panel starts with in linear mode.",
        ),
        "ZInterval": (
            "Orthogonal array: Z interval",
            "Distance between the elements along Z, in mm, that the orthogonal array task "
            "panel starts with in linear mode.",
        ),
        "XNumOfElements": (
            "Orthogonal array: number in X",
            "Number of elements along X that the orthogonal array task panel starts with in "
            "linear mode.",
        ),
        "YNumOfElements": (
            "Orthogonal array: number in Y",
            "Number of elements along Y that the orthogonal array task panel starts with in "
            "linear mode.",
        ),
        "ZNumOfElements": (
            "Orthogonal array: number in Z",
            "Number of elements along Z that the orthogonal array task panel starts with in "
            "linear mode.",
        ),
    },
    "Mod/Arch": {
        "applyConstructionStyle": (
            "Construction style for subcomponents",
            "Gives an object the construction colour when it becomes an addition or a "
            "subtraction of a BIM component.",
        ),
        "ClaimHosted": (
            "Show hosted objects under their host",
            "Lists the objects a BIM component hosts, such as the windows of a wall, under "
            "that component in the tree.",
        ),
        "CoveringAlignment": (
            "Covering: tile alignment",
            "Tile alignment a new covering gets: that of the covering last made with the "
            "Covering task panel, which stores it.",
        ),
        "CoveringFinishMode": (
            "Covering: finish mode",
            "Finish mode a new covering gets: that of the covering last made with the "
            "Covering task panel, which stores it.",
        ),
        "CoveringJoint": (
            "Covering: joint width",
            "Joint width, in mm, a new covering gets: that of the covering last made with the "
            "Covering task panel, which stores it.",
        ),
        "CoveringLength": (
            "Covering: tile length",
            "Tile length, in mm, a new covering gets: that of the covering last made with the "
            "Covering task panel, which stores it.",
        ),
        "CoveringThickness": (
            "Covering: tile thickness",
            "Tile thickness, in mm, a new covering gets: that of the covering last made with "
            "the Covering task panel, which stores it.",
        ),
        "CoveringRotation": (
            "Covering: rotation",
            "Rotation, in degrees, a new covering gets: that of the covering last made with "
            "the Covering task panel, which stores it.",
        ),
        "CoveringWidth": (
            "Covering: tile width",
            "Tile width, in mm, a new covering gets: that of the covering last made with the "
            "Covering task panel, which stores it.",
        ),
        "CustomIfcSchema": _legacy_ifc("custom schema", "the file of an IFC schema to use"),
        "createIfcGroups": _legacy_ifc("create groups", "puts the IFC groups in groups"),
        "forceIfcPythonParser": _legacy_ifc(
            "force the Python parser", "uses its own parser even when IfcOpenShell is there"
        ),
        "ifcAggregateWindows": _legacy_ifc("aggregate windows", "aggregates the windows"),
        "ifcAsMesh": _legacy_ifc(
            "types imported as meshes", "IFC types to import as meshes, separated by commas"
        ),
        "IfcExportList": _legacy_ifc(
            "export list", "also writes a text list of what its exporter wrote"
        ),
        "ifcJoinSolids": _legacy_ifc("join solids", "joins the solids of an object"),
        "IfcScalingFactor": _legacy_ifc("scaling factor", "the factor its exporter scales by"),
        "ifcSeparatePlacements": _legacy_ifc(
            "separate placements", "keeps the placements apart from the shapes"
        ),
        "DoorHeight": ("Door height", "Height, in mm, the Door tool starts with." + _LAST),
        "DoorWidth": ("Door width", "Width, in mm, the Door tool starts with." + _LAST),
        "DoorSill": (
            "Door sill height",
            "Sill height, in mm, the Door tool starts with." + _LAST,
        ),
        "DoorPreset": (
            "Door preset",
            "The preset the Door tool starts with, by its place in the tool's list: the one "
            "last chosen.",
        ),
        "FreeLinking": (
            "Free linking",
            "Lets the Building and Level commands put selected sites and buildings into the "
            "new object too, where they are otherwise left out with a warning.",
        ),
        "getStandardType": (
            "IFC export: standard case types",
            "Exports the objects that qualify under the StandardCase type of their IFC class, "
            "IfcWallStandardCase for a wall.",
        ),
        "ifcImportLayer": (
            "IFC import: layers",
            "Makes a Draft layer for each layer of an imported IFC file.",
        ),
        "ifcMergeProfiles": (
            "IFC export: merge profiles",
            "With compression on, writes a profile used by several objects once.",
        ),
        "MultiMaterialColumnWidth0": _state(
            "Multi-material editor: first column width",
            "Width, in pixels, of the first column of the multi-material editor.",
        ),
        "MultiMaterialColumnWidth1": _state(
            "Multi-material editor: second column width",
            "Width, in pixels, of the second column of the multi-material editor.",
        ),
        "PanelThickness": (
            "Panel thickness",
            "Thickness, in mm, the Panel tool starts with." + _LAST,
        ),
        "PrecastBase": _precast("base", "base height"),
        "PrecastChamfer": _precast("chamfer", "chamfer"),
        "PrecastDentHeight": _precast("dent height", "dent height"),
        "PrecastDentLength": _precast("dent length", "dent length"),
        "PrecastDentWidth": _precast("dent width", "dent width"),
        "PrecastDownLength": _precast("down length", "down length"),
        "PrecastGrooveDepth": _precast("groove depth", "groove depth"),
        "PrecastGrooveHeight": _precast("groove height", "groove height"),
        "PrecastGrooveSpacing": _precast("groove spacing", "groove spacing"),
        "PrecastHoleMajor": _precast("hole major diameter", "major diameter of the holes"),
        "PrecastHoleMinor": _precast("hole minor diameter", "minor diameter of the holes"),
        "PrecastHoleSpacing": _precast("hole spacing", "spacing of the holes"),
        "PrecastRiser": _precast("riser", "riser height"),
        "PrecastTread": _precast("tread", "tread depth"),
        "ProfilePreset": (
            "Profile preset",
            "The profile the Profile tool starts with: the one last chosen, as its values "
            "separated by semicolons.",
        ),
        "ScheduleColumnWidth0": _state(
            "Schedule editor: first column width",
            "Width, in pixels, of the first column of the schedule editor.",
        ),
        "ScheduleColumnWidth1": _state(
            "Schedule editor: second column width",
            "Width, in pixels, of the second column of the schedule editor.",
        ),
        "ScheduleColumnWidth2": _state(
            "Schedule editor: third column width",
            "Width, in pixels, of the third column of the schedule editor.",
        ),
        "ScheduleColumnWidth3": _state(
            "Schedule editor: fourth column width",
            "Width, in pixels, of the fourth column of the schedule editor.",
        ),
        "ScheduleDialogHeight": _state(
            "Schedule editor height", "Height, in pixels, of the schedule editor."
        ),
        "ScheduleDialogWidth": _state(
            "Schedule editor width", "Width, in pixels, of the schedule editor."
        ),
        "StructurePreset": (
            "Structure preset",
            "The profile the Structure tool starts with: the one last chosen. Empty for none.",
        ),
        "swallowAdditions": (
            "Site: additions as children",
            "Lists the additions of a site under the site in the tree.",
        ),
        "swallowSubtractions": (
            "Site: subtractions as children",
            "Lists the subtractions of a site under the site in the tree.",
        ),
        "WallAlignment": (
            "Wall alignment",
            "Alignment the Wall tool starts with, and a wall made by a script without one: "
            "the one last chosen in the tool's task panel.",
            ["Center", "Left", "Right"],
        ),
        "WallHeight": (
            "Wall height",
            "Height, in mm, the Wall tool starts with, and of a wall or curtain wall made "
            "without one." + _LAST,
        ),
        "WallWidth": (
            "Wall width",
            "Width, in mm, the Wall tool starts with, and of a wall made by a script without "
            "one." + _LAST,
        ),
        "WallOffset": (
            "Wall offset",
            "Offset, in mm, the Wall tool starts with, and of a wall made by a script without "
            "one." + _LAST,
        ),
        "WindowHeight": ("Window height", "Height, in mm, the Window tool starts with." + _LAST),
        "WindowWidth": ("Window width", "Width, in mm, the Window tool starts with." + _LAST),
        "WindowSill": (
            "Window sill height",
            "Sill height, in mm, the Window tool starts with." + _LAST,
        ),
        "WindowPreset": (
            "Window preset",
            "The preset the Window tool starts with, by its place in the tool's list: the one "
            "last chosen.",
        ),
        "WindowW1": (
            "Window: W1",
            "The W1 value of a window preset, the thickness of the fixed frame, in mm, that "
            "the Window tool starts with." + _LAST,
        ),
        # page settings without a tool tip
        "autoJoinWalls": (None, "The Wall tool joins a new wall with the walls it meets."),
        "MoveBase": (
            None,
            "New BIM components get their Move Base property on: moving the component moves "
            "its base object with it.",
        ),
        "MaxComputeAreas": (
            None,
            "A BIM component with more faces than this does not compute its vertical and "
            "horizontal areas, which takes long on a complex shape.",
        ),
        "ReferenceCheckInterval": (
            None,
            "How often, in seconds, an external reference checks whether its file has "
            "changed. Takes effect for the references made or loaded afterwards.",
        ),
        "SymbolLineThickness": (
            None,
            "How many times the normal line width the symbols of a section view are drawn "
            "with.",
        ),
        "WallColor": (None, "Colour new walls get."),
        "StructureColor": (None, "Colour new structural elements get."),
        "RebarColor": (None, "Colour new rebars get."),
        "WindowTransparency": (None, "Transparency, in percent, of the glass of new windows."),
        "WindowGlassColor": (None, "Colour of the glass of new windows."),
        "PanelColor": (None, "Colour new panels get."),
        "ColorHelpers": (None, "Colour of new helper objects: grids, axes, section planes."),
        "defaultSpaceTransparency": (None, "Transparency, in percent, new spaces get."),
        "defaultSpaceStyle": (None, "Draw style of the lines of new spaces."),
        "defaultSpaceColor": (None, "Colour of the lines of new spaces."),
        "StairsWidth": (None, "Width new stairs get."),
        "StairsLength": (None, "Length new stairs get."),
        "StairsHeight": (None, "Height new stairs get."),
        "StairsSteps": (None, "Number of steps new stairs get."),
        "PipeDiameter": (None, "Diameter new pipes get."),
        "RebarDiameter": (None, "Diameter new rebars get."),
        "RebarOffset": (None, "Distance new rebars keep from the faces of their host."),
        "sh3dShowDialog": (
            None,
            "Shows the import options each time a SweetHome3D file is imported.",
        ),
        "ifcMulticore": ("IFC import: cores", None),
    },
    "Mod/BIM": {
        "BIMSketchPlacementOnly": (
            "BIM sketch: placement only",
            "The BIM Sketch command only places the new sketch on the working plane, without "
            "giving it the line width, point size and colours of the view's defaults.",
        ),
        "WallBaseline": (
            None,
            "What a wall drawn with the Wall tool is based on: nothing, a Draft line, or a "
            "sketch. The tool's task panel stores it too.",
        ),
    },
}

DOCS["Mod/Arch"].update(_sizes("Beam", "Beam tool"))
DOCS["Mod/Arch"].update(_sizes("Column", "Column tool"))
for _dim in ("Length", "Width", "Height"):
    DOCS["Mod/Arch"]["Structure" + _dim] = (
        "Structure " + _dim.lower(),
        "%s, in mm, of a structure made by a script that gives none." % _dim,
    )
DOCS["Mod/Arch"].update(
    {k: v for k, v in _sizes("Panel", "Panel tool").items() if k != "PanelHeight"}
)
for _name in ("W2", "H1", "H2", "H3", "O1", "O2"):
    DOCS["Mod/Arch"]["Window" + _name] = (
        "Window: " + _name,
        "The %s value of a window preset, in mm, that the Window tool starts with." % _name
        + _LAST,
    )

DOCS["Mod/NativeIFC"] = {
    "AskBeforeSaving": (
        None,
        "Asks, when a document is saved, whether to save the modified IFC files of its "
        "projects too.",
    ),
    "SingleDoc": (
        None,
        "A new IFC document is locked: the document itself is the IFC project, instead of "
        "holding the project as one of its objects.",
    ),
    "SingleDocAskAgain": (
        "Ask every time whether to lock a new document",
        "Asks at each new IFC document whether to lock it.",
    ),
    "ProjectFull": (
        None,
        "A new IFC project is made with a default structure: a site, a building and a level.",
    ),
    "ProjectAskAgain": (
        "Ask every time which project to create",
        "Asks at each new IFC project whether to give it a default structure.",
    ),
}

# The preference pages whose settings the table does not read.
EXTRA_PAGES = (":/ui/preferencesNativeIFC.ui",)


def _dialog(name, what, width, height):
    return {
        name + "DialogWidth": (
            "int",
            width,
            what.capitalize() + " width",
            "Width, in pixels, of the %s. Stored when it closes." % what,
        ),
        name + "DialogHeight": (
            "int",
            height,
            what.capitalize() + " height",
            "Height, in pixels, of the %s. Stored when it closes." % what,
        ),
    }


# The settings BIM's code reads with the name written out, which neither the table nor a
# page has: (type, the default every reader passes, title, documentation), by group and
# entry. Left out on purpose, as no one default describes them: LibraryOnline (its
# default depends on whether a parts library is installed), BimViewWidth and
# BimViewHeight (stored as numbers and read as switches), and Mod/NativeIFC/SingleDoc is
# taken from its page although two readers disagree on it.
EXTRA = {
    "Mod/BIM": {
        "ScheduleAutoUpdate": (
            "bool",
            False,
            "Schedule: auto update",
            "The Auto update check box the schedule editor opens with. Stored when it closes.",
        ),
        "BimClassificationVisibleState": (
            "int",
            0,
            "Classification manager: only visible",
            "The check state of 'Only visible' in the classification manager: 0 off, 2 on."
            + _TOGGLE,
        ),
        "BimClassificationSystemNamePrefix": (
            "int",
            1,
            "Classification manager: system name prefix",
            "The check state of the prefix check box of the classification manager: the "
            "class is written with the name of its system in front." + _TOGGLE,
        ),
        "IfcPropertiesSelectedState": (
            "int",
            0,
            "IFC properties manager: only selected",
            "The check state of 'Only selected' in the IFC properties manager: 0 off, 2 on."
            + _TOGGLE,
        ),
        "IfcPropertiesVisibleState": (
            "int",
            0,
            "IFC properties manager: only visible",
            "The check state of 'Only visible' in the IFC properties manager: 0 off, 2 on."
            + _TOGGLE,
        ),
        "LibraryFCStdOnly": (
            "bool",
            False,
            "Library: FreeCAD files only",
            "The library panel lists FCStd files only." + _TOGGLE,
        ),
        "3DPreview": (
            "bool",
            False,
            "Library: 3D preview",
            "The library panel shows the selected item in the 3D view." + _TOGGLE,
        ),
        "LibraryPreview": (
            "bool",
            False,
            "Library: preview pane",
            "The library panel shows its preview pane. Stored when the pane is toggled.",
        ),
        "SaveThumbnails": (
            "bool",
            False,
            "Library: save thumbnails",
            "The library panel saves a thumbnail of an item it had to open to preview.",
        ),
        "LibraryDefaultInsert": (
            "int",
            0,
            "Library: insert mode",
            "How the library panel inserts an item, by its place in the panel's list: the "
            "mode last chosen.",
        ),
        "LibraryTimeStamp": (
            "uint",
            0,
            "Library: last update",
            "When the online library index was last refreshed, in seconds since 1970. "
            "Written by the library panel.",
        ),
        "LibraryWarning": (
            "bool",
            False,
            "Library: online warning seen",
            "Set once the warning about searching online has been answered, so that it is "
            "not shown again.",
        ),
        "FirstTime": (
            "bool",
            True,
            "First start of BIM",
            "Shows the welcome dialogue the first time the BIM workbench is activated. The "
            "welcome dialogue and the BIM setup switch it off.",
        ),
        "RestoreBimViews": (
            "bool",
            True,
            "Restore the views manager",
            "Shows the BIM views manager when the BIM workbench is activated. Stored as the "
            "manager is shown or hidden.",
        ),
        "BimViewArea": (
            "int",
            1,
            "Views manager: dock area",
            "The dock area the BIM views manager was last in, as Qt numbers it.",
        ),
        "BimViewFloat": (
            "bool",
            True,
            "Views manager: floating",
            "Whether the BIM views manager was last floating instead of docked.",
        ),
        "BimViewTabs": (
            "string",
            "",
            "Views manager: tabbed with",
            "The panels the BIM views manager was last tabbed with, separated by ';;'.",
        ),
        "ViewManagerColumnWidth": (
            "int",
            100,
            "Views manager: first column width",
            "Width, in pixels, of the first column of the BIM views manager.",
        ),
        "ViewManagerFloating": (
            "bool",
            False,
            "Views manager: floating (older key)",
            "Whether the BIM views manager floats, as an earlier version stored it; still "
            "read when the manager is made.",
        ),
        "TDTemplateDir": (
            "string",
            "",
            "TechDraw template folder",
            "The folder the BIM page command last took a TechDraw template from.",
        ),
        "lastIfcExplorerFolder": (
            "string",
            "",
            "IFC explorer: last folder",
            "The folder the IFC explorer last opened a file from.",
        ),
    },
    "Mod/Arch": {
        "DefaultClassificationSystem": (
            "string",
            "",
            "Default classification system",
            "The classification system the classification manager opens with: the one last "
            "used.",
        ),
    },
}
EXTRA["Mod/BIM"].update(_dialog("BimClassification", "classification manager", 629, 516))
EXTRA["Mod/BIM"].update(_dialog("BimIfcProperties", "IFC properties manager", 1200, 608))
EXTRA["Mod/BIM"].update(_dialog("BimIfcQuantities", "IFC quantities manager", 680, 512))
EXTRA["Mod/BIM"].update(_dialog("BimMaterial", "BIM material chooser", 230, 350))

_CONTINUE = {
    "Arc_3Points": "Arc From 3 Points",
    "BezCurve": "Bezier Curve",
    "Bspline": "B-Spline",
    "CubicBezCurve": "Cubic Bezier Curve",
}


def _continue_mode(entry):
    name = _CONTINUE.get(entry, entry)
    return (
        "Continue: " + name,
        "The Continue check box of the task panel while the %s command runs: the command "
        "starts again after each object, until it is cancelled." % name + _TOGGLE,
    )


def _prop(widget, name, kinds=("string", "cstring")):
    """The text of a property of a widget element of a .ui file, or None."""
    if widget is None:
        return None
    for elem in widget.findall("property"):
        if elem.attrib.get("name") == name:
            for kind in kinds:
                sub = elem.find(kind)
                if sub is not None:
                    return sub.text or ""
    return None


def _number(widget, name, default):
    text = _prop(widget, name, ("number", "double"))
    return float(default if text is None else text)


def _label(root, parents, widget):
    """The label of a widget that has no text of its own: the one that names it as its
    buddy, else the one before it in its row of the layout."""
    name = widget.attrib.get("name")
    for label in root.iter("widget"):
        if label.attrib.get("class") == "QLabel" and _prop(label, "buddy") == name:
            return label
    item = parents.get(widget)
    layout = parents.get(item) if item is not None else None
    if layout is None or layout.tag != "layout":
        return None
    items = [i for i in layout if i.tag == "item"]
    if "row" in item.attrib:
        column = int(item.attrib.get("column", "0"))
        best = None
        for other in items:
            label = other.find("widget")
            if (
                label is not None
                and label.attrib.get("class") == "QLabel"
                and other.attrib.get("row") == item.attrib["row"]
                and int(other.attrib.get("column", "0")) < column
            ):
                if best is None or int(other.attrib.get("column", "0")) > best[0]:
                    best = (int(other.attrib.get("column", "0")), label)
        return best[1] if best else None
    for other in reversed(items[: items.index(item)]):
        label = other.find("widget")
        if label is not None:
            return label if label.attrib.get("class") == "QLabel" else None
    return None


def _from_widget(root, widget, parents):
    """What the widget of a preference page tells about its setting."""
    cls = widget.attrib.get("class", "")
    title = None
    label = None
    if cls in _OWN_TEXT:
        title = _prop(widget, "text") or _prop(widget, "title")
    if not title:
        label = _label(root, parents, widget)
        title = _prop(label, "text")
    doc = _prop(widget, "toolTip") or _prop(label, "toolTip") or ""
    if "</" in doc:
        # a tool tip written as rich text
        doc = re.sub(r"<[^>]+>", "", doc)
    spec = {
        # the page's class is the context its texts are translated in
        "context": root.findtext("class") or "",
        "title": re.sub(r"&(?!&)", "", title or "").strip().rstrip(":").strip(),
        "doc": doc.strip(),
    }
    if cls == "Gui::PrefComboBox":
        items = [_prop(item, "text") or "" for item in widget.findall("item")]
        if items:
            spec.update(proxy="ComboBox", items=items)
    elif cls == "Gui::PrefSpinBox":
        spec.update(
            proxy="SpinBox",
            minimum=_number(widget, "minimum", 0),
            maximum=_number(widget, "maximum", 99),
            step=_number(widget, "singleStep", 1),
        )
    elif cls == "Gui::PrefDoubleSpinBox":
        spec.update(
            proxy="SpinBox",
            minimum=_number(widget, "minimum", 0),
            maximum=_number(widget, "maximum", 99.99),
            step=_number(widget, "singleStep", 1),
            decimals=int(_number(widget, "decimals", 2)),
        )
    elif cls == "Gui::PrefColorButton":
        spec.update(proxy="Color", transparency=False)
    elif cls == "Gui::PrefFileChooser":
        spec["proxy"] = "File"
    return spec


def describe(path, entry, typ, page=None, parents=None):
    """The keywords of `FreeCAD.registerParam()` for one row of the table, the default
    apart. `page` is the .ui root and widget of a page setting."""
    spec = {"context": "draft", "title": "", "doc": ""}
    if page is not None:
        root, widget = page
        if parents is None:
            parents = {}
        if root not in parents:
            parents[root] = {child: parent for parent in root.iter() for child in parent}
        spec = _from_widget(root, widget, parents[root])
    written = DOCS.get(path, {}).get(entry)
    if written is None and path == "Mod/Draft/ContinueMode":
        written = _continue_mode(entry)
    if written is not None:
        if written[0]:
            spec["title"] = written[0]
        if written[1]:
            spec["doc"] = written[1]
        if len(written) > 2:
            spec.update(proxy="ComboBox", items=list(written[2]), translateItems=False)
    if not spec["title"]:
        spec["title"] = entry
    if typ == "unsigned" and "proxy" not in spec:
        spec.update(proxy="Color", transparency=False)
    spec["namespace"] = "Draft" if path.startswith("Mod/Draft") else "BIM"
    return spec


def register(param_dict, page_widgets):
    """Describe the rows of `draftutils.params.PARAM_DICT` that are Draft's and BIM's own.

    `page_widgets` has, for a setting of a preference page, the root of its .ui file and
    its widget, by (path, entry).
    """
    add = getattr(App, "registerParam", None)
    if add is None:
        # no registry on this side: the sandbox guest, whose host has it
        return
    parents = {}

    def one(path, entry, typ, default, page=None, written=None):
        try:
            spec = describe(path, entry, typ, page, parents)
            if written:
                spec.update(title=written[0], doc=written[1])
            add(_PREFIX + path, entry, _TYPES[typ], default, **spec)
        except (KeyError, TypeError, ValueError) as err:
            App.Console.PrintWarning(
                "draftutils.params: %s/%s is not described: %s\n" % (path, entry, err)
            )

    for path, entries in param_dict.items():
        if not path.startswith(_OWN):
            continue
        for entry, (typ, default) in entries.items():
            one(path, entry, typ, default, page_widgets.get((path, entry)))
    for path, entry, typ, default, page in extra_page_settings():
        one(path, entry, typ, default, page)
    for path, entries in EXTRA.items():
        for entry, (typ, default, title, doc) in entries.items():
            if entry not in param_dict.get(path, {}):
                one(path, entry, typ, default, None, (title, doc))


def extra_page_settings():
    """The settings of `EXTRA_PAGES`: (path, entry, type, default, (root, widget)) each.

    These pages have check boxes and combo boxes only; a widget of another kind is
    skipped.
    """
    try:
        import xml.etree.ElementTree as ET
        from PySide import QtCore
    except ImportError:
        return []
    found = []
    for name in EXTRA_PAGES:
        file = QtCore.QFile(name)
        if not file.open(QtCore.QIODevice.ReadOnly | QtCore.QFile.Text):
            continue
        root = ET.fromstring(file.readAll().data().decode())
        file.close()
        for widget in root.iter("widget"):
            cls = widget.attrib.get("class", "")
            path, entry = _prop(widget, "prefPath"), _prop(widget, "prefEntry")
            if not path or not entry:
                continue
            if cls in _OWN_TEXT:
                checked = _prop(widget, "checked", ("bool",))
                found.append((path, entry, "bool", checked == "true", (root, widget)))
            elif cls == "Gui::PrefComboBox":
                index = int(_number(widget, "currentIndex", 0))
                found.append((path, entry, "int", index, (root, widget)))
    return found

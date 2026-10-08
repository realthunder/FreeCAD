# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************
'''Auto code generator for parameters in Preferences/Mod/Sketcher
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString, ParamHex, ParamColor

NameSpace = 'Sketcher'
ClassName = 'SketcherParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Sketcher'
ClassDoc = 'Convenient class to obtain the settings of the Sketcher'

# The settings of the group itself, and below them those of its
# sub-groups and those the Sketcher keeps in Preferences/View. Not listed, because the program keeps
# them for itself:
# which sections of the task panel are expanded, the last values of the
# polygon and array dialogs, the width of the datum dialog. The class lives
# in the App library because a new sketch reads some of these; the settings
# of the Gui library are here with it.
Params = [
    ParamBool('AutoRecompute', False,
        title = "Auto update",
        doc = "Recompute the active document after every sketch action. Set from\n"
              "the Auto-update box of the solver messages panel; applies at once."),
    ParamBool('AutoRemoveRedundants', False,
        title = "Auto remove redundant constraints",
        doc = "Remove redundant constraints automatically when they are detected.\n"
              "Works only while Auto-update is on. Applies at once."),
    ParamBool('ContinuousCreationMode', True,
        title = "Geometry creation continue mode",
        doc = "Keep a geometry tool active after it creates an element, so the\n"
              "next one can be drawn at once. Applies to the next element."),
    ParamBool('ContinuousConstraintMode', True,
        title = "Constraint creation continue mode",
        doc = "Keep a constraint tool active after it creates a constraint.\n"
              "Applies the next time a constraint tool is started."),
    ParamBool('ShowDialogOnDistanceConstraint', True,
        title = "Ask for value after creating a dimensional constraint",
        doc = "Ask for the value right after a dimensional constraint is created.\n"
              "Applies to the next constraint."),
    ParamFloat('RadiusDiameterConstraintDisplayBaseAngle', 15.0,
        title = "Radius and diameter label angle",
        doc = "Angle in degrees at which the label of a new radius or diameter\n"
              "constraint is placed. Applies to new constraints."),
    ParamFloat('RadiusDiameterConstraintDisplayAngleRandomness', 0.0,
        title = "Radius and diameter label angle spread",
        doc = "Random spread in degrees added around the base angle of new radius\n"
              "or diameter labels, so labels overlap less."),
    ParamInt('GeometryHistoryLevel', 1,
        title = "Geometry history level",
        doc = "How much geometry history a sketch keeps for stable element names:\n"
              "0 none, 1 default, above 1 keeps more. Read when a sketch object\n"
              "is created or loaded."),
    ParamFloat('ArcFitTolerance', 1e-6,
        title = "Arc fit tolerance",
        doc = "Tolerance used to fit arcs for a new sketch; copied into the\n"
              "sketch's ArcFitTolerance property when it is created."),
    ParamInt('ExternalBSplineMaxDegree', 5,
        title = "External B-spline maximum degree",
        doc = "Maximum degree of B-splines made from external geometry in a new\n"
              "sketch. Copied into the sketch when it is created."),
    ParamFloat('ExternalBSplineTolerance', 1e-4,
        title = "External B-spline tolerance",
        doc = "Tolerance of B-splines made from external geometry in a new\n"
              "sketch.0001. Copied into the sketch when it is created."),
    ParamBool('MakeInternals', True,
        title = "Generate internal faces",
        doc = "Generate internal faces from the closed regions of a new sketch.\n"
              "Copied into the sketch's MakeInternals property when it is\n"
              "created."),
    ParamBool('LeaveSketchWithEscape', True,
        title = "Esc can leave sketch edit mode",
        doc = "Let the Esc key leave sketch edit mode. Takes effect the next time\n"
              "a sketch is edited."),
    ParamBool('ShowSolverAdvancedWidget', False,
        title = "Show section 'Advanced solver control'",
        doc = "Show the Advanced solver control section in the sketch task panel.\n"
              "Takes effect the next time a sketch is edited."),
    ParamBool('RecalculateInitialSolutionWhileDragging', True,
        title = "Improve solving while dragging",
        doc = "Recalculate the solver's starting point while a point is dragged,\n"
              "which improves solving. Takes effect the next time a sketch is\n"
              "edited."),
    ParamBool('ExtendedConstraintInformation', False,
        title = "Extended constraint information",
        doc = "Show extended information for each entry of the constraint list.\n"
              "Applies at once."),
    ParamBool('HideInternalAlignment', False,
        title = "Hide internal alignment constraints",
        doc = "Hide internal alignment constraints in the constraint list.\n"
              "Applies at once."),
    ParamBool('VisualisationTrackingFilter', False,
        title = "Show only filtered constraints in the 3D view",
        doc = "Show in the 3D view only the constraints that pass the constraint\n"
              "list filter. Applies at once."),
    ParamBool('HideUnits', False,
        title = "Hide base length units",
        doc = "Hide the base length unit in dimension labels and cursor\n"
              "coordinates for unit systems that support it. Shown at the next\n"
              "redraw."),
    ParamBool('ShowDimensionalName', True,
        title = "Show dimensional constraint name",
        doc = "Show the name of a named dimensional constraint in its label,\n"
              "using the format string. Shown at the next redraw."),
    ParamString('DimensionalStringFormat', '%N = %V',
        title = "Dimensional constraint label format",
        doc = "Format of a named dimension's label: %N is the constraint name, %V\n"
              "its value. Shown at the next redraw."),
    ParamBool('ShowCursorCoords', True,
        title = "Show coordinates beside cursor",
        doc = "Show the coordinates next to the cursor while drawing in a sketch.\n"
              "Applies at once."),
    ParamBool('UseSystemDecimals', True,
        title = "Use system decimals for cursor coordinates",
        doc = "Show cursor coordinates with the number of decimals of the unit\n"
              "settings instead of a short form. Applies at once."),

    # ------------------------------------------------------------------
    # The sub-groups. Most of General and all of View reach a sketch in
    # edit through ViewProviderSketch, which is told of a change by the
    # parameter groups themselves: it keeps reading them and takes its
    # DEFAULTS from here. Not listed, because the program keeps them for
    # itself: which B-spline overlays are on, the filters of the
    # constraint and element lists, the picking mode a constraint tool
    # was left in. Not listed, because nothing reads them: SnapTolerance
    # and ArcCircleHelperVisible.

    # --- General
    ParamBool('AllowFaceExternalPick', True, subpath='General',
        title = "Allow picking faces as external geometry",
        doc = "Allow picking a face as external geometry. Set in the Edit\n"
              "controls of the sketch task panel; applies at once."),
    ParamBool('ViewBottomOnEdit', False, subpath='General',
        title = "View sketch from bottom",
        doc = "Look at the sketch from below instead of from above when it is\n"
              "edited. Set by the view-sketch-from-bottom commands."),
    ParamBool('AdjustCamera', True, subpath='General',
        title = "Adjust camera when a sketch is edited",
        doc = "Turn the camera to face the sketch plane when a sketch is opened\n"
              "for editing. Applies the next time a sketch is edited."),
    ParamBool('FitSketchOnEdit', True, subpath='General',
        title = "Fit sketch when it is edited",
        doc = "Fit the view to the sketch's geometry when it is opened for\n"
              "editing. Applies the next time a sketch is edited."),
    ParamBool('HideDependent', True, subpath='General',
        title = "Hide objects depending on the sketch",
        doc = "Hide the objects that depend on a sketch while it is edited, for\n"
              "new sketches."),
    ParamBool('ShowLinks', True, subpath='General',
        title = "Show objects the sketch links to",
        doc = "Keep the objects a sketch links to visible while it is edited, for\n"
              "new sketches."),
    ParamBool('ShowSupport', True, subpath='General',
        title = "Show the sketch's support",
        doc = "Keep the object a sketch is attached to visible while it is\n"
              "edited, for new sketches."),
    ParamBool('RestoreCamera', True, subpath='General',
        title = "Restore camera when leaving a sketch",
        doc = "Put the camera back where it was when editing of a sketch ends,\n"
              "for new sketches."),
    ParamBool('ForceOrtho', False, subpath='General',
        title = "Force orthographic camera in a sketch",
        doc = "Switch the view to an orthographic camera while a sketch is\n"
              "edited, and back afterwards, for new sketches. Needs\n"
              "RestoreCamera."),
    ParamBool('SectionView', False, subpath='General',
        title = "Section view in a sketch",
        doc = "Clip everything in front of the sketch plane while a sketch is\n"
              "edited, for new sketches."),
    ParamBool('AutoConstraints', True, subpath='General',
        title = "Automatic constraints",
        doc = "Suggest and apply automatic constraints while drawing, for new\n"
              "sketches."),
    ParamBool('AvoidRedundantAutoconstraints', True, subpath='General',
        title = "Avoid redundant automatic constraints",
        doc = "Do not create automatic constraints that would be redundant, for\n"
              "new sketches."),
    ParamBool('ShowOriginalColor', False, subpath='General',
        title = "Show original colours while editing",
        doc = "Draw the sketch in its original colours instead of the constraint-\n"
              "status colours while editing. Applies at once."),
    ParamBool('SketchAutoTransparentPick', False, subpath='General',
        title = "Transparent picking of external geometry",
        doc = "While picking external geometry, make objects transparent to\n"
              "picking so hidden edges can be chosen. Applies at once while the\n"
              "tool is active."),
    ParamFloat('ZHeight', 1e-6, subpath='General',
        title = "Height step between sketch layers",
        doc = "Height step between the drawing layers of a sketch in edit mode\n"
              "(lines, constraints, points). Raise it if elements flicker through\n"
              "each other. Applies at once."),
    ParamInt('AxisTransparency', 30, subpath='General',
        title = "Sketch axes transparency",
        doc = "Transparency of the sketch axes in edit mode, in percent. 0 to\n"
              "100. Applies at once."),
    ParamHex('FaceColor', 0x54ABFF7F, subpath='General', proxy=ParamColor(),
        title = "Internal face colour",
        doc = "Colour and opacity of the faces shown inside a sketch's closed\n"
              "regions. Applies at once to sketches that use automatic colours."),
    ParamBool('ShowGrid', True, subpath='General',
        title = "Show grid in new sketches",
        doc = "Show a grid in new sketches while they are edited. An existing\n"
              "sketch keeps its own setting."),
    ParamFloat('GridSize', 10.0, subpath='General/GridSize',
        title = "Grid spacing",
        doc = "Distance in millimetres between two grid lines of a new sketch;\n"
              "with automatic spacing, the spacing it starts from. A sketch that\n"
              "exists keeps its own."),
    ParamBool('GridAuto', True, subpath='General',
        title = "Automatic grid spacing",
        doc = "Let the grid spacing of new sketches adapt to the zoom level."),
    ParamInt('GridSizePixelThreshold', 15, subpath='General',
        title = "Grid pixel threshold",
        doc = "With auto spacing, the smallest distance in pixels between two\n"
              "grid lines before the grid switches to a coarser spacing. 3 to\n"
              "10000. Applies at once."),
    ParamInt('GridNumberSubdivision', 10, subpath='General',
        title = "Grid subdivisions",
        doc = "Number of grid cells between two major (division) lines. 1 to\n"
              "10000. Applies at once."),
    ParamInt('GridLinePattern', 0xFFFF, subpath='General',
        title = "Minor grid line pattern",
        doc = "Line pattern of the minor grid lines, as a 16 bit stipple mask.\n"
              "Applies at once."),
    ParamInt('GridDivLinePattern', 0xFFFF, subpath='General',
        title = "Major grid line pattern",
        doc = "Line pattern of the major grid lines, as a 16 bit stipple mask.\n"
              "Applies at once."),
    ParamInt('GridLineWidth', 1, subpath='General',
        title = "Minor grid line width",
        doc = "Width of the minor grid lines in pixels. 1 to 99. Applies at once."),
    ParamInt('GridDivLineWidth', 2, subpath='General',
        title = "Major grid line width",
        doc = "Width of the major grid lines in pixels. 1 to 99. Applies at once."),
    ParamHex('GridLineColor', 0xB2B2B2FF, subpath='General', proxy=ParamColor(),
        title = "Minor grid line colour",
        doc = "Colour of the minor grid lines. Applies at once."),
    ParamHex('GridDivLineColor', 0xB2B2B2FF, subpath='General', proxy=ParamColor(),
        title = "Major grid line colour",
        doc = "Colour of the major grid lines. Applies at once."),
    ParamInt('GridTransparency', 60, subpath='General',
        title = "Grid transparency",
        doc = "Transparency of the grid lines in percent, 0 is opaque. 0 to 100.\n"
              "Applies at once."),
    ParamInt('TopRenderGeometryId', 1, subpath='General',
        title = "Geometry drawn on top",
        doc = "Which kind of geometry is drawn on top in sketch edit mode: 1\n"
              "normal, 2 construction, 3 external. Set by dragging in the\n"
              "Rendering order list; applies at once."),
    ParamInt('MidRenderGeometryId', 2, subpath='General',
        title = "Geometry drawn in the middle",
        doc = "Which kind of geometry is drawn in the middle: 1 normal, 2\n"
              "construction, 3 external."),
    ParamInt('LowRenderGeometryId', 3, subpath='General',
        title = "Geometry drawn at the bottom",
        doc = "Which kind of geometry is drawn at the bottom: 1 normal, 2\n"
              "construction, 3 external."),
    ParamBool('BSplineExternalVisible', False, subpath='General',
        title = "B-spline information of external geometry",
        doc = "Show the B-spline information overlays for external B-splines too."),
    ParamBool('EditDatumInPlace', True, subpath='General',
        title = "Edit dimensions at their label",
        doc = "Edit a dimension's value in a field at its label instead of in a\n"
              "dialog. Applies to the next edit."),
    ParamBool('DatumEscapeTakesBack', False, subpath='General',
        title = "Esc cancels a dimension being typed",
        doc = "Esc cancels the dimension value being typed at a label instead of\n"
              "leaving the field with the value entered."),
    ParamBool('ShowDirectionalAutoConstraintHints', True, subpath='General',
        title = "Show hints for direction constraints",
        doc = "Show helper lines for direction based automatic constraints (line\n"
              "extension, parallel, perpendicular) while drawing. Applies at\n"
              "once."),
    ParamInt('DragAutoConstraintDelay', 400, subpath='General',
        title = "Delay of automatic constraints while dragging",
        doc = "Time in milliseconds the pointer must rest while dragging before\n"
              "an automatic constraint is offered. 0 to 5000."),
    ParamBool('NotifyConstraintSubstitutions', True, subpath='General',
        title = "Notify automatic constraint substitutions",
        doc = "Show a message when the Sketcher replaces constraints\n"
              "automatically, for example a coincident and a tangent by an\n"
              "endpoint tangency."),

    # --- View
    ParamInt('EdgeWidth', 2, subpath='View',
        title = "Normal geometry line width",
        doc = "Line width of normal geometry in edit mode, in pixels. 1 to 99.\n"
              "Applies at once."),
    ParamInt('EdgePattern', 0xFFFF, subpath='View',
        title = "Normal geometry line pattern",
        doc = "Line pattern of normal geometry in edit mode, a 16 bit stipple\n"
              "mask."),
    ParamInt('ConstructionWidth', 2, subpath='View',
        title = "Construction geometry line width",
        doc = "Line width of construction geometry in edit mode, in pixels."),
    ParamInt('ConstructionPattern', 0xFCFC, subpath='View',
        title = "Construction geometry line pattern",
        doc = "Line pattern of construction geometry in edit mode."),
    ParamInt('InternalWidth', 2, subpath='View',
        title = "Internal alignment line width",
        doc = "Line width of internal alignment geometry in edit mode, in pixels."),
    ParamInt('InternalPattern', 0xFCFC, subpath='View',
        title = "Internal alignment line pattern",
        doc = "Line pattern of internal alignment geometry in edit mode."),
    ParamInt('ExternalWidth', 2, subpath='View',
        title = "External geometry line width",
        doc = "Line width of external geometry in edit mode, in pixels."),
    ParamInt('ExternalPattern', 0xFCFC, subpath='View',
        title = "External geometry line pattern",
        doc = "Line pattern of external geometry in edit mode."),
    ParamInt('ExternalDefiningWidth', 2, subpath='View',
        title = "Defining external geometry line width",
        doc = "Line width of defining external geometry in edit mode, in pixels."),
    ParamInt('ExternalDefiningPattern', 0xFFFF, subpath='View',
        title = "Defining external geometry line pattern",
        doc = "Line pattern of defining external geometry in edit mode."),
    ParamInt('InformationWidth', 1, subpath='View',
        title = "Information layer line width",
        doc = "Line width of the information layer (B-spline polygons, combs,\n"
              "hints), in pixels."),
    ParamInt('InformationPattern', 0xFCFC, subpath='View',
        title = "Information layer line pattern",
        doc = "Line pattern of the information layer."),
    ParamInt('DimensionalConstraintLineWidth', 2, subpath='View',
        title = "Dimensional constraint line width",
        doc = "Line width of dimensional constraints, in pixels. 1 to 4."),
    ParamInt('DimensionalConstraintLinePattern', 0xFFFF, subpath='View',
        title = "Dimensional constraint line pattern",
        doc = "Line pattern of dimensional constraints."),
    ParamInt('AxisLineWidth', 2, subpath='View',
        title = "Sketch axes line width",
        doc = "Line width of the sketch axes in edit mode, in pixels."),
    ParamInt('AxisLinePattern', 0xFFFF, subpath='View',
        title = "Sketch axes line pattern",
        doc = "Line pattern of the sketch axes in edit mode."),

    # --- dimensioning
    ParamBool('SingleDimensioningTool', True, subpath='dimensioning',
        title = "Single dimension tool",
        doc = "Put the single Dimension tool on the Sketcher tool bar. Together\n"
              "with the separated tools option this gives the modes Single tool,\n"
              "Separated tools, Both. Tool bars are rebuilt when the page is\n"
              "saved."),
    ParamBool('SeparatedDimensioningTools', False, subpath='dimensioning',
        title = "Separated dimension tools",
        doc = "Put the separate dimension tools (horizontal, vertical, distance,\n"
              "radius/diameter, angle, lock) on the Sketcher tool bar."),
    ParamBool('DimensioningDiameter', True, subpath='dimensioning',
        title = "Dimension tool makes diameters",
        doc = "Let the Dimension tool create diameters. With radius also on the\n"
              "tool picks: diameter for circles, radius for arcs."),
    ParamBool('DimensioningRadius', True, subpath='dimensioning',
        title = "Dimension tool makes radii",
        doc = "Let the Dimension tool create radii. With diameter also on the\n"
              "tool picks: diameter for circles, radius for arcs."),
    ParamInt('AutoScaleMode', 2, subpath='dimensioning',
        title = "Scale sketch to first dimension",
        doc = "Scale the whole sketch to the value of its first dimension: 0\n"
              "always, 1 never, 2 only when no feature that fixes the scale is\n"
              "visible. Applies to the next dimension."),

    # --- Tools
    ParamInt('OnViewParameterVisibility', 1, subpath='Tools',
        title = "On-view parameters",
        doc = "Which on-view parameters a drawing tool shows at the cursor: 0\n"
              "none, 1 dimensions only, 2 position and dimensions. Applies to the\n"
              "next tool started."),

    # --- Constraints
    ParamBool('UnifiedCoincident', True, subpath='Constraints',
        title = "Unify coincident and point-on-object",
        doc = "Use one tool for coincident and point-on-object constraints. Tool\n"
              "bars and menus follow when the page is saved; the shortcuts follow\n"
              "after a restart."),
    ParamBool('AutoHorVer', True, subpath='Constraints',
        title = "Automatic horizontal or vertical tool",
        doc = "Use one tool that chooses between a horizontal and a vertical\n"
              "constraint. Tool bars follow when the page is saved."),

    # --- Commands
    ParamBool('UnifiedLineCommands', True, subpath='Commands',
        title = "Group line and polyline",
        doc = "Group the polyline and line commands under one tool bar button.\n"
              "Tool bars follow when the page is saved."),

    # --- Snap
    ParamBool('Snap', True, subpath='Snap',
        title = "Snap",
        doc = "Master switch for snapping in sketch edit mode. Toggled by the\n"
              "Snap tool bar button; applies at once."),
    ParamBool('SnapToObjects', True, subpath='Snap',
        title = "Snap to objects",
        doc = "Snap new points to the preselected object, and to the middle of\n"
              "lines and arcs. Applies at once."),
    ParamBool('SnapToGrid', False, subpath='Snap',
        title = "Snap to grid",
        doc = "Snap new points to the nearest grid line when closer than a fifth\n"
              "of the grid spacing. Applies at once."),
    ParamFloat('SnapAngle', 5.0, subpath='Snap',
        title = "Snap angle",
        doc = "Angular step in degrees for tools that snap at an angle while Ctrl\n"
              "is held, measured from the sketch's positive X axis. Applies at\n"
              "once."),

    # --- Elements
    ParamInt('ElementIconSize', 32, subpath='Elements',
        title = "Element list icon size",
        doc = "Size in pixels of the icons in the element list of the sketch task\n"
              "panel. 16 to 128. Takes effect the next time a sketch is edited."),

    # ------------------------------------------------------------------
    # The keys the Sketcher keeps in Preferences/View, the 3D view's group.
    # A colour's default is what the Appearance page shows for it; the
    # sketch used to take it from a constant of its own, which was the
    # same colour to within a step or two of rounding in four cases, and
    # another colour in one: external geometry, where the page is the one
    # that was changed. EditSketcherFontSize and ConstraintSymbolSize are 0
    # while they are not set, and 0 is the height of the application's
    # font, which is not a number a definition can hold.
    # CursorCrosshairColor is on the Sketcher's page and read by Gui.
    ParamString('EditSketcherFontName', '', subpath='User parameter:BaseApp/Preferences/View',
        title = "Sketch label font",
        doc = "Font family of the dimension labels in sketch edit mode. Empty\n"
              "means the labels' own font. Applies at once."),
    ParamInt('EditSketcherFontSize', 0, subpath='User parameter:BaseApp/Preferences/View',
        title = "Sketch label font size",
        doc = "Size in pixels of the dimension labels in sketch edit mode. 0, the\n"
              "value while it is not set, means the height of the application's\n"
              "font. Applies at once."),
    ParamInt('ConstraintSymbolSize', 0, subpath='User parameter:BaseApp/Preferences/View',
        title = "Constraint symbol size",
        doc = "Size in pixels of the constraint symbols in sketch edit mode, 6 at\n"
              "least. 0, the value while it is not set, means the height of the\n"
              "application's font. Applies at once."),
    ParamInt('ConstraintIconLabelsPerLine', 10, subpath='User parameter:BaseApp/Preferences/View',
        title = "Constraint numbers per line",
        doc = "How many constraint numbers fit on one line of the label beside a\n"
              "combined constraint icon, 1 to 100."),
    ParamInt('ConstraintIconLabelLines', 3, subpath='User parameter:BaseApp/Preferences/View',
        title = "Lines of constraint numbers",
        doc = "Largest number of lines of the label beside a combined constraint\n"
              "icon, 1 to 100."),
    ParamFloat('ViewScalingFactor', 1.0, subpath='User parameter:BaseApp/Preferences/View',
        title = "Sketch view scale factor",
        doc = "Scale factor of the fixed pixel sizes of sketch edit mode (points,\n"
              "constraint lines), 0.5 to 5. Applies at once."),
    ParamInt('SegmentsPerGeometry', 50, subpath='User parameter:BaseApp/Preferences/View',
        title = "Segments per geometry",
        doc = "Number of straight segments a curve is drawn with in sketch edit\n"
              "mode. Applies at once."),
    ParamHex('CursorTextColor', 0x0000FFFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Cursor text colour",
        doc = "Colour of the coordinate text shown at the cursor in sketch edit\n"
              "mode. Takes effect the next time a sketch is edited."),
    ParamHex('SketchEdgeColor', 0xFFFFFFFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Sketch edge colour",
        doc = "Colour of a sketch's edges outside edit mode, for sketches that\n"
              "use automatic colours. Applies at once."),
    ParamHex('SketchVertexColor', 0xFFFFFFFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Sketch vertex colour",
        doc = "Colour of a sketch's vertices outside edit mode, for sketches that\n"
              "use automatic colours. Applies at once."),
    ParamHex('EditedEdgeColor', 0xFFFFFFFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Geometry colour",
        doc = "Colour of normal geometry in sketch edit mode. Applies at once."),
    ParamHex('ConstructionColor', 0x0000DCFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Construction geometry colour",
        doc = "Colour of construction geometry in sketch edit mode. Applies at\n"
              "once."),
    ParamHex('InternalAlignedGeoColor', 0xB2B27FFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Internal alignment colour",
        doc = "Colour of internal alignment geometry in sketch edit mode. Applies\n"
              "at once."),
    ParamHex('FullyConstraintElementColor', 0x80D0A0FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Fully constrained geometry colour",
        doc = "Colour of a fully constrained element of normal geometry in sketch\n"
              "edit mode. Applies at once."),
    ParamHex('FullyConstraintConstructionElementColor', 0x8FA9FDFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Fully constrained construction colour",
        doc = "Colour of a fully constrained element of construction geometry in\n"
              "sketch edit mode. Applies at once."),
    ParamHex('FullyConstraintInternalAlignmentColor', 0xDEDEC8FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Fully constrained internal alignment colour",
        doc = "Colour of a fully constrained element of internal alignment\n"
              "geometry in sketch edit mode. Applies at once."),
    ParamHex('InvalidSketchColor', 0xFF6D00FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Invalid sketch colour",
        doc = "Colour of the geometry of a sketch with conflicting or redundant\n"
              "constraints in sketch edit mode. Applies at once."),
    ParamHex('FullyConstrainedColor', 0x00FF00FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Fully constrained sketch colour",
        doc = "Colour of the geometry of a fully constrained sketch in sketch\n"
              "edit mode. Applies at once."),
    ParamHex('ConstrainedDimColor', 0xFF2600FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Dimensional constraint colour",
        doc = "Colour of dimensional constraints in sketch edit mode. Applies at\n"
              "once."),
    ParamHex('ConstrainedIcoColor', 0xFF2600FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Constraint symbol colour",
        doc = "Colour of constraint symbols in sketch edit mode. Applies at once."),
    ParamHex('NonDrivingConstrDimColor', 0x0026FFFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Reference constraint colour",
        doc = "Colour of reference (non-driving) dimensional constraints in\n"
              "sketch edit mode. Applies at once."),
    ParamHex('ExprBasedConstrDimColor', 0xFF7F26FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Expression constraint colour",
        doc = "Colour of dimensional constraints whose value is an expression in\n"
              "sketch edit mode. Applies at once."),
    ParamHex('DeactivatedConstrDimColor', 0xCCCCCCFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Deactivated constraint colour",
        doc = "Colour of deactivated constraints in sketch edit mode. Applies at\n"
              "once."),
    ParamHex('ExternalColor', 0xCC3399FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "External geometry colour",
        doc = "Colour of external geometry in sketch edit mode. Applies at once."),
    ParamHex('ExternalDefiningColor', 0xCC3399FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Defining external geometry colour",
        doc = "Colour of defining external geometry in sketch edit mode. Applies\n"
              "at once."),
    ParamHex('InformationColor', 0x00FF00FF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Information layer colour",
        doc = "Colour of the information layer -- B-spline polygons, combs, hints\n"
              "-- in sketch edit mode. Applies at once."),
    ParamHex('FrozenColor', 0x7FFFFFFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Frozen external geometry colour",
        doc = "Colour of frozen external geometry in sketch edit mode. Applies at\n"
              "once."),
    ParamHex('DetachedColor', 0x1C7F1CFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Detached external geometry colour",
        doc = "Colour of detached external geometry in sketch edit mode. Applies\n"
              "at once."),
    ParamHex('MissingColor', 0x7F00FFFF, subpath='User parameter:BaseApp/Preferences/View', proxy=ParamColor(transparency=False),
        title = "Missing external geometry colour",
        doc = "Colour of external geometry whose source is missing in sketch edit\n"
              "mode. Applies at once."),

    # ------------------------------------------------------------------
    # The solver, sub-group SolverAdvanced: set in the "Advanced solver
    # control" box of the sketch task panel, which gives them to the
    # solver of the sketch in edit. The tolerances are kept as text.
    ParamInt('DefaultSolver', 2, subpath='SolverAdvanced',
        title = "Sketch solver",
        doc = "Algorithm a sketch is solved with: 0 BFGS, 1 Levenberg-Marquardt,\n"
              "2 DogLeg. Used at the next solve."),
    ParamInt('DogLegGaussStep', 0, subpath='SolverAdvanced',
        title = "DogLeg Gauss step",
        doc = "Gauss step of the DogLeg solver: 0 FullPivLU, 1\n"
              "LeastNormFullPivLU, 2 LeastNormLdlt."),
    ParamInt('MaxIter', 100, subpath='SolverAdvanced',
        title = "Solver iterations",
        doc = "Largest number of iterations of the sketch solver, up to 999."),
    ParamBool('SketchSizeMultiplier', False, subpath='SolverAdvanced',
        title = "Sketch size multiplier",
        doc = "Multiply the iteration limit of the sketch solver by the number of\n"
              "parameters of the sketch."),
    ParamString('Convergence', '1E-10', subpath='SolverAdvanced',
        title = "Solver convergence",
        doc = "Squared error below which a solution of the sketch solver counts\n"
              "as converged. Text, read as a number."),
    ParamInt('QRMethod', 1, subpath='SolverAdvanced',
        title = "QR method",
        doc = "QR decomposition used to diagnose a sketch: 0 dense, 1 sparse."),
    ParamString('QRPivotThreshold', '1E-13', subpath='SolverAdvanced',
        title = "QR pivot threshold",
        doc = "Values below this are taken for zero while a sketch is diagnosed\n"
              "by QR decomposition. Text, read as a number."),
    ParamInt('RedundantDefaultSolver', 2, subpath='SolverAdvanced',
        title = "Redundant solver",
        doc = "Algorithm used to find redundant constraints: 0 BFGS, 1 Levenberg-\n"
              "Marquardt, 2 DogLeg."),
    ParamInt('RedundantSolverMaxIterations', 100, subpath='SolverAdvanced',
        title = "Redundant solver iterations",
        doc = "Largest number of iterations of the solver that finds redundant\n"
              "constraints, up to 999."),
    ParamBool('RedundantSketchSizeMultiplier', False, subpath='SolverAdvanced',
        title = "Redundant sketch size multiplier",
        doc = "Multiply the iteration limit of the solver that finds redundant\n"
              "constraints by the number of parameters of the sketch."),
    ParamString('RedundantConvergence', '1E-10', subpath='SolverAdvanced',
        title = "Redundant solver convergence",
        doc = "Squared error below which a solution counts as converged, for the\n"
              "solver that finds redundant constraints. Text, read as a number."),
    ParamInt('DebugMode', 1, subpath='SolverAdvanced',
        title = "Solver console mode",
        doc = "Messages of the sketch solver in the report view: 0 none, 1\n"
              "minimal, 2 at every iteration."),
    ParamString('LM_eps', '1e-10', subpath='SolverAdvanced',
        title = "Levenberg-Marquardt eps",
        doc = "Tolerance eps of the Levenberg-Marquardt solver. Text, read as a\n"
              "number."),
    ParamString('LM_eps1', '1e-80', subpath='SolverAdvanced',
        title = "Levenberg-Marquardt eps1",
        doc = "Tolerance eps1 of the Levenberg-Marquardt solver. Text, read as a\n"
              "number."),
    ParamString('LM_tau', '0.001', subpath='SolverAdvanced',
        title = "Levenberg-Marquardt tau",
        doc = "Factor tau of the Levenberg-Marquardt solver. Text, read as a\n"
              "number."),
    ParamString('DL_tolg', '1e-80', subpath='SolverAdvanced',
        title = "DogLeg tolg",
        doc = "Tolerance tolg of the DogLeg solver. Text, read as a number."),
    ParamString('DL_tolx', '1e-80', subpath='SolverAdvanced',
        title = "DogLeg tolx",
        doc = "Tolerance tolx of the DogLeg solver. Text, read as a number."),
    ParamString('DL_tolf', '1e-10', subpath='SolverAdvanced',
        title = "DogLeg tolf",
        doc = "Tolerance tolf of the DogLeg solver. Text, read as a number."),
    ParamString('Redundant_LM_eps', '1e-10', subpath='SolverAdvanced',
        title = "Redundant Levenberg-Marquardt eps",
        doc = "Tolerance eps of Levenberg-Marquardt when it looks for redundant\n"
              "constraints. Text, read as a number."),
    ParamString('Redundant_LM_eps1', '1e-80', subpath='SolverAdvanced',
        title = "Redundant Levenberg-Marquardt eps1",
        doc = "Tolerance eps1 of Levenberg-Marquardt when it looks for redundant\n"
              "constraints. Text, read as a number."),
    ParamString('Redundant_LM_tau', '0.001', subpath='SolverAdvanced',
        title = "Redundant Levenberg-Marquardt tau",
        doc = "Factor tau of Levenberg-Marquardt when it looks for redundant\n"
              "constraints. Text, read as a number."),
    ParamString('Redundant_DL_tolg', '1e-80', subpath='SolverAdvanced',
        title = "Redundant DogLeg tolg",
        doc = "Tolerance tolg of DogLeg when it looks for redundant constraints.\n"
              "Text, read as a number."),
    ParamString('Redundant_DL_tolx', '1e-80', subpath='SolverAdvanced',
        title = "Redundant DogLeg tolx",
        doc = "Tolerance tolx of DogLeg when it looks for redundant constraints.\n"
              "Text, read as a number."),
    ParamString('Redundant_DL_tolf', '1e-10', subpath='SolverAdvanced',
        title = "Redundant DogLeg tolf",
        doc = "Tolerance tolf of DogLeg when it looks for redundant constraints.\n"
              "Text, read as a number."),
    ParamBool('ParameterQRKeepsColumnOrder', False, subpath='SolverAdvanced',
        title = "QR keeps the column order",
        doc = "Keep the order of the columns when a sketch's parameters are\n"
              "diagnosed by QR decomposition. For comparing results."),
    ParamBool('SkipUnneededConstraintQR', True, subpath='SolverAdvanced',
        title = "Skip unneeded constraint QR",
        doc = "Skip the QR decomposition of the constraints when a sketch is\n"
              "diagnosed and it is not needed. For comparing results."),
    ParamBool('FillJacobianFromConstraintParams', True, subpath='SolverAdvanced',
        title = "Fill Jacobian from constraint parameters",
        doc = "Fill the Jacobian of a sketch from the parameters each constraint\n"
              "names instead of from all of them. For comparing results."),
    # --- what the program keeps for itself. Their readers read the group
    # as before. SelectedConstraintFilters of the General group is not
    # here: its default is one bit for each entry of the filter list.
    ParamBool('ExpandedMessagesWidget', True,
        title = 'Sketch panel: solver messages open',
        doc = "The Solver messages box of the sketch task panel was open when the\n"
              "sketch was last left. Stored by the program."),
    ParamBool('ExpandedSolverAdvancedWidget', False,
        title = 'Sketch panel: advanced solver control open',
        doc = "The Advanced solver control box of the sketch task panel was open\n"
              "when the sketch was last left. Stored by the program."),
    ParamBool('ExpandedEditControlWidget', False,
        title = 'Sketch panel: edit controls open',
        doc = "The Edit controls box of the sketch task panel was open when the\n"
              "sketch was last left. Stored by the program."),
    ParamBool('ExpandedConstraintsWidget', True,
        title = 'Sketch panel: constraints open',
        doc = "The Constraints box of the sketch task panel was open when the\n"
              "sketch was last left. Stored by the program."),
    ParamBool('ExpandedElementsWidget', True,
        title = 'Sketch panel: elements open',
        doc = "The Elements box of the sketch task panel was open when the sketch\n"
              "was last left. Stored by the program."),
    ParamInt('DatumDialogWidth', 0,
        title = 'Datum dialog width',
        doc = "Width in pixels the dialog that asks for the value of a\n"
              "dimensional constraint last had; used when above 100. Stored by\n"
              "the program when the dialog closes."),
    ParamInt('ConstraintExternalPick', 0, subpath='General',
        title = 'Constraint tools: picking outside geometry',
        doc = "The mode the constraint tools were last left in for picking\n"
              "geometry outside the sketch, 0 for off; the next constraint tool\n"
              "starts in it. Stored when an external geometry command is pressed\n"
              "inside a constraint tool."),
    ParamBool('ConstraintFilterEnabled', True, subpath='General',
        title = 'Constraint list: filter on',
        doc = "The filter of the constraint list of the sketch task panel was\n"
              "last on. Stored when its box is toggled."),
    ParamInt('ElementFilterState', 0x7fffffff, subpath='General',
        title = 'Element list: filter',
        doc = "Which kinds of elements the element list of the sketch task panel\n"
              "last showed, a bit for each entry of its filter; all of them\n"
              "while it has not been used. Stored when the filter changes."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

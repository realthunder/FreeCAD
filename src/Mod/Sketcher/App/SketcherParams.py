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

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString

NameSpace = 'Sketcher'
ClassName = 'SketcherParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Sketcher'
ClassDoc = 'Convenient class to obtain the settings of the Sketcher'

# The settings of the group itself. Its sub-groups (General, View, Snap,
# SolverAdvanced, ...) and the keys the Sketcher keeps in Preferences/View
# are not here yet. Not listed, because the program keeps them for itself:
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
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

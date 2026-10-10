# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/ReverseEngineering/BSplineFit.

They are what the Fit B-spline surface dialog was last left with: its
widgets store them when it closes and read them when it opens, so no
code names them and no class is generated for them. They are described to
the settings registry when Init.py imports this file (freecad.params), so
that the omni search lists them with the others. The title, the default and
the range of each are the dialog's; the documentation is what
ReverseEngineering.approxSurface() does with the value.
"""
import sys

from freecad.params import ParamBool, ParamFloat, ParamInt, ParamSpinBox, register

NameSpace = "ReverseEngineering"
ClassName = "ReverseEngineeringParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/ReverseEngineering"

_KEPT = " Stored when the Fit B-spline surface dialog closes, read when it opens."

Params = [
    ParamInt(
        "BSplineFitUDegree",
        2,
        subpath="BSplineFit",
        param_name="UDegree",
        proxy=ParamSpinBox(1, 11, 1),
        title="B-spline fit: degree in u",
        doc="The degree of the fitted surface in its u parametric direction." + _KEPT,
    ),
    ParamInt(
        "BSplineFitNbUPoles",
        6,
        subpath="BSplineFit",
        param_name="NbUPoles",
        proxy=ParamSpinBox(2, 100, 1),
        title="B-spline fit: control points in u",
        doc="The number of control points of the fitted surface in its u parametric "
        "direction." + _KEPT,
    ),
    ParamInt(
        "BSplineFitVDegree",
        2,
        subpath="BSplineFit",
        param_name="VDegree",
        proxy=ParamSpinBox(1, 11, 1),
        title="B-spline fit: degree in v",
        doc="The degree of the fitted surface in its v parametric direction." + _KEPT,
    ),
    ParamInt(
        "BSplineFitNbVPoles",
        6,
        subpath="BSplineFit",
        param_name="NbVPoles",
        proxy=ParamSpinBox(2, 100, 1),
        title="B-spline fit: control points in v",
        doc="The number of control points of the fitted surface in its v parametric "
        "direction." + _KEPT,
    ),
    ParamInt(
        "BSplineFitIterations",
        5,
        subpath="BSplineFit",
        param_name="Iterations",
        proxy=ParamSpinBox(-1, 100, 1),
        title="B-spline fit: iterations",
        doc="The number of iterations of the fit; the parameters of the points are "
        "corrected at each." + _KEPT,
    ),
    ParamFloat(
        "BSplineFitSizeFactor",
        1.0,
        subpath="BSplineFit",
        param_name="Size factor",
        proxy=ParamSpinBox(1.0, 2.0, 0.01, 2),
        title="B-spline fit: size factor",
        doc="Makes the fitted surface larger than the points it is fitted to: 1 is "
        "the extent of the points, 2 three times it." + _KEPT,
    ),
    ParamBool(
        "BSplineFitUserDefinedUVDir",
        False,
        subpath="BSplineFit",
        param_name="User-Defined UVDir",
        title="B-spline fit: user-defined u/v directions",
        doc="The u and v directions of the fitted surface are the x and y axes of a "
        "selected placement object. Off, they come from the best-fit plane of the "
        "points." + _KEPT,
    ),
    ParamFloat(
        "BSplineFitTotalWeight",
        0.1,
        subpath="BSplineFit",
        param_name="Total Weight",
        proxy=ParamSpinBox(0.0, 1000.0, 0.1, 2),
        title="B-spline fit: smoothing, total weight",
        doc="Weight of the smoothing energy terms altogether against the distance to "
        "the points." + _KEPT,
    ),
    ParamFloat(
        "BSplineFitLengthOfGradient",
        1.0,
        subpath="BSplineFit",
        param_name="Length of gradient",
        proxy=ParamSpinBox(0.0, 1.0, 0.1, 2),
        title="B-spline fit: smoothing, length of gradient",
        doc="Weight of the gradient term among the smoothing energy terms." + _KEPT,
    ),
    ParamFloat(
        "BSplineFitBendingEnergy",
        0.0,
        subpath="BSplineFit",
        param_name="Bending energy",
        proxy=ParamSpinBox(0.0, 1.0, 0.1, 2),
        title="B-spline fit: smoothing, bending energy",
        doc="Weight of the bending energy term among the smoothing energy terms."
        + _KEPT,
    ),
    ParamFloat(
        "BSplineFitCurvatureVariation",
        0.0,
        subpath="BSplineFit",
        param_name="Curvature variation",
        proxy=ParamSpinBox(0.0, 1.0, 0.1, 2),
        title="B-spline fit: smoothing, curvature variation",
        doc="Weight of the term for the variation of curvature among the smoothing "
        "energy terms." + _KEPT,
    ),
]

register(sys.modules[__name__])

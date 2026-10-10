# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/CAM that only CAM's Python code reads.

The ones C++ reads are in App/CAMParams.py, which a class is generated from.
These have no class: they are described to the settings registry when
Init.py imports this file (freecad.params), so that the omni search lists
them with the others. Path/Preferences.py names most of them and reads them
through functions of its own; it is left as it is, and each default here is
the one it passes.

Not here: the place and size of the post processor's dialogues, and the list
of versions the asset migration was offered for, which are kept in groups
named at run time.
"""
import sys

from freecad.params import (
    ParamBool,
    ParamColor,
    ParamComboBox,
    ParamFloat,
    ParamHex,
    ParamInt,
    ParamSpinBox,
    ParamString,
    register,
)

NameSpace = "Path"
ClassName = "CAMPyParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/CAM"

_JOB = " Set on the Job Preferences page."

Params = [
    ParamString(
        "DefaultFilePath",
        "",
        title="Default file path",
        doc="Folder CAM looks in first for its files. Empty, the folder of the tool assets "
        "is used." + _JOB,
    ),
    ParamString(
        "DefaultJobTemplate",
        "",
        title="Default job template",
        doc="Template file a new job is made from when none is chosen." + _JOB,
    ),
    ParamString(
        "DefaultStockTemplate",
        "",
        title="Default stock",
        doc="The stock a new job starts with, as the Job Preferences page stores it. Empty, "
        "the job makes its own from the model.",
    ),
    ParamFloat(
        "GeometryTolerance",
        0.01,
        title="Geometry tolerance",
        doc="Geometry tolerance a new job gets, in mm." + _JOB,
    ),
    ParamFloat(
        "LibAreaCurveAccuracy",
        0.01,
        title="Curve accuracy",
        doc="Accuracy, in mm, with which the area library approximates curves." + _JOB,
    ),
    ParamString(
        "PostProcessorDefault",
        "",
        title="Default post processor",
        doc="The post processor a new job gets." + _JOB,
    ),
    ParamString(
        "PostProcessorDefaultArgs",
        "",
        title="Default post processor arguments",
        doc="Arguments a new job hands to its post processor." + _JOB,
    ),
    ParamString(
        "PostProcessorBlacklist",
        "",
        title="Hidden post processors",
        doc="The post processors left out of the lists, as a JSON list of names." + _JOB,
    ),
    ParamString(
        "PostProcessorOutputFile",
        "",
        title="Default output file",
        doc="File name, or pattern of one, a new job writes its G-code to." + _JOB,
    ),
    ParamString(
        "PostProcessorOutputPolicy",
        "",
        title="Output file policy",
        doc="What a new job does when its output file is there already, as the Job "
        "Preferences page stores it.",
    ),
    ParamBool(
        "PostProcessorShowEditor",
        False,
        title="Show editor before writing G-code",
        doc="Pops up the G-code editor for review and editing before the output file is "
        "written.",
    ),
    ParamInt(
        "DefaultTaskPanelLayout",
        0,
        proxy=ParamComboBox(
            ["Classic", "Classic - reversed", "Multi-panel", "Multi-panel - reversed"]
        ),
        title="Task panel layout",
        doc="How the pages of an operation's task panel are laid out. Takes effect for the "
        "panels opened afterwards.",
    ),
    ParamHex(
        "DefaultHighlightPathColor",
        0xFF7D00FF,
        proxy=ParamColor(transparency=False),
        title="Highlighted path colour",
        doc="Colour of the highlighted part of a path, the holding tag being edited for "
        "one. Takes effect for the markers made afterwards.",
    ),
    ParamInt(
        "MaxHighlighterSize",
        1000,
        proxy=ParamSpinBox(0, 999999999, 1),
        title="Maximum lines of G-code to use highlighter",
        doc="G-code longer than this many lines is shown without syntax highlighting in "
        "the Inspect and the export windows. Lower it if those windows are slow; 0 "
        "switches the highlighter off.",
    ),
    ParamBool(
        "WarningSuppressRapidSpeeds",
        True,
        title="Suppress missing rapid speeds warning",
        doc="Suppresses the warning about setting the rapid speed rates for an accurate "
        "cycle time. Ignored when all speed warnings are suppressed.",
    ),
    ParamBool(
        "WarningSuppressSelectionMode",
        True,
        title="Suppress selection mode warning",
        doc="Suppresses the warning shown whenever a path selection mode is activated.",
    ),
    ParamBool(
        "WarningSuppressOpenCamLib",
        True,
        title="Suppress OpenCamLib warning",
        doc="Suppresses the warning that OpenCamLib cannot be found.",
    ),
    ParamBool(
        "EnableAdvancedOCLFeatures",
        False,
        title="Enable OCL dependent features",
        doc="Offers the operations that need OpenCamLib. Takes effect when the CAM "
        "workbench is next activated.",
    ),
    ParamBool(
        "EnableExperimentalFeatures",
        False,
        title="Enable experimental features",
        doc="Offers CAM's experimental features. Takes effect when the CAM workbench is "
        "next activated.",
    ),
    # where the Inspect window was last; numbers kept as text
    ParamString(
        "inspecteditorX",
        "0",
        title="Inspect window: left",
        doc="Left edge, in pixels, of the G-code Inspect window. Stored when it closes.",
    ),
    ParamString(
        "inspecteditorY",
        "0",
        title="Inspect window: top",
        doc="Top edge, in pixels, of the G-code Inspect window. Stored when it closes.",
    ),
    ParamString(
        "inspecteditorW",
        "600",
        title="Inspect window: width",
        doc="Width, in pixels, of the G-code Inspect window. Stored when it closes.",
    ),
    ParamString(
        "inspecteditorH",
        "500",
        title="Inspect window: height",
        doc="Height, in pixels, of the G-code Inspect window. Stored when it closes.",
    ),
    ParamString(
        "ToolPath",
        "",
        subpath="Tools",
        title="Tool assets folder",
        doc="Folder of the tool libraries, tool bits and tool shapes. Empty, a folder in "
        "the user's data is used.",
    ),
    ParamString(
        "LastToolLibrary",
        "",
        subpath="Tools",
        title="Last tool library",
        doc="The tool library last open in the library editor.",
    ),
    ParamString(
        "LastToolLibrarySortKey",
        "",
        subpath="Tools",
        title="Tool library sort key",
        doc="What the tools of the library editor were last sorted by.",
    ),
    ParamBool(
        "ToolUpdateOnLoad",
        True,
        subpath="Tools",
        title="Check tools for updates on load",
        doc="Opening a document checks its tools against the library and offers to apply "
        "the updates found. On the Assets tab of CAM's preferences.",
    ),
]

register(sys.modules[__name__])

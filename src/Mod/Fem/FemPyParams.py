# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/Fem that only Fem's Python code reads.

The ones C++ reads are in App/FemParams.py, which a class is generated from.
These have no class: they are described to the settings registry when
Init.py imports this file (freecad.params), so that the omni search lists
them with the others. Nearly all are on Fem's preference pages, and the
title, the documentation and the editor of each are the page's; the code
that reads them is left as it is.

Two settings of the pages are not here, because no one default describes
them: Ccx/AnalysisNumCPUs and Netgen/NumOfThreads default to the number of
cores of the machine.
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

NameSpace = "Fem"
ClassName = "FemPyParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/Fem"

Params = [
    ParamString(
        "CcxCcxBinaryPath",
        "",
        subpath="Ccx",
        param_name="ccxBinaryPath",
        proxy=ParamFile(),
        title="CalculiX path",
        doc="Leave blank to use default CalculiX ccx binary file",
    ),
    ParamBool(
        "CcxSplitInputWriter",
        False,
        subpath="Ccx",
        param_name="SplitInputWriter",
        title="Split writing of *.inp",
        doc="A new CalculiX solver object writes its input in several files, one for each "
        "kind of data, which the main .inp file includes.",
    ),
    ParamInt(
        "CcxAnalysisType",
        0,
        subpath="Ccx",
        param_name="AnalysisType",
        proxy=ParamComboBox(["Static", "Frequency", "Thermomech", "Check Mesh", "Buckling"]),
        title="Type",
        doc="Default type on analysis",
    ),
    ParamBool(
        "CcxUseNonCcxIterationParam",
        False,
        subpath="Ccx",
        param_name="UseNonCcxIterationParam",
        title="Overwrite CCX defaults",
        doc="A new CalculiX solver object replaces CalculiX's own iteration control "
        "parameters with the ones of its properties.",
    ),
    ParamBool(
        "CcxBeamShellOutput",
        True,
        subpath="Ccx",
        param_name="BeamShellOutput",
        title="3D output, unchecked for 2D output",
        doc="A new CalculiX solver object writes the results of beam and shell elements for "
        "their expanded 3D form; off, for the 1D and 2D elements.",
    ),
    ParamInt(
        "CcxSolver",
        0,
        subpath="Ccx",
        param_name="Solver",
        proxy=ParamComboBox(
            [
                "Default",
                "PaStiX",
                "Pardiso",
                "SPOOLES equation solver",
                "Iterative Scaling",
                "Cholesky iterative solver",
            ]
        ),
        title="Matrix solver",
        doc="The matrix solver of a new CalculiX solver object.",
    ),
    ParamFloat(
        "CcxTimeMaximumIncrement",
        1.0,
        subpath="Ccx",
        param_name="TimeMaximumIncrement",
        proxy=ParamSpinBox(1e-09, 99.99, 0.01, 9),
        title="Maximum time increment",
        doc="Maximum time increment of a new CalculiX solver object.",
    ),
    ParamFloat(
        "CcxTimePeriod",
        1.0,
        subpath="Ccx",
        param_name="TimePeriod",
        proxy=ParamSpinBox(1e-09, 99.99, 1.0, 9),
        title="Time period",
        doc="Time period of the step of a new CalculiX solver object.",
    ),
    ParamFloat(
        "CcxTimeMinimumIncrement",
        1e-05,
        subpath="Ccx",
        param_name="TimeMinimumIncrement",
        proxy=ParamSpinBox(1e-09, 99.99, 0.01, 9),
        title="Minimum time increment",
        doc="Minimum time increment of a new CalculiX solver object.",
    ),
    ParamBool(
        "CcxNonlinearGeometry",
        False,
        subpath="Ccx",
        param_name="NonlinearGeometry",
        title="Use geometrical nonlinearity",
        doc="A new CalculiX solver object takes geometrical nonlinearity into account.",
    ),
    ParamFloat(
        "CcxTimeInitialIncrement",
        1.0,
        subpath="Ccx",
        param_name="TimeInitialIncrement",
        proxy=ParamSpinBox(1e-09, 99.99, 0.01, 9),
        title="Initial time increment",
        doc="Initial time increment of a new CalculiX solver object.",
    ),
    ParamInt(
        "CcxStepMaxIncrements",
        2000,
        subpath="Ccx",
        param_name="StepMaxIncrements",
        proxy=ParamSpinBox(1, 10000000, 10),
        title="Maximum number of increments",
        doc="Maximum number of increments in a step of a new CalculiX solver object.",
    ),
    ParamBool(
        "CcxResultAsPipeline",
        True,
        subpath="Ccx",
        param_name="ResultAsPipeline",
        title="No legacy results (use enhanced solver)",
        doc="Load results as pipeline instead of CCX_Results objects. After\n"
        "unchecking this option, the CalculiX command behaves like\n"
        "SolverCalculiXCcxTools",
    ),
    ParamBool(
        "CcxBinaryOutput",
        False,
        subpath="Ccx",
        param_name="BinaryOutput",
        title="Use binary format",
        doc="Save result in binary format. Only takes effect if 'Pipeline only'\n"
        "is enabled",
    ),
    ParamBool(
        "CcxStaticAnalysis",
        True,
        subpath="Ccx",
        param_name="StaticAnalysis",
        title="Use steady state",
        doc="A new CalculiX solver object computes a thermo-mechanical analysis as steady state.",
    ),
    ParamInt(
        "CcxEigenmodesCount",
        10,
        subpath="Ccx",
        param_name="EigenmodesCount",
        proxy=ParamSpinBox(0, 100, 1),
        title="Number of eigenmodes",
        doc="Number of eigenmodes a frequency analysis computes, a new CalculiX solver object.",
    ),
    ParamFloat(
        "CcxEigenmodeHighLimit",
        1000000.0,
        subpath="Ccx",
        param_name="EigenmodeHighLimit",
        proxy=ParamSpinBox(0.0, 1000000.0, 10000.0, 1),
        title="Upper frequency bound",
        doc="Upper bound of the frequency range of a frequency analysis, a new CalculiX "
        "solver object.",
    ),
    ParamFloat(
        "CcxEigenmodeLowLimit",
        0.0,
        subpath="Ccx",
        param_name="EigenmodeLowLimit",
        proxy=ParamSpinBox(0.0, 1000000.0, 10000.0, 1),
        title="Lower frequency bound",
        doc="Lower bound of the frequency range of a frequency analysis, a new CalculiX "
        "solver object.",
    ),
    ParamString(
        "ElmerElmerBinaryPath",
        "",
        subpath="Elmer",
        param_name="elmerBinaryPath",
        proxy=ParamFile(),
        title="ElmerSolver path",
        doc="Leave blank to use default ElmerSolver binary file",
    ),
    ParamString(
        "ElmerGridBinaryPath",
        "",
        subpath="Elmer",
        param_name="gridBinaryPath",
        proxy=ParamFile(),
        title="ElmerGrid path",
        doc="Leave blank to use default ElmerGrid binary file",
    ),
    ParamString(
        "ElmerMpiBinaryPath",
        "",
        subpath="Elmer",
        param_name="mpiBinaryPath",
        proxy=ParamFile(),
        title="MPI path",
        doc="Leave blank to use default MPI binary file",
    ),
    ParamInt(
        "ElmerMaxOutputLevel",
        10,
        subpath="Elmer",
        param_name="MaxOutputLevel",
        proxy=ParamSpinBox(0, 31, 1),
        title="Log verbosity",
        doc="Maximum output level",
    ),
    ParamInt(
        "ElmerNumberOfTasks",
        1,
        subpath="Elmer",
        param_name="NumberOfTasks",
        proxy=ParamSpinBox(1, 99, 1),
        title="Number of tasks",
        doc="Number of parallel tasks. Set to `1` if Elmer does not use\n"
        "MPI.<br>It is recommended to use an even number of cores to benefit\n"
        "from mesh symmetries<br>(Using 8 cores can be faster than 9\n"
        "cores).<br>In extreme cases ElmerSolver might not converge if the\n"
        "core number is too high.",
    ),
    ParamInt(
        "ElmerThreadsPerTask",
        1,
        subpath="Elmer",
        param_name="ThreadsPerTask",
        proxy=ParamSpinBox(1, 99, 1),
        title="Threads per task",
        doc="Number of threads per task. Take effect if Elmer uses OpenMP.",
    ),
    ParamBool(
        "ElmerBinaryOutput",
        False,
        subpath="Elmer",
        param_name="BinaryOutput",
        title="Use binary format",
        doc="Save result in binary format",
    ),
    ParamBool(
        "ElmerSaveGeometryIndex",
        False,
        subpath="Elmer",
        param_name="SaveGeometryIndex",
        title="Save geometry IDs",
        doc="Save the index of geometric entities",
    ),
    ParamBool(
        "GeneralUseTempDirectory",
        True,
        subpath="General",
        param_name="UseTempDirectory",
        title="Temporary directories",
        doc="Let the application manage (create, delete) the working directories\n"
        "for all solvers. Use temporary directories.",
    ),
    ParamBool(
        "GeneralUseBesideDirectory",
        False,
        subpath="General",
        param_name="UseBesideDirectory",
        title="Beside .FCStd file",
        doc="Create a directory in the same folder in which the FCStd file of the\n"
        "document is located. Use Subfolder for each object (e.g. for a file\n"
        "./mydoc.FCStd and a solver with the label Elmer002 use\n"
        "./mydoc/Elmer002).",
    ),
    ParamBool(
        "GeneralUseCustomDirectory",
        False,
        subpath="General",
        param_name="UseCustomDirectory",
        title="Custom directory",
        doc="Create own subdirectory for each object. Name directory after the\n"
        "solver label prefixed with the document name. Leave blank to use\n"
        "user home directory.",
    ),
    ParamString(
        "GeneralCustomDirectoryPath",
        "",
        subpath="General",
        param_name="CustomDirectoryPath",
        proxy=ParamFile(),
        title="Custom directory",
        doc="The directory the solver files are written to when the custom directory is chosen.",
    ),
    ParamString(
        "GeneralExternalEditorPath",
        "",
        subpath="General",
        param_name="ExternalEditorPath",
        proxy=ParamFile(),
        title="Editor path",
        doc="Leave blank to use default FreeCAD internal editor",
    ),
    ParamBool(
        "GeneralAnalysisGroupMeshing",
        False,
        subpath="General",
        param_name="AnalysisGroupMeshing",
        title="Create mesh groups for analysis reference shapes (experimental)",
        doc="Gmsh makes mesh groups for the shapes the objects of the analysis refer to. "
        "Experimental.",
    ),
    ParamBool(
        "GeneralKeepResultsOnReRun",
        False,
        subpath="General",
        param_name="KeepResultsOnReRun",
        title="Keep results on calculation re-run",
        doc="Existing result objects will be kept otherwise overwritten by new\n"
        "solver run",
    ),
    ParamBool(
        "GeneralRestoreResultDialog",
        True,
        subpath="General",
        param_name="RestoreResultDialog",
        title="Restore result dialog settings",
        doc="The results dialog will be opened with the last used dialog settings",
    ),
    ParamBool(
        "GeneralHideConstraint",
        False,
        subpath="General",
        param_name="HideConstraint",
        title="Hide analysis features when opening result dialog",
        doc="All analysis features are hidden in the model view when the results\n"
        "dialog is opened",
    ),
    ParamString(
        "GmshGmshBinaryPath",
        "",
        subpath="Gmsh",
        param_name="gmshBinaryPath",
        proxy=ParamFile(),
        title="Gmsh path",
        doc="Leave blank to use default Gmsh binary file",
    ),
    ParamInt(
        "InOutVtkImportObject",
        0,
        subpath="InOutVtk",
        param_name="ImportObject",
        proxy=ParamComboBox(["VTK result object", "FEM mesh object", "FreeCAD result object"]),
        title="Which object to import into",
        doc="What a VTK file is imported as. VTK result object: the kind of object "
        "that was exported. FEM mesh object: the mesh only, the results are left out. "
        "FreeCAD result object: the data converted into a FEM result object, which "
        "needs the exact names of the result components and so works with VTK files "
        "exported from FreeCAD only.",
    ),
    ParamString(
        "MystranMystranBinaryPath",
        "",
        subpath="Mystran",
        param_name="mystranBinaryPath",
        proxy=ParamFile(),
        title="Mystran path",
        doc="Leave blank to use default mystran binary file",
    ),
    ParamBool(
        "MystranWriteCommentsToInputFile",
        True,
        subpath="Mystran",
        param_name="writeCommentsToInputFile",
        title="Write comments to input file",
        doc="Writes comments into the Mystran input file.",
    ),
    ParamString(
        "Z88Z88BinaryPath",
        "",
        subpath="Z88",
        param_name="z88BinaryPath",
        proxy=ParamFile(),
        title="z88r path",
        doc="Leave blank to use default z88r binary file",
    ),
    ParamInt(
        "Z88MaxGS",
        100000000,
        subpath="Z88",
        param_name="MaxGS",
        proxy=ParamSpinBox(6000000, 2147483647, 10000000),
        title="Stiffness matrix entries",
        doc="Maximum places in the stiffness matrix. You might need to increase\n"
        "this when using the Cholesky solver and getting the error message\n"
        "that \"MAXGS\" needs to be increased.",
    ),
    ParamInt(
        "Z88MaxKOI",
        2800000,
        subpath="Z88",
        param_name="MaxKOI",
        proxy=ParamSpinBox(50000, 2147483647, 100000),
        title="Coincidence vector entries",
        doc="Maximal places in coincidence vector. (number of knots per element\n"
        "times number of finite elements) You might need to increase this\n"
        "when using an iterative solver and you get the error message that\n"
        "\"MAXKOI\" needs to be increased.",
    ),
    ParamBool(
        "NetgenUseLegacyNetgen",
        True,
        subpath="Netgen",
        param_name="UseLegacyNetgen",
        title="Legacy Netgen",
        doc="Use legacy Netgen object implementation",
    ),
    ParamString(
        "NetgenNetgenPythonPath",
        "",
        subpath="Netgen",
        param_name="NetgenPythonPath",
        proxy=ParamFile(),
        title="Python path",
        doc="Python executable for which Netgen Python bindings are installed.\n"
        "Leave blank to use default Python executable",
    ),
    ParamInt(
        "NetgenLogVerbosity",
        2,
        subpath="Netgen",
        param_name="LogVerbosity",
        proxy=ParamComboBox(
            ["None", "Least", "Little", "Moderate", "Much", "Most"], translate=False
        ),
        title="Log verbosity",
        doc="Level of verbosity printed on the task panel",
    ),
    # what the mesh preview panel was last left with; no page has them
    ParamBool(
        "GmshPreviewAutoEnable",
        False,
        subpath="Gmsh",
        param_name="previewAutoEnable",
        title="Mesh preview: open automatically",
        doc="The mesh preview opens with its panel. Stored when the panel closes.",
    ),
    ParamInt(
        "GmshPreviewMeshFactor",
        5,
        subpath="Gmsh",
        param_name="previewMeshFactor",
        title="Mesh preview: coarsening factor",
        doc="How many times coarser than the mesh the preview is meshed: the factor last "
        "set in the mesh preview panel, which stores it when it closes.",
    ),
]

register(sys.modules[__name__])

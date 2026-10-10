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
'''Auto code generator for the parameters of the expression sandbox, in
Preferences/Expression/Sandbox and Preferences/Expression/Security
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString

NameSpace = 'App'
ClassName = 'SandboxParams'
ParamPath = 'User parameter:BaseApp/Preferences/Expression'
ClassDoc = 'Convenient class to obtain the settings of the expression sandbox'

# Only Evaluate is set from the interface (the sandbox indicator of the
# status bar and FreeCAD.ExpressionSandbox.setRouting()); the others are
# on no page. Where a path is empty the reader goes on to an environment
# variable and then to what the build or the user's data directory has, as
# the documentation of each says: the preference is the first place looked
# at, not the only one.
#
# The four budgets are cached by the sandbox host itself
# (ExpressionImageHost.cpp), which follows the group; it takes its defaults
# from this class.
Params = [
    ParamBool('Evaluate', True, subpath='Sandbox',
        title='Evaluate expressions in the sandbox',
        doc="Expressions that call Python, and the Python objects of a document,\n"
            "are evaluated in the sandbox instead of the program's own\n"
            "interpreter, when the sandbox is there. The sandbox indicator of\n"
            "the status bar switches it. Takes effect at once."),
    ParamString('Runtime', '', subpath='Sandbox',
        title='Sandbox runtime',
        doc='Which runtime carries the sandbox: "pyodide" or "wasi". Empty, the\n'
            "environment variable FCX_RUNTIME decides, and then the build:\n"
            "pyodide where it has it. Read when the sandbox starts."),
    ParamInt('BudgetMs', 5000, subpath='Sandbox',
        title='Sandbox time budget (ms)',
        doc="Milliseconds one evaluation in the sandbox may take before it is\n"
            "interrupted. Takes effect at the next evaluation."),
    ParamInt('GraceMs', 1000, subpath='Sandbox',
        title='Sandbox grace time (ms)',
        doc="Milliseconds an interrupted evaluation is given to stop by itself\n"
            "after its time budget, before the sandbox is stopped. Takes effect\n"
            "at the next evaluation."),
    ParamInt('MemoryMB', 1024, subpath='Sandbox',
        title='Sandbox memory budget (MB)',
        doc="Megabytes of memory the sandbox may use; an evaluation that takes\n"
            "it past that stops the sandbox. Takes effect the next time the\n"
            "sandbox's memory grows."),
    ParamInt('EngineHeapMB', 512, subpath='Sandbox',
        title='Sandbox engine heap (MB)',
        doc="Megabytes of heap the engine that runs the sandbox may use for\n"
            "itself; 0 leaves it to the engine. Takes effect when the sandbox\n"
            "next starts."),
    ParamString('ImagePath', '', subpath='Sandbox',
        title='Sandbox image (wasi)',
        doc="Path of the sandbox image the wasi runtime loads. Empty, the\n"
            "environment variable FCX_IMAGE is used, and then the image that\n"
            "comes with the program. Read when the sandbox starts."),
    ParamString('StdlibPath', '', subpath='Sandbox',
        title='Sandbox standard library (wasi)',
        doc="Folder of the Python standard library of the wasi image. Empty,\n"
            "the environment variable FCX_STDLIB is used, and then the one that\n"
            "comes with the program. Read when the sandbox starts."),
    ParamString('PyodideDir', '', subpath='Sandbox',
        title='Pyodide runtime folder',
        doc="Folder of the Pyodide runtime the sandbox boots. Empty, the\n"
            "environment variable FCX_PYODIDE is used, and then what is\n"
            "installed for the user or comes with the program. Read when the\n"
            "sandbox starts."),
    ParamString('PyodideWheel', '', subpath='Sandbox',
        title='Sandbox wheel (Pyodide)',
        doc="Path of the wheel that holds the sandbox's own Python package.\n"
            "Empty, the environment variable FCX_PYODIDE_WHEEL is used, and then\n"
            "the one that comes with the runtime. Read when the sandbox starts."),
    ParamString('PyodideUserDir', '', subpath='Sandbox',
        title='Pyodide user folder',
        doc="Folder the sandbox keeps what is installed for the user in. Empty,\n"
            "the environment variable FCX_PYODIDE_USER is used, and then the\n"
            "folder Pyodide of the user's application data."),
    ParamString('PyodidePackages', '', subpath='Sandbox',
        title='Pyodide packages folder',
        doc="Folder of the packages installed into the sandbox. Empty, the\n"
            "environment variable FCX_PYODIDE_PACKAGES is used, and then the\n"
            "folder packages of the Pyodide user folder."),
    ParamBool('PyodideUnpinned', False, subpath='Sandbox',
        title='Accept an unpinned Pyodide release',
        doc="For development only: boots a Pyodide release that is not in the\n"
            "table of releases the program was pinned to, or whose files differ\n"
            "from it, and says so at every boot. The environment variable\n"
            "FCX_PYODIDE_UNPINNED does the same. Read when the sandbox starts."),
    ParamBool('Enforce', True, subpath='Security',
        title='Enforce document permissions',
        doc="The permission rules of a document are enforced: what its\n"
            "expressions and Python objects may reach is checked against what\n"
            "the user granted. Off, nothing is checked. On no page. Takes\n"
            "effect at once."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

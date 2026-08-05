# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
'''Auto code generator for parameters in Preferences/Document
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamUInt, ParamFloat

NameSpace = 'App'
ClassName = 'DocumentParams'
ParamPath = 'User parameter:BaseApp/Preferences/Document'
ClassDoc = 'Convenient class to obtain App::Document related parameters'
Signal = True

Params = [
    ParamString('prefAuthor', ""),
    ParamBool('prefSetAuthorOnSave', False),
    ParamString('prefCompany', ""),
    ParamInt('prefLicenseType', 0),
    ParamString('prefLicenseUrl', ""),
    ParamInt('CompressionLevel', 3),
    ParamBool('CheckExtension', True),
    ParamInt('ForceXML', 3),
    ParamBool('SplitXML', True),
    ParamBool('PreferBinary', False),
    ParamInt('InlineListSize', 64,
        doc='Largest list property, in bytes of values, still written inline\n'
            'in the XML instead of taking an archive entry of its own. An\n'
            'entry costs around 190 bytes of zip headers before any content,\n'
            'and one more thing for the reader to open, which a one-element\n'
            'colour list has no way of paying back. Written in the same form\n'
            'the reader has always used for lists that cannot be streamed, so\n'
            'the file stays readable by FreeCAD versions without this option.\n'
            'Set to 0 to give every list an entry, as before.'),
    ParamBool('SaveObjectDefaults', True,
        doc='Save the objects of a document as a difference from their class\n'
            'defaults, which are written once for the whole document. Most of\n'
            'what an object holds is what its constructor gave it -- an\n'
            'identity placement, an empty expression engine, a flag nobody\n'
            'touched -- so on a large assembly this makes Document.xml a\n'
            'fraction of its size and cuts the properties a load has to\n'
            'restore by the same share. Turn it off to write every property\n'
            'on every object, as FreeCAD versions without this option expect.'),
    ParamBool('AutoRemoveFile', True),
    ParamBool('AutoNameDynamicProperty', False),
    ParamBool('BackupPolicy', True),
    ParamBool('CreateBackupFiles', True),
    ParamBool('UseFCBakExtension', False),
    ParamString('SaveBackupDateFormat', "%Y%m%d-%H%M%S"),
    ParamInt('CountBackupFiles', 1),
    ParamBool('OptimizeRecompute', True),
    ParamBool('CanAbortRecompute', True),
    ParamBool('UseHasher', True),
    ParamBool('ViewObjectTransaction', False),
    ParamBool('WarnRecomputeOnRestore', True),
    ParamBool('NoPartialLoading', False),
    ParamBool('SaveThumbnail', False),
    ParamBool('ThumbnailNoBackground', False),
    ParamBool('AddThumbnailLogo', True),
    ParamInt('ThumbnailSampleSize', 0),
    ParamInt('ThumbnailSize', 128),
    ParamBool('DuplicateLabels', False),
    ParamBool('TransactionOnRecompute', False),
    ParamBool('RelativeStringID', True),
    ParamBool('HashIndexedName', False,
        doc='Enable special encoding of indexes name in toponaming. Disabled by\n'
            'default for backward compatibility'),
    ParamBool('EnableMaterialEdit', True),
    ParamBool('MCPServerAutoStart', False,
        doc='Start the MCP debug console server (freecad.mcp_console) when the\n'
            'application starts. Toggled by the Tools -> MCP Server menu action.'),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

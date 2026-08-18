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
    ParamBool('ArchiveRandomAccess', True,
        doc='Restore a document archive through its zip central directory\n'
            'instead of one forward-only stream. Entries are then opened\n'
            'independently and served in registration order whatever their\n'
            'archive order, nothing pays for inflating entries nobody reads,\n'
            'and an entry can be reopened after the restore. Turn off to\n'
            'fall back to the forward-only walk.'),
    ParamBool('DeferShapeLoad', True,
        doc='Park shape archive entries during restore and read each one on\n'
            'first real use instead of before the document opens, so the\n'
            'window is up while shapes stream in with the progressive visual\n'
            'fill. Requires ArchiveRandomAccess. An entry not yet served is\n'
            'read when anything asks for the shape -- visual build, script,\n'
            'save -- so the value is never observably missing; the trade is\n'
            'that the document must not be rewritten externally while loads\n'
            'are pending. Off by default until gated on the large references.'),
    ParamBool('DedupShapePCurves', True,
        doc='Store each 2D curve of a shape once, and leave out the ones\n'
            'reading the file back computes again anyway.\n'
            '\n'
            'Two things, because they are the same bargain. A pcurve computed\n'
            'twice used to be written twice, which on a real project is the\n'
            'largest single duplication inside a shape file; and a pcurve on a\n'
            'planar face need not be stored at all, since the kernel projects\n'
            'the 3D curve onto the plane when it finds none. Neither changes\n'
            'the geometry that comes back: a merged pcurve is the identical\n'
            'curve, and a dropped one is checked against the projection that\n'
            'will replace it before it is dropped.\n'
            '\n'
            'Applies to shapes written as ASCII BRep. Turn off to write what\n'
            'the kernel holds, entry for entry.'),
    ParamBool('DedupCongruentShapes', True,
        doc='Store one file for parts that are the same shape in different\n'
            'places, and record the motion between them instead of writing the\n'
            'geometry again.\n'
            '\n'
            'Content addressing already shares parts whose bytes match, which\n'
            'an exporter that bakes each placement into the coordinates\n'
            'defeats: the same part at twenty positions is twenty distinct\n'
            'contents. Two instances are only merged once the rigid motion\n'
            'between them has been recovered and checked sub-shape by\n'
            'sub-shape, so a mirrored instance or a near-miss is written out\n'
            'in full rather than merged.'),
    ParamBool('DedupCrossFileGeometry', False,
        doc='Let a shape file name the surfaces and curves another shape file\n'
            'already holds instead of writing its own copy of them.\n'
            '\n'
            'Each shape file carries its own table of surfaces, 3D curves and\n'
            '2D curves, so a face two parts have in common is written once per\n'
            'part. On a real project those tables are most of the bytes and\n'
            'about half of what they hold repeats between files. An entry may\n'
            'instead name a file and a position in its table, and the reader\n'
            'then puts the entry it parsed there into this file.\n'
            '\n'
            'Off by default: it makes a shape file depend on another one for\n'
            'its geometry, not only for whole sub-shapes, so a file that goes\n'
            'missing costs more than it did. Applies to shapes written as\n'
            'ASCII BRep inside a document; an exported file names nothing.'),
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
    ParamInt('MCPServerPort', 8765,
        doc='Port the MCP debug console server listens on. If it is already in\n'
            'use the server takes the next free port after it, so the port it\n'
            'ends up on is reported in the console and in the Tools -> MCP\n'
            'Server tooltip.'),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

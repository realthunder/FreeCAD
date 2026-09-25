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
    ParamBool('ArchiveBlobStore', True,
        doc='Keep a document\'s included files in a pack store: a few zip\n'
            'segment files in the transient directory instead of a file per\n'
            'blob (docs/FileBlobsManager.md sec 15.7-15.10). An opened archive\n'
            'is split into segments, new content is compressed once and\n'
            'batched into them, and a save copies the members as they are. A\n'
            'blob gets a file of its own only when something asks for a path.\n'
            'Turn off for a file per blob, which on a monitored filesystem\n'
            'costs a file create per blob.'),
    ParamInt('BlobSegmentSize', 64 * 1024,
        doc='Cap of one pack store segment, in KB. A segment is rewritten\n'
            'whole when content is added to it or dropped from it, so this\n'
            'bounds the cost of every such rewrite. Content over a quarter of\n'
            'it is kept as a file of its own.'),
    ParamBool('DeferShapeLoad', True,
        doc='Park shape archive entries during restore and read each one on\n'
            'first real use instead of before the document opens, so the\n'
            'window is up while shapes stream in with the progressive visual\n'
            'fill. Requires ArchiveRandomAccess. An entry not yet served is\n'
            'read when anything asks for the shape -- visual build, script,\n'
            'save -- so the value is never observably missing; the trade is\n'
            'that the document must not be rewritten externally while loads\n'
            'are pending. Off by default until gated on the large references.'),
    ParamBool('SaveMaterialCards', True,
        doc='Write every material card into the document, including the\n'
            'stock ones.\n'
            '\n'
            'A stock card used to be left out: the hash says which card it\n'
            'was, and any installation holding the same library can produce\n'
            'the content again. That holds only while the library does not\n'
            'move. It moved -- retuning the default appearance changed the\n'
            'Default card, and every document written before it then named a\n'
            'hash no installed card answers to, losing the material outright\n'
            'rather than degrading to the uuid. A shipped library is not a\n'
            'fixed point, so a document cannot be built on the assumption\n'
            'that it is.\n'
            '\n'
            'Carrying the content costs almost nothing now that identical\n'
            'cards are stored once per document: a model whose objects all\n'
            'share one card writes that card once, whatever the object\n'
            'count. Turn off to write only the hash of a stock card, which\n'
            'is smaller by that one card and readable only by an\n'
            'installation whose library still matches.'),
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
    ParamBool('StableShapeBytes', True,
        doc='Write a shape\'s file from the shape alone, not from what was\n'
            'done with it.\n'
            '\n'
            'An edge keeps a 2D curve for every face built on it, including\n'
            'faces of other objects: extruding a sketch\'s face gives the\n'
            'sketch\'s own edges a curve on each side face, and those were saved\n'
            'with the sketch. Some flags record what was last done to a shape\n'
            'rather than what it is. With this on, curves on surfaces that no\n'
            'face of the saved shape carries are left out and those flags are\n'
            'written as constants, so an unchanged shape saves to the same bytes\n'
            '(docs/TransactionLog.md sec 23.12). Nothing the shape needs is\n'
            'lost. Needs the realthunder OCCT fork; ignored without it.'),
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
    ParamInt('TransactionLog', 0,
        doc='Transaction log mode (docs/TransactionLog.md sec 13.3): 0 off,\n'
            '1 session -- the log lives in the document transient directory\n'
            'and dies with it. Off by default while the writer is synchronous.'),
    ParamBool('TransactionLogIdentity', False,
        doc='Record the user and host name in the transaction log session\n'
            'row (sec 13.3, privacy). Off by default.'),
    ParamInt('TransactionLogDerived', 1,
        doc='What the transaction log does with derived values, i.e. values\n'
            'written by their own object recompute (sec 10): 0 none (the op\n'
            'notes the change, no value), 1 cache (evictable tier), 2 full.'),
    ParamInt('TransactionLogSnapshotTransactions', 200,
        doc='The transaction log takes an unnamed version (sec 16.3) every\n'
            'this many committed transactions since the last version; 0 for\n'
            'none. A snapshot serialises the document like a save, without\n'
            'writing an archive. With the time rule of AutoSaveTimeout, it\n'
            'bounds how much a crash recovery replays (sec 25.3).'),
    ParamInt('TransactionLogKeepVersions', 0,
        doc='How many unnamed versions the transaction log keeps (sec 16.3):\n'
            'when a version is added, the oldest unnamed ones over this count\n'
            'are evicted -- never a named one, never the newest. 0 keeps all.'),
    ParamBool('AutoSaveEnabled', True,
        doc='Autosave. Without the transaction log, the Gui writes a recovery\n'
            'file every AutoSaveTimeout minutes; with it, the log takes an\n'
            'unnamed version at the first commit that many minutes after the\n'
            'last one (docs/TransactionLog.md sec 25.3).'),
    ParamInt('AutoSaveTimeout', 15,
        doc='The autosave interval in minutes, see AutoSaveEnabled.'),
    ParamInt('TransactionLogDeltaHops', 8,
        doc='How long a reverse-delta chain the transaction log allows (sec\n'
            '23.2): an entity superseded by a newer one is re-encoded as a\n'
            'patch against it unless the chain below it would then be this\n'
            'many hops from a full entity. 0 stores everything full.'),
    ParamInt('TransactionLogDeltaRatio', 50,
        doc='The largest patch the transaction log keeps, as a percent of the\n'
            'full compressed size (sec 23.2); a patch over it means the codec\n'
            'found nothing to share and the entity stays full.'),
    ParamBool('TransactionLogVerify', False,
        doc='A composed snapshot (sec 23.3) serialises the properties it\n'
            'would have taken from the log anyway and compares: a mismatch\n'
            'names a value changed without aboutToSetValue (sec 23.6). Always\n'
            'on in a debug build.'),
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

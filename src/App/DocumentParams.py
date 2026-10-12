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
    ParamString('prefAuthor', "",
        title = 'Author name',
        doc = "Author name given to new documents as their creator. Also written as\n"
              "the last modifier on save when that option is on. Leave empty to stay\n"
              "anonymous."),
    ParamBool('prefSetAuthorOnSave', False,
        title = 'Set author on save',
        doc = "Write the author name from the preferences into a document's 'Last\n"
              "modified by' field each time it is saved."),
    ParamString('prefCompany', "",
        title = 'Company',
        doc = "Company name given to new documents."),
    ParamInt('prefLicenseType', 0,
        title = 'Default license',
        doc = "License given to new documents, as a position in the license list.\n"
              "0 is All rights reserved, 1 to 12 the Creative Commons licenses,\n"
              "13 Public Domain, 14 FreeArt, 15 to 17 the CERN hardware licences;\n"
              "18 (Other) leaves the license empty."),
    ParamString('prefLicenseUrl', "",
        title = 'License URL',
        doc = "Address of the license text given to new documents. Empty uses the\n"
              "address that belongs to the license chosen from the list."),
    ParamInt('CompressionLevel', 7,
        doc = "How hard a document file is compressed when saved, from 0 (none,\n"
              "fastest) to 9 (smallest, slowest). Has no effect on a document saved\n"
              "as a directory."),
    ParamBool('CheckExtension', True,
        doc = "Add .FCStd to the file name when a document is saved under a name\n"
              "without that extension, so that a save cannot overwrite an unrelated\n"
              "file by accident."),
    ParamInt('ForceXML', 3,
        title = 'Force XML',
        doc = "How much object data new documents keep inside the XML when saved\n"
              "as a directory. 0 none, 1 lists, 2 also meshes, points and text\n"
              "shapes, 3 also binary shapes, 4 and up also included files."),
    ParamBool('SplitXML', True,
        title = 'Split XML',
        doc = "Give each object an XML file of its own in new documents saved as a\n"
              "directory, instead of one file for the whole document. Has no effect\n"
              "on a document saved as a single file."),
    ParamBool('PreferBinary', False,
        doc = "Save the object data of new documents in binary instead of text\n"
              "form. Files get smaller but compare poorly under version control.\n"
              "Each document carries its own copy of this choice."),
    # The long form, kept here; the documentation shown is the short one below.
    # Largest list property, in bytes of values, still written inline
    # in the XML instead of taking an archive entry of its own. An
    # entry costs around 190 bytes of zip headers before any content,
    # and one more thing for the reader to open, which a one-element
    # colour list has no way of paying back. Written in the same form
    # the reader has always used for lists that cannot be streamed, so
    # the file stays readable by FreeCAD versions without this option.
    # Set to 0 to give every list an entry, as before.
    ParamInt('InlineListSize', 64,
        doc="Largest list property, in bytes, still written inside the document\n"
            "XML instead of as a separate entry of the file. Small lists are\n"
            "cheaper inline. 0 gives every list its own entry."),
    ParamBool('ArchiveRandomAccess', True,
        doc='Restore a document archive through its zip central directory\n'
            'instead of one forward-only stream. Entries are then opened\n'
            'independently and served in registration order whatever their\n'
            'archive order, nothing pays for inflating entries nobody reads,\n'
            'and an entry can be reopened after the restore. Turn off to\n'
            'fall back to the forward-only walk.'),
    # The long form, kept here; the documentation shown is the short one below.
    # Keep a document's included files in a pack store: a few zip
    # segment files in the transient directory instead of a file per
    # blob (docs/FileBlobsManager.md sec 15.7-15.10). An opened archive
    # is split into segments, new content is compressed once and
    # batched into them, and a save copies the members as they are. A
    # blob gets a file of its own only when something asks for a path.
    # Turn off for a file per blob, which on a monitored filesystem
    # costs a file create per blob.
    ParamBool('ArchiveBlobStore', True,
        doc='Keep a document\'s included files in a pack store: a few zip\n'
            'segment files in the transient directory instead of a file per\n'
            'blob. A blob gets a file of its own only when something asks for\n'
            'a path. Turn off for a file per blob.'),
    ParamInt('BlobSegmentSize', 64 * 1024,
        doc='Cap of one pack store segment, in KB. A segment is rewritten\n'
            'whole when content is added to it or dropped from it, so this\n'
            'bounds the cost of every such rewrite. Content over a quarter of\n'
            'it is kept as a file of its own.'),
    # The long form, kept here; the documentation shown is the short one below.
    # Park shape archive entries during restore and read each one on
    # first real use instead of before the document opens, so the
    # window is up while shapes stream in with the progressive visual
    # fill. Requires ArchiveRandomAccess. An entry not yet served is
    # read when anything asks for the shape -- visual build, script,
    # save -- so the value is never observably missing; the trade is
    # that the document must not be rewritten externally while loads
    # are pending. Off by default until gated on the large references.
    ParamBool('DeferShapeLoad', True,
        doc="When opening a document, read each shape on first use instead of\n"
            "before the window comes up. Requires ArchiveRandomAccess. The file\n"
            "must not be rewritten by another program while shapes are still to be\n"
            "read. Off by default."),
    # The long form, kept here; the documentation shown is the short one below.
    # Write every material card into the document, including the
    # stock ones.
    #
    # A stock card used to be left out: the hash says which card it
    # was, and any installation holding the same library can produce
    # the content again. That holds only while the library does not
    # move. It moved -- retuning the default appearance changed the
    # Default card, and every document written before it then named a
    # hash no installed card answers to, losing the material outright
    # rather than degrading to the uuid. A shipped library is not a
    # fixed point, so a document cannot be built on the assumption
    # that it is.
    #
    # Carrying the content costs almost nothing now that identical
    # cards are stored once per document: a model whose objects all
    # share one card writes that card once, whatever the object
    # count. Turn off to write only the hash of a stock card, which
    # is smaller by that one card and readable only by an
    # installation whose library still matches.
    ParamBool('SaveMaterialCards', True,
        doc="Write every material card used into the document, including the\n"
            "stock ones, so the document does not depend on the installed material\n"
            "library. Off writes only a reference to a stock card, which is lost\n"
            "if the library changes."),
    # The long form, kept here; the documentation shown is the short one below.
    # Store each 2D curve of a shape once, and leave out the ones
    # reading the file back computes again anyway.
    #
    # Two things, because they are the same bargain. A pcurve computed
    # twice used to be written twice, which on a real project is the
    # largest single duplication inside a shape file; and a pcurve on a
    # planar face need not be stored at all, since the kernel projects
    # the 3D curve onto the plane when it finds none. Neither changes
    # the geometry that comes back: a merged pcurve is the identical
    # curve, and a dropped one is checked against the projection that
    # will replace it before it is dropped.
    #
    # Applies to shapes written as ASCII BRep. Turn off to write what
    # the kernel holds, entry for entry.
    ParamBool('DedupShapePCurves', True,
        doc="Write each 2D curve of a shape once, and leave out those on planar\n"
            "faces, which are computed again on reading. The geometry read back is\n"
            "the same. Applies to shapes written as ASCII BRep."),
    # The long form, kept here; the documentation shown is the short one below.
    # Write a shape's file from the shape alone, not from what was
    # done with it.
    #
    # An edge keeps a 2D curve for every face built on it, including
    # faces of other objects: extruding a sketch's face gives the
    # sketch's own edges a curve on each side face, and those were saved
    # with the sketch. Some flags record what was last done to a shape
    # rather than what it is. With this on, curves on surfaces that no
    # face of the saved shape carries are left out and those flags are
    # written as constants, so an unchanged shape saves to the same bytes
    # (docs/TransactionLog.md sec 23.12). Nothing the shape needs is
    # lost. Needs the realthunder OCCT fork; ignored without it.
    ParamBool('StableShapeBytes', True,
        doc='Write a shape\'s file from the shape alone, not from what was\n'
            'done with it: 2D curves on surfaces no face of the saved shape\n'
            'carries are left out, so an unchanged shape saves to the same\n'
            'bytes. Nothing the shape needs is lost. Needs the realthunder OCCT\n'
            'fork; ignored without it.'),
    # The long form, kept here; the documentation shown is the short one below.
    # Store one file for parts that are the same shape in different
    # places, and record the motion between them instead of writing the
    # geometry again.
    #
    # Content addressing already shares parts whose bytes match, which
    # an exporter that bakes each placement into the coordinates
    # defeats: the same part at twenty positions is twenty distinct
    # contents. Two instances are only merged once the rigid motion
    # between them has been recovered and checked sub-shape by
    # sub-shape, so a mirrored instance or a near-miss is written out
    # in full rather than merged.
    ParamBool('DedupCongruentShapes', True,
        doc="Store one shape file for parts that are the same shape in different\n"
            "places, and record the motion between them. Parts are merged only\n"
            "after the motion has been checked sub-shape by sub-shape."),
    # The long form, kept here; the documentation shown is the short one below.
    # Let a shape file name the surfaces and curves another shape file
    # already holds instead of writing its own copy of them.
    #
    # Each shape file carries its own table of surfaces, 3D curves and
    # 2D curves, so a face two parts have in common is written once per
    # part. On a real project those tables are most of the bytes and
    # about half of what they hold repeats between files. An entry may
    # instead name a file and a position in its table, and the reader
    # then puts the entry it parsed there into this file.
    #
    # Off by default: it makes a shape file depend on another one for
    # its geometry, not only for whole sub-shapes, so a file that goes
    # missing costs more than it did. Applies to shapes written as
    # ASCII BRep inside a document; an exported file names nothing.
    ParamBool('DedupCrossFileGeometry', False,
        doc="Let a shape file refer to surfaces and curves another shape file of\n"
            "the same document already holds instead of writing them again.\n"
            "Smaller files, but a shape file then depends on another for its\n"
            "geometry. Off by default."),
    ParamBool('AutoRemoveFile', True,
        doc = "Delete the files a document no longer uses from its directory when\n"
              "it is saved as a directory. Turn off to leave the files of removed\n"
              "objects in place."),
    ParamBool('AutoNameDynamicProperty', False,
        doc = "Rename a property added to an object when its name is empty, not a\n"
              "valid name or already taken, instead of refusing to add it. A\n"
              "warning reports the name chosen."),
    ParamBool('BackupPolicy', True,
        doc = "Save a document to a temporary file first and move it over the old\n"
              "file only once the write succeeded, keeping backups as configured.\n"
              "Turn off to write straight over the file, with no backup."),
    ParamBool('CreateBackupFiles', True,
        doc = "Keep the previous version of a document file as a backup each time\n"
              "it is saved. When off the old file is deleted once the new one is\n"
              "written."),
    ParamBool('UseFCBakExtension', True,
        doc = "Name a backup after the document, with the date of the replaced\n"
              "file and the extension .FCBak. When off a backup is the document\n"
              "file name followed by a number, as in Part.FCStd1."),
    ParamString('SaveBackupDateFormat', "%Y%m%d-%H%M%S",
        doc = "Date format used in the names of .FCBak backup files, in strftime\n"
              "notation. A dot in the format is written as a dash."),
    ParamInt('CountBackupFiles', 1,
        doc = "How many backup files are kept for one document. The oldest are\n"
              "deleted when a save would exceed the number. 0 keeps none."),
    ParamBool('OptimizeRecompute', True,
        doc = "Recompute an object only when one of its properties really changed.\n"
              "Writing a property the value it already has then leaves the object\n"
              "untouched. Turn off to recompute on every write."),
    ParamBool('CanAbortRecompute', True,
        doc = "Show progress while a document recomputes and let Esc abort it.\n"
              "Costs a little recompute time."),
    ParamBool('UseHasher', True,
        doc = "Store the generated element names of new documents as short\n"
              "references into a string table of the document instead of in full.\n"
              "Each document carries its own copy of this choice."),
    ParamBool('ViewObjectTransaction', False,
        doc = "Let a change to a view property alone, such as colour or visibility,\n"
              "create an undo step whatever command made it. When off only the\n"
              "commands that ask for it do."),
    ParamBool('WarnRecomputeOnRestore', True,
        doc = "Ask to recompute after opening a document that needs it to be\n"
              "brought up to date with this version. When off the document opens\n"
              "without the question and is left as it is."),
    ParamBool('NoPartialLoading', False,
        doc = "Load every externally linked document in full. When off a document\n"
              "opened only because another links to it loads just the linked\n"
              "objects and what they depend on, and cannot be edited until\n"
              "reloaded."),
    ParamBool('SaveThumbnail', True,
        doc = "Save a preview picture of the 3D view into new documents each time\n"
              "they are saved. Each document carries its own copy of this choice."),
    ParamBool('ThumbnailNoBackground', False,
        doc = "Leave the view background out of the thumbnail saved with a\n"
              "document, so the picture has a transparent background."),
    ParamBool('AddThumbnailLogo', True,
        doc = "Put the application icon in the bottom right corner of the thumbnail\n"
              "saved with a document."),
    ParamInt('ThumbnailSampleSize', 0,
        doc = "Number of antialiasing samples used to render the thumbnail saved\n"
              "with a document. 0 renders without antialiasing."),
    ParamInt('ThumbnailSize', 256,
        doc = "Width and height, in pixels, of the thumbnail saved with a document.\n"
              "Values outside 64 to 1024 are brought into that range."),
    ParamBool('DuplicateLabels', False,
        doc = "Allow several objects of one document to carry the same label. When\n"
              "off a label already in use gets a number added to make it unique."),
    ParamBool('TransactionOnRecompute', False,
        doc = "Record a recompute started with the Refresh command as an undo step.\n"
              "When off, refreshing leaves the undo and redo history alone."),
    ParamInt('TransactionLog', 2,
        doc='Transaction log mode (docs/TransactionLog.md sec 13.3, 27.5). The\n'
            'log is always on; what this chooses is whether a save writes the\n'
            'history into the file: 2 (the default) writes it, 1 keeps it in\n'
            'the document transient directory only, where it dies with the\n'
            'session. 0 switches the log off, for A/B checks.'),
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
    ParamInt('TransactionLogCompactSize', 256,
        doc='When the file-scope state of the transaction log is compacted on\n'
            'its own (docs/TransactionLog.md sec 27.48, 27.49), in KB: after a\n'
            'trim, a branch deletion or a squash, once the names estimated to be\n'
            'referred to by nothing and the strings of the file hasher nothing\n'
            'holds come to this many bytes together. 0 never.'),
    ParamInt('ClosePinnedVersion', 0,
        doc='When the last link pinned to a version of a file goes, what\n'
            'happens to the document that showed it (docs/TransactionLog.md\n'
            'sec 27.21 Q5): 0 ask, 1 close it, 2 keep it open. With no Gui to\n'
            'ask, 0 keeps it open.'),
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
    ParamInt('MergeConflictSide', 0,
        doc='The side a conflict starts on when a merge, or rows applied, are\n'
            'previewed (docs/TransactionLog.md sec 32.17): 0 ours (the default),\n'
            '1 theirs. It is what the merge dialog shows each conflict picked as,\n'
            'and so what the result is shown with and what is merged unless\n'
            'another side is picked there. A merge asked for by a script with no\n'
            'side for a conflict is refused whatever this says.'),
    ParamBool('RelativeStringID', True,
        doc = "Write the ids in a document's string table as differences from the\n"
              "id before, which makes the saved file smaller. Turn off to write\n"
              "every id in full."),
    ParamBool('HashIndexedName', True,
        doc='Encode a mapped name\'s trailing index apart from its text, as upstream\n'
            'FreeCAD does. Sets the mode of new documents only: a document keeps the\n'
            'mode it was saved in, and one saved before the mode was stored gets the\n'
            'one its string table was written in.'),
    ParamBool('EnableMaterialEdit', True,
        doc = "Show appearance properties in the property view with an editor for\n"
              "their colours, shininess and transparency. When off they are not\n"
              "listed. Applies to objects created or loaded afterwards."),
    ParamBool('MCPServerAutoStart', False,
        doc='Start the MCP debug console server (freecad.mcp_console) when the\n'
            'application starts. Toggled by the Tools -> MCP Server menu action.'),
    ParamInt('MCPServerPort', 8765,
        doc='Port the MCP debug console server listens on. If it is already in\n'
            'use the server takes the next free port after it, so the port it\n'
            'ends up on is reported in the console and in the Tools -> MCP\n'
            'Server tooltip.'),
    ParamBool('AutoSaveEnabled', True,
        title = 'Save auto-recovery information',
        doc = "Save auto-recovery information of the open documents at regular\n"
              "intervals. Takes effect at once."),
    ParamInt('AutoSaveTimeout', 15,
        title = 'Auto-recovery interval',
        doc = "Minutes between two saves of auto-recovery information. 1 to 60.\n"
              "Takes effect at once."),
    ParamBool('AutoSaveCompressed', True,
        title = 'Compress auto-recovery files',
        doc = "Write auto-recovery information as one compressed file per\n"
              "document instead of separate uncompressed files. Takes effect at\n"
              "the next save of it."),
    ParamBool('SaveBinaryBrep', True,
        title = 'Binary shapes in auto-recovery files',
        doc = "Write shapes in binary BREP format into a compressed\n"
              "auto-recovery file. Read at each save of it."),
    ParamBool('RecoveryEnabled', True,
        title = 'Run file recovery at startup',
        doc = "Look at startup for documents a crashed session left behind and\n"
              "offer to recover them."),
    ParamBool('CreateNewDoc', False,
        title = 'Create new document at startup',
        doc = "Create an empty document when the program starts with none\n"
              "open."),
    ParamBool('UsingUndo', True,
        title = 'Allow undo and redo',
        doc = "Record undo and redo steps for documents. Applies to documents\n"
              "created or opened afterwards."),
    ParamInt('MaxUndoSize', 20,
        title = 'Maximum undo steps',
        doc = "Largest number of undo steps kept for a document. Applies to\n"
              "documents created or opened afterwards."),
    ParamBool('ChangeViewProviderTouchDocument', True,
        title = 'View changes modify the document',
        doc = "Mark a document as modified when a view property of one of its\n"
              "objects changes. Applies to documents created or opened\n"
              "afterwards."),
    ParamInt('JsonIndent', 2,
        title = 'JSON indentation',
        doc = "Indentation of the JSON text the properties of Python objects are\n"
              "saved as."),
    ParamBool('PreferCompactFormat', True,
        title = 'Last format chosen for a new file',
        doc = "The compact format was chosen the last time a file was saved under\n"
              "a new name, and is what the next such save starts on. Stored by\n"
              "the Save As dialog."),
    ParamBool('WarnCompactFormat', True,
        title = 'Warn about the compact format',
        doc = "The warning shown when a save comes out in the compact format is\n"
              "still to be shown. The program switches it off when the warning\n"
              "is dismissed for good."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

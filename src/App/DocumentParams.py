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
    ParamInt('CompressionLevel', 3,
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
    ParamBool('ArchiveBlobStore', True,
        doc='Serve the included files of a document archive out of one copy\n'
            'of the archive in the transient directory, and give each its own\n'
            'file only when something asks for a path. Requires\n'
            'ArchiveRandomAccess. Turn off to write every included file out\n'
            'during the restore, which on a monitored filesystem costs a file\n'
            'create per entry.'),
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
    ParamBool('UseFCBakExtension', False,
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
    ParamBool('SaveThumbnail', False,
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
    ParamInt('ThumbnailSize', 128,
        doc = "Width and height, in pixels, of the thumbnail saved with a document.\n"
              "Values outside 64 to 1024 are brought into that range."),
    ParamBool('DuplicateLabels', False,
        doc = "Allow several objects of one document to carry the same label. When\n"
              "off a label already in use gets a number added to make it unique."),
    ParamBool('TransactionOnRecompute', False,
        doc = "Record a recompute started with the Refresh command as an undo step.\n"
              "When off, refreshing leaves the undo and redo history alone."),
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
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD contributors                               *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
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
# ***************************************************************************

"""Reference-counted included-file storage (App::PropertyFileIncluded).

The behaviour under test is described in docs/FileBlobsManager.md. The cases are
grouped by the concern they pin down:

  BlobStorageCases      content addressing, sharing, naming, immutability
  BlobRefCountCases     lifetime: who keeps a file alive, who deletes it
  BlobPersistenceCases  archive shape and round-trips, including the
                        forward-only-merge ordering regression (docs §4)
  BlobExportImportCases the export/import path (copyObject, clipboard)

Run headless with:  FreeCADCmd -t FileBlobs
"""

import base64
import hashlib
import os
import shutil
import tempfile
import unittest
import zipfile

import FreeCAD

try:
    import Part

    HAS_PART = True
except ImportError:
    HAS_PART = False

BLOB_DIR = "blobs"


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------


class BlobTestCase(unittest.TestCase):
    """Scratch directory, document bookkeeping and blob-aware assertions."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="fc_blob_test_")
        self.docs = []

    def tearDown(self):
        for name in list(self.docs):
            if name in FreeCAD.listDocuments():
                FreeCAD.closeDocument(name)
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- fixtures ----------------------------------------------------------

    def newDocument(self, name="BlobDoc"):
        doc = FreeCAD.newDocument(name)
        self.docs.append(doc.Name)
        return doc

    def openDocument(self, path):
        doc = FreeCAD.openDocument(path)
        self.docs.append(doc.Name)
        return doc

    def sourceFile(self, name, content=b"payload"):
        """An external file to be embedded, outside any transient directory."""
        path = os.path.join(self.tmp, name)
        with open(path, "wb") as handle:
            handle.write(content)
        return path

    def fileObject(self, doc, name, content=b"payload", saveName=None):
        obj = doc.addObject("App::DocumentObjectFileIncluded", name)
        src = self.sourceFile(name + ".src", content)
        obj.File = (src, saveName) if saveName else src
        return obj

    def projectPath(self, name="project.FCStd"):
        return os.path.join(self.tmp, name)

    def directoryPath(self, name="project_dir"):
        """An unpacked project: a directory save, which is a different writer."""
        path = os.path.join(self.tmp, name)
        os.makedirs(path)
        return path

    # -- assertions --------------------------------------------------------

    def assertContent(self, obj, expected):
        path = obj.File
        self.assertTrue(path, "property has no value")
        self.assertTrue(os.path.exists(path), "stored file is missing: %s" % path)
        with open(path, "rb") as handle:
            self.assertEqual(handle.read(), expected)

    def blobEntries(self, project):
        names = zipfile.ZipFile(project).namelist()
        return [n for n in names if n.startswith(BLOB_DIR + "/")]

    def storedBlobs(self, doc):
        """Files actually present in the document's blob store."""
        blobdir = os.path.join(doc.TransientDir, BLOB_DIR)
        if not os.path.isdir(blobdir):
            return []
        return sorted(os.listdir(blobdir))

    def documentXml(self, project):
        return zipfile.ZipFile(project).read("Document.xml").decode("utf-8")

    def directoryBlobs(self, project):
        blobdir = os.path.join(project, BLOB_DIR)
        if not os.path.isdir(blobdir):
            return []
        return sorted(os.listdir(blobdir))

    def directoryXml(self, project):
        """Every XML file of an unpacked project, joined -- SplitXML decides
        which of them an object's properties ended up in."""
        parts = []
        for name in sorted(os.listdir(project)):
            if name.endswith(".xml"):
                with open(os.path.join(project, name), "rb") as handle:
                    parts.append(handle.read().decode("utf-8", "replace"))
        return "\n".join(parts)

    @staticmethod
    def sha1(content):
        return hashlib.sha1(content).hexdigest()


# ---------------------------------------------------------------------------
# storage: content addressing, sharing, naming
# ---------------------------------------------------------------------------


class BlobStorageCases(BlobTestCase):
    def testStoredByContentHash(self):
        """The stored file is named by the hash of its bytes, nothing else."""
        doc = self.newDocument()
        content = b"content addressed"
        obj = self.fileObject(doc, "File1", content)
        self.assertEqual(os.path.basename(obj.File), self.sha1(content))
        self.assertEqual(os.path.basename(os.path.dirname(obj.File)), BLOB_DIR)

    def testIdenticalContentIsShared(self):
        """Same bytes, different source files and names -> one file on disk."""
        doc = self.newDocument()
        first = self.fileObject(doc, "File1", b"shared", saveName="a.txt")
        second = self.fileObject(doc, "File2", b"shared", saveName="b.txt")
        self.assertEqual(first.File, second.File)
        self.assertEqual(len(self.storedBlobs(doc)), 1)

    def testDistinctContentIsNotShared(self):
        doc = self.newDocument()
        first = self.fileObject(doc, "File1", b"one")
        second = self.fileObject(doc, "File2", b"two")
        self.assertNotEqual(first.File, second.File)
        self.assertEqual(len(self.storedBlobs(doc)), 2)

    def testEmptyValue(self):
        doc = self.newDocument()
        obj = doc.addObject("App::DocumentObjectFileIncluded", "File1")
        self.assertEqual(obj.File, "")
        self.assertEqual(self.storedBlobs(doc), [])

    def testBinaryIntegrity(self):
        """Arbitrary bytes survive storage untouched (no text mangling)."""
        doc = self.newDocument()
        content = bytes(range(256)) * 64
        obj = self.fileObject(doc, "File1", content)
        self.assertContent(obj, content)

    def testStoredFileIsReadOnly(self):
        """Blobs are immutable; the stored file must not be writable."""
        doc = self.newDocument()
        obj = self.fileObject(doc, "File1", b"immutable")
        self.assertFalse(os.access(obj.File, os.W_OK))

    def testAssigningSameFileRejected(self):
        doc = self.newDocument()
        obj = self.fileObject(doc, "File1", b"payload")
        with self.assertRaises(Exception):
            obj.File = obj.File

    def testReplacingContentCreatesNewBlob(self):
        doc = self.newDocument()
        obj = self.fileObject(doc, "File1", b"first")
        first = obj.File
        obj.File = self.sourceFile("second.src", b"second")
        self.assertNotEqual(obj.File, first)
        self.assertContent(obj, b"second")

    def testReplacingContentDropsUnreferencedBlob(self):
        doc = self.newDocument()
        doc.UndoMode = 0
        obj = self.fileObject(doc, "File1", b"first")
        first = obj.File
        obj.File = self.sourceFile("second.src", b"second")
        self.assertFalse(os.path.exists(first))


# ---------------------------------------------------------------------------
# lifetime
# ---------------------------------------------------------------------------


class BlobRefCountCases(BlobTestCase):
    def testSharedFileSurvivesOneReferrer(self):
        doc = self.newDocument()
        doc.UndoMode = 0
        first = self.fileObject(doc, "File1", b"shared")
        second = self.fileObject(doc, "File2", b"shared")
        path = first.File
        doc.removeObject(first.Name)
        self.assertTrue(os.path.exists(path))
        self.assertContent(second, b"shared")

    def testLastReferrerDeletesFile(self):
        doc = self.newDocument()
        doc.UndoMode = 0
        obj = self.fileObject(doc, "File1", b"only")
        path = obj.File
        doc.removeObject(obj.Name)
        self.assertFalse(os.path.exists(path))

    def testUndoRestoresPreviousContent(self):
        doc = self.newDocument()
        doc.UndoMode = 1
        obj = self.fileObject(doc, "File1", b"first")
        doc.openTransaction("replace")
        obj.File = self.sourceFile("second.src", b"second")
        doc.commitTransaction()
        self.assertContent(obj, b"second")
        doc.undo()
        self.assertContent(obj, b"first")
        doc.redo()
        self.assertContent(obj, b"second")

    def testUndoOfDeleteRestoresContent(self):
        doc = self.newDocument()
        doc.UndoMode = 1
        obj = self.fileObject(doc, "File1", b"payload")
        doc.openTransaction("remove")
        doc.removeObject(obj.Name)
        doc.commitTransaction()
        doc.undo()
        self.assertContent(doc.getObject("File1"), b"payload")

    def testTransactionSnapshotDoesNotDuplicate(self):
        """Copy() is a refcount increment: undo state costs no extra file."""
        doc = self.newDocument()
        doc.UndoMode = 1
        obj = self.fileObject(doc, "File1", b"payload")
        doc.openTransaction("touch")
        obj.Label = "renamed"
        doc.commitTransaction()
        self.assertEqual(len(self.storedBlobs(doc)), 1)

    def testCopyObjectSharesContent(self):
        doc = self.newDocument()
        obj = self.fileObject(doc, "File1", b"shared")
        copies = doc.copyObject([obj])
        self.assertEqual(len(copies), 1)
        self.assertContent(copies[0], b"shared")
        self.assertEqual(len(self.storedBlobs(doc)), 1)


# ---------------------------------------------------------------------------
# persistence
# ---------------------------------------------------------------------------


class BlobPersistenceCases(BlobTestCase):
    def testRoundTrip(self):
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"round trip")
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"round trip")

    @unittest.skipUnless(HAS_PART, "Part module not available")
    def testOrderingFilePropertyBeforeShape(self):
        """A file property followed by any other archive entry (docs §4).

        The forward-only merge in ZipReader::readFiles dropped the blob here:
        the shape entry was written between the property and its content.
        """
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"ordered")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"ordered")
        self.assertTrue(reopened.getObject("Box").Shape.isValid())

    @unittest.skipUnless(HAS_PART, "Part module not available")
    def testOrderingFilePropertyAfterShape(self):
        doc = self.newDocument()
        doc.addObject("Part::Box", "Box")
        self.fileObject(doc, "File1", b"ordered")
        doc.recompute()
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"ordered")

    @unittest.skipUnless(HAS_PART, "Part module not available")
    def testOrderingInterleaved(self):
        doc = self.newDocument()
        for index in range(3):
            self.fileObject(doc, "File%d" % index, b"payload %d" % index)
            doc.addObject("Part::Box", "Box%d" % index)
        doc.recompute()
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        for index in range(3):
            self.assertContent(reopened.getObject("File%d" % index), b"payload %d" % index)

    def testArchiveLayout(self):
        """Document.xml first, then the blob entries, then everything else."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"layout")
        project = self.projectPath()
        doc.saveAs(project)
        names = zipfile.ZipFile(project).namelist()
        self.assertEqual(names[0], "Document.xml")
        self.assertTrue(names[1].startswith(BLOB_DIR + "/"))
        tail = [n for n in names[2:] if n.startswith(BLOB_DIR + "/")]
        blobs = [n for n in names if n.startswith(BLOB_DIR + "/")]
        self.assertEqual(len(tail), len(blobs) - 1, "blob entries must be contiguous")

    def testSharedContentIsOneArchiveEntry(self):
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"shared", saveName="a.txt")
        self.fileObject(doc, "File2", b"shared", saveName="b.txt")
        project = self.projectPath()
        doc.saveAs(project)
        self.assertEqual(len(self.blobEntries(project)), 1)

    def testManyReferrersOneArchiveEntry(self):
        doc = self.newDocument()
        for index in range(10):
            self.fileObject(doc, "File%d" % index, b"shared")
        project = self.projectPath()
        doc.saveAs(project)
        self.assertEqual(len(self.blobEntries(project)), 1)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        for index in range(10):
            self.assertContent(reopened.getObject("File%d" % index), b"shared")
        self.assertEqual(len(self.storedBlobs(reopened)), 1)

    def testEntryNamedByHash(self):
        doc = self.newDocument()
        content = b"named by hash"
        self.fileObject(doc, "File1", content)
        project = self.projectPath()
        doc.saveAs(project)
        self.assertEqual(self.blobEntries(project), ["%s/%s" % (BLOB_DIR, self.sha1(content))])

    def testPropertyKeepsItsOwnName(self):
        """Sharing content must not merge the properties' file names."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"shared", saveName="alpha.txt")
        self.fileObject(doc, "File2", b"shared", saveName="beta.txt")
        project = self.projectPath()
        doc.saveAs(project)
        xml = self.documentXml(project)
        self.assertIn('name="alpha.txt"', xml)
        self.assertIn('name="beta.txt"', xml)

    def testRestoreLeavesNoUnclaimedContent(self):
        """The restore hold is cleared, and clears only what nobody claimed."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"claimed")
        self.fileObject(doc, "File2", b"also claimed")
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertEqual(len(self.storedBlobs(reopened)), 2)
        self.assertContent(reopened.getObject("File1"), b"claimed")
        self.assertContent(reopened.getObject("File2"), b"also claimed")

    def testResaveDropsReplacedContent(self):
        doc = self.newDocument()
        doc.UndoMode = 0
        obj = self.fileObject(doc, "File1", b"first")
        project = self.projectPath()
        doc.saveAs(project)
        obj.File = self.sourceFile("second.src", b"second")
        doc.save()
        self.assertEqual(len(self.blobEntries(project)), 1)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"second")

    def testSaveAsRelocatesStore(self):
        """saveAs changes TransientDir; stored paths must be repaired."""
        doc = self.newDocument()
        obj = self.fileObject(doc, "File1", b"relocated")
        doc.saveAs(self.projectPath("first.FCStd"))
        doc.saveAs(self.projectPath("second.FCStd"))
        self.assertContent(obj, b"relocated")
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(self.projectPath("second.FCStd"))
        self.assertContent(reopened.getObject("File1"), b"relocated")

    def testSaveCopyKeepsDocumentUsable(self):
        doc = self.newDocument()
        obj = self.fileObject(doc, "File1", b"copy")
        doc.saveAs(self.projectPath("main.FCStd"))
        doc.saveCopy(self.projectPath("copy.FCStd"))
        self.assertContent(obj, b"copy")
        self.assertEqual(len(self.blobEntries(self.projectPath("copy.FCStd"))), 1)

    def testRevertRestoresContent(self):
        doc = self.newDocument()
        obj = self.fileObject(doc, "File1", b"reverted")
        project = self.projectPath()
        doc.saveAs(project)
        doc.restore()
        self.assertContent(doc.getObject("File1"), b"reverted")

    def testSchemaFourFallback(self):
        """Lowering the schema keeps the per-property self-contained form."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"legacy", saveName="legacy.txt")
        doc.SaveSchemaVersion = 4
        project = self.projectPath()
        doc.saveAs(project)
        self.assertEqual(self.blobEntries(project), [])
        self.assertIn("legacy.txt", zipfile.ZipFile(project).namelist())
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"legacy")

    def testSchemaFourSharedContentIsDuplicated(self):
        """Schema 4 cannot express sharing: one entry per property."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"shared", saveName="a.txt")
        self.fileObject(doc, "File2", b"shared", saveName="b.txt")
        doc.SaveSchemaVersion = 4
        project = self.projectPath()
        doc.saveAs(project)
        names = zipfile.ZipFile(project).namelist()
        self.assertIn("a.txt", names)
        self.assertIn("b.txt", names)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"shared")
        self.assertContent(reopened.getObject("File2"), b"shared")

    def testUnwritableSchemaIsClamped(self):
        doc = self.newDocument()
        doc.SaveSchemaVersion = 99
        self.assertIn(doc.SaveSchemaVersion, (4, 5))


# ---------------------------------------------------------------------------
# the document's save options (docs §6.1)
# ---------------------------------------------------------------------------


class BlobSaveOptionCases(BlobTestCase):
    """ForceXML, SplitXML and PreferBinary against the blob path.

    ForceXML and SplitXML only take effect for a directory save, so most of
    these save an unpacked project -- which is also the writer autosave
    recovery uses.
    """

    def testDirectoryRoundTrip(self):
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"unpacked", saveName="a.txt")
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(self.directoryBlobs(project), [self.sha1(b"unpacked")])
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"unpacked")

    def testDirectorySharedContentIsOneFile(self):
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"shared", saveName="a.txt")
        self.fileObject(doc, "File2", b"shared", saveName="b.txt")
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(len(self.directoryBlobs(project)), 1)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"shared")
        self.assertContent(reopened.getObject("File2"), b"shared")
        self.assertEqual(len(self.storedBlobs(reopened)), 1)

    def testDirectoryResaveKeepsStoredContent(self):
        """Content addressing makes an existing file proof of equality, so a
        re-save skips it -- and must not lose it by skipping."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"unchanged", saveName="a.txt")
        project = self.directoryPath()
        doc.saveAs(project)
        stored = os.path.join(project, BLOB_DIR, self.sha1(b"unchanged"))
        stamp = os.stat(stored).st_mtime_ns
        doc.save()
        self.assertTrue(os.path.exists(stored))
        self.assertEqual(os.stat(stored).st_mtime_ns, stamp, "unchanged blob rewritten")

    def testSplitXmlRoundTrip(self):
        """Object data in per-object XML files: those are written after the
        blobs, so only the up-front collect pass can have caught them."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"split", saveName="a.txt")
        doc.SplitXML = True
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertIn("File1.xml", os.listdir(project))
        self.assertEqual(self.directoryBlobs(project), [self.sha1(b"split")])
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"split")

    def testNoSplitXmlRoundTrip(self):
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"joined", saveName="a.txt")
        doc.SplitXML = False
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertNotIn("File1.xml", os.listdir(project))
        self.assertEqual(self.directoryBlobs(project), [self.sha1(b"joined")])
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"joined")

    def testForceXmlInlinesContent(self):
        """Above level 3 the document carries its content itself: the same
        table, written as base64 inside Document.xml instead of as entries."""
        doc = self.newDocument()
        content = b"inlined"
        self.fileObject(doc, "File1", content, saveName="a.txt")
        doc.ForceXML = 4
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(self.directoryBlobs(project), [])
        xml = self.directoryXml(project)
        self.assertIn('<Blobs Count="1">', xml)
        self.assertIn('<Blob hash="%s"' % self.sha1(content), xml)
        self.assertIn(base64.b64encode(content).decode(), xml)
        # The property form does not change with the option.
        self.assertIn('<FileIncluded hash="%s"' % self.sha1(content), xml)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), content)

    def testForceXmlBlobsPrecedeTheObjects(self):
        """The table has to be ahead of everything that can refer to it."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"ordered", saveName="a.txt")
        doc.SplitXML = False
        doc.ForceXML = 4
        project = self.directoryPath()
        doc.saveAs(project)
        with open(os.path.join(project, "Document.xml")) as handle:
            xml = handle.read()
        self.assertLess(xml.index("<Blobs "), xml.index("<Properties "))
        self.assertLess(xml.index("</Blobs>"), xml.index("<Objects "))

    def testForceXmlKeepsContentShared(self):
        """Inlining is a different place for the table, not a reason to give
        up sharing: one entry for two properties, in the file and after it."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"shared", saveName="a.txt")
        self.fileObject(doc, "File2", b"shared", saveName="b.txt")
        doc.ForceXML = 4
        project = self.directoryPath()
        doc.saveAs(project)
        xml = self.directoryXml(project)
        self.assertIn('<Blobs Count="1">', xml)
        self.assertEqual(xml.count("<Blob hash="), 1)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"shared")
        self.assertContent(reopened.getObject("File2"), b"shared")
        self.assertEqual(len(self.storedBlobs(reopened)), 1)

    def testForceXmlBinaryIntegrity(self):
        """Every remainder of the base64 group, since the table is base64."""
        for length in (1, 2, 3, 4, 5, 255):
            content = bytes(bytearray((i % 251) for i in range(length)))
            doc = self.newDocument("BlobDoc%d" % length)
            self.fileObject(doc, "File1", content, saveName="a.bin")
            doc.ForceXML = 4
            project = self.directoryPath("inline_%d" % length)
            doc.saveAs(project)
            name = doc.Name
            FreeCAD.closeDocument(name)
            self.docs.remove(name)
            reopened = self.openDocument(project)
            name = reopened.Name
            self.assertContent(reopened.getObject("File1"), content)
            FreeCAD.closeDocument(name)
            self.docs.remove(name)

    def testForceXmlKeepsPropertyNames(self):
        """Sharing content must not merge the names, inline form included: the
        stored file is named by hash, so the name lives on the property and has
        to survive the round trip."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"shared", saveName="alpha.txt")
        self.fileObject(doc, "File2", b"shared", saveName="beta.txt")
        doc.ForceXML = 4
        project = self.directoryPath()
        doc.saveAs(project)
        xml = self.directoryXml(project)
        self.assertIn('name="alpha.txt"', xml)
        self.assertIn('name="beta.txt"', xml)
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        resaved = self.directoryPath("resaved_dir")
        reopened.saveAs(resaved)
        again = self.directoryXml(resaved)
        self.assertIn('name="alpha.txt"', again)
        self.assertIn('name="beta.txt"', again)

    def testForceXmlBelowFourKeepsBlobEntries(self):
        """Level 3 -- the default -- is not a request for inline content."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"entries", saveName="a.txt")
        doc.ForceXML = 3
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(self.directoryBlobs(project), [self.sha1(b"entries")])
        self.assertIn("<FileIncluded hash=", self.directoryXml(project))

    def testForceXmlDoesNotReachTheArchive(self):
        """ForceXML is a directory-save option; a packed project ignores it."""
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"packed", saveName="a.txt")
        doc.ForceXML = 4
        project = self.projectPath()
        doc.saveAs(project)
        self.assertEqual(self.blobEntries(project), ["%s/%s" % (BLOB_DIR, self.sha1(b"packed"))])
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"packed")

    def testPreferBinaryLeavesContentAlone(self):
        """Stored content is opaque bytes: the option picks a format for
        generated data, and there is nothing to pick for a file."""
        doc = self.newDocument()
        content = bytes(bytearray(range(256)))
        self.fileObject(doc, "File1", content, saveName="a.bin")
        doc.PreferBinary = True
        project = self.projectPath()
        doc.saveAs(project)
        self.assertEqual(self.blobEntries(project), ["%s/%s" % (BLOB_DIR, self.sha1(content))])
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), content)

    def testPreferBinaryInDirectoryRoundTrip(self):
        doc = self.newDocument()
        self.fileObject(doc, "File1", b"binary mode", saveName="a.bin")
        doc.PreferBinary = True
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(self.directoryBlobs(project), [self.sha1(b"binary mode")])
        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.getObject("File1"), b"binary mode")


# ---------------------------------------------------------------------------
# export / import (clipboard, cross-document copy)
# ---------------------------------------------------------------------------


class BlobExportImportCases(BlobTestCase):
    def testCopyAcrossDocuments(self):
        source = self.newDocument("BlobSource")
        target = self.newDocument("BlobTarget")
        obj = self.fileObject(source, "File1", b"copied")
        copies = target.copyObject([obj])
        self.assertContent(copies[0], b"copied")
        self.assertEqual(len(self.storedBlobs(target)), 1)

    def testCopyTargetOwnsItsContent(self):
        """The target document must not depend on the source staying open."""
        source = self.newDocument("BlobSource")
        target = self.newDocument("BlobTarget")
        obj = self.fileObject(source, "File1", b"owned")
        copies = target.copyObject([obj])
        FreeCAD.closeDocument(source.Name)
        self.assertContent(copies[0], b"owned")
        project = self.projectPath("target.FCStd")
        target.saveAs(project)
        FreeCAD.closeDocument(target.Name)
        reopened = self.openDocument(project)
        self.assertContent(reopened.Objects[0], b"owned")

    def testCopySharedContentStaysShared(self):
        source = self.newDocument("BlobSource")
        target = self.newDocument("BlobTarget")
        first = self.fileObject(source, "File1", b"shared", saveName="a.txt")
        second = self.fileObject(source, "File2", b"shared", saveName="b.txt")
        target.copyObject([first, second])
        self.assertEqual(len(self.storedBlobs(target)), 1)

    def testCopyCarriesOnlyTheExportedSubset(self):
        """A clipboard buffer must not carry blobs of unrelated objects."""
        source = self.newDocument("BlobSource")
        target = self.newDocument("BlobTarget")
        wanted = self.fileObject(source, "File1", b"wanted")
        self.fileObject(source, "File2", b"unrelated")
        target.copyObject([wanted])
        self.assertEqual(len(self.storedBlobs(target)), 1)

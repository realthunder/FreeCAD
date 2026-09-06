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

"""How a document stores its shapes (docs/SharedShapeStorage.md).

  ShapeLocationCases  location canonicalization (sec 11.4, build step 12.2):
                      the geometry goes out at the identity and the top level
                      location is written as a `loc=` attribute instead
  ShapeBlobCases      the geometry as a file in the document's blob store
                      (sec 12.3): named after its property, shared by content,
                      skipped when unchanged, parsed once
  ShapeRefCases       external file references (sec 11.6, build step 12.4): a
                      sub-shape already stored in another object's file is
                      referenced instead of serialized again
  BaseShapeCases      retained base shapes (docs/TopoNamingEnhance.md sec 7):
                      the generation a missing reference was resolved against
                      is kept as a `_BaseShape<N>` property while it is needed
  ForeignBaseShapeCases  the sub-shapes a document's references into other
                      documents resolved against (sec 7.13): kept by the
                      referring document, rebuilt at every save, served when
                      the reference comes back missing

Run headless with:  FreeCADCmd -t ShapeStorage
"""

import os
import re
import shutil
import tempfile
import unittest
import zipfile
from xml.etree import ElementTree

import FreeCAD

try:
    import Part

    HAS_PART = True
except ImportError:
    HAS_PART = False

BLOB_DIR = "blobs"
BLOB_INDEX = "Content.xml"


@unittest.skipUnless(HAS_PART, "Part module not available")
class ShapeTestCase(unittest.TestCase):
    """Scratch directory, document bookkeeping and store-aware assertions."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="fc_shape_test_")
        self.docs = []
        self.stateSaveMaterialCards(False)

    def tearDown(self):
        for name in list(self.docs):
            if name in FreeCAD.listDocuments():
                FreeCAD.closeDocument(name)
        shutil.rmtree(self.tmp, ignore_errors=True)
        self.restoreSaveMaterialCards()

    # -- settings this case depends on -------------------------------------

    DOC_PARAMS = "User parameter:BaseApp/Preferences/Document"

    def stateSaveMaterialCards(self, value):
        """State the SaveMaterialCards setting this case depends on.

        It defaults ON, which writes a stock card's content into the document
        as a .FCMat file. These cases predate that default and assume a
        document that carries no card, so they say so rather than inherit it.

        tearDown puts the parameter back, and REMOVES it when it was unset --
        leaving a stale one behind makes every later run of the whole suite
        disagree with a fresh one, which is exactly how these failures hid.
        """
        params = FreeCAD.ParamGet(self.DOC_PARAMS)
        self._savedCardsKey = "SaveMaterialCards" in params.GetBools()
        self._savedCards = params.GetBool("SaveMaterialCards", True)
        params.SetBool("SaveMaterialCards", value)

    def restoreSaveMaterialCards(self):
        if not hasattr(self, "_savedCardsKey"):
            return
        params = FreeCAD.ParamGet(self.DOC_PARAMS)
        if self._savedCardsKey:
            params.SetBool("SaveMaterialCards", self._savedCards)
        else:
            params.RemBool("SaveMaterialCards")
        del self._savedCardsKey

    # -- fixtures ----------------------------------------------------------

    def newDocument(self, name="ShapeDoc"):
        doc = FreeCAD.newDocument(name)
        self.docs.append(doc.Name)
        # Canonicalized locations are part of this fork's format. Stated
        # rather than inherited, so these cases do not move when the default
        # does -- and the case that wants upstream's format says so too.
        doc.SaveSchemaVersion = 5
        return doc

    def openDocument(self, path):
        doc = FreeCAD.openDocument(path)
        self.docs.append(doc.Name)
        return doc

    def box(self, doc, name, placement=None, length=10, width=20, height=30):
        obj = doc.addObject("Part::Box", name)
        obj.Length = length
        obj.Width = width
        obj.Height = height
        if placement is not None:
            obj.Placement = placement
        return obj

    def placement(self, x, y, z, angle=0.0, axis=(0, 0, 1)):
        return FreeCAD.Placement(
            FreeCAD.Vector(x, y, z), FreeCAD.Rotation(FreeCAD.Vector(*axis), angle)
        )

    def projectPath(self, name="project.FCStd"):
        return os.path.join(self.tmp, name)

    def directoryPath(self, name="project_dir"):
        path = os.path.join(self.tmp, name)
        os.makedirs(path)
        return path

    # -- assertions --------------------------------------------------------

    def documentXml(self, project):
        """All of a project's XML.

        A directory project is split per object (SplitXML, on by default in
        this fork), so a shape's element is not in Document.xml at all.
        """
        if os.path.isdir(project):
            parts = []
            for name in sorted(os.listdir(project)):
                if name.endswith(".xml"):
                    with open(os.path.join(project, name), "rb") as handle:
                        parts.append(handle.read().decode("utf-8"))
            return "\n".join(parts)
        archive = zipfile.ZipFile(project)
        return "\n".join(
            archive.read(name).decode("utf-8")
            for name in sorted(archive.namelist())
            if name.endswith(".xml") and not name.startswith(BLOB_DIR + "/")
        )

    def blobNames(self, project):
        """The stored files of a project, index excluded."""
        if os.path.isdir(project):
            blobdir = os.path.join(project, BLOB_DIR)
            if not os.path.isdir(blobdir):
                return []
            return sorted(n for n in os.listdir(blobdir) if n != BLOB_INDEX)
        archive = zipfile.ZipFile(project)
        prefix = BLOB_DIR + "/"
        return sorted(
            n[len(prefix) :]
            for n in archive.namelist()
            if n.startswith(prefix) and n != prefix + BLOB_INDEX
        )

    def blobBytes(self, project, name):
        if os.path.isdir(project):
            with open(os.path.join(project, BLOB_DIR, name), "rb") as handle:
                return handle.read()
        return zipfile.ZipFile(project).read("%s/%s" % (BLOB_DIR, name))

    def blobIndex(self, project):
        """The content index: name -> (hash, referrer tokens)."""
        entry = "%s/%s" % (BLOB_DIR, BLOB_INDEX)
        if os.path.isdir(project):
            path = os.path.join(project, BLOB_DIR, BLOB_INDEX)
            if not os.path.exists(path):
                return {}
            with open(path, "rb") as handle:
                data = handle.read()
        else:
            archive = zipfile.ZipFile(project)
            if entry not in archive.namelist():
                return {}
            data = archive.read(entry)
        root = ElementTree.fromstring(data)
        return {n.get("n"): (n.get("h"), (n.get("r") or "").split()) for n in root}

    def storedGeometry(self, project):
        """The serialized geometry of a project, wherever this writer put it.

        From step 12.3 that is one stored file per distinct geometry, whether
        the project is a directory or an archive. A save asked to carry
        everything inside its XML puts it there instead, so that form is
        collected too -- what is compared is the geometry and nothing around
        it.
        """
        bodies = [self.blobBytes(project, name) for name in self.blobNames(project)]
        for text in self.documentXml(project).split("<Part ")[1:]:
            head, _, rest = text.partition(">")
            if "brep=" in head or "binary=" in head:
                bodies.append(rest.split("</Part>")[0].encode("utf-8"))
        return bodies

    def assertPlacement(self, obj, expected, msg=""):
        actual = obj.Placement
        self.assertLess(
            (actual.Base - expected.Base).Length,
            1e-9,
            "%s position %s != %s" % (msg, actual.Base, expected.Base),
        )
        self.assertLess(
            abs(actual.Rotation.Angle - expected.Rotation.Angle),
            1e-9,
            "%s rotation %s != %s" % (msg, actual.Rotation, expected.Rotation),
        )


@unittest.skipUnless(HAS_PART, "Part module not available")
class ShapeLocationCases(ShapeTestCase):
    """The location lives in the XML, never in the geometry.

    Three things follow from that and are pinned down here: a placement still
    survives a round trip (it must -- outside a recompute Feature::onChanged
    reads Placement back out of the shape's own transform, so geometry
    announced at the identity would zero it), moving an object no longer
    rewrites its geometry, and two equal parts at different placements
    serialize to the same bytes.
    """

    # -- cases -------------------------------------------------------------

    def testPlacementSurvivesRoundTrip(self):
        """The whole reason the location cannot simply be dropped."""
        doc = self.newDocument()
        wanted = {
            "Plain": self.placement(0, 0, 0),
            "Moved": self.placement(11, -22.5, 3.25),
            "Turned": self.placement(1.5, 2.5, 3.5, angle=37.0, axis=(1, 2, 3)),
        }
        for name, placement in wanted.items():
            self.box(doc, name, placement)
        doc.recompute()
        elements = {n: doc.getObject(n).Shape.ElementMapSize for n in wanted}
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        for name, placement in wanted.items():
            obj = reopened.getObject(name)
            self.assertPlacement(obj, placement, name)
            # The element map is written against the same geometry and is
            # index-based, so taking the location off must not disturb it --
            # a mismatch would come back as a document asking to recompute.
            self.assertEqual(obj.Shape.ElementMapSize, elements[name], name)
            self.assertEqual(obj.State, ["Up-to-date"], name)
            # The geometry has to have moved with it, not just the property.
            self.assertLess(
                (obj.Shape.BoundBox.Center - placement.multVec(FreeCAD.Vector(5, 10, 15))).Length,
                1e-9,
                "%s geometry is not where its placement says" % name,
            )

    def testServingTheGeometryDoesNotZeroThePlacement(self):
        """The restore trap, at the moment it would fire.

        A schema-5 shape is served out of the store on first use, long after
        the placement was restored. That serve announces a value, and outside
        a recompute Feature::onChanged answers by reading Placement out of the
        shape -- so the location has to be back on before the announcement.
        """
        doc = self.newDocument()
        wanted = self.placement(7, 8, 9, angle=45.0, axis=(0, 1, 0))
        self.box(doc, "Moved", wanted)
        doc.recompute()
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        obj = reopened.getObject("Moved")
        self.assertPlacement(obj, wanted, "before the shape is touched")
        self.assertFalse(obj.Shape.isNull())
        self.assertPlacement(obj, wanted, "after the shape is served")

    def testLocationIsWrittenAsAnAttribute(self):
        doc = self.newDocument()
        self.box(doc, "Plain")
        self.box(doc, "Moved", self.placement(11, -22.5, 3.25))
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        xml = self.documentXml(project)
        # One placed shape, one at the identity -- and an identity location is
        # nothing to say, so exactly one attribute is expected. A placement of
        # zero is still a location object (it just does nothing), so this is
        # the assertion that stops it from being written.
        self.assertEqual(xml.count(' loc="'), 1)
        self.assertIn('loc="1 0 0 11 0 1 0 -22.5 0 0 1 3.25"', xml)
        # Round numbers stay round: the shortest text that reads back exactly.
        self.assertNotIn("-22.500000", xml)
        # And with the location gone, the two boxes are the same geometry --
        # which, geometry being stored by content, is one file.
        self.assertEqual(len(self.storedGeometry(project)), 1)

    def moveLeavesTheGeometryUntouched(self, project):
        doc = self.newDocument()
        obj = self.box(doc, "Moved", self.placement(1, 2, 3))
        doc.recompute()
        doc.saveAs(project)
        before = self.storedGeometry(project)
        self.assertTrue(before, "the document stored no geometry to compare")

        moved = self.placement(40, 50, 60, angle=15.0)
        obj.Placement = moved
        doc.recompute()
        doc.save()
        self.assertEqual(self.storedGeometry(project), before)

        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertPlacement(reopened.getObject("Moved"), moved)

    def testMoveLeavesTheGeometryUntouched(self):
        """The point of the step: a move is an XML diff, not a rewrite."""
        self.moveLeavesTheGeometryUntouched(self.directoryPath())

    def testMoveLeavesTheStoredGeometryUntouched(self):
        """The same, through the archive writer -- where the geometry is the
        shape store, and an unchanged store is a blob the manager's hash skip
        does not write at all."""
        self.moveLeavesTheGeometryUntouched(self.projectPath())

    def testPlacementDoesNotReachTheGeometry(self):
        """Two documents differing only in placement store the same bytes.

        This is what makes duplicates visible to whole-file hashing: the
        fasteners of a real assembly are equal parts sitting at different
        placements, each with its own TShape (docs sec 11.4).
        """
        placed = self.newDocument("Placed")
        self.box(placed, "A", self.placement(0, 0, 0))
        self.box(placed, "B", self.placement(100, 5, -7, angle=30.0, axis=(1, 1, 0)))
        placed.recompute()
        placedProject = self.directoryPath("placed_dir")
        placed.saveAs(placedProject)

        flat = self.newDocument("Flat")
        self.box(flat, "A")
        self.box(flat, "B")
        flat.recompute()
        flatProject = self.directoryPath("flat_dir")
        flat.saveAs(flatProject)

        self.assertEqual(self.storedGeometry(placedProject), self.storedGeometry(flatProject))

    def testInlinedGeometryCarriesNoLocationEither(self):
        """Geometry written inside the XML is a separate branch of both the
        save and the restore. A save asked to carry everything itself takes
        it -- above ForceXML level 3 there are no stored files to be in."""
        doc = self.newDocument()
        wanted = self.placement(4, 5, 6, angle=60.0, axis=(1, 0, 1))
        self.box(doc, "Moved", wanted)
        doc.recompute()
        doc.ForceXML = 4
        project = self.directoryPath()
        doc.saveAs(project)
        xml = self.documentXml(project)
        self.assertEqual(self.blobNames(project), [])
        self.assertIn(' loc="', xml)
        self.assertIn(' brep="1"', xml)
        # The location is not in the geometry as well: the BRep location table
        # of a shape written at the identity is empty.
        self.assertIn("Locations 0", xml)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        obj = reopened.getObject("Moved")
        self.assertPlacement(obj, wanted)
        self.assertLess(
            (obj.Shape.BoundBox.Center - wanted.multVec(FreeCAD.Vector(5, 10, 15))).Length,
            1e-9,
        )

    def testCopyBetweenDocumentsKeepsThePlacement(self):
        """The export writer is self-contained by design and carries no
        schema, so it never canonicalizes -- the location stays in the
        geometry there, and has to still arrive."""
        source = self.newDocument("Source")
        wanted = self.placement(2, -3, 4, angle=90.0, axis=(0, 1, 0))
        obj = self.box(source, "Moved", wanted)
        source.recompute()
        target = self.newDocument("Target")
        copied = target.copyObject(obj)
        target.recompute()
        self.assertPlacement(copied, wanted)
        self.assertLess(
            (copied.Shape.BoundBox.Center - wanted.multVec(FreeCAD.Vector(5, 10, 15))).Length,
            1e-9,
        )

    def testSchemaFourIsUnchanged(self):
        """Schema 4 is upstream's format and stays exactly that: the geometry
        keeps its location, and an older reader that never heard of `loc=`
        reads the document unharmed."""
        doc = FreeCAD.newDocument("LegacyDoc")
        self.docs.append(doc.Name)
        wanted = self.placement(3, 4, 5, angle=20.0)
        self.box(doc, "Moved", wanted)
        doc.recompute()
        doc.SaveSchemaVersion = 4
        self.assertEqual(doc.SaveSchemaVersion, 4)
        project = self.projectPath("legacy.FCStd")
        doc.saveAs(project)
        self.assertNotIn(' loc="', self.documentXml(project))
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        self.assertPlacement(reopened.getObject("Moved"), wanted)
        # And the file's own format comes back with it: a document restored
        # from upstream's format is not converted by the next save.
        self.assertEqual(reopened.SaveSchemaVersion, 4)


@unittest.skipUnless(HAS_PART, "Part module not available")
class ShapeBlobCases(ShapeTestCase):
    """The geometry is an ordinary file in the document's blob store.

    Named after the property that owns it, addressed by content, skipped when
    unchanged, pruned when orphaned, parsed once however many objects share
    it -- docs/SharedShapeStorage.md sec 12.3.
    """

    def testGeometryIsAFileNamedAfterTheProperty(self):
        doc = self.newDocument()
        self.box(doc, "Box")
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(self.blobNames(project), ["Box.Shape.brp"])
        # Standard ASCII BRep, openable by anything that reads one.
        self.assertTrue(self.blobBytes(project, "Box.Shape.brp").startswith(b"\nCASCADE"))
        xml = self.documentXml(project)
        self.assertIn(' hash="', xml)
        self.assertNotIn(' brep="1"', xml)
        self.assertNotIn(' store="', xml)

    def testEqualGeometryIsOneFileWithTwoReferrers(self):
        """Two boxes built independently have two TShapes and no sharing at
        all in memory -- and still come out as one file, because the file is
        addressed by what is in it."""
        doc = self.newDocument()
        self.box(doc, "Box")
        self.box(doc, "Box001", self.placement(50, 0, 0))
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(self.blobNames(project), ["Box.Shape.brp"])
        index = self.blobIndex(project)
        self.assertEqual(len(index["Box.Shape.brp"][1]), 2)

    def testSharedFileIsParsedOnce(self):
        """One file, one parse, one TShape -- which is the sharing the central
        store used to provide, arrived at by content addressing instead."""
        doc = self.newDocument()
        self.box(doc, "Box")
        self.box(doc, "Box001", self.placement(50, 0, 0))
        doc.recompute()
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        first = reopened.getObject("Box").Shape
        second = reopened.getObject("Box001").Shape
        self.assertTrue(first.isPartner(second), "the two shapes are not one TShape")
        # And they are still where they belong.
        self.assertPlacement(reopened.getObject("Box001"), self.placement(50, 0, 0))

    def testUnchangedGeometryIsNotRewritten(self):
        doc = self.newDocument()
        self.box(doc, "Box")
        obj = self.box(doc, "Other", self.placement(0, 0, 40), length=3)
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        before = {n: self.blobBytes(project, n) for n in self.blobNames(project)}
        stamps = {
            n: os.stat(os.path.join(project, BLOB_DIR, n)).st_mtime_ns for n in before
        }
        self.assertEqual(len(before), 2)

        obj.Length = 7
        doc.recompute()
        doc.save()
        after = {n: self.blobBytes(project, n) for n in self.blobNames(project)}
        self.assertEqual(after["Box.Shape.brp"], before["Box.Shape.brp"])
        self.assertEqual(
            os.stat(os.path.join(project, BLOB_DIR, "Box.Shape.brp")).st_mtime_ns,
            stamps["Box.Shape.brp"],
            "an unchanged shape was written again",
        )
        self.assertNotEqual(after["Other.Shape.brp"], before["Other.Shape.brp"])

    def testDeletedObjectLeavesNoFile(self):
        doc = self.newDocument()
        self.box(doc, "Box")
        obj = self.box(doc, "Other", self.placement(0, 0, 40), length=3)
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(len(self.blobNames(project)), 2)

        doc.removeObject(obj.Name)
        doc.recompute()
        doc.save()
        self.assertEqual(self.blobNames(project), ["Box.Shape.brp"])

    def testDocumentWithoutGeometryHasNoBlobDirectory(self):
        doc = self.newDocument()
        doc.addObject("App::FeatureTest", "Plain")
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertFalse(os.path.isdir(os.path.join(project, BLOB_DIR)))

    def testBinaryGeometryRoundTrip(self):
        doc = self.newDocument()
        wanted = self.placement(1, 2, 3, angle=25.0)
        self.box(doc, "Box", wanted)
        doc.recompute()
        doc.PreferBinary = True
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertEqual(self.blobNames(project), ["Box.Shape.bin"])
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        obj = reopened.getObject("Box")
        self.assertPlacement(obj, wanted)
        self.assertEqual(len(obj.Shape.Faces), 6)

    def testResaveOfAnUnmodifiedProjectChangesNothing(self):
        """What the whole naming and skipping scheme is for: version control
        sees no diff when the model did not change."""
        doc = self.newDocument()
        self.box(doc, "Box", self.placement(3, 0, 0))
        self.box(doc, "Other", self.placement(0, 5, 0), length=3)
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        before = {n: self.blobBytes(project, n) for n in self.blobNames(project)}
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        reopened.save()
        self.assertEqual({n: self.blobBytes(project, n) for n in self.blobNames(project)}, before)


@unittest.skipUnless(HAS_PART, "Part module not available")
class ShapeRefCases(ShapeTestCase):
    """A sub-shape another file already holds is named, not stored again.

    Sub-shape sharing is what a save destroyed and what this restores: an
    aggregate's leaves *are* its children's shapes, and with one file per
    object each side used to be written, parsed and tessellated separately.
    """

    def sharedDocument(self):
        """Two boxes and a compound over them -- the aggregate whose leaves
        are its children's very shapes."""
        doc = self.newDocument()
        first = self.box(doc, "BoxA")
        second = self.box(doc, "BoxB", length=5)
        compound = doc.addObject("Part::Compound", "Comp")
        compound.Links = [first, second]
        doc.recompute()
        return doc, compound

    def testSubShapeIsBorrowedFromAnotherFile(self):
        doc, compound = self.sharedDocument()
        project = self.directoryPath()
        doc.saveAs(project)

        stored = {n: self.blobBytes(project, n) for n in self.blobNames(project)}
        self.assertEqual(sorted(stored), ["BoxA.Shape.brp", "BoxB.Shape.brp", "Comp.Shape.brp"])
        borrower = stored["Comp.Shape.brp"]
        self.assertIn(b"\nFiles 2\n", borrower)
        # Both children are named, and neither is written out again: the
        # compound is a record and two references, not two solids.
        self.assertEqual(borrower.count(b"\nTShapes 1\n"), 1)
        self.assertLess(
            len(borrower),
            min(len(stored["BoxA.Shape.brp"]), len(stored["BoxB.Shape.brp"])),
            "the compound stored geometry it should have referenced",
        )

    def testAFileThatBorrowsNothingSaysNothing(self):
        """The extension is present only where sharing is, so most of a
        project stays a shape file any OCCT tool can read."""
        doc, _ = self.sharedDocument()
        project = self.directoryPath()
        doc.saveAs(project)
        for name in ("BoxA.Shape.brp", "BoxB.Shape.brp"):
            data = self.blobBytes(project, name)
            self.assertNotIn(b"Files ", data)
            self.assertTrue(data.startswith(b"\nCASCADE Topology V1"), name)

    def testSharingSurvivesTheRoundTrip(self):
        """The point of the whole design: after a reopen the aggregate's leaf
        and the child object are one TShape again, which they were not."""
        doc, compound = self.sharedDocument()
        self.assertTrue(compound.Shape.Solids[0].isPartner(doc.getObject("BoxA").Shape))
        project = self.projectPath()
        doc.saveAs(project)
        volume = compound.Shape.Volume
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        leaf = reopened.getObject("Comp").Shape.Solids[0]
        self.assertTrue(leaf.isPartner(reopened.getObject("BoxA").Shape))
        self.assertAlmostEqual(reopened.getObject("Comp").Shape.Volume, volume, places=6)
        self.assertEqual(len(reopened.getObject("Comp").Shape.Faces), 12)

    def testResaveOfABorrowingProjectChangesNothing(self):
        doc, _ = self.sharedDocument()
        project = self.directoryPath()
        doc.saveAs(project)
        before = {n: self.blobBytes(project, n) for n in self.blobNames(project)}
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        reopened.save()
        self.assertEqual({n: self.blobBytes(project, n) for n in self.blobNames(project)}, before)

    def testAWholeSharedShapeIsOneFile(self):
        """A shape that is another object's *whole* shape is shared, not
        referenced.

        Writing a reference there would produce a file holding nothing but
        that reference, and would cost the sharing content addressing was
        already getting right -- the two objects have identical bytes, so they
        are one file. Borrowing pays below a root, where there is geometry to
        leave out; at the root it is pure indirection.
        """
        doc = self.newDocument()
        source = doc.addObject("Part::Feature", "Source")
        twin = doc.addObject("Part::Feature", "Twin")
        solid = Part.makeBox(10, 20, 30)
        source.Shape = solid
        twin.Shape = solid
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)

        names = [n for n in self.blobNames(project) if n.endswith(".brp")]
        self.assertEqual(names, ["Source.Shape.brp"], "the twin wrote a file of its own")
        self.assertNotIn(b"Files ", self.blobBytes(project, "Source.Shape.brp"))
        # One file, both properties named against it.
        self.assertEqual(len(self.blobIndex(project)["Source.Shape.brp"][1]), 2)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        first = reopened.getObject("Source").Shape
        second = reopened.getObject("Twin").Shape
        self.assertTrue(first.isPartner(second))
        self.assertAlmostEqual(second.Volume, 10 * 20 * 30, places=6)

    def testAStoredFaceKeepsItsEdgesCurves(self):
        """The invariant the whole scheme lives under.

        A face keys its edges' 2D curves on the `Geom_Surface` object it
        carries, and an edge keys its vertices' parameters on its curve. Those
        are object identities inside one file's tables and they do not survive
        the other file being parsed separately: an edge borrowed into a stored
        face comes back with pcurves naming a surface that is not this face's,
        `CurveOnSurface` finds nothing, and anything projecting the shape
        dereferences a null 2D curve. A shell split the same way comes back
        with two edges everywhere the model has one.

        So a face, an edge, a wire and a shell are each borrowed whole or
        stored whole. A boolean on a curved solid is the smallest thing that
        asks for it -- planar faces hide it, because OCCT stores no pcurve for
        those and computes one when asked.
        """
        doc = self.newDocument()
        whole = doc.addObject("Part::Feature", "Whole")
        carved = doc.addObject("Part::Feature", "Carved")
        ball = Part.makeSphere(10)
        whole.Shape = ball
        carved.Shape = ball.cut(Part.makeBox(20, 20, 20))
        doc.recompute()
        # The premise: the two really do share sub-shapes in memory.
        shared = sum(
            1 for a in ball.Edges for b in carved.Shape.Edges if a.isPartner(b)
        )
        self.assertGreater(shared, 0, "nothing was shared, so this proves nothing")

        project = self.directoryPath()
        doc.saveAs(project)
        volume = carved.Shape.Volume
        edges = len(carved.Shape.Edges)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        shape = reopened.getObject("Carved").Shape
        for index, face in enumerate(shape.Faces):
            for edge in face.Edges:
                self.assertIsNotNone(
                    face.curveOnSurface(edge),
                    "face %d has an edge with no curve on it" % index,
                )
        self.assertTrue(shape.isValid(), "the shell came back split")
        self.assertEqual(len(shape.Edges), edges, "the shell gained edges")
        self.assertAlmostEqual(shape.Volume, volume, places=6)

    def testABorrowerIsRewrittenWhenWhatItBorrowedIsGone(self):
        """An unchanged shape is not on its own a reason to keep the file
        written for it.

        These two share a TShape with no dependency between them, so replacing
        the first object's shape leaves the second untouched -- and its file
        still naming a file this save no longer writes. What catches that is
        the plan the file was written with, not the shape.
        """
        doc = self.newDocument()
        source = doc.addObject("Part::Feature", "Source")
        borrower = doc.addObject("Part::Feature", "Borrower")
        solid = Part.makeBox(10, 20, 30)
        source.Shape = solid
        borrower.Shape = Part.Compound([solid])
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertIn(b"Files 1", self.blobBytes(project, "Borrower.Shape.brp"))

        # The source now holds different geometry; the borrower is untouched.
        source.Shape = Part.makeBox(4, 4, 4)
        doc.recompute()
        doc.save()
        rewritten = self.blobBytes(project, "Borrower.Shape.brp")
        self.assertNotIn(b"Files ", rewritten)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        recovered = reopened.getObject("Borrower").Shape
        self.assertFalse(recovered.isNull())
        self.assertAlmostEqual(recovered.Volume, 10 * 20 * 30, places=6)


if __name__ == "__main__":
    unittest.main()


@unittest.skipUnless(HAS_PART, "Part module not available")
class ShapeCongruenceCases(ShapeTestCase):
    """The same part in two places is stored once and moved back.

    Content addressing shares parts whose bytes match. An exporter that
    multiplies the placement into the coordinates defeats it: the same part at
    twenty positions is twenty distinct contents. transformGeometry() is that
    exporter in miniature -- it bakes a transform into the numbers -- so these
    cases are the real thing at a size a test can check.
    """

    def congruentDocument(self, doc):
        """A part, the same part baked into two other positions, and one that
        is a different part."""
        base = Part.makeBox(10, 20, 30).cut(
            Part.makeCylinder(3, 40, FreeCAD.Vector(5, 10, -5))
        )
        first = doc.addObject("Part::Feature", "First")
        first.Shape = base

        moved = FreeCAD.Matrix()
        moved.rotateZ(0.7)
        moved.move(FreeCAD.Vector(100, 50, 25))
        second = doc.addObject("Part::Feature", "Second")
        second.Shape = base.transformGeometry(moved)

        elsewhere = FreeCAD.Matrix()
        elsewhere.rotateX(1.3)
        elsewhere.move(FreeCAD.Vector(-40, 5, 9))
        third = doc.addObject("Part::Feature", "Third")
        third.Shape = base.transformGeometry(elsewhere)

        other = doc.addObject("Part::Feature", "Other")
        other.Shape = Part.makeBox(10, 20, 31)
        doc.recompute()
        return first, second, third, other

    def centre(self, shape):
        """The average of a shape's vertices.

        Not the bounding box, which OCCT estimates from surface poles and which
        therefore is not invariant under a rigid motion: a shape stored once
        and moved back reports a different box while being the same geometry to
        1.8e-15. Not CenterOfMass either, which a compound does not have.
        """
        points = [v.Point for v in shape.Vertexes]
        total = FreeCAD.Vector()
        for point in points:
            total = total + point
        return total * (1.0 / len(points)) if points else total

    def savedWith(self, congruent, name):
        group = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        previous = group.GetBool("DedupCongruentShapes", True)
        group.SetBool("DedupCongruentShapes", congruent)
        try:
            doc = self.newDocument(name)
            shapes = self.congruentDocument(doc)
            volumes = [obj.Shape.Volume for obj in shapes]
            centres = [self.centre(obj.Shape) for obj in shapes]
            project = self.directoryPath(name + "_dir")
            doc.saveAs(project)
            FreeCAD.closeDocument(doc.Name)
            self.docs.remove(name)
            return project, volumes, centres
        finally:
            group.SetBool("DedupCongruentShapes", previous)

    def testCongruentInstancesShareOneFile(self):
        plain, _, _ = self.savedWith(False, "CongOff")
        shared, _, _ = self.savedWith(True, "CongOn")
        self.assertEqual(
            len(self.blobNames(plain)), 4, "each part should have its own file"
        )
        # Second and Third are one part in two places; First is that part with
        # exact geometry rather than the spline transformGeometry leaves, and
        # Other is a different part. Three files, not two.
        self.assertEqual(
            len(self.blobNames(shared)),
            3,
            "the two instances of one part should share a file",
        )

    def testSharedInstancesComeBackWhereTheyWere(self):
        project, volumes, centres = self.savedWith(True, "CongBack")
        doc = self.openDocument(project)
        for i, name in enumerate(("First", "Second", "Third", "Other")):
            shape = doc.getObject(name).Shape
            self.assertAlmostEqual(
                shape.Volume,
                volumes[i],
                delta=abs(volumes[i]) * 1e-9,
                msg="%s came back as a different shape" % name,
            )
            self.assertLess(
                (self.centre(shape) - centres[i]).Length,
                1e-8,
                "%s came back in the wrong place" % name,
            )

    def testPlacementIsNotUsedToCarryTheMotion(self):
        """The motion goes into the geometry, never into the placement.

        A restored shape's location is the object's Placement, which is model
        data: an App::Link that replaces its source's placement with its own
        reads it, and would draw this geometry where the instance it borrowed
        from sits. A first attempt at this moved 1146 links that way.
        """
        project, _, _ = self.savedWith(True, "CongPlacement")
        doc = self.openDocument(project)
        for name in ("First", "Second", "Third", "Other"):
            self.assertPlacement(
                doc.getObject(name),
                FreeCAD.Placement(),
                "%s placement was used to carry the motion" % name,
            )

    def testDifferentPartsAreNotMerged(self):
        """Everything cheap says these two are the same part.

        The near-miss this guards against is real: an exact cylinder and the
        spline transformGeometry() writes for it share every vertex, every edge
        midpoint, their area and their volume, and are not the same shape.
        """
        project, volumes, _ = self.savedWith(True, "CongDistinct")
        doc = self.openDocument(project)
        first = doc.getObject("First").Shape.Volume
        second = doc.getObject("Second").Shape.Volume
        self.assertGreater(
            abs(first - second) / abs(first),
            1e-6,
            "an approximation was stored in place of the shape it approximates",
        )

    def testSwitchedOffNothingIsShared(self):
        plain, volumes, centres = self.savedWith(False, "CongDisabled")
        self.assertEqual(len(self.blobNames(plain)), 4)
        doc = self.openDocument(plain)
        for i, name in enumerate(("First", "Second", "Third", "Other")):
            self.assertAlmostEqual(
                doc.getObject(name).Shape.Volume,
                volumes[i],
                delta=abs(volumes[i]) * 1e-12,
            )


@unittest.skipUnless(HAS_PART, "Part module not available")
class ShapeGeometryCases(ShapeTestCase):
    """A surface or curve another file already writes is named, not written.

    Sub-shape sharing needs the two files to hold one TShape. Two parts that
    were built apart share none, and still stand on the same planes and the
    same circles -- which each file writes out in full. That repetition is
    about half of what a project's geometry tables hold, and it is what this
    names instead (docs/SharedShapeStorage.md sec 12.13).
    """

    def savedWith(self, sharing, name):
        group = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        previous = group.GetBool("DedupCrossFileGeometry", False)
        group.SetBool("DedupCrossFileGeometry", sharing)
        try:
            doc = self.newDocument(name)
            plain = doc.addObject("Part::Feature", "Plain")
            plain.Shape = Part.makeBox(10, 20, 30)
            # A different content, so the two are not one file, and a
            # different TShape, so nothing can be borrowed as a sub-shape --
            # and the same box geometry, entry for entry.
            twin = doc.addObject("Part::Feature", "Twin")
            twin.Shape = Part.Compound(
                [Part.makeBox(10, 20, 30), Part.makeCylinder(2, 5)]
            )
            doc.recompute()
            volumes = [plain.Shape.Volume, twin.Shape.Volume]
            project = self.directoryPath(name + "_dir")
            doc.saveAs(project)
            FreeCAD.closeDocument(doc.Name)
            self.docs.remove(name)
            return project, volumes
        finally:
            group.SetBool("DedupCrossFileGeometry", previous)

    def testSwitchedOffNoFileNamesAnother(self):
        """Off is the format that ships, so this is the baseline everything
        else is measured against."""
        project, _ = self.savedWith(False, "GeomOff")
        for name in self.blobNames(project):
            self.assertNotIn(b"\nFiles ", self.blobBytes(project, name))

    def testEqualGeometryIsNamedInTheEarlierFile(self):
        plain, _ = self.savedWith(False, "GeomPlain")
        shared, _ = self.savedWith(True, "GeomShared")
        before = self.blobBytes(plain, "Twin.Shape.brp")
        after = self.blobBytes(shared, "Twin.Shape.brp")
        self.assertIn(b"\nFiles 1\n", after)
        self.assertIn(b"\nE1 ", after)
        self.assertLess(len(after), len(before), "no geometry was left out")
        # The file it names is untouched: what is written first owns what it
        # holds, and pays nothing for being named.
        self.assertEqual(
            self.blobBytes(plain, "Plain.Shape.brp"),
            self.blobBytes(shared, "Plain.Shape.brp"),
        )

    def testSharedGeometryComesBackWhole(self):
        """Byte counts cannot gate a format change.

        The first cut of the pcurve dedup reported smaller files, the same
        file count and the same reference count -- and had emptied 25443 face
        pcurves. So every storage change is checked by reopening it: the
        shapes are valid, every face still has a curve on each of its edges,
        and the volumes are the ones that were saved.
        """
        project, volumes = self.savedWith(True, "GeomBack")
        doc = self.openDocument(project)
        for index, name in enumerate(("Plain", "Twin")):
            shape = doc.getObject(name).Shape
            self.assertTrue(shape.isValid(), "%s came back invalid" % name)
            self.assertAlmostEqual(
                shape.Volume,
                volumes[index],
                delta=abs(volumes[index]) * 1e-9,
                msg="%s came back as a different shape" % name,
            )
            for face in shape.Faces:
                for edge in face.Edges:
                    self.assertIsNotNone(
                        face.curveOnSurface(edge),
                        "%s has a face with an edge carrying no curve" % name,
                    )

    def testResaveOfASharingProjectChangesNothing(self):
        """A file's bytes are its geometry and what the save decided to name,
        so the plan a reopened file states has to cover the geometry too --
        otherwise every save rewrites every file."""
        group = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        previous = group.GetBool("DedupCrossFileGeometry", False)
        group.SetBool("DedupCrossFileGeometry", True)
        try:
            project, _ = self.savedWith(True, "GeomResave")
            before = {n: self.blobBytes(project, n) for n in self.blobNames(project)}
            reopened = self.openDocument(project)
            reopened.save()
            self.assertEqual(
                {n: self.blobBytes(project, n) for n in self.blobNames(project)},
                before,
            )
        finally:
            group.SetBool("DedupCrossFileGeometry", previous)

    def testTheNamedFileGoingAwayRewritesTheOther(self):
        """The cost of the format, and the reason it is off by default: one
        file's geometry now depends on another file being there. What keeps
        that from dangling is the same plan check a borrowed sub-shape rests
        on, and this is the case that exercises it."""
        group = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        previous = group.GetBool("DedupCrossFileGeometry", False)
        group.SetBool("DedupCrossFileGeometry", True)
        try:
            project, volumes = self.savedWith(True, "GeomGone")
            doc = self.openDocument(project)
            doc.removeObject("Plain")
            doc.recompute()
            doc.save()
            self.assertNotIn(
                b"\nFiles ",
                self.blobBytes(project, "Twin.Shape.brp"),
                "the surviving file still names one that is gone",
            )
            FreeCAD.closeDocument(doc.Name)

            reopened = self.openDocument(project)
            shape = reopened.getObject("Twin").Shape
            self.assertTrue(shape.isValid())
            self.assertAlmostEqual(
                shape.Volume, volumes[1], delta=abs(volumes[1]) * 1e-9
            )
        finally:
            group.SetBool("DedupCrossFileGeometry", previous)


@unittest.skipUnless(HAS_PART, "Part module not available")
class BaseShapeCases(ShapeTestCase):
    """Retained base shapes (docs/TopoNamingEnhance.md section 7).

    The generation a missing element reference was last resolved against
    is kept on the referenced feature as a dynamic `_BaseShape<N>`
    property, and `_BaseShapeRefs` says which referrer holds which one.
    A generation lives exactly as long as some referrer names it, and a
    healthy document carries neither.

    The model is section 2.3's: a box cut by a cylinder, and planes
    attached FlatFace to faces of the cut. Replacing the cut's Base with a
    brand new box is what defeats the tag-based recovery; the top face
    (`Face3`) then moves to a new plane and its reference goes missing,
    while a side face (`Face6`) is repaired by geometry -- until the new
    box is also wider, which moves that face too.
    """

    def model(self, doc, faces=("Face3",)):
        box = self.box(doc, "Box", length=20, width=20, height=20)
        cyl = doc.addObject("Part::Cylinder", "Cylinder")
        cyl.Radius = 3
        cyl.Height = 40
        cyl.Placement = self.placement(10, 10, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base = box
        cut.Tool = cyl
        doc.recompute()
        planes = []
        for face in faces:
            plane = doc.addObject("Part::Plane", "Plane")
            plane.AttachmentSupport = [(cut, (face,))]
            plane.MapMode = "FlatFace"
            planes.append(plane)
        doc.recompute()
        return cut, planes

    def replaceBase(self, doc, cut, name, height=25, length=20):
        """Break: a *new* box as the cut's Base, at a new height."""
        cut.Base = self.box(doc, name, length=length, width=20, height=height)
        doc.recompute()

    def support(self, plane):
        return plane.AttachmentSupport[0][1][0]

    def entries(self, held):
        """The manifest for {plane: generation}.  A plane references the cut
        through two link properties, AttachmentSupport and its legacy twin
        Support, and each is a referrer of its own."""
        refs = {}
        for plane, generation in held.items():
            refs[plane.Name + ".AttachmentSupport"] = generation
            refs[plane.Name + ".Support"] = generation
        return refs

    def versions(self, obj):
        return sorted(
            n for n in obj.PropertiesList if n.startswith("_BaseShape") and n != "_BaseShapeRefs"
        )

    def refs(self, obj):
        if "_BaseShapeRefs" not in obj.PropertiesList:
            return {}
        return dict(obj._BaseShapeRefs)

    def filesWithHash(self, project, digest):
        return [name for name, (h, _) in self.blobIndex(project).items() if h == digest]

    def testAHealthyDocumentCarriesNoGeneration(self):
        """Gate 1: an edit the recovery survives leaves nothing behind, and
        the file is what it was before any of this existed."""
        doc = self.newDocument()
        cut, (plane,) = self.model(doc)
        doc.getObject("Box").Height = 22
        doc.recompute()
        self.assertEqual(self.support(plane), "Face3")
        self.assertEqual(self.versions(cut), [])
        self.assertEqual(self.refs(cut), {})
        project = self.directoryPath()
        doc.saveAs(project)
        self.assertNotIn("_BaseShape", self.documentXml(project))

    def testABrokenReferenceRetainsTheGeneration(self):
        """Gate 2: the generation is a property, the manifest names it, and
        the geometry the last save already wrote is not written again."""
        doc = self.newDocument()
        cut, (plane,) = self.model(doc)
        volume = cut.Shape.Volume
        project = self.directoryPath()
        doc.saveAs(project)
        oldHash = self.blobIndex(project)["Cut.Shape.brp"][0]
        oldBytes = self.blobBytes(project, "Cut.Shape.brp")

        self.replaceBase(doc, cut, "NewBox")
        self.assertTrue(self.support(plane).startswith("?"))
        self.assertEqual(self.versions(cut), ["_BaseShape1"])
        self.assertEqual(self.refs(cut), self.entries({plane: "_BaseShape1"}))
        self.assertAlmostEqual(cut._BaseShape1.Volume, volume)
        doc.save()
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        cut = reopened.getObject("Cut")
        self.assertEqual(self.versions(cut), ["_BaseShape1"])
        self.assertEqual(
            self.refs(cut),
            {"Plane.AttachmentSupport": "_BaseShape1", "Plane.Support": "_BaseShape1"},
        )
        self.assertAlmostEqual(cut._BaseShape1.Volume, volume)
        self.assertIn('name="_BaseShape1"', self.documentXml(project))
        files = self.filesWithHash(project, oldHash)
        self.assertEqual(len(files), 1)
        self.assertEqual(self.blobBytes(project, files[0]), oldBytes)

    def testTwoBreaksAtDifferentGenerationsKeepBoth(self):
        """Gate 3: B breaks at the first edit and A at the second; each keeps
        the generation it last resolved against.

        A's side face is re-resolved by position only after the cut's own
        resolve pass (by the attach extension re-setting Support), so for a
        moment A reads as missing too; re-setting releases it, and the end
        of the same recompute lets it go -- before any save.
        """
        doc = self.newDocument()
        cut, (planeB, planeA) = self.model(doc, ("Face3", "Face6"))
        project = self.directoryPath()
        self.replaceBase(doc, cut, "NewBox")
        self.assertTrue(self.support(planeB).startswith("?"))
        self.assertFalse(self.support(planeA).startswith("?"))
        self.assertEqual(self.refs(cut), self.entries({planeB: "_BaseShape1"}))
        doc.saveAs(project)
        self.assertEqual(self.refs(cut), self.entries({planeB: "_BaseShape1"}))

        self.replaceBase(doc, cut, "WideBox", length=30)
        self.assertTrue(self.support(planeA).startswith("?"))
        self.assertEqual(self.versions(cut), ["_BaseShape1", "_BaseShape2"])
        self.assertEqual(
            self.refs(cut), self.entries({planeB: "_BaseShape1", planeA: "_BaseShape2"})
        )
        doc.save()
        self.assertEqual(
            self.refs(cut), self.entries({planeB: "_BaseShape1", planeA: "_BaseShape2"})
        )
        index = self.blobIndex(project)
        hashes = set(h for h, _ in index.values())
        self.assertEqual(len(hashes), len(index))
        self.assertEqual(self.versions(doc.getObject("Cut")), ["_BaseShape1", "_BaseShape2"])

    def testTwoBreaksAtOneGenerationShareIt(self):
        """Gate 4: one shape, one map, however many referrers."""
        doc = self.newDocument()
        cut, (planeB, planeA) = self.model(doc, ("Face3", "Face3"))
        project = self.directoryPath()
        doc.saveAs(project)
        oldHash = self.blobIndex(project)["Cut.Shape.brp"][0]

        self.replaceBase(doc, cut, "NewBox")
        self.assertEqual(self.versions(cut), ["_BaseShape1"])
        self.assertEqual(
            self.refs(cut), self.entries({planeB: "_BaseShape1", planeA: "_BaseShape1"})
        )
        doc.save()
        self.assertEqual(len(self.filesWithHash(project, oldHash)), 1)

    def testARepairedReferenceLetsGo(self):
        """Gate 5: a reference repaired by hand drops its entry at the end
        of the recompute that repaired it, with no save in between; the
        generation stays while anyone names it."""
        doc = self.newDocument()
        cut, (planeB, planeA) = self.model(doc, ("Face3", "Face3"))
        project = self.directoryPath()
        doc.saveAs(project)
        self.replaceBase(doc, cut, "NewBox")
        self.assertEqual(len(self.refs(cut)), 4)

        planeB.AttachmentSupport = [(cut, ("Face3",))]
        doc.recompute()
        self.assertEqual(self.support(planeB), "Face3")
        self.assertEqual(self.refs(cut), self.entries({planeA: "_BaseShape1"}))
        self.assertEqual(self.versions(cut), ["_BaseShape1"])

        planeA.AttachmentSupport = [(cut, ("Face3",))]
        doc.recompute()
        self.assertEqual(self.versions(cut), [])
        self.assertNotIn("_BaseShapeRefs", cut.PropertiesList)
        doc.save()
        self.assertNotIn("_BaseShape", self.documentXml(project))

    def testARepointedReferenceLetsGo(self):
        """A reference moved to another object is nobody's business here
        any more, whatever it points at now."""
        doc = self.newDocument()
        cut, (plane,) = self.model(doc)
        self.replaceBase(doc, cut, "NewBox")
        self.assertEqual(self.versions(cut), ["_BaseShape1"])

        plane.AttachmentSupport = [(doc.getObject("Box"), ("Face1",))]
        doc.recompute()
        self.assertEqual(self.versions(cut), [])
        self.assertEqual(self.refs(cut), {})

    def testADeletedReferrerLetsGo(self):
        """Gate 6: a deleted referrer is nobody; its generation goes with the
        last one."""
        doc = self.newDocument()
        cut, (planeB, planeA) = self.model(doc, ("Face3", "Face3"))
        project = self.directoryPath()
        doc.saveAs(project)
        self.replaceBase(doc, cut, "NewBox")
        heldByA = self.entries({planeA: "_BaseShape1"})

        doc.removeObject(planeB.Name)
        doc.recompute()
        self.assertEqual(self.refs(cut), heldByA)

        doc.removeObject(planeA.Name)
        doc.recompute()
        self.assertEqual(self.versions(cut), [])
        self.assertEqual(self.refs(cut), {})
        doc.save()
        self.assertNotIn("_BaseShape", self.documentXml(project))

    def testUndoTakesTheGenerationWithIt(self):
        """Gate 7: the transaction records the property, so undo removes it
        and redo brings it back."""
        doc = self.newDocument()
        doc.UndoMode = 1
        cut, (plane,) = self.model(doc)
        doc.openTransaction("break")
        self.replaceBase(doc, cut, "NewBox")
        doc.commitTransaction()
        self.assertEqual(self.versions(cut), ["_BaseShape1"])

        doc.undo()
        self.assertEqual(self.versions(cut), [])
        self.assertEqual(self.refs(cut), {})
        self.assertEqual(self.support(plane), "Face3")

        doc.redo()
        self.assertEqual(self.versions(cut), ["_BaseShape1"])
        self.assertEqual(self.refs(cut), self.entries({plane: "_BaseShape1"}))

    def testAMovedFaceStaysMissingAfterReload(self):
        """V3, gate 4 of sec 5.7: the retained generation is asked again on
        reload, and a face that moved is still not repointed at a plausible
        neighbour."""
        doc = self.newDocument()
        cut, (plane,) = self.model(doc)
        project = self.directoryPath()
        self.replaceBase(doc, cut, "NewBox")
        self.assertTrue(self.support(plane).startswith("?"))
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        self.assertEqual(self.support(reopened.getObject("Plane")), "?Face3")
        self.assertEqual(self.versions(reopened.getObject("Cut")), ["_BaseShape1"])

    def testAReferenceComesBackOnReload(self):
        """V3: a reference is asked about once per session.  Broken by a
        moved face and not repaired when the face comes back, it is repaired
        on reload from the persisted generation, and the generation is let
        go at the next save.

        The referrer is a SubShapeBinder: a plane's attach extension re-sets
        its support by position at every recompute (7.12), which would
        repair the reference in-session and leave nothing to reload.
        """
        doc = self.newDocument()
        cut, () = self.model(doc, ())
        binder = doc.addObject("Part::SubShapeBinder", "Binder")
        binder.Support = [(cut, ("Face3",))]
        doc.recompute()
        project = self.directoryPath()
        self.replaceBase(doc, cut, "NewBox")
        self.assertEqual(binder.Support[0][1][0], "?Face3")
        doc.getObject("NewBox").Height = 20
        doc.recompute()
        self.assertEqual(binder.Support[0][1][0], "?Face3")
        self.assertEqual(self.versions(cut), ["_BaseShape1"])
        self.assertEqual(self.refs(cut), {"Binder.Support": "_BaseShape1"})
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        self.assertEqual(reopened.getObject("Binder").Support[0][1][0], "Face3")
        reopened.save()
        self.assertEqual(self.versions(reopened.getObject("Cut")), [])
        self.assertNotIn("_BaseShape", self.documentXml(project))


class ForeignBaseShapeCases(ShapeTestCase):
    """The evidence a document keeps for its references into other documents
    (docs/TopoNamingEnhance.md 7.13).

    A feature counts only the referrers in its own document, so an assembly
    referencing a part's face keeps the face itself: one document-wide
    `_ForeignBaseShapes` compound of every foreign sub-shape its element
    references resolved against, and a `_ForeignBaseShapeRefs` map from the
    full reference name to the child index.  Both are rebuilt at every save
    and served when the reference comes back missing on reload -- the case
    the feature side can never protect, because the part is edited while
    the assembly is closed.

    The part is the box cut by a cylinder of BaseShapeCases.  The assembly
    references it two ways, both SubShapeBinders (a plane's attach
    extension re-sets by position, 7.12): one linking the part's Cut
    directly, an XLink filed under the part's file path, and one through a
    local App::Link, filed under the link (`Link.Face1`).

    A SubShapeBinder rewrites its support to indexed names, and a
    reference given by index is resolved by index on reload, by design; so
    the binder cases break with both documents open and are served on
    reload.  The case that matters most -- the part edited while the
    assembly is closed -- needs a reference held by mapped name, which is
    what a selection produces: a FeaturePython with a plain
    App::PropertyXLinkSubList keeps it (persisted as `shadowed=`), and on
    reload it is resolved by the mapped name, comes back missing, and asks
    the store.
    """

    SHAPES = "_ForeignBaseShapes"
    REFS = "_ForeignBaseShapeRefs"

    def openDocument(self, path):
        """Open, recording the dependency documents too."""
        before = set(FreeCAD.listDocuments())
        doc = FreeCAD.openDocument(path)
        for name in FreeCAD.listDocuments():
            if name not in before:
                self.docs.append(name)
        return doc

    def part(self, name="part"):
        """The part, saved so that it can be linked to."""
        doc = self.newDocument("PartDoc")
        box = self.box(doc, "Box", length=20, width=20, height=20)
        cyl = doc.addObject("Part::Cylinder", "Cylinder")
        cyl.Radius = 3
        cyl.Height = 40
        cyl.Placement = self.placement(10, 10, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base = box
        cut.Tool = cyl
        doc.recompute()
        path = os.path.join(self.tmp, name + ".FCStd")
        doc.saveAs(path)
        return doc, path

    def assembly(self, part, face="Face1", through="Face1"):
        """The assembly: a binder on the part directly, and one through a
        local link, both by mapped name."""
        cut = part.getObject("Cut")
        mapped = lambda name: ";" + cut.Shape.getElementMappedName(name)
        doc = self.newDocument("AsmDoc")
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = cut
        direct = doc.addObject("Part::SubShapeBinder", "Binder")
        direct.Support = [(cut, (mapped(face),))]
        binderLink = doc.addObject("Part::SubShapeBinder", "BinderLink")
        binderLink.Support = [(link, (mapped(through),))]
        doc.recompute()
        path = os.path.join(self.tmp, "asm.FCStd")
        doc.saveAs(path)
        return doc, path

    def support(self, binder):
        """The reference as the binder holds it, in the indexed form."""
        return binder.Support[0][1][0]

    def children(self, doc):
        if self.SHAPES not in doc.PropertiesList:
            return []
        return getattr(doc, self.SHAPES).SubShapes

    def refs(self, doc):
        if self.REFS not in doc.PropertiesList:
            return {}
        return dict(getattr(doc, self.REFS))

    def replaceBase(self, part, height=20):
        """The edit that changes every mapped name of the cut: a brand new
        box as its Base.  At the same height the faces keep their geometry;
        taller, the top face moves."""
        cut = part.getObject("Cut")
        cut.Base = self.box(part, "NewBox", length=20, width=20, height=height)
        part.recompute()
        part.save()

    def testAPartAloneCarriesNoStore(self):
        """A document without a reference into another document saves as it
        did before any of this existed."""
        part, path = self.part()
        self.assertNotIn(self.SHAPES, part.PropertiesList)
        self.assertNotIn("_ForeignBaseShape", self.documentXml(path))
        self.assertNotIn("_BaseShape", self.documentXml(path))

    def testTheStoreNamesEveryForeignReference(self):
        """One child per distinct reference name: the direct binder's is
        filed under the part's file, the other under its local link."""
        part, partPath = self.part()
        asm, asmPath = self.assembly(part, through="Face2")
        refs = self.refs(asm)
        self.assertEqual(len(refs), 2)
        self.assertEqual(refs["Link.Face2"], "1")
        (xref,) = [k for k in refs if k != "Link.Face2"]
        self.assertTrue(xref.endswith("part.FCStd#Cut.Face1"), xref)
        self.assertEqual(refs[xref], "2")
        children = self.children(asm)
        self.assertEqual(len(children), 2)
        for child in children:
            self.assertAlmostEqual(child.Area, 400.0)
        self.assertIn('name="_ForeignBaseShapes"', self.documentXml(asmPath))
        # The part itself retains nothing: a foreign referrer is not counted
        self.assertNotIn("_BaseShape", self.documentXml(partPath))

        FreeCAD.closeDocument(asm.Name)
        reopened = self.openDocument(asmPath)
        self.assertEqual(self.refs(reopened), refs)
        self.assertEqual(len(self.children(reopened)), 2)

    def breakInSession(self, part, asm):
        """The top face moves with both documents open: the part's own
        in-memory generation answers the request, finds nothing, and the
        references go missing.  The part retains nothing for them -- they
        are foreign -- and the assembly's store keeps the child it has."""
        self.replaceBase(part, height=25)
        self.assertEqual(self.support(asm.getObject("Binder")), "?Face3")
        self.assertEqual(self.support(asm.getObject("BinderLink")), "?Face3")
        self.assertNotIn("_BaseShapeRefs", part.getObject("Cut").PropertiesList)
        asm.save()
        self.assertEqual(len(self.refs(asm)), 2)
        children = self.children(asm)
        self.assertEqual(len(children), 1)
        self.assertAlmostEqual(children[0].CenterOfMass.z, 20.0)

    def testAReferenceComesBackAcrossDocuments(self):
        """The case the feature side cannot protect: broken with both
        documents open, the assembly is closed; the part is repaired while
        it is closed; on reopening, both references ask the store and are
        repaired from the child it kept."""
        part, partPath = self.part()
        asm, asmPath = self.assembly(part, face="Face3", through="Face3")
        self.breakInSession(part, asm)
        FreeCAD.closeDocument(asm.Name)
        part.getObject("NewBox").Height = 20
        part.recompute()
        part.save()
        FreeCAD.closeDocument(part.Name)

        reopened = self.openDocument(asmPath)
        self.assertEqual(self.support(reopened.getObject("Binder")), "Face3")
        self.assertEqual(self.support(reopened.getObject("BinderLink")), "Face3")
        reopened.save()
        self.assertEqual(len(self.refs(reopened)), 2)
        self.assertAlmostEqual(self.children(reopened)[0].CenterOfMass.z, 20.0)

    def testAMovedFaceStaysMissingAndKeepsItsChild(self):
        """Gate 4 across documents: reopened with the face still moved, the
        references are not repointed at a plausible neighbour, and the
        store keeps the child they were resolved against -- the face as it
        was, not as it is."""
        part, partPath = self.part()
        asm, asmPath = self.assembly(part, face="Face3", through="Face3")
        self.breakInSession(part, asm)
        FreeCAD.closeDocument(asm.Name)
        FreeCAD.closeDocument(part.Name)

        reopened = self.openDocument(asmPath)
        self.assertEqual(self.support(reopened.getObject("Binder")), "?Face3")
        self.assertEqual(self.support(reopened.getObject("BinderLink")), "?Face3")
        reopened.save()
        self.assertEqual(len(self.refs(reopened)), 2)
        self.assertAlmostEqual(self.children(reopened)[0].CenterOfMass.z, 20.0)

    def referrer(self, part, face="Face1"):
        """An assembly whose references keep their mapped names: one to the
        part's Cut directly, one through a local link.

        Both are verified on reload.  The direct one when its link is
        restored; the one through the link when the document's references
        are re-registered, because its geometry lives in another document
        and was not saved together with the reference (7.16) -- before
        that it was registered with its restored shadow and read as saved,
        whatever the part did while the assembly was closed.
        """
        cut = part.getObject("Cut")
        mapped = ";" + cut.Shape.getElementMappedName(face)
        doc = self.newDocument("AsmDoc")
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = cut
        ref = doc.addObject("App::FeaturePython", "Ref")
        ref.addProperty("App::PropertyXLinkSubList", "Refs")
        ref.Refs = [(cut, (mapped,)), (link, (mapped,))]
        doc.recompute()
        path = os.path.join(self.tmp, "asm.FCStd")
        doc.saveAs(path)
        self.assertIn("shadowed=", self.documentXml(path))
        return doc, path

    def refsOf(self, ref):
        """The references by target name: the order of the list is not
        stable across a reload."""
        return {obj.Name: subs[0] for obj, subs in ref.Refs}

    def testAnEditWhileClosedIsRecoveredOnOpen(self):
        """The case the feature side cannot protect: the part is edited with
        the assembly closed, every mapped name changes, the geometry does
        not.  On reopening, both references resolve by mapped name, come
        back missing, and are repaired from the store."""
        part, partPath = self.part()
        asm, asmPath = self.referrer(part)
        self.assertEqual(sorted(self.refs(asm)), ["Link.Face1", self.xref(asm)])
        FreeCAD.closeDocument(asm.Name)
        self.replaceBase(part)
        FreeCAD.closeDocument(part.Name)

        reopened = self.openDocument(asmPath)
        refs = self.refsOf(reopened.getObject("Ref"))
        self.assertEqual(refs["Cut"], "Face1")
        self.assertEqual(refs["Link"], "Face1")
        reopened.save()
        self.assertEqual(self.shadowed(asmPath), [self.mapped(reopened)] * 2)

    def testAFaceMovedWhileClosedStaysMissing(self):
        """Gate 4 for the same edit: the top face moved, and neither
        reference is repointed at a plausible neighbour."""
        part, partPath = self.part()
        asm, asmPath = self.referrer(part, face="Face3")
        FreeCAD.closeDocument(asm.Name)
        self.replaceBase(part, height=25)
        FreeCAD.closeDocument(part.Name)

        reopened = self.openDocument(asmPath)
        refs = self.refsOf(reopened.getObject("Ref"))
        self.assertEqual(refs["Cut"], "?Face3")
        self.assertEqual(refs["Link"], "?Face3")

    def testAnUpgradeKeepsEveryReference(self):
        """The part's element map is a version behind, so its first
        recompute regenerates the map and re-resolves every reference to
        it ('reverse').  A part opened through the assembly is partial and
        recomputes with the assembly.  Nothing is newly missing, through
        the link or not, and the file ends up with the current names."""
        part, partPath = self.part()
        version = part.getObject("Cut").getElementMapVersion("Shape")
        asm, asmPath = self.referrer(part)
        FreeCAD.closeDocument(asm.Name)
        FreeCAD.closeDocument(part.Name)
        self.ageElementMap(partPath, version)

        reopened = self.openDocument(asmPath)
        refs = self.refsOf(reopened.getObject("Ref"))
        self.assertEqual([refs["Cut"], refs["Link"]], ["Face1", "Face1"])
        reopened.recompute()
        refs = self.refsOf(reopened.getObject("Ref"))
        self.assertEqual([refs["Cut"], refs["Link"]], ["Face1", "Face1"])
        reopened.save()
        self.assertEqual(self.shadowed(asmPath), [self.mapped(reopened)] * 2)

    def testAMissingReferenceSurvivesTheUpgrade(self):
        """The same regeneration over a reference that came back missing:
        it stays marked, and is not blanked into the whole object -- the
        marker name looked up as a name is an unknown mapped name with no
        indexed one, which is what a regeneration used to write out."""
        part, partPath = self.part()
        version = part.getObject("Cut").getElementMapVersion("Shape")
        asm, asmPath = self.referrer(part, face="Face3")
        FreeCAD.closeDocument(asm.Name)
        self.replaceBase(part, height=25)
        FreeCAD.closeDocument(part.Name)
        self.ageElementMap(partPath, version)

        reopened = self.openDocument(asmPath)
        refs = self.refsOf(reopened.getObject("Ref"))
        self.assertEqual([refs["Cut"], refs["Link"]], ["?Face3", "?Face3"])
        reopened.recompute()
        refs = self.refsOf(reopened.getObject("Ref"))
        self.assertEqual([refs["Cut"], refs["Link"]], ["?Face3", "?Face3"])
        reopened.save()
        self.assertEqual(self.subs(asmPath), ["?Face3", "?Face3"])

    def xref(self, asm):
        (key,) = [k for k in self.refs(asm) if "#" in k]
        return key

    def mapped(self, asm, face="Face1"):
        """The face's mapped name as the assembly's references persist it."""
        cut = [o for o, subs in asm.getObject("Ref").Refs if o.Name == "Cut"][0]
        return ";" + cut.Shape.getElementMappedName(face) + "." + face

    def subs(self, path):
        """Every persisted sub-name: a lone one is an attribute of the
        link, several are <Sub> elements."""
        return re.findall(r'(?:<Sub value|\ssub)="([^"]*)"', self.documentXml(path))

    def shadowed(self, path):
        return re.findall(r'shadowed="([^"]*)"', self.documentXml(path))

    def ageElementMap(self, path, version):
        """Rewrite the saved part as if its element map were one map
        version older, which is what a file from an earlier release is."""
        major, minor, rest = version.split(".", 2)
        older = "%s.%d.%s" % (major, int(minor) - 1, rest)
        xml = self.documentXml(path)
        self.assertIn('ElementMap="%s"' % version, xml)
        xml = xml.replace(version, older)
        tmp = path + ".tmp"
        with zipfile.ZipFile(path) as zin, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                data = zin.read(info.filename)
                if info.filename == "Document.xml":
                    data = xml.encode("utf-8")
                zout.writestr(info, data)
        os.replace(tmp, path)

    def testADroppedReferenceLeavesTheStore(self):
        """A reference removed is not kept; the last one takes the store."""
        part, partPath = self.part()
        asm, asmPath = self.assembly(part, through="Face2")
        asm.removeObject("Binder")
        asm.recompute()
        asm.save()
        self.assertEqual(list(self.refs(asm)), ["Link.Face2"])
        self.assertEqual(len(self.children(asm)), 1)

        asm.removeObject("BinderLink")
        asm.recompute()
        asm.save()
        self.assertNotIn(self.SHAPES, asm.PropertiesList)
        self.assertNotIn(self.REFS, asm.PropertiesList)
        self.assertNotIn("_ForeignBaseShape", self.documentXml(asmPath))
